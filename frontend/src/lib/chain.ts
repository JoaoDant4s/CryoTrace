import {
  type Address,
  BaseError,
  ContractFunctionRevertedError,
  type EIP1193Provider,
  type Hash,
  type Hex,
  type TransactionReceipt,
  UserRejectedRequestError,
  type WalletClient,
  createPublicClient,
  createWalletClient,
  custom,
  http,
} from "viem";
import { cryoTraceAbi } from "./abi";
import { CONTRACT_ADDRESS, DEPLOY_BLOCK, EXPLORER_URL, RPC_URL, chain } from "./config";

/** Reads (free, no wallet needed). */
export const publicClient = createPublicClient({ chain, transport: http(RPC_URL) });

export const contract = { address: CONTRACT_ADDRESS, abi: cryoTraceAbi } as const;

declare global {
  interface Window {
    ethereum?: EIP1193Provider;
  }
}

export interface Wallet {
  client: WalletClient;
  account: Address;
  provider: EIP1193Provider;
  /** Wallet name, e.g. "MetaMask". */
  name: string;
  /** False while the wallet is on another network: reads still work, transactions do not. */
  onChain: boolean;
}

/**
 * Wallets announce themselves through EIP-6963. With several extensions
 * installed (MetaMask, Brave Wallet, Phantom, Coinbase…) they fight over
 * window.ethereum, so MetaMask is picked explicitly when present.
 */
interface AnnouncedWallet {
  info: { uuid: string; name: string; rdns: string };
  provider: EIP1193Provider;
}
const announced: AnnouncedWallet[] = [];
if (typeof window !== "undefined") {
  window.addEventListener("eip6963:announceProvider", (e) => {
    const detail = (e as CustomEvent<AnnouncedWallet>).detail;
    if (detail?.provider && !announced.some((w) => w.info.uuid === detail.info.uuid)) announced.push(detail);
  });
  window.dispatchEvent(new Event("eip6963:requestProvider"));
}

function findWallet(): { provider: EIP1193Provider; name: string } | null {
  const w = announced.find((x) => x.info.rdns === "io.metamask") ?? announced[0];
  if (w) return { provider: w.provider, name: w.info.name };
  return typeof window !== "undefined" && window.ethereum ? { provider: window.ethereum, name: "browser wallet" } : null;
}

export function hasBrowserWallet(): boolean {
  return findWallet() !== null;
}

/** A failed step of the wallet connection, with the wallet's own reason. */
export class WalletStepError extends Error {
  constructor(step: string, cause: unknown) {
    const code = (cause as { code?: number }).code ?? (cause instanceof BaseError ? (cause.walk() as { code?: number }).code : undefined);
    super(`${step}: ${describeError(cause)}${code !== undefined ? ` (code ${code})` : ""}`);
  }
}

function hasCode(err: unknown, code: number): boolean {
  return (err as { code?: number }).code === code || !!(err as BaseError).walk?.((e) => (e as { code?: number }).code === code);
}

/** Asks the wallet to switch to the app's network, adding it first if unknown (e.g. local anvil). */
export async function switchToAppChain(wallet: Wallet): Promise<void> {
  try {
    await wallet.client.switchChain({ id: chain.id });
  } catch (err) {
    if (!hasCode(err, 4902)) throw new WalletStepError(`Switching ${wallet.name} to ${chain.name}`, err);
    try {
      await wallet.client.addChain({ chain });
    } catch (err2) {
      throw new WalletStepError(`Adding ${chain.name} to ${wallet.name}`, err2);
    }
  }
}

/**
 * Connects the browser wallet and tries to switch it to the app's network.
 * Refusing only the network switch keeps the wallet connected (onChain = false).
 */
export async function connectWallet(): Promise<Wallet> {
  const found = findWallet();
  if (!found) throw new Error("No browser wallet found. Install MetaMask to send transactions.");
  const client = createWalletClient({ chain, transport: custom(found.provider) });

  let account: Address;
  try {
    [account] = await client.requestAddresses();
  } catch (err) {
    throw new WalletStepError(`${found.name} did not share an account`, err);
  }
  if (!account) throw new Error(`${found.name} returned no account. Unlock it and try again.`);

  const wallet: Wallet = { client, account, ...found, onChain: (await client.getChainId()) === chain.id };
  if (!wallet.onChain) {
    try {
      await switchToAppChain(wallet);
      wallet.onChain = true;
    } catch {
      // stays connected; the app offers the switch again
    }
  }
  return wallet;
}

type WriteName = "registerDevice" | "registerBatch" | "recordEvent" | "grantRole" | "revokeRole";

/**
 * Simulates the call first, so a call the contract would refuse is reported
 * with its reason before the wallet asks for a signature (and costs no gas).
 */
export async function sendContractTx(
  wallet: Wallet,
  functionName: WriteName,
  args: readonly unknown[],
  onSent?: (hash: Hash) => void,
): Promise<TransactionReceipt> {
  if (!wallet.onChain) throw new Error(`Switch ${wallet.name} to ${chain.name} first (button at the top).`);
  const { request } = await publicClient.simulateContract({
    ...contract,
    functionName,
    args,
    account: wallet.account,
  } as Parameters<typeof publicClient.simulateContract>[0]);
  // The wallet estimates gas itself against the live network.
  const hash = await wallet.client.writeContract({ ...request, gas: undefined } as Parameters<WalletClient["writeContract"]>[0]);
  onSent?.(hash);
  const receipt = await publicClient.waitForTransactionReceipt({ hash });
  if (receipt.status !== "success") throw new Error(`Transaction ${hash} reverted`);
  return receipt;
}

const ERROR_HINTS: Record<string, string> = {
  AccessControlUnauthorizedAccount: "the connected account does not have the role this action needs",
  UnknownBox: "this box has no device registered yet",
  BatchAlreadyRegistered: "this batch is already assigned to a box (one batch per trip)",
  BatchNotOnBox: "the event's batch is not assigned to the event's box",
  InvalidSigner: "the signature does not come from the box's registered device key",
  AlreadyRecorded: "this event is already on-chain",
  InvalidEvent: "the event breaks the contract rules (kind, type or times)",
  ZeroAddress: "the address cannot be zero",
  ECDSAInvalidSignature: "malformed signature",
  ECDSAInvalidSignatureS: "non-canonical (high-s) signature",
  ECDSAInvalidSignatureLength: "signature must be 65 bytes",
};

/** Human-readable reason for a failed read or transaction. */
export function describeError(err: unknown): string {
  if (err instanceof BaseError) {
    if (err.walk((e) => e instanceof UserRejectedRequestError)) return "Request rejected in the wallet.";
    const revert = err.walk((e) => e instanceof ContractFunctionRevertedError);
    if (revert instanceof ContractFunctionRevertedError) {
      const name = revert.data?.errorName ?? revert.reason;
      if (name) {
        const hint = ERROR_HINTS[name];
        return `Contract refused: ${name}${hint ? ` — ${hint}` : ""}.`;
      }
    }
    return err.shortMessage;
  }
  return err instanceof Error ? err.message : String(err);
}

export function txUrl(hash: Hash): string | undefined {
  return EXPLORER_URL && `${EXPLORER_URL}/tx/${hash}`;
}

export function addressUrl(address: Address): string | undefined {
  return EXPLORER_URL && `${EXPLORER_URL}/address/${address}`;
}

/** Public RPCs cap the block range of eth_getLogs, so scans go in chunks. */
const LOG_CHUNK = 5_000n;

export type ContractLog = Awaited<ReturnType<typeof fetchContractLogs>>[number];

/** All contract events in [fromBlock, toBlock], oldest first. */
export async function fetchContractLogs(fromBlock: bigint, toBlock: bigint, batchId?: Hex) {
  const logs = [];
  for (let start = fromBlock < DEPLOY_BLOCK ? DEPLOY_BLOCK : fromBlock; start <= toBlock; start += LOG_CHUNK) {
    const end = start + LOG_CHUNK - 1n < toBlock ? start + LOG_CHUNK - 1n : toBlock;
    const chunk = batchId
      ? await publicClient.getContractEvents({
          ...contract,
          eventName: "EventRecorded",
          args: { batchId },
          fromBlock: start,
          toBlock: end,
        })
      : await publicClient.getContractEvents({ ...contract, fromBlock: start, toBlock: end });
    logs.push(...chunk);
  }
  return logs;
}

const blockTimes = new Map<bigint, Promise<number>>();

/** Block timestamp in ms (cached, including requests in flight: final blocks never change). */
export function blockTimeMs(blockNumber: bigint): Promise<number> {
  let time = blockTimes.get(blockNumber);
  if (!time) {
    time = publicClient.getBlock({ blockNumber }).then((b) => Number(b.timestamp) * 1000);
    time.catch(() => blockTimes.delete(blockNumber));
    blockTimes.set(blockNumber, time);
  }
  return time;
}
