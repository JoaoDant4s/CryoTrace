import type { Address, Hash, Hex } from "viem";
import { KIND_ANOMALY } from "./format";

/** The fields of a contract log the registry needs (a subset of viem's decoded log). */
export interface RegistryLog {
  eventName: string;
  args: { boxId?: Hex; batchId?: Hex; device?: Address; kind?: number; [other: string]: unknown };
  blockNumber: bigint;
  logIndex: number;
  transactionHash: Hash;
}

export interface DeviceKey {
  device: Address;
  blockNumber: bigint;
  transactionHash: Hash;
}

export interface BoxEntry {
  boxId: Hex;
  /** Key the contract accepts now (the last one registered). */
  device: Address;
  /** Every key registered for the box, oldest first; more than one means it was rotated. */
  keys: DeviceKey[];
  /** Batches assigned to the box, oldest first (one per trip). */
  batches: Hex[];
}

export interface BatchEntry {
  batchId: Hex;
  boxId: Hex;
  blockNumber: bigint;
  transactionHash: Hash;
  /** Box key when the batch was assigned. */
  deviceAtAssignment?: Address;
  /**
   * Keys that signed the batch's recorded events. The contract accepts an event
   * only if it is signed by the box's key at that moment, so this is known
   * exactly from the order of the logs.
   */
  signers: Address[];
  events: number;
  anomalies: number;
  lastEventBlock?: bigint;
}

export interface Registry {
  boxes: BoxEntry[];
  batches: BatchEntry[];
}

function byChainOrder(a: RegistryLog, b: RegistryLog): number {
  return a.blockNumber === b.blockNumber ? a.logIndex - b.logIndex : a.blockNumber < b.blockNumber ? -1 : 1;
}

/** Rebuilds boxes and batches from the contract logs. */
export function buildRegistry(logs: readonly RegistryLog[]): Registry {
  const boxes = new Map<Hex, BoxEntry>();
  const batches = new Map<Hex, BatchEntry>();

  for (const log of [...logs].sort(byChainOrder)) {
    const { boxId, batchId, device, kind } = log.args;
    if (log.eventName === "DeviceRegistered" && boxId && device) {
      const key = { device, blockNumber: log.blockNumber, transactionHash: log.transactionHash };
      const box = boxes.get(boxId);
      if (box) {
        box.device = device;
        box.keys.push(key);
      } else {
        boxes.set(boxId, { boxId, device, keys: [key], batches: [] });
      }
    } else if (log.eventName === "BatchRegistered" && boxId && batchId) {
      const box = boxes.get(boxId);
      box?.batches.push(batchId);
      batches.set(batchId, {
        batchId,
        boxId,
        blockNumber: log.blockNumber,
        transactionHash: log.transactionHash,
        deviceAtAssignment: box?.device,
        signers: [],
        events: 0,
        anomalies: 0,
      });
    } else if (log.eventName === "EventRecorded" && boxId && batchId) {
      const batch = batches.get(batchId);
      if (!batch) continue;
      batch.events++;
      if (kind === KIND_ANOMALY) batch.anomalies++;
      batch.lastEventBlock = log.blockNumber;
      const signer = boxes.get(boxId)?.device;
      if (signer && !batch.signers.includes(signer)) batch.signers.push(signer);
    }
  }

  return {
    boxes: [...boxes.values()],
    batches: [...batches.values()].reverse(), // newest first
  };
}
