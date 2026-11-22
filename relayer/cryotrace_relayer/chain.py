"""Submission of signed events to the CryoTrace contract."""

import json
from dataclasses import dataclass
from pathlib import Path

from eth_account import Account
from eth_utils import function_signature_to_4byte_selector, to_checksum_address
from web3 import Web3
from web3.exceptions import ContractCustomError, ContractLogicError

from .payload import SignedEvent

DEFAULT_ABI_PATH = Path(__file__).resolve().parents[2] / "contracts/out/CryoTrace.sol/CryoTrace.json"


class Rejected(Exception):
    """The contract refuses the event; retrying will not help."""

    def __init__(self, reason: str):
        super().__init__(reason)
        self.reason = reason


@dataclass(frozen=True)
class Receipt:
    tx_hash: str
    block_number: int
    gas_used: int


def load_abi(path: Path = DEFAULT_ABI_PATH) -> list:
    """ABI from the Foundry build output (run `forge build` in contracts/)."""
    return json.loads(Path(path).read_text())["abi"]


def _error_selectors(abi: list) -> dict[str, str]:
    selectors = {}
    for item in abi:
        if item.get("type") == "error":
            signature = f"{item['name']}({','.join(i['type'] for i in item['inputs'])})"
            selectors["0x" + function_signature_to_4byte_selector(signature).hex()] = item["name"]
    return selectors


class ChainClient:
    def __init__(self, w3: Web3, contract_address: str, abi: list, relayer_key: str,
                 receipt_timeout_s: float = 120):
        self.w3 = w3
        self.contract = w3.eth.contract(address=to_checksum_address(contract_address), abi=abi)
        self.account = Account.from_key(relayer_key)
        self.receipt_timeout_s = receipt_timeout_s
        self._errors = _error_selectors(abi)
        # OpenZeppelin ECDSA errors are reported by selector too.
        for signature in ("ECDSAInvalidSignature()", "ECDSAInvalidSignatureLength(uint256)",
                          "ECDSAInvalidSignatureS(bytes32)"):
            selector = "0x" + function_signature_to_4byte_selector(signature).hex()
            self._errors.setdefault(selector, signature.split("(")[0])

    def _reason(self, exc: Exception) -> str:
        data = getattr(exc, "data", None) or (exc.args[0] if exc.args else "")
        if isinstance(data, str) and data.startswith("0x") and len(data) >= 10:
            return self._errors.get(data[:10], data)
        return str(exc)

    def digest(self, signed: SignedEvent) -> bytes:
        return self.contract.functions.hashEvent(signed.event.as_contract_tuple()).call()

    def is_recorded(self, signed: SignedEvent) -> bool:
        return self.contract.functions.recorded(self.digest(signed)).call()

    def submit(self, signed: SignedEvent) -> Receipt:
        """Sends recordEvent. Raises Rejected if the contract refuses the event.

        Gas estimation simulates the call first, so a rejected event fails
        here without spending gas. Network errors propagate as-is (transient).
        """
        call = self.contract.functions.recordEvent(signed.event.as_contract_tuple(), signed.signature)
        try:
            tx = call.build_transaction({
                "from": self.account.address,
                "nonce": self.w3.eth.get_transaction_count(self.account.address, "pending"),
            })
        except (ContractCustomError, ContractLogicError) as exc:
            raise Rejected(self._reason(exc)) from exc

        raw = self.account.sign_transaction(tx).raw_transaction
        tx_hash = self.w3.eth.send_raw_transaction(raw)
        receipt = self.w3.eth.wait_for_transaction_receipt(tx_hash, timeout=self.receipt_timeout_s)
        if receipt["status"] != 1:
            raise Rejected(f"transaction {tx_hash.hex()} reverted")
        return Receipt(tx_hash="0x" + tx_hash.hex().removeprefix("0x"),
                       block_number=receipt["blockNumber"], gas_used=receipt["gasUsed"])
