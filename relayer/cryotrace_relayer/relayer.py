"""Decision for each MQTT message: forward it, skip it, or drop it."""

import logging
import time
from collections.abc import Callable
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
    """Several relayers can run side by side (each with its own wallet and MQTT
    client id): the contract accepts an event from anyone and rejects
    duplicates, so censoring one relayer does not stop the system.

    submit_delay_s lets a backup relayer wait before submitting; it then only
    pays gas if the primary has not recorded the event by then.
    """

    def __init__(self, chain: ChainClient, submit_delay_s: float = 0,
                 sleep: Callable[[float], None] = time.sleep):
        self.chain = chain
        self.submit_delay_s = submit_delay_s
        self._sleep = sleep

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

        if self.submit_delay_s > 0:
            self._sleep(self.submit_delay_s)
        if self.chain.is_recorded(signed):
            return Result(Outcome.DUPLICATE, f"counter {signed.event.counter} already recorded")
        try:
            receipt = self.chain.submit(signed)
        except Rejected as exc:
            # Another relayer may have won the race between our check and our
            # transaction: that is a duplicate, not a rejection.
            if exc.reason == "AlreadyRecorded" or self.chain.is_recorded(signed):
                return Result(Outcome.DUPLICATE,
                              f"counter {signed.event.counter} recorded by another relayer")
            return Result(Outcome.REJECTED, exc.reason)
        return Result(Outcome.RECORDED, f"counter {signed.event.counter}", receipt)
