"""End-to-end relayer tests against a local chain (anvil) running the real contract.

Requires Foundry (anvil, forge) on PATH. Events are signed here with
eth_account, independently of the firmware and Foundry implementations.
"""

import json
import shutil
import socket
import subprocess
import time
from pathlib import Path

import pytest
from eth_account import Account
from web3 import Web3

from cryotrace_relayer.chain import DEFAULT_ABI_PATH, ChainClient, load_abi
from cryotrace_relayer.payload import box_id_of
from cryotrace_relayer.relayer import Outcome, Relayer

pytestmark = pytest.mark.skipif(shutil.which("anvil") is None, reason="anvil not installed")

CONTRACTS_DIR = Path(__file__).resolve().parents[2] / "contracts"
# anvil's default funded account #1 (public, well-known test key)
RELAYER_KEY = "0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d"
DEVICE_KEY = "0x" + "00" * 29 + "c0ffee"
TOPIC = "coldchain/box01/anomaly"

EVENT_TYPES = {
    "ColdChainEvent": [
        {"name": "boxId", "type": "bytes32"},
        {"name": "batchId", "type": "bytes32"},
        {"name": "kind", "type": "uint8"},
        {"name": "anomalyType", "type": "uint8"},
        {"name": "startTime", "type": "uint64"},
        {"name": "endTime", "type": "uint64"},
        {"name": "temperature", "type": "int16"},
        {"name": "peak", "type": "int16"},
        {"name": "counter", "type": "uint32"},
    ]
}


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture(scope="module")
def chain():
    if not DEFAULT_ABI_PATH.exists():
        subprocess.run(["forge", "build"], cwd=CONTRACTS_DIR, check=True, capture_output=True)

    port = _free_port()
    anvil = subprocess.Popen(["anvil", "--port", str(port), "--silent"])
    w3 = Web3(Web3.HTTPProvider(f"http://127.0.0.1:{port}"))
    for _ in range(50):
        if w3.is_connected():
            break
        time.sleep(0.1)
    else:
        anvil.kill()
        pytest.fail("anvil did not start")

    artifact = json.loads(DEFAULT_ABI_PATH.read_text())
    admin = w3.eth.accounts[0]
    factory = w3.eth.contract(abi=artifact["abi"], bytecode=artifact["bytecode"]["object"])
    tx = factory.constructor(admin).transact({"from": admin})
    address = w3.eth.wait_for_transaction_receipt(tx)["contractAddress"]
    cryo = w3.eth.contract(address=address, abi=artifact["abi"])

    for role in (cryo.functions.PROVISIONER_ROLE().call(), cryo.functions.LOGISTICS_ROLE().call()):
        cryo.functions.grantRole(role, admin).transact({"from": admin})
    device = Account.from_key(DEVICE_KEY).address
    cryo.functions.registerDevice(box_id_of("box01"), device).transact({"from": admin})
    cryo.functions.registerBatch(box_id_of("batch-demo-001"), box_id_of("box01")).transact({"from": admin})

    yield w3, cryo
    anvil.terminate()
    anvil.wait()


@pytest.fixture(scope="module")
def relayer(chain):
    w3, cryo = chain
    return Relayer(ChainClient(w3, cryo.address, load_abi(), RELAYER_KEY))


def _event(counter: int, **overrides) -> dict:
    ev = {
        "boxId": box_id_of("box01"),
        "batchId": box_id_of("batch-demo-001"),
        "kind": 0,
        "anomalyType": 1,  # ABOVE
        "startTime": 1767225600000 + counter,
        "endTime": 0,
        "temperature": 812,
        "peak": 845,
        "counter": counter,
    }
    ev.update(overrides)
    return ev


def _sign(w3, cryo, ev: dict, key: str = DEVICE_KEY) -> bytes:
    domain = {
        "name": "CryoTrace",
        "version": "1",
        "chainId": w3.eth.chain_id,
        "verifyingContract": cryo.address,
    }
    return Account.sign_typed_data(key, domain, EVENT_TYPES, ev).signature


def _payload(ev: dict, signature: bytes) -> bytes:
    """Same JSON layout the firmware publishes."""
    obj = {k: ("0x" + v.hex() if isinstance(v, bytes) else v) for k, v in ev.items()}
    obj["signature"] = "0x" + signature.hex()
    return json.dumps(obj).encode()


def test_records_signed_event(chain, relayer):
    w3, cryo = chain
    ev = _event(1)
    result = relayer.handle(TOPIC, _payload(ev, _sign(w3, cryo, ev)))

    assert result.outcome is Outcome.RECORDED, result.detail
    assert result.receipt.gas_used > 0
    stored = cryo.functions.getBatchEvents(box_id_of("batch-demo-001")).call()
    assert any(e[0][8] == 1 for e in stored)  # data.counter


def test_skips_duplicate(chain, relayer):
    w3, cryo = chain
    ev = _event(2)
    payload = _payload(ev, _sign(w3, cryo, ev))
    assert relayer.handle(TOPIC, payload).outcome is Outcome.RECORDED
    # e.g. an MQTT QoS 1 redelivery
    assert relayer.handle(TOPIC, payload).outcome is Outcome.DUPLICATE


def test_rejects_tampered_event(chain, relayer):
    w3, cryo = chain
    ev = _event(3)
    signature = _sign(w3, cryo, ev)
    ev["temperature"] = 500  # hides the excursion
    result = relayer.handle(TOPIC, _payload(ev, signature))
    assert result.outcome is Outcome.REJECTED
    assert result.detail == "InvalidSigner"


def test_rejects_foreign_key(chain, relayer):
    w3, cryo = chain
    ev = _event(4)
    attacker = "0x" + "00" * 31 + "0b"
    result = relayer.handle(TOPIC, _payload(ev, _sign(w3, cryo, ev, key=attacker)))
    assert result.outcome is Outcome.REJECTED
    assert result.detail == "InvalidSigner"


def test_rejects_batch_not_on_box(chain, relayer):
    w3, cryo = chain
    ev = _event(5, batchId=box_id_of("batch-other"))
    result = relayer.handle(TOPIC, _payload(ev, _sign(w3, cryo, ev)))
    assert result.outcome is Outcome.REJECTED
    assert result.detail == "BatchNotOnBox"


def test_rejects_topic_box_mismatch(chain, relayer):
    w3, cryo = chain
    ev = _event(6)
    result = relayer.handle("coldchain/box02/anomaly", _payload(ev, _sign(w3, cryo, ev)))
    assert result.outcome is Outcome.INVALID


def test_does_not_spend_gas_on_rejection(chain, relayer):
    w3, cryo = chain
    relayer_address = Account.from_key(RELAYER_KEY).address
    nonce_before = w3.eth.get_transaction_count(relayer_address)
    ev = _event(7)
    signature = _sign(w3, cryo, ev)
    ev["peak"] = 0
    assert relayer.handle(TOPIC, _payload(ev, signature)).outcome is Outcome.REJECTED
    assert w3.eth.get_transaction_count(relayer_address) == nonce_before


# ---- several relayers (censorship resistance) ----

# anvil's default funded account #2, a second relayer with its own wallet
BACKUP_KEY = "0x5de4111afa1a4b94908f83103eb1f1706367c2e68ca870fc3fb9a804cdab365a"


def _nonce(w3, key: str) -> int:
    return w3.eth.get_transaction_count(Account.from_key(key).address)


def test_second_relayer_skips_recorded_event(chain, relayer):
    w3, cryo = chain
    backup = Relayer(ChainClient(w3, cryo.address, load_abi(), BACKUP_KEY))
    ev = _event(10)
    payload = _payload(ev, _sign(w3, cryo, ev))

    assert relayer.handle(TOPIC, payload).outcome is Outcome.RECORDED
    nonce_before = _nonce(w3, BACKUP_KEY)
    assert backup.handle(TOPIC, payload).outcome is Outcome.DUPLICATE
    assert _nonce(w3, BACKUP_KEY) == nonce_before  # no gas spent


def test_lost_race_counts_as_duplicate(chain, relayer, monkeypatch):
    """The backup checked before the primary's transaction was mined."""
    w3, cryo = chain
    backup = Relayer(ChainClient(w3, cryo.address, load_abi(), BACKUP_KEY))
    ev = _event(11)
    payload = _payload(ev, _sign(w3, cryo, ev))
    assert relayer.handle(TOPIC, payload).outcome is Outcome.RECORDED

    real = backup.chain.is_recorded
    calls = []

    def first_check_is_stale(signed):
        calls.append(signed)
        return False if len(calls) == 1 else real(signed)

    monkeypatch.setattr(backup.chain, "is_recorded", first_check_is_stale)
    result = backup.handle(TOPIC, payload)
    assert result.outcome is Outcome.DUPLICATE
    assert "another relayer" in result.detail


def test_backup_relayer_waits_then_submits(chain):
    w3, cryo = chain
    waits = []
    backup = Relayer(ChainClient(w3, cryo.address, load_abi(), BACKUP_KEY),
                     submit_delay_s=30, sleep=waits.append)
    ev = _event(12)
    # The primary never forwarded this event (e.g. censored): the backup records it.
    result = backup.handle(TOPIC, _payload(ev, _sign(w3, cryo, ev)))
    assert waits == [30]
    assert result.outcome is Outcome.RECORDED
