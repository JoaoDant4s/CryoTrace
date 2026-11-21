// SPDX-License-Identifier: MIT
pragma solidity ^0.8.28;

import {Test} from "forge-std/Test.sol";
import {ECDSA} from "@openzeppelin/contracts/utils/cryptography/ECDSA.sol";
import {IAccessControl} from "@openzeppelin/contracts/access/IAccessControl.sol";
import {CryoTrace} from "../src/CryoTrace.sol";

contract CryoTraceTest is Test {
    CryoTrace internal cryo;

    address internal admin = makeAddr("admin");
    address internal provisioner = makeAddr("provisioner");
    address internal logistics = makeAddr("logistics");
    address internal relayer = makeAddr("relayer");

    // Plays the role of the ESP32 key.
    uint256 internal devicePk = 0xC0FFEE;
    address internal device;

    bytes32 internal constant BOX = keccak256("box01");
    bytes32 internal constant BATCH = keccak256("batch-demo-001");

    // secp256k1 group order, used to build a malleable (high-s) signature.
    uint256 internal constant SECP256K1_N =
        0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141;

    function setUp() public {
        device = vm.addr(devicePk);
        cryo = _deployConfigured();
    }

    // ---------------------------------------------------------------- helpers

    function _deployConfigured() internal returns (CryoTrace c) {
        c = new CryoTrace(admin);
        vm.startPrank(admin);
        c.grantRole(c.PROVISIONER_ROLE(), provisioner);
        c.grantRole(c.LOGISTICS_ROLE(), logistics);
        vm.stopPrank();
        vm.prank(provisioner);
        c.registerDevice(BOX, device);
        vm.prank(logistics);
        c.registerBatch(BATCH, BOX);
    }

    function _anomaly(uint32 counter) internal pure returns (CryoTrace.ColdChainEvent memory) {
        return CryoTrace.ColdChainEvent({
            boxId: BOX,
            batchId: BATCH,
            kind: 0,
            anomalyType: 0, // ABOVE
            startTime: 1_767_225_600_000,
            endTime: 0,
            temperature: 812, // 8.12 C
            peak: 845,
            counter: counter
        });
    }

    function _recovery(uint32 counter) internal pure returns (CryoTrace.ColdChainEvent memory ev) {
        ev = _anomaly(counter);
        ev.kind = 1;
        ev.endTime = ev.startTime + 15_000;
        ev.temperature = 790;
        ev.peak = 1121;
    }

    function _sign(CryoTrace c, uint256 pk, CryoTrace.ColdChainEvent memory ev)
        internal
        view
        returns (bytes memory)
    {
        (uint8 v, bytes32 r, bytes32 s) = vm.sign(pk, c.hashEvent(ev));
        return abi.encodePacked(r, s, v);
    }

    // ------------------------------------------------------------ happy path

    function test_RecordsSignedAnomalyAndRecovery() public {
        CryoTrace.ColdChainEvent memory start = _anomaly(1);
        CryoTrace.ColdChainEvent memory end = _recovery(2);

        vm.startPrank(relayer);
        cryo.recordEvent(start, _sign(cryo, devicePk, start));
        cryo.recordEvent(end, _sign(cryo, devicePk, end));
        vm.stopPrank();

        CryoTrace.StoredEvent[] memory events = cryo.getBatchEvents(BATCH);
        assertEq(events.length, 2);
        assertEq(events[0].data.kind, 0);
        assertEq(events[0].data.temperature, 812);
        assertEq(events[1].data.kind, 1);
        // The auditor pairs start and recovery by startTime.
        assertEq(events[1].data.startTime, events[0].data.startTime);
        assertEq(events[1].data.endTime - events[1].data.startTime, 15_000);
        assertEq(events[0].recordedAt, block.timestamp);
    }

    function test_EmitsEventRecorded() public {
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        bytes memory sig = _sign(cryo, devicePk, ev);

        vm.expectEmit(true, true, false, true, address(cryo));
        emit CryoTrace.EventRecorded(
            BATCH, BOX, 0, 0, ev.startTime, 0, 812, 845, 1, cryo.hashEvent(ev)
        );
        cryo.recordEvent(ev, sig);
    }

    function test_AcceptsCountersOutOfOrder() public {
        // Events buffered offline may reach the chain out of order.
        CryoTrace.ColdChainEvent memory later = _anomaly(7);
        CryoTrace.ColdChainEvent memory earlier = _anomaly(3);
        cryo.recordEvent(later, _sign(cryo, devicePk, later));
        cryo.recordEvent(earlier, _sign(cryo, devicePk, earlier));
        assertEq(cryo.eventCount(BATCH), 2);
    }

    function test_AcceptsUnsyncedClock() public {
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        ev.startTime = 0;
        ev.anomalyType = 2; // SENSOR_OFFLINE
        ev.temperature = type(int16).min;
        cryo.recordEvent(ev, _sign(cryo, devicePk, ev));
        assertEq(cryo.eventCount(BATCH), 1);
    }

    // ------------------------------------------------------- forgery & replay

    function test_RejectsSignatureFromAnotherKey() public {
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        uint256 attackerPk = 0xBAD;
        bytes memory sig = _sign(cryo, attackerPk, ev);

        vm.expectRevert(
            abi.encodeWithSelector(CryoTrace.InvalidSigner.selector, vm.addr(attackerPk), device)
        );
        cryo.recordEvent(ev, sig);
    }

    /// Tamper test: the relayer changes a signed field before submitting.
    function test_RejectsTamperedEvent() public {
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        bytes memory sig = _sign(cryo, devicePk, ev);

        ev.temperature = 500; // hides the excursion: 8.12 C -> 5.00 C
        // The signature now recovers to an unrelated address.
        vm.expectPartialRevert(CryoTrace.InvalidSigner.selector);
        cryo.recordEvent(ev, sig);
        assertEq(cryo.eventCount(BATCH), 0);
    }

    function test_RejectsReplay() public {
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        bytes memory sig = _sign(cryo, devicePk, ev);
        cryo.recordEvent(ev, sig);

        // e.g. an MQTT QoS 1 duplicate forwarded twice
        vm.expectRevert(abi.encodeWithSelector(CryoTrace.AlreadyRecorded.selector, cryo.hashEvent(ev)));
        cryo.recordEvent(ev, sig);
    }

    function test_RejectsSignatureFromAnotherContract() public {
        // Same box, batch and key, but a different deployment (domain).
        CryoTrace other = _deployConfigured();
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        bytes memory sigForOther = _sign(other, devicePk, ev);

        vm.expectPartialRevert(CryoTrace.InvalidSigner.selector);
        cryo.recordEvent(ev, sigForOther);
    }

    function test_RejectsSignatureForAnotherChain() public {
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        bytes memory sig = _sign(cryo, devicePk, ev);

        vm.chainId(block.chainid + 1);
        vm.expectPartialRevert(CryoTrace.InvalidSigner.selector);
        cryo.recordEvent(ev, sig);
    }

    function test_RejectsHighSSignature() public {
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        (uint8 v, bytes32 r, bytes32 s) = vm.sign(devicePk, cryo.hashEvent(ev));
        bytes32 highS = bytes32(SECP256K1_N - uint256(s));
        uint8 flippedV = v == 27 ? 28 : 27;

        vm.expectRevert(abi.encodeWithSelector(ECDSA.ECDSAInvalidSignatureS.selector, highS));
        cryo.recordEvent(ev, abi.encodePacked(r, highS, flippedV));
    }

    function test_RejectsRotatedKey() public {
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        bytes memory oldSig = _sign(cryo, devicePk, ev);

        vm.prank(provisioner);
        cryo.registerDevice(BOX, vm.addr(0xBEEF));

        vm.expectPartialRevert(CryoTrace.InvalidSigner.selector);
        cryo.recordEvent(ev, oldSig);
    }

    // ------------------------------------------------------ batch attribution

    function test_RejectsBatchNotOnBox() public {
        bytes32 otherBatch = keccak256("batch-other");
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        ev.batchId = otherBatch;
        bytes memory sig = _sign(cryo, devicePk, ev);

        vm.expectRevert(abi.encodeWithSelector(CryoTrace.BatchNotOnBox.selector, otherBatch, BOX));
        cryo.recordEvent(ev, sig);
    }

    function test_KeepsBatchOfLateEventAfterBoxGetsNewBatch() public {
        // Event signed while carrying BATCH, submitted after a new batch was packed.
        CryoTrace.ColdChainEvent memory ev = _anomaly(1);
        bytes memory sig = _sign(cryo, devicePk, ev);

        bytes32 nextBatch = keccak256("batch-002");
        vm.prank(logistics);
        cryo.registerBatch(nextBatch, BOX);

        cryo.recordEvent(ev, sig);
        assertEq(cryo.eventCount(BATCH), 1);
        assertEq(cryo.eventCount(nextBatch), 0);
    }

    // ------------------------------------------------------ event validation

    function test_RejectsMalformedEvents() public {
        CryoTrace.ColdChainEvent memory ev;

        ev = _anomaly(1);
        ev.kind = 2;
        _expectInvalidEvent(ev);

        ev = _anomaly(1);
        ev.anomalyType = 3;
        _expectInvalidEvent(ev);

        ev = _anomaly(1);
        ev.endTime = ev.startTime + 1; // ANOMALY must not carry an end time
        _expectInvalidEvent(ev);

        ev = _recovery(1);
        ev.endTime = ev.startTime - 1; // ends before it starts
        _expectInvalidEvent(ev);
    }

    function _expectInvalidEvent(CryoTrace.ColdChainEvent memory ev) internal {
        // Sign first: expectRevert applies to the next external call.
        bytes memory sig = _sign(cryo, devicePk, ev);
        vm.expectRevert(CryoTrace.InvalidEvent.selector);
        cryo.recordEvent(ev, sig);
    }

    // --------------------------------------------------------- access control

    function test_OnlyProvisionerRegistersDevices() public {
        vm.expectRevert(
            abi.encodeWithSelector(
                IAccessControl.AccessControlUnauthorizedAccount.selector, relayer, cryo.PROVISIONER_ROLE()
            )
        );
        vm.prank(relayer);
        cryo.registerDevice(keccak256("box99"), relayer);
    }

    function test_OnlyLogisticsRegistersBatches() public {
        vm.expectRevert(
            abi.encodeWithSelector(
                IAccessControl.AccessControlUnauthorizedAccount.selector, relayer, cryo.LOGISTICS_ROLE()
            )
        );
        vm.prank(relayer);
        cryo.registerBatch(keccak256("batch-x"), BOX);
    }

    function test_BatchRegistrationRules() public {
        vm.startPrank(logistics);
        vm.expectRevert(abi.encodeWithSelector(CryoTrace.BatchAlreadyRegistered.selector, BATCH));
        cryo.registerBatch(BATCH, BOX);

        bytes32 unknownBox = keccak256("box99");
        vm.expectRevert(abi.encodeWithSelector(CryoTrace.UnknownBox.selector, unknownBox));
        cryo.registerBatch(keccak256("batch-x"), unknownBox);
        vm.stopPrank();
    }

    // ------------------------------------------------ encoding spec (firmware)

    /// Spells out the exact bytes the firmware must hash, independently of
    /// OpenZeppelin, so a mismatch in the contract encoding is caught here.
    function test_DigestMatchesEip712Spec() public view {
        CryoTrace.ColdChainEvent memory ev = _recovery(42);

        bytes32 domain = keccak256(
            abi.encode(
                keccak256("EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)"),
                keccak256("CryoTrace"),
                keccak256("1"),
                block.chainid,
                address(cryo)
            )
        );
        bytes32 structHash = keccak256(
            abi.encode(
                keccak256(
                    "ColdChainEvent(bytes32 boxId,bytes32 batchId,uint8 kind,uint8 anomalyType,"
                    "uint64 startTime,uint64 endTime,int16 temperature,int16 peak,uint32 counter)"
                ),
                ev.boxId,
                ev.batchId,
                uint256(ev.kind),
                uint256(ev.anomalyType),
                uint256(ev.startTime),
                uint256(ev.endTime),
                int256(ev.temperature), // sign-extended to 32 bytes
                int256(ev.peak),
                uint256(ev.counter)
            )
        );

        assertEq(cryo.domainSeparator(), domain);
        assertEq(cryo.structHash(ev), structHash);
        assertEq(cryo.hashEvent(ev), keccak256(abi.encodePacked("\x19\x01", domain, structHash)));
    }
}
