"""Decision logic for the retained batch topics (no chain, no broker)."""

from cryotrace_relayer.batches import (
    Assignment,
    BatchTracker,
    Check,
    batch_id_from_payload,
    batch_topic,
    box_id_from_topic,
)
from cryotrace_relayer.payload import box_id_of

BOX = box_id_of("box01")
TRIP1 = box_id_of("trip-1")
TRIP2 = box_id_of("trip-2")


def _assign(batch: bytes, block: int, box: bytes = BOX) -> Assignment:
    return Assignment(box_id=box, batch_id=batch, block_number=block, tx_hash="0x" + "11" * 32)


def _never_called(*_):
    raise AssertionError("the contract should not be queried")


def test_topic_round_trip():
    topic = batch_topic(BOX)
    assert topic == "cryotrace/0x8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb/batch"
    assert box_id_from_topic(topic) == BOX


def test_rejects_other_topics():
    for topic in ("cryotrace/box01/batch", "cryotrace/0x1234/batch", "cryotrace/box01/anomaly",
                  "cryotrace/0x" + "zz" * 32 + "/batch", "other/0x" + "00" * 32 + "/batch"):
        assert box_id_from_topic(topic) is None


def test_payload_round_trip():
    a = _assign(TRIP1, 10)
    assert batch_id_from_payload(a.payload(), BOX) == TRIP1
    assert batch_id_from_payload(a.payload(), box_id_of("box02")) is None  # for another box
    assert batch_id_from_payload(b"{not json", BOX) is None
    assert batch_id_from_payload(b'{"boxId": "0x00"}', BOX) is None


def test_current_is_the_latest_trip():
    t = BatchTracker()
    assert t.add(_assign(TRIP1, 10))
    assert t.add(_assign(TRIP2, 20))
    assert not t.add(_assign(TRIP2, 20))  # seen twice (overlapping scans)
    assert t.current(BOX).batch_id == TRIP2
    assert [a.batch_id for a in t.all_current()] == [TRIP2]


def test_check_current_assignment_is_ok():
    t = BatchTracker()
    t.add(_assign(TRIP1, 10))
    assert t.check(BOX, _assign(TRIP1, 10).payload(), _never_called) is Check.OK


def test_check_previous_trip_is_republished():
    t = BatchTracker()
    t.add(_assign(TRIP1, 10))
    t.add(_assign(TRIP2, 20))
    assert t.check(BOX, _assign(TRIP1, 10).payload(), _never_called) is Check.REPUBLISH


def test_check_forged_batch():
    forged = _assign(box_id_of("not-on-this-box"), 99).payload()
    t = BatchTracker()
    assert t.check(BOX, forged, lambda *_: False) is Check.CLEAR  # nothing to put back
    t.add(_assign(TRIP1, 10))
    assert t.check(BOX, forged, lambda *_: False) is Check.REPUBLISH


def test_check_newer_real_batch_means_we_are_behind():
    t = BatchTracker()
    t.add(_assign(TRIP1, 10))
    assert t.check(BOX, _assign(TRIP2, 20).payload(), lambda b, x: (b, x) == (TRIP2, BOX)) is Check.LAGGING


def test_check_malformed_or_cleared():
    t = BatchTracker()
    assert t.check(BOX, b"garbage", _never_called) is Check.CLEAR
    assert t.check(BOX, b"", _never_called) is Check.OK
    t.add(_assign(TRIP1, 10))
    assert t.check(BOX, b"garbage", _never_called) is Check.REPUBLISH
    assert t.check(BOX, b"", _never_called) is Check.REPUBLISH
