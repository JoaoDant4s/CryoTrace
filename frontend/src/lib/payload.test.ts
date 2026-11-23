import { describe, expect, it } from "vitest";
import { idOf } from "./names";
import { PayloadError, eventDigest, eventRuleViolation, parsePayload, recoverSigner, toPayloadJson } from "./payload";

// Shared vector with the firmware (test/host/test_signed_event.c) and the contract
// (contracts/test/FirmwareVector.t.sol): a fourth, independent EIP-712 implementation.
const CHAIN_ID = 11155111;
const CONTRACT = "0xC0FFEE00000000000000000000000000c0FFEe00";
const DEVICE = "0xF5A5E415061470A8b9137959180901aEa72450a4";
const DIGEST = "0xfe87e48fa206d1338445822589ef6bf432ecc32277dae7ae5b1454fd8e30e3b5";
const FIRMWARE_SIGNATURE =
  "0x08a7c85b3031fc6059aec942042ef1e10e1bc7b17ecf9fa752a3a1fc44c74cb55a2f3eca400efae2f3591feba1e1aad4e5dcd42b02381dad3d2a12883133eee81c";

function vectorJson(overrides: Record<string, unknown> = {}): string {
  return JSON.stringify({
    boxId: idOf("box01"),
    batchId: idOf("batch-demo-001"),
    kind: 1,
    anomalyType: 1,
    startTime: 1767225600000,
    endTime: 1767225615000,
    temperature: 790,
    peak: 1121,
    counter: 42,
    signature: FIRMWARE_SIGNATURE,
    ...overrides,
  });
}

describe("firmware test vector", () => {
  it("gives the same digest as the contract and the firmware", () => {
    const { event } = parsePayload(vectorJson());
    expect(eventDigest(event, CHAIN_ID, CONTRACT)).toBe(DIGEST);
  });

  it("recovers the device address from the firmware signature", async () => {
    expect(await recoverSigner(parsePayload(vectorJson()), CHAIN_ID, CONTRACT)).toBe(DEVICE);
  });

  it("recovers another address when a field is altered", async () => {
    const tampered = parsePayload(vectorJson({ peak: 750 })); // hides the 11.21 °C peak
    expect(await recoverSigner(tampered, CHAIN_ID, CONTRACT)).not.toBe(DEVICE);
  });

  it("binds the signature to the contract and the network", async () => {
    const signed = parsePayload(vectorJson());
    expect(await recoverSigner(signed, CHAIN_ID, "0xF8A328b314C89Ef22E3494886A00419Dcc434Dd4")).not.toBe(DEVICE);
    expect(await recoverSigner(signed, 1, CONTRACT)).not.toBe(DEVICE);
  });

  it("round-trips through the payload JSON", () => {
    const signed = parsePayload(vectorJson());
    expect(parsePayload(toPayloadJson(signed))).toEqual(signed);
  });
});

describe("parsePayload", () => {
  it.each([
    ["not json", "{", "not valid JSON"],
    ["array", "[]", "expected a JSON object"],
    ["short box id", vectorJson({ boxId: "0x1234" }), "boxId: expected 32 bytes"],
    ["missing 0x", vectorJson({ batchId: "ab".repeat(32) }), "batchId: expected a 0x-prefixed"],
    ["string number", vectorJson({ counter: "42" }), "counter: expected an integer"],
    ["fraction", vectorJson({ kind: 1.5 }), "kind: expected an integer"],
    ["int16 overflow", vectorJson({ temperature: 40000 }), "temperature: 40000 out of range"],
    ["negative time", vectorJson({ startTime: -1 }), "startTime: -1 out of range"],
    ["short signature", vectorJson({ signature: "0x" + "00".repeat(64) }), "signature: expected 65 bytes"],
  ])("rejects %s", (_name, text, message) => {
    expect(() => parsePayload(text)).toThrow(PayloadError);
    expect(() => parsePayload(text)).toThrow(message);
  });

  it("accepts the 'no reading' temperature", () => {
    expect(parsePayload(vectorJson({ temperature: -32768 })).event.temperature).toBe(-32768);
  });
});

describe("eventRuleViolation (same rules as the contract)", () => {
  const base = parsePayload(vectorJson()).event;
  it("accepts valid anomaly and recovery events", () => {
    expect(eventRuleViolation(base)).toBeNull();
    expect(eventRuleViolation({ ...base, kind: 0, endTime: 0n })).toBeNull();
    expect(eventRuleViolation({ ...base, startTime: 0n, endTime: 0n })).toBeNull(); // clock never synced
  });
  it("rejects invalid ones", () => {
    expect(eventRuleViolation({ ...base, kind: 0 })).toMatch(/endTime = 0/);
    expect(eventRuleViolation({ ...base, endTime: base.startTime - 1n })).toMatch(/end after/);
    expect(eventRuleViolation({ ...base, kind: 2 })).toMatch(/kind 2/);
    expect(eventRuleViolation({ ...base, anomalyType: 3 })).toMatch(/anomalyType 3/);
  });
});
