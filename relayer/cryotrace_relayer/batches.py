"""Tells each box which batch it carries.

When logistics assigns a batch to a box (BatchRegistered on the contract), the
relayer publishes it, retained, on cryotrace/<boxId>/batch. A box that was off
gets it as soon as it connects, and signs its next events with that batch.

The topic uses the box id (0x + 64 hex, the keccak256 of the box name), not the name: the contract
only knows the hash, and the box computes the same hash from its own name.

Anyone with broker credentials could publish a fake batch, which would make the
box sign events the contract refuses (BatchNotOnBox). So the relayer also
watches these topics and checks every message against the contract: a forged
or outdated assignment is overwritten with the current one. Broker ACLs that
let only relayers publish here are the first line of defence.
"""

import json
import logging
import threading
from collections.abc import Callable
from dataclasses import dataclass
from enum import Enum

log = logging.getLogger(__name__)

BATCH_TOPIC_FILTER = "cryotrace/+/batch"


def batch_topic(box_id: bytes) -> str:
    return f"cryotrace/0x{box_id.hex()}/batch"


def box_id_from_topic(topic: str) -> bytes | None:
    parts = topic.split("/")
    if len(parts) != 3 or parts[0] != "cryotrace" or parts[2] != "batch" or not parts[1].startswith("0x"):
        return None
    try:
        box_id = bytes.fromhex(parts[1][2:])
    except ValueError:
        return None
    return box_id if len(box_id) == 32 else None


@dataclass(frozen=True)
class Assignment:
    box_id: bytes
    batch_id: bytes
    block_number: int
    tx_hash: str

    def payload(self) -> bytes:
        return json.dumps({
            "boxId": "0x" + self.box_id.hex(),
            "batchId": "0x" + self.batch_id.hex(),
            "blockNumber": self.block_number,
            "txHash": self.tx_hash,
        }).encode()


def batch_id_from_payload(raw: bytes, box_id: bytes) -> bytes | None:
    """batchId of a retained message, or None if it is malformed or for another box."""
    try:
        obj = json.loads(raw)
        if bytes.fromhex(obj["boxId"].removeprefix("0x")) != box_id:
            return None
        batch_id = bytes.fromhex(obj["batchId"].removeprefix("0x"))
    except (ValueError, KeyError, TypeError, AttributeError):
        return None
    return batch_id if len(batch_id) == 32 else None


class Check(Enum):
    OK = "ok"                # the message is the current assignment
    REPUBLISH = "republish"  # forged, malformed or outdated: overwrite with the current one
    CLEAR = "clear"          # forged, and no assignment known for the box: remove it
    LAGGING = "lagging"      # a real assignment newer than ours: catch up with the chain


class BatchTracker:
    """Batches assigned to each box, in chain order. Pure logic, no I/O."""

    def __init__(self):
        self._history: dict[bytes, list[Assignment]] = {}

    def add(self, a: Assignment) -> bool:
        """Records an assignment; False if it was already known."""
        history = self._history.setdefault(a.box_id, [])
        if any(h.batch_id == a.batch_id for h in history):
            return False
        history.append(a)
        return True

    def current(self, box_id: bytes) -> Assignment | None:
        history = self._history.get(box_id)
        return history[-1] if history else None

    def all_current(self) -> list[Assignment]:
        return [h[-1] for h in self._history.values() if h]

    def check(self, box_id: bytes, raw: bytes, is_on_box: Callable[[bytes, bytes], bool]) -> Check:
        """Decides what to do with a retained message found on a box's batch topic.

        is_on_box(batch_id, box_id) asks the contract (boxOfBatch) and is only
        called for a batch this relayer has not seen for the box.
        """
        current = self.current(box_id)
        if not raw:  # topic cleared
            return Check.REPUBLISH if current else Check.OK
        batch_id = batch_id_from_payload(raw, box_id)
        if batch_id is None:
            return Check.REPUBLISH if current else Check.CLEAR
        if current and batch_id == current.batch_id:
            return Check.OK
        if any(h.batch_id == batch_id for h in self._history.get(box_id, [])):
            return Check.REPUBLISH  # a previous trip of this box
        if is_on_box(batch_id, box_id):
            return Check.LAGGING  # assigned on-chain after the last block we read
        return Check.REPUBLISH if current else Check.CLEAR


class BatchAnnouncer:
    """Reads BatchRegistered logs and keeps the retained batch topics in sync.

    poll() runs in its own thread, on_retained() in the MQTT thread; both
    publish through `publish(topic, payload)`, which must send QoS 1, retained.
    """

    def __init__(self, contract, publish: Callable[[str, bytes], None], start_block: int = 0,
                 chunk_blocks: int = 10_000):
        self.contract = contract
        self.w3 = contract.w3
        self.publish = publish
        self.next_block = start_block
        self.chunk_blocks = chunk_blocks
        self.tracker = BatchTracker()
        self._lock = threading.Lock()       # tracker
        self._poll_lock = threading.Lock()  # one log scan at a time

    def poll(self) -> list[Assignment]:
        """Reads new BatchRegistered logs and announces new assignments.

        RPC errors propagate; the same blocks are read again on the next call.
        """
        with self._poll_lock:
            return self._poll()

    def _poll(self) -> list[Assignment]:
        latest = self.w3.eth.block_number
        fresh = []
        while self.next_block <= latest:
            end = min(self.next_block + self.chunk_blocks - 1, latest)
            logs = self.contract.events.BatchRegistered.get_logs(from_block=self.next_block, to_block=end)
            with self._lock:
                for entry in sorted(logs, key=lambda e: (e["blockNumber"], e["logIndex"])):
                    a = Assignment(box_id=bytes(entry["args"]["boxId"]),
                                   batch_id=bytes(entry["args"]["batchId"]),
                                   block_number=entry["blockNumber"],
                                   tx_hash="0x" + entry["transactionHash"].hex().removeprefix("0x"))
                    if self.tracker.add(a):
                        fresh.append(a)
                self.next_block = end + 1
        for a in fresh:
            # Only the newest per box matters; older ones may come in the same scan.
            if self.tracker.current(a.box_id) == a:
                log.info("box 0x%s now carries batch 0x%s (block %d)", a.box_id.hex()[:12],
                         a.batch_id.hex()[:12], a.block_number)
                self.publish(batch_topic(a.box_id), a.payload())
        return fresh

    def republish_all(self) -> None:
        """Announces every current assignment again (e.g. after reconnecting)."""
        with self._lock:
            current = self.tracker.all_current()
        for a in current:
            self.publish(batch_topic(a.box_id), a.payload())

    def _is_on_box(self, batch_id: bytes, box_id: bytes) -> bool:
        return bytes(self.contract.functions.boxOfBatch(batch_id).call()) == box_id

    def on_retained(self, topic: str, raw: bytes) -> Check | None:
        """Checks a message seen on a batch topic and repairs it if needed."""
        box_id = box_id_from_topic(topic)
        if box_id is None:
            return None
        with self._lock:
            result = self.tracker.check(box_id, raw, self._is_on_box)
            current = self.tracker.current(box_id)
        if result is Check.REPUBLISH:
            log.warning("%s held a wrong batch; republishing the current one", topic)
            self.publish(topic, current.payload())
        elif result is Check.CLEAR:
            log.warning("%s held a batch not assigned to this box; clearing it", topic)
            self.publish(topic, b"")
        elif result is Check.LAGGING:
            self.poll()
        return result
