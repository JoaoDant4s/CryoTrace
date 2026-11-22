"""Decision for each MQTT message: forward it, skip it, or drop it."""

import logging
from dataclasses import dataclass
from enum import Enum

from .chain import ChainClient, Receipt, Rejected
from .payload import PayloadError, box_id_of, parse_payload

log = logging.getLogger(__name__)


class Outcome(Enum):
    RECORDED = "recorded"    # transaction mined
    DUPLICATE = "duplicate"  # already on-chain (e.g. MQTT QoS 1 redelivery)
    REJECTED = "rejected"    # the contract refuses it (bad signature, wrong batch...)
    INVALID = "invalid"      # malformed payload or topic


@dataclass(frozen=True)
class Result:
    outcome: Outcome
    detail: str = ""
    receipt: Receipt | None = None


def box_name_from_topic(topic: str) -> str | None:
    parts = topic.split("/")
    if len(parts) != 3 or parts[0] != "coldchain" or parts[2] != "anomaly" or not parts[1]:
        return None
    return parts[1]


class Relayer:
    def __init__(self, chain: ChainClient):
        self.chain = chain

    def handle(self, topic: str, raw: bytes) -> Result:
        """Processes one message. Network/RPC errors propagate so the caller can retry."""
        box_name = box_name_from_topic(topic)
        if box_name is None:
            return Result(Outcome.INVALID, f"unexpected topic {topic!r}")
        try:
            signed = parse_payload(raw)
        except PayloadError as exc:
            return Result(Outcome.INVALID, str(exc))
        # The topic is not signed, but a mismatch means a misrouted message.
        if signed.event.box_id != box_id_of(box_name):
            return Result(Outcome.INVALID, f"boxId does not match topic box {box_name!r}")

        if self.chain.is_recorded(signed):
            return Result(Outcome.DUPLICATE, f"counter {signed.event.counter} already recorded")
        try:
            receipt = self.chain.submit(signed)
        except Rejected as exc:
            return Result(Outcome.REJECTED, exc.reason)
        return Result(Outcome.RECORDED, f"counter {signed.event.counter}", receipt)
