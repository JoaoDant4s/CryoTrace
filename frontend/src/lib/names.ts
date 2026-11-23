import { type Hex, keccak256, toBytes, zeroHash } from "viem";

/**
 * The contract only stores keccak256 of box and batch names ("box01" -> 0x...),
 * so a hash cannot be turned back into its name. Every name typed in the app
 * is remembered here (per browser) to label hashes found on-chain.
 */
const STORAGE_KEY = "cryotrace.names";
const SEED = ["box01", "batch-demo-001"];

const book = new Map<string, string>();

function load(): void {
  let saved: string[] = [];
  try {
    saved = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? "[]");
  } catch {
    // storage unavailable (private window, tests): keep only the seed names
  }
  for (const name of [...SEED, ...saved]) book.set(keccak256(toBytes(name)), name);
}

function save(): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify([...book.values()]));
  } catch {
    // not persisted; labels still work for this session
  }
}

load();

const HASH_RE = /^0x[0-9a-fA-F]{64}$/;

/** Id the contract uses for a name, as the firmware computes it. A 0x hash passes through. */
export function idOf(nameOrHash: string): Hex {
  const input = nameOrHash.trim();
  if (HASH_RE.test(input)) return input.toLowerCase() as Hex;
  return keccak256(toBytes(input));
}

/** Keeps a name the user submitted, to label its hash later. */
export function remember(nameOrHash: string): void {
  const input = nameOrHash.trim();
  if (!input || HASH_RE.test(input)) return;
  const id = keccak256(toBytes(input));
  if (!book.has(id)) {
    book.set(id, input);
    save();
  }
}

export function nameOf(id: Hex): string | undefined {
  return book.get(id.toLowerCase());
}

/** Name when known, otherwise a shortened hash. */
export function labelOf(id: Hex): string {
  if (id === zeroHash) return "—";
  return nameOf(id) ?? `${id.slice(0, 10)}…${id.slice(-6)}`;
}
