// SPDX-License-Identifier: MIT
pragma solidity ^0.8.28;

import {Test, console} from "forge-std/Test.sol";
import {CryoTrace} from "../src/CryoTrace.sol";

/// Shared test vector with the firmware host tests (test/host/test_signed_event.c):
/// same chain id, contract address, device key and event on both sides.
contract FirmwareVectorTest is Test {
    uint256 internal constant CHAIN_ID = 11155111; // Sepolia
    address internal constant CONTRACT = address(0xC0FFEE00000000000000000000000000c0FFEe00);
    uint256 internal constant DEVICE_PK = 0xC0FFEE;

    CryoTrace internal cryo;

    function setUp() public {
        vm.chainId(CHAIN_ID);
        address admin = makeAddr("admin");
        deployCodeTo("CryoTrace.sol:CryoTrace", abi.encode(admin), CONTRACT);
        cryo = CryoTrace(CONTRACT);

        vm.startPrank(admin);
        cryo.grantRole(cryo.PROVISIONER_ROLE(), admin);
        cryo.grantRole(cryo.LOGISTICS_ROLE(), admin);
        cryo.registerDevice(keccak256("box01"), vm.addr(DEVICE_PK));
        cryo.registerBatch(keccak256("batch-demo-001"), keccak256("box01"));
        vm.stopPrank();
    }

    function _vectorEvent() internal pure returns (CryoTrace.ColdChainEvent memory) {
        return CryoTrace.ColdChainEvent({
            boxId: keccak256("box01"),
            batchId: keccak256("batch-demo-001"),
            kind: 1, // RECOVERY
            anomalyType: 0, // ABOVE
            startTime: 1_767_225_600_000,
            endTime: 1_767_225_615_000,
            temperature: 790,
            peak: 1121,
            counter: 42
        });
    }

    /// Signature produced by the firmware code (main/signed_event.c) for the
    /// vector event, checked byte for byte in test/host/test_signed_event.c.
    bytes internal constant FIRMWARE_SIGNATURE =
        hex"028e9e5fa9cfadd4baa59be30fa4b0e04212925aef1f6826cff3e559c437b7db"
        hex"1f172a209746ba8c7b51c0680424bd1d77f1f229b6441a0b671e23b6a5bafb7a" hex"1b";

    function test_ContractAcceptsFirmwareSignature() public {
        cryo.recordEvent(_vectorEvent(), FIRMWARE_SIGNATURE);

        CryoTrace.StoredEvent[] memory events = cryo.getBatchEvents(keccak256("batch-demo-001"));
        assertEq(events.length, 1);
        assertEq(events[0].data.counter, 42);
    }

    /// Run with -vv to regenerate the values hardcoded in the firmware tests.
    function test_PrintVector() public view {
        CryoTrace.ColdChainEvent memory ev = _vectorEvent();
        (uint8 v, bytes32 r, bytes32 s) = vm.sign(DEVICE_PK, cryo.hashEvent(ev));
        console.log("device address");
        console.logAddress(vm.addr(DEVICE_PK));
        console.log("domain separator");
        console.logBytes32(cryo.domainSeparator());
        console.log("struct hash");
        console.logBytes32(cryo.structHash(ev));
        console.log("digest");
        console.logBytes32(cryo.hashEvent(ev));
        console.log("signature");
        console.logBytes(abi.encodePacked(r, s, v));
    }
}
