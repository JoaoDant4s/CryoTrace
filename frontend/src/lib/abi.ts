import { parseAbi } from "viem";

/**
 * The part of the CryoTrace ABI the app uses, written out so viem can type every
 * call. abi.test.ts checks it against the Foundry build output.
 */
export const cryoTraceAbi = parseAbi([
  "struct ColdChainEvent { bytes32 boxId; bytes32 batchId; uint8 kind; uint8 anomalyType; uint64 startTime; uint64 endTime; int16 temperature; int16 peak; uint32 counter; }",
  "struct StoredEvent { ColdChainEvent data; uint64 recordedAt; }",

  "function DEFAULT_ADMIN_ROLE() view returns (bytes32)",
  "function PROVISIONER_ROLE() view returns (bytes32)",
  "function LOGISTICS_ROLE() view returns (bytes32)",
  "function hasRole(bytes32 role, address account) view returns (bool)",
  "function grantRole(bytes32 role, address account)",
  "function revokeRole(bytes32 role, address account)",

  "function deviceOf(bytes32 boxId) view returns (address)",
  "function boxOfBatch(bytes32 batchId) view returns (bytes32)",
  "function recorded(bytes32 digest) view returns (bool)",
  "function getBatchEvents(bytes32 batchId) view returns (StoredEvent[])",
  "function eventCount(bytes32 batchId) view returns (uint256)",
  "function hashEvent(ColdChainEvent ev) view returns (bytes32)",
  "function domainSeparator() view returns (bytes32)",

  "function registerDevice(bytes32 boxId, address device)",
  "function registerBatch(bytes32 batchId, bytes32 boxId)",
  "function recordEvent(ColdChainEvent ev, bytes signature)",

  "event DeviceRegistered(bytes32 indexed boxId, address device)",
  "event BatchRegistered(bytes32 indexed batchId, bytes32 indexed boxId)",
  "event EventRecorded(bytes32 indexed batchId, bytes32 indexed boxId, uint8 kind, uint8 anomalyType, uint64 startTime, uint64 endTime, int16 temperature, int16 peak, uint32 counter, bytes32 digest)",
  "event RoleGranted(bytes32 indexed role, address indexed account, address indexed sender)",
  "event RoleRevoked(bytes32 indexed role, address indexed account, address indexed sender)",

  "error ZeroAddress()",
  "error UnknownBox(bytes32 boxId)",
  "error BatchAlreadyRegistered(bytes32 batchId)",
  "error BatchNotOnBox(bytes32 batchId, bytes32 boxId)",
  "error InvalidSigner(address recovered, address expected)",
  "error AlreadyRecorded(bytes32 digest)",
  "error InvalidEvent()",
  "error AccessControlUnauthorizedAccount(address account, bytes32 neededRole)",
  "error AccessControlBadConfirmation()",
  "error ECDSAInvalidSignature()",
  "error ECDSAInvalidSignatureLength(uint256 length)",
  "error ECDSAInvalidSignatureS(bytes32 s)",
]);
