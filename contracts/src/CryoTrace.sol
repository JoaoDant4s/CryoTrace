// SPDX-License-Identifier: MIT
pragma solidity ^0.8.28;

import {AccessControl} from "@openzeppelin/contracts/access/AccessControl.sol";
import {ECDSA} from "@openzeppelin/contracts/utils/cryptography/ECDSA.sol";
import {EIP712} from "@openzeppelin/contracts/utils/cryptography/EIP712.sol";

/// @title CryoTrace — on-chain anchoring of vaccine cold-chain anomalies.
/// @notice Each thermal box (ESP32-S3) signs its anomaly/recovery events with
///         its own secp256k1 key (EIP-712). Anyone may submit them (in practice,
///         the relayer), because authorization comes from the device signature:
///         a relayer can delay or drop events, but never forge them.
contract CryoTrace is AccessControl, EIP712 {
    /// @notice Registers the device key of each box.
    bytes32 public constant PROVISIONER_ROLE = keccak256("PROVISIONER_ROLE");
    /// @notice Assigns vaccine batches to boxes.
    bytes32 public constant LOGISTICS_ROLE = keccak256("LOGISTICS_ROLE");

    uint8 public constant KIND_ANOMALY = 0;
    uint8 public constant KIND_RECOVERY = 1;
    uint8 public constant MAX_ANOMALY_TYPE = 2; // BELOW, ABOVE, SENSOR_OFFLINE

    /// @dev Must match the firmware byte for byte: any change here is a
    ///      breaking change to what the ESP32 signs.
    bytes32 public constant EVENT_TYPEHASH = keccak256(
        "ColdChainEvent(bytes32 boxId,bytes32 batchId,uint8 kind,uint8 anomalyType,"
        "uint64 startTime,uint64 endTime,int16 temperature,int16 peak,uint32 counter)"
    );

    struct ColdChainEvent {
        bytes32 boxId;       // keccak256 of the box id string, e.g. "box01"
        bytes32 batchId;     // keccak256 of the batch id string
        uint8 kind;          // KIND_ANOMALY or KIND_RECOVERY
        uint8 anomalyType;   // 0 = BELOW, 1 = ABOVE, 2 = SENSOR_OFFLINE
        uint64 startTime;    // epoch ms of the first out-of-range reading (0 = clock not synced)
        uint64 endTime;      // epoch ms of the return to range (0 for KIND_ANOMALY)
        int16 temperature;   // centi-degrees C of the triggering reading (-32768 = no reading)
        int16 peak;          // centi-degrees C of the reading furthest from the range
        uint32 counter;      // device counter, never reused
    }

    struct StoredEvent {
        ColdChainEvent data;
        uint64 recordedAt; // block timestamp (s): upper bound when startTime is 0
    }

    mapping(bytes32 boxId => address device) public deviceOf;
    mapping(bytes32 batchId => bytes32 boxId) public boxOfBatch;
    mapping(bytes32 digest => bool) public recorded;
    mapping(bytes32 batchId => StoredEvent[]) private _eventsOfBatch;

    event DeviceRegistered(bytes32 indexed boxId, address device);
    event BatchRegistered(bytes32 indexed batchId, bytes32 indexed boxId);
    event EventRecorded(
        bytes32 indexed batchId,
        bytes32 indexed boxId,
        uint8 kind,
        uint8 anomalyType,
        uint64 startTime,
        uint64 endTime,
        int16 temperature,
        int16 peak,
        uint32 counter,
        bytes32 digest
    );

    error ZeroAddress();
    error UnknownBox(bytes32 boxId);
    error BatchAlreadyRegistered(bytes32 batchId);
    error BatchNotOnBox(bytes32 batchId, bytes32 boxId);
    error InvalidSigner(address recovered, address expected);
    error AlreadyRecorded(bytes32 digest);
    error InvalidEvent();

    constructor(address admin) EIP712("CryoTrace", "1") {
        if (admin == address(0)) revert ZeroAddress();
        _grantRole(DEFAULT_ADMIN_ROLE, admin);
    }

    /// @notice Binds a box to the address derived from its device public key.
    ///         Calling it again replaces the key (rotation); events signed by
    ///         the old key are rejected from then on.
    function registerDevice(bytes32 boxId, address device) external onlyRole(PROVISIONER_ROLE) {
        if (device == address(0)) revert ZeroAddress();
        deviceOf[boxId] = device;
        emit DeviceRegistered(boxId, device);
    }

    /// @notice Assigns a batch to a box, once, when the batch is packed.
    function registerBatch(bytes32 batchId, bytes32 boxId) external onlyRole(LOGISTICS_ROLE) {
        if (deviceOf[boxId] == address(0)) revert UnknownBox(boxId);
        if (boxOfBatch[batchId] != bytes32(0)) revert BatchAlreadyRegistered(batchId);
        boxOfBatch[batchId] = boxId;
        emit BatchRegistered(batchId, boxId);
    }

    /// @notice Records an event signed by the box device. Callable by anyone.
    /// @dev Duplicates are detected by digest, not by counter order, so events
    ///      buffered offline may arrive out of order.
    function recordEvent(ColdChainEvent calldata ev, bytes calldata signature) external {
        _validate(ev);

        bytes32 digest = hashEvent(ev);
        if (recorded[digest]) revert AlreadyRecorded(digest);

        // ECDSA.recover rejects malformed and high-s (malleable) signatures.
        address signer = ECDSA.recover(digest, signature);
        address expected = deviceOf[ev.boxId];
        if (signer != expected) revert InvalidSigner(signer, expected);
        if (boxOfBatch[ev.batchId] != ev.boxId) revert BatchNotOnBox(ev.batchId, ev.boxId);

        recorded[digest] = true;
        _eventsOfBatch[ev.batchId].push(StoredEvent({data: ev, recordedAt: uint64(block.timestamp)}));
        emit EventRecorded(
            ev.batchId,
            ev.boxId,
            ev.kind,
            ev.anomalyType,
            ev.startTime,
            ev.endTime,
            ev.temperature,
            ev.peak,
            ev.counter,
            digest
        );
    }

    /// @notice Full history of a batch, for auditing (free to call).
    function getBatchEvents(bytes32 batchId) external view returns (StoredEvent[] memory) {
        return _eventsOfBatch[batchId];
    }

    function eventCount(bytes32 batchId) external view returns (uint256) {
        return _eventsOfBatch[batchId].length;
    }

    /// @notice EIP-712 digest the device signs. Exposed so the relayer and
    ///         firmware tests can check their encoding against the contract.
    function hashEvent(ColdChainEvent calldata ev) public view returns (bytes32) {
        return _hashTypedDataV4(structHash(ev));
    }

    function structHash(ColdChainEvent calldata ev) public pure returns (bytes32) {
        return keccak256(
            abi.encode(
                EVENT_TYPEHASH,
                ev.boxId,
                ev.batchId,
                ev.kind,
                ev.anomalyType,
                ev.startTime,
                ev.endTime,
                ev.temperature,
                ev.peak,
                ev.counter
            )
        );
    }

    function domainSeparator() external view returns (bytes32) {
        return _domainSeparatorV4();
    }

    function _validate(ColdChainEvent calldata ev) private pure {
        if (ev.anomalyType > MAX_ANOMALY_TYPE) revert InvalidEvent();
        if (ev.kind == KIND_ANOMALY) {
            if (ev.endTime != 0) revert InvalidEvent();
        } else if (ev.kind == KIND_RECOVERY) {
            // Both 0 is allowed (clock never synced); otherwise end >= start.
            if (ev.endTime < ev.startTime) revert InvalidEvent();
        } else {
            revert InvalidEvent();
        }
    }
}
