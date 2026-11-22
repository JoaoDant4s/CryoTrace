// SPDX-License-Identifier: MIT
pragma solidity ^0.8.28;

import {Script, console} from "forge-std/Script.sol";
import {CryoTrace} from "../src/CryoTrace.sol";

/// Deploys CryoTrace. The deployer becomes admin and, unless PROVISIONER /
/// LOGISTICS are set, also holds both operational roles (enough for the demo).
///
///   forge script script/Deploy.s.sol --rpc-url sepolia --account <keystore> --broadcast
contract Deploy is Script {
    function run() external returns (CryoTrace cryo) {
        vm.startBroadcast();
        // The broadcasting wallet: msg.sender is not set to it when using --account.
        (, address deployer,) = vm.readCallers();
        address provisioner = vm.envOr("PROVISIONER", deployer);
        address logistics = vm.envOr("LOGISTICS", deployer);

        cryo = new CryoTrace(deployer);
        cryo.grantRole(cryo.PROVISIONER_ROLE(), provisioner);
        cryo.grantRole(cryo.LOGISTICS_ROLE(), logistics);
        vm.stopBroadcast();

        console.log("CryoTrace deployed at", address(cryo));
        console.log("chain id            ", block.chainid);
        console.log("admin               ", deployer);
        console.log("provisioner         ", provisioner);
        console.log("logistics           ", logistics);
    }
}
