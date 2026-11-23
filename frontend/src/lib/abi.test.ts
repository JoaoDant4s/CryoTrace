import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import { type Abi, toEventSelector, toFunctionSelector } from "viem";
import { formatAbiItem } from "viem/utils";
import { describe, expect, it } from "vitest";
import { cryoTraceAbi } from "./abi";

const ARTIFACT = resolve(__dirname, "../../../contracts/out/CryoTrace.sol/CryoTrace.json");

function signatures(abi: Abi): Set<string> {
  const out = new Set<string>();
  for (const item of abi) {
    if (item.type === "function") out.add(`function ${toFunctionSelector(item)} ${item.stateMutability}`);
    else if (item.type === "event") out.add(`event ${toEventSelector(item)}`);
    else if (item.type === "error") out.add(`error ${formatAbiItem(item)}`);
  }
  return out;
}

describe.skipIf(!existsSync(ARTIFACT))("ABI", () => {
  it("matches the compiled contract (run forge build in ../contracts)", () => {
    const compiled = signatures(JSON.parse(readFileSync(ARTIFACT, "utf8")).abi);
    for (const sig of signatures(cryoTraceAbi)) expect(compiled, sig).toContain(sig);
  });
});
