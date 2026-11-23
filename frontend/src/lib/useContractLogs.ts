import { useEffect, useState, useSyncExternalStore } from "react";
import { type ContractLog, blockTimeMs, describeError, fetchContractLogs, publicClient } from "./chain";
import { DEPLOY_BLOCK, POLL_MS } from "./config";

/**
 * All contract logs since the deploy, shared by every view that needs them:
 * fetched once, then polled only for new blocks while some view is open.
 */
interface Snapshot {
  logs: ContractLog[];
  loading: boolean;
  error: string | null;
}

let snapshot: Snapshot = { logs: [], loading: true, error: null };
let nextBlock = DEPLOY_BLOCK;
let timer: ReturnType<typeof setInterval> | undefined;
let polling = false;
const listeners = new Set<() => void>();

function publish(update: Partial<Snapshot>) {
  snapshot = { ...snapshot, ...update };
  listeners.forEach((l) => l());
}

async function poll() {
  if (polling) return;
  polling = true;
  try {
    const latest = await publicClient.getBlockNumber();
    if (latest >= nextBlock) {
      const fresh = await fetchContractLogs(nextBlock, latest);
      nextBlock = latest + 1n;
      publish({ logs: fresh.length ? [...snapshot.logs, ...fresh] : snapshot.logs, error: null, loading: false });
    } else {
      publish({ error: null, loading: false });
    }
  } catch (err) {
    publish({ error: describeError(err), loading: false });
  } finally {
    polling = false;
  }
}

function subscribe(listener: () => void) {
  listeners.add(listener);
  if (listeners.size === 1) {
    void poll();
    timer = setInterval(poll, POLL_MS);
  }
  return () => {
    listeners.delete(listener);
    if (listeners.size === 0) clearInterval(timer);
  };
}

/** Re-checks right away (e.g. after the user sent a transaction). */
export function refreshContractLogs(): void {
  void poll();
}

export function useContractLogs(): Snapshot {
  return useSyncExternalStore(subscribe, () => snapshot);
}

/** Timestamps (ms) of the given blocks, fetched once each. */
export function useBlockTimes(blocks: bigint[]): Map<bigint, number> {
  const [times, setTimes] = useState<Map<bigint, number>>(new Map());
  const key = [...new Set(blocks)].join(",");
  useEffect(() => {
    if (!key) return;
    let cancelled = false;
    const list = key.split(",").map(BigInt);
    Promise.all(list.map(async (b) => [b, await blockTimeMs(b)] as const))
      .then((pairs) => !cancelled && setTimes(new Map(pairs)))
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, [key]);
  return times;
}
