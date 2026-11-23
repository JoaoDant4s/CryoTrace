import type { Address } from "viem";
import { foundry, sepolia } from "viem/chains";

const env = import.meta.env;

/** Sepolia by default; 31337 runs the app against a local anvil chain. */
export const chain = Number(env.VITE_CHAIN_ID ?? sepolia.id) === foundry.id ? foundry : sepolia;
const isLocal = chain.id === foundry.id;

/** Read-only RPC. Bundled into the page, so it must not carry a private API key. */
export const RPC_URL: string =
  env.VITE_RPC_URL || (isLocal ? "http://127.0.0.1:8545" : "https://ethereum-sepolia-rpc.publicnode.com");

export const CONTRACT_ADDRESS = (env.VITE_CONTRACT_ADDRESS ||
  "0xF8A328b314C89Ef22E3494886A00419Dcc434Dd4") as Address;

/** No logs exist before the deploy block, so log scans start there. */
export const DEPLOY_BLOCK = BigInt(env.VITE_DEPLOY_BLOCK ?? (isLocal ? 0 : 12190943));

export const POLL_MS = Number(env.VITE_POLL_MS ?? 30_000);

/** Vaccine storage range enforced by the firmware (WHO PQS / PNI cold chain). */
export const TEMP_MIN_C = 2;
export const TEMP_MAX_C = 8;

export const EXPLORER_URL: string | undefined = chain.blockExplorers?.default.url;
