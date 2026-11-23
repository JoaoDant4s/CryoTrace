"""Batch announcements against the real contract on a local chain (anvil)."""

import json
import shutil
import socket
import subprocess
import time

import pytest
from web3 import Web3

from cryotrace_relayer.batches import BatchAnnouncer, Check, batch_topic
from cryotrace_relayer.chain import DEFAULT_ABI_PATH
from cryotrace_relayer.payload import box_id_of

pytestmark = pytest.mark.skipif(shutil.which("anvil") is None, reason="anvil not installed")

CONTRACTS_DIR = DEFAULT_ABI_PATH.parents[2]
BOX = box_id_of("box01")
BOX2 = box_id_of("box02")


@pytest.fixture
def cryo():
    if not DEFAULT_ABI_PATH.exists():
        subprocess.run(["forge", "build"], cwd=CONTRACTS_DIR, check=True, capture_output=True)
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
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
    w3.eth.default_account = admin
    factory = w3.eth.contract(abi=artifact["abi"], bytecode=artifact["bytecode"]["object"])
    address = w3.eth.wait_for_transaction_receipt(factory.constructor(admin).transact())["contractAddress"]
    contract = w3.eth.contract(address=address, abi=artifact["abi"])
    for role in (contract.functions.PROVISIONER_ROLE().call(), contract.functions.LOGISTICS_ROLE().call()):
        contract.functions.grantRole(role, admin).transact()
    for box in (BOX, BOX2):
        contract.functions.registerDevice(box, w3.eth.accounts[5]).transact()
    yield contract
    anvil.terminate()
    anvil.wait()


def _assign(contract, batch: str, box: bytes = BOX):
    contract.functions.registerBatch(box_id_of(batch), box).transact()


def _announcer(contract):
    published = []
    return BatchAnnouncer(contract, lambda topic, payload: published.append((topic, payload)),
                          chunk_blocks=3), published


def _batch_of(payload: bytes) -> bytes:
    return bytes.fromhex(json.loads(payload)["batchId"][2:])


def test_announces_the_latest_batch_of_each_box(cryo):
    _assign(cryo, "trip-1")
    _assign(cryo, "other-box-trip", BOX2)
    _assign(cryo, "trip-2")  # box01 reused for its next trip
    announcer, published = _announcer(cryo)

    announcer.poll()  # small chunks: the scan spans several get_logs calls
    assert {(t, _batch_of(p)) for t, p in published} == {
        (batch_topic(BOX), box_id_of("trip-2")),
        (batch_topic(BOX2), box_id_of("other-box-trip")),
    }


def test_announces_new_assignments_once(cryo):
    announcer, published = _announcer(cryo)
    announcer.poll()
    assert published == []

    _assign(cryo, "trip-1")
    announcer.poll()
    announcer.poll()  # nothing new: no second publish
    assert [(t, _batch_of(p)) for t, p in published] == [(batch_topic(BOX), box_id_of("trip-1"))]


def test_repairs_a_forged_retained_message(cryo):
    _assign(cryo, "trip-1")
    announcer, published = _announcer(cryo)
    announcer.poll()
    published.clear()

    forged = json.dumps({"boxId": "0x" + BOX.hex(), "batchId": "0x" + box_id_of("fake").hex()}).encode()
    assert announcer.on_retained(batch_topic(BOX), forged) is Check.REPUBLISH
    assert [(t, _batch_of(p)) for t, p in published] == [(batch_topic(BOX), box_id_of("trip-1"))]


def test_accepts_a_batch_from_another_box_only_if_on_chain(cryo):
    _assign(cryo, "other-box-trip", BOX2)
    announcer, published = _announcer(cryo)
    # box01 has no batch: a message naming box02's batch is forged, and is cleared
    wrong = json.dumps({"boxId": "0x" + BOX.hex(), "batchId": "0x" + box_id_of("other-box-trip").hex()}).encode()
    assert announcer.on_retained(batch_topic(BOX), wrong) is Check.CLEAR
    assert published == [(batch_topic(BOX), b"")]


def test_catches_up_when_another_relayer_is_ahead(cryo):
    _assign(cryo, "trip-1")
    announcer, published = _announcer(cryo)
    announcer.poll()
    _assign(cryo, "trip-2")  # assigned after our last scan; another relayer already announced it
    newer = json.dumps({"boxId": "0x" + BOX.hex(), "batchId": "0x" + box_id_of("trip-2").hex()}).encode()

    assert announcer.on_retained(batch_topic(BOX), newer) is Check.LAGGING
    assert announcer.tracker.current(BOX).batch_id == box_id_of("trip-2")  # caught up by polling


def test_republish_all_after_reconnect(cryo):
    _assign(cryo, "trip-1")
    _assign(cryo, "other-box-trip", BOX2)
    announcer, published = _announcer(cryo)
    announcer.poll()
    published.clear()
    announcer.republish_all()
    assert len(published) == 2
