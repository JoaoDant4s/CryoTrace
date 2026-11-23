// SPDX-License-Identifier: MIT
pragma solidity ^0.8.28;

import {Script, console} from "forge-std/Script.sol";
import {CryoTrace} from "../src/CryoTrace.sol";

/// Registers a box (its ESP32 device address) and assigns a batch to it.
/// Safe to rerun: steps already done are skipped.
///
///   CONTRACT_ADDRESS=0x... BOX_NAME=box01 DEVICE_ADDRESS=0x... BATCH_NAME=batch-demo-001 \
///   forge script script/Provision.s.sol --rpc-url sepolia --account <keystore> --broadcast
///
/// BOX_NAME and BATCH_NAME are the same names configured in the firmware
/// (menuconfig > CryoTrace); DEVICE_ADDRESS is the one the ESP32 logs at boot.
contract Provision is Script {
    function run() external {
        CryoTrace cryo = CryoTrace(vm.envAddress("CONTRACT_ADDRESS"));
        string memory box = vm.envString("BOX_NAME");
        string memory batch = vm.envString("BATCH_NAME");
        address device = vm.envAddress("DEVICE_ADDRESS");
        bytes32 boxId = keccak256(bytes(box));
        bytes32 batchId = keccak256(bytes(batch));

        vm.startBroadcast();
        if (cryo.deviceOf(boxId) == device) {
            console.log("box already registered with this device:", box);
        } else {
            cryo.registerDevice(boxId, device);
            console.log("registered box", box, "device", device);
        }

        bytes32 currentBox = cryo.boxOfBatch(batchId);
        if (currentBox == boxId) {
            console.log("batch already assigned to this box:", batch);
        } else if (currentBox != bytes32(0)) {
            revert("batch already assigned to another box");
        } else {
            cryo.registerBatch(batchId, boxId);
            console.log("assigned batch", batch, "to box", box);
        }
        vm.stopBroadcast();
    }
}
