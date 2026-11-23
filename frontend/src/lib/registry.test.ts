import type { Address, Hash } from "viem";
import { describe, expect, it } from "vitest";
import { idOf } from "./names";
import { type RegistryLog, buildRegistry } from "./registry";

const BOX = idOf("box01");
const BOX2 = idOf("box02");
const KEY_A = "0x00000000000000000000000000000000000000aA" as Address;
const KEY_B = "0x00000000000000000000000000000000000000bB" as Address;
const TX = ("0x" + "11".repeat(32)) as Hash;

let block = 100n;
function log(eventName: string, args: RegistryLog["args"]): RegistryLog {
  return { eventName, args, blockNumber: block++, logIndex: 0, transactionHash: TX };
}

describe("buildRegistry", () => {
  it("is empty without logs", () => {
    expect(buildRegistry([])).toEqual({ boxes: [], batches: [] });
  });

  it("lists boxes with their keys and batches, and batches with their signers", () => {
    const logs = [
      log("RoleGranted", {}),
      log("DeviceRegistered", { boxId: BOX, device: KEY_A }),
      log("BatchRegistered", { batchId: idOf("trip-1"), boxId: BOX }),
      log("EventRecorded", { batchId: idOf("trip-1"), boxId: BOX, kind: 0 }),
      log("EventRecorded", { batchId: idOf("trip-1"), boxId: BOX, kind: 1 }),
      // the box is reused for a second trip, and its key is rotated mid-trip
      log("BatchRegistered", { batchId: idOf("trip-2"), boxId: BOX }),
      log("EventRecorded", { batchId: idOf("trip-2"), boxId: BOX, kind: 0 }),
      log("DeviceRegistered", { boxId: BOX, device: KEY_B }),
      log("EventRecorded", { batchId: idOf("trip-2"), boxId: BOX, kind: 1 }),
      log("DeviceRegistered", { boxId: BOX2, device: KEY_A }),
    ];
    const { boxes, batches } = buildRegistry(logs);

    expect(boxes.map((b) => [b.boxId, b.device, b.keys.length, b.batches])).toEqual([
      [BOX, KEY_B, 2, [idOf("trip-1"), idOf("trip-2")]],
      [BOX2, KEY_A, 1, []],
    ]);
    expect(batches.map((b) => [b.batchId, b.events, b.anomalies, b.deviceAtAssignment, b.signers])).toEqual([
      [idOf("trip-2"), 2, 1, KEY_A, [KEY_A, KEY_B]],
      [idOf("trip-1"), 2, 1, KEY_A, [KEY_A]],
    ]);
  });

  it("orders logs by block and log index", () => {
    const later = { ...log("BatchRegistered", { batchId: idOf("t"), boxId: BOX }), blockNumber: 5n, logIndex: 1 };
    const earlier = { ...log("DeviceRegistered", { boxId: BOX, device: KEY_A }), blockNumber: 5n, logIndex: 0 };
    const { batches } = buildRegistry([later, earlier]);
    expect(batches[0].deviceAtAssignment).toBe(KEY_A);
  });
});
