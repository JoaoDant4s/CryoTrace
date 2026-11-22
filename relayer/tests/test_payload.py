import json

import pytest

from cryotrace_relayer.payload import PayloadError, box_id_of, parse_payload
from cryotrace_relayer.relayer import box_name_from_topic

# Exact payload produced by the firmware for the shared test vector
# (test/host/test_signed_event.c, test_payload_json).
FIRMWARE_PAYLOAD = (
    b'{"boxId":"0x8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb",'
    b'"batchId":"0x36c5cfd13ef95136a333b8467f27e5810f10588f87c862682a58cbe406b12fad",'
    b'"kind":1,"anomalyType":1,"startTime":1767225600000,"endTime":1767225615000,'
    b'"temperature":790,"peak":1121,"counter":42,'
    b'"signature":"0x08a7c85b3031fc6059aec942042ef1e10e1bc7b17ecf9fa752a3a1fc44c74cb5'
    b'5a2f3eca400efae2f3591feba1e1aad4e5dcd42b02381dad3d2a12883133eee81c"}'
)


def test_parses_firmware_payload():
    signed = parse_payload(FIRMWARE_PAYLOAD)
    ev = signed.event
    assert ev.box_id == box_id_of("box01")
    assert ev.batch_id == box_id_of("batch-demo-001")
    assert (ev.kind, ev.anomaly_type, ev.counter) == (1, 1, 42)
    assert (ev.start_time, ev.end_time) == (1767225600000, 1767225615000)
    assert (ev.temperature, ev.peak) == (790, 1121)
    assert len(signed.signature) == 65 and signed.signature[-1] == 0x1C
    assert ev.as_contract_tuple()[2:] == (1, 1, 1767225600000, 1767225615000, 790, 1121, 42)


def test_accepts_no_reading_temperature():
    obj = json.loads(FIRMWARE_PAYLOAD)
    obj["temperature"] = -32768
    assert parse_payload(json.dumps(obj).encode()).event.temperature == -32768


@pytest.mark.parametrize(
    "change",
    [
        {"boxId": "8c4d"},                         # no 0x prefix
        {"boxId": "0x8c4d"},                       # wrong length
        {"batchId": "0x" + "zz" * 32},             # not hex
        {"kind": "1"},                             # string instead of number
        {"kind": True},                            # bool is not an integer here
        {"temperature": 40000},                    # outside int16
        {"counter": -1},                           # outside uint32
        {"startTime": 2**64},                      # outside uint64
        {"signature": "0x" + "00" * 64},           # 64 bytes, v missing
    ],
)
def test_rejects_malformed_fields(change):
    obj = json.loads(FIRMWARE_PAYLOAD)
    obj.update(change)
    with pytest.raises(PayloadError):
        parse_payload(json.dumps(obj).encode())


def test_rejects_missing_field_and_bad_json():
    obj = json.loads(FIRMWARE_PAYLOAD)
    del obj["peak"]
    with pytest.raises(PayloadError):
        parse_payload(json.dumps(obj).encode())
    with pytest.raises(PayloadError):
        parse_payload(b"{not json")
    with pytest.raises(PayloadError):
        parse_payload(b"[1, 2]")


def test_topic_parsing():
    assert box_name_from_topic("coldchain/box01/anomaly") == "box01"
    assert box_name_from_topic("coldchain/box01/telemetry") is None
    assert box_name_from_topic("coldchain//anomaly") is None
    assert box_name_from_topic("other/box01/anomaly") is None
