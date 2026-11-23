"""Parsing and validation of the JSON the firmware publishes on cryotrace/{box_id}/anomaly.

The field names match the contract's ColdChainEvent struct, so a parsed event
maps one to one onto the recordEvent() arguments. Validation here only rejects
payloads that could never be recorded (bad types or ranges); the contract is
the authority on signatures and business rules.
"""

import json
from dataclasses import dataclass

from eth_utils import keccak

SIGNATURE_LEN = 65


class PayloadError(ValueError):
    """The MQTT payload is not a well-formed signed event."""


@dataclass(frozen=True)
class ColdChainEvent:
    box_id: bytes
    batch_id: bytes
    kind: int
    anomaly_type: int
    start_time: int
    end_time: int
    temperature: int
    peak: int
    counter: int

    def as_contract_tuple(self) -> tuple:
        """Argument for CryoTrace.recordEvent / hashEvent (struct field order)."""
        return (
            self.box_id,
            self.batch_id,
            self.kind,
            self.anomaly_type,
            self.start_time,
            self.end_time,
            self.temperature,
            self.peak,
            self.counter,
        )


@dataclass(frozen=True)
class SignedEvent:
    event: ColdChainEvent
    signature: bytes


def box_id_of(box_name: str) -> bytes:
    """keccak256 of the box id string, as the firmware computes it."""
    return keccak(text=box_name)


def _hex_bytes(obj: dict, field: str, length: int) -> bytes:
    value = obj.get(field)
    if not isinstance(value, str) or not value.startswith("0x"):
        raise PayloadError(f"{field}: expected a 0x-prefixed hex string")
    try:
        data = bytes.fromhex(value[2:])
    except ValueError as exc:
        raise PayloadError(f"{field}: invalid hex") from exc
    if len(data) != length:
        raise PayloadError(f"{field}: expected {length} bytes, got {len(data)}")
    return data


def _int(obj: dict, field: str, low: int, high: int) -> int:
    value = obj.get(field)
    # bool is a subclass of int in Python; reject it explicitly.
    if not isinstance(value, int) or isinstance(value, bool):
        raise PayloadError(f"{field}: expected an integer")
    if not low <= value <= high:
        raise PayloadError(f"{field}: {value} out of range [{low}, {high}]")
    return value


def parse_payload(raw: bytes) -> SignedEvent:
    try:
        obj = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PayloadError("not valid JSON") from exc
    if not isinstance(obj, dict):
        raise PayloadError("expected a JSON object")

    event = ColdChainEvent(
        box_id=_hex_bytes(obj, "boxId", 32),
        batch_id=_hex_bytes(obj, "batchId", 32),
        kind=_int(obj, "kind", 0, 255),
        anomaly_type=_int(obj, "anomalyType", 0, 255),
        start_time=_int(obj, "startTime", 0, 2**64 - 1),
        end_time=_int(obj, "endTime", 0, 2**64 - 1),
        temperature=_int(obj, "temperature", -(2**15), 2**15 - 1),
        peak=_int(obj, "peak", -(2**15), 2**15 - 1),
        counter=_int(obj, "counter", 0, 2**32 - 1),
    )
    return SignedEvent(event=event, signature=_hex_bytes(obj, "signature", SIGNATURE_LEN))
