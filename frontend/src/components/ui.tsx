import { useCallback, useState, type ReactNode } from "react";
import type { Address, Hash, TransactionReceipt } from "viem";
import { addressUrl, describeError, txUrl } from "../lib/chain";
import { shortHex } from "../lib/format";

function MaybeLink({ href, children, title }: { href?: string; children: ReactNode; title?: string }) {
  return href ? (
    <a href={href} target="_blank" rel="noreferrer" title={title} className="mono">
      {children}
    </a>
  ) : (
    <span className="mono" title={title}>
      {children}
    </span>
  );
}

export function AddressLink({ address, full = false }: { address: Address; full?: boolean }) {
  return (
    <MaybeLink href={addressUrl(address)} title={address}>
      {full ? address : shortHex(address)}
    </MaybeLink>
  );
}

export function TxLink({ hash }: { hash: Hash }) {
  return (
    <MaybeLink href={txUrl(hash)} title={hash}>
      {shortHex(hash)}
    </MaybeLink>
  );
}

export type Tone = "ok" | "bad" | "warn" | "info";

export function Badge({ tone, children }: { tone: Tone; children: ReactNode }) {
  return <span className={`badge ${tone}`}>{children}</span>;
}

export function Notice({ tone, children }: { tone: Tone; children: ReactNode }) {
  return <div className={`notice ${tone}`}>{children}</div>;
}

export type TxState =
  | { phase: "idle" }
  | { phase: "wallet" }
  | { phase: "pending"; hash: Hash }
  | { phase: "done"; receipt: TransactionReceipt }
  | { phase: "error"; message: string };

/** Runs a transaction and tracks its state for <TxStatus>. */
export function useTx() {
  const [state, setState] = useState<TxState>({ phase: "idle" });
  const run = useCallback(
    async (send: (onSent: (hash: Hash) => void) => Promise<TransactionReceipt>): Promise<boolean> => {
      setState({ phase: "wallet" });
      try {
        const receipt = await send((hash) => setState({ phase: "pending", hash }));
        setState({ phase: "done", receipt });
        return true;
      } catch (err) {
        setState({ phase: "error", message: describeError(err) });
        return false;
      }
    },
    [],
  );
  const busy = state.phase === "wallet" || state.phase === "pending";
  return { state, run, busy };
}

export function TxStatus({ state }: { state: TxState }) {
  switch (state.phase) {
    case "idle":
      return null;
    case "wallet":
      return <Notice tone="info">Checking with the contract, then confirm in your wallet…</Notice>;
    case "pending":
      return (
        <Notice tone="info">
          Sent, waiting to be mined: <TxLink hash={state.hash} />
        </Notice>
      );
    case "done":
      return (
        <Notice tone="ok">
          Confirmed in block {state.receipt.blockNumber.toString()} ({state.receipt.gasUsed.toLocaleString()} gas):{" "}
          <TxLink hash={state.receipt.transactionHash} />
        </Notice>
      );
    case "error":
      return <Notice tone="bad">{state.message}</Notice>;
  }
}
