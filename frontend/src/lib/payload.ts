import { type Address, type Hex, hashTypedData, recoverTypedDataAddress } from "viem";
import { KIND_ANOMALY, KIND_RECOVERY } from "./format";

/** The event a box signs: same fields, names and order as the contract struct. */
export interface ColdChainEvent {
  boxId: Hex;
  batchId: Hex;
  kind: number;
  anomalyType: number;
  startTime: bigint;
  endTime: bigint;
  temperature: number;
  peak: number;
  counter: number;
}

export interface SignedEvent {
  event: ColdChainEvent;
  signature: Hex;
}

export const EVENT_TYPES = {
  ColdChainEvent: [
    { name: "boxId", type: "bytes32" },
    { name: "batchId", type: "bytes32" },
    { name: "kind", type: "uint8" },
    { name: "anomalyType", type: "uint8" },
    { name: "startTime", type: "uint64" },
    { name: "endTime", type: "uint64" },
    { name: "temperature", type: "int16" },
    { name: "peak", type: "int16" },
    { name: "counter", type: "uint32" },
  ],
} as const;

export function eip712Domain(chainId: number, verifyingContract: Address) {
  return { name: "CryoTrace", version: "1", chainId, verifyingContract } as const;
}

export class PayloadError extends Error {}

function hexField(obj: Record<string, unknown>, field: string, bytes: number): Hex {
  const value = obj[field];
  if (typeof value !== "string" || !value.startsWith("0x")) {
    throw new PayloadError(`${field}: expected a 0x-prefixed hex string`);
  }
  if (!/^0x[0-9a-fA-F]*$/.test(value)) throw new PayloadError(`${field}: invalid hex`);
  const length = (value.length - 2) / 2;
  if (length !== bytes) throw new PayloadError(`${field}: expected ${bytes} bytes, got ${length}`);
  return value.toLowerCase() as Hex;
}

function intField(obj: Record<string, unknown>, field: string, low: bigint, high: bigint): bigint {
  const value = obj[field];
  if (typeof value !== "number" || !Number.isInteger(value)) {
    throw new PayloadError(`${field}: expected an integer`);
  }
  const n = BigInt(value);
  if (n < low || n > high) throw new PayloadError(`${field}: ${value} out of range [${low}, ${high}]`);
  return n;
}

/**
 * Parses the JSON a box publishes on coldchain/{box}/anomaly. Mirrors the
 * relayer's parser (relayer/cryotrace_relayer/payload.py): only types and
 * ranges are checked here; the signature and business rules are checked
 * separately, as the contract does.
 */
export function parsePayload(text: string): SignedEvent {
  let obj: unknown;
  try {
    obj = JSON.parse(text);
  } catch {
    throw new PayloadError("not valid JSON");
  }
  if (typeof obj !== "object" || obj === null || Array.isArray(obj)) {
    throw new PayloadError("expected a JSON object");
  }
  const o = obj as Record<string, unknown>;
  const event: ColdChainEvent = {
    boxId: hexField(o, "boxId", 32),
    batchId: hexField(o, "batchId", 32),
    kind: Number(intField(o, "kind", 0n, 255n)),
    anomalyType: Number(intField(o, "anomalyType", 0n, 255n)),
    startTime: intField(o, "startTime", 0n, 2n ** 64n - 1n),
    endTime: intField(o, "endTime", 0n, 2n ** 64n - 1n),
    temperature: Number(intField(o, "temperature", -(2n ** 15n), 2n ** 15n - 1n)),
    peak: Number(intField(o, "peak", -(2n ** 15n), 2n ** 15n - 1n)),
    counter: Number(intField(o, "counter", 0n, 2n ** 32n - 1n)),
  };
  return { event, signature: hexField(o, "signature", 65) };
}

/** Same rules as CryoTrace._validate; returns the problem, or null if valid. */
export function eventRuleViolation(ev: ColdChainEvent): string | null {
  if (ev.anomalyType > 2) return `anomalyType ${ev.anomalyType} is not 0, 1 or 2`;
  if (ev.kind === KIND_ANOMALY) {
    return ev.endTime === 0n ? null : "an anomaly event must have endTime = 0";
  }
  if (ev.kind === KIND_RECOVERY) {
    return ev.endTime >= ev.startTime ? null : "a recovery must end after it starts";
  }
  return `kind ${ev.kind} is not 0 (anomaly) or 1 (recovery)`;
}

/** EIP-712 digest the device signs (equals CryoTrace.hashEvent). */
export function eventDigest(ev: ColdChainEvent, chainId: number, contract: Address): Hex {
  return hashTypedData({
    domain: eip712Domain(chainId, contract),
    types: EVENT_TYPES,
    primaryType: "ColdChainEvent",
    message: ev,
  });
}

/** Address of the key that signed the event. */
export function recoverSigner(signed: SignedEvent, chainId: number, contract: Address): Promise<Address> {
  return recoverTypedDataAddress({
    domain: eip712Domain(chainId, contract),
    types: EVENT_TYPES,
    primaryType: "ColdChainEvent",
    message: signed.event,
    signature: signed.signature,
  });
}

/** Back to the published JSON layout (for editing in the verify view). */
export function toPayloadJson(signed: SignedEvent): string {
  const { event: e } = signed;
  return JSON.stringify(
    {
      boxId: e.boxId,
      batchId: e.batchId,
      kind: e.kind,
      anomalyType: e.anomalyType,
      startTime: Number(e.startTime),
      endTime: Number(e.endTime),
      temperature: e.temperature,
      peak: e.peak,
      counter: e.counter,
      signature: signed.signature,
    },
    null,
    2,
  );
}
