import type { ContractLog } from "../lib/chain";
import { DEPLOY_BLOCK, POLL_MS } from "../lib/config";
import { anomalyName, formatDate, formatTemp } from "../lib/format";
import { labelOf } from "../lib/names";
import { roleName } from "../lib/roles";
import { useBlockTimes, useContractLogs } from "../lib/useContractLogs";
import { AddressLink, Badge, Notice, TxLink } from "./ui";

const SHOWN = 50;

function describe(log: ContractLog) {
  switch (log.eventName) {
    case "DeviceRegistered":
      return (
        <>
          <Badge tone="info">Device</Badge> box <b>{labelOf(log.args.boxId!)}</b> → device key{" "}
          <AddressLink address={log.args.device!} />
        </>
      );
    case "BatchRegistered":
      return (
        <>
          <Badge tone="info">Batch</Badge> batch <b>{labelOf(log.args.batchId!)}</b> assigned to box{" "}
          <b>{labelOf(log.args.boxId!)}</b>
        </>
      );
    case "EventRecorded": {
      const a = log.args;
      const anomaly = a.kind === 0;
      return (
        <>
          <Badge tone={anomaly ? "bad" : "ok"}>{anomaly ? "Anomaly" : "Recovery"}</Badge> {anomalyName(a.anomalyType!)}{" "}
          · {anomaly ? "reading" : "back at"} {formatTemp(a.temperature!)}, peak {formatTemp(a.peak!)}
          {!anomaly && a.startTime! > 0n && ` · ${Math.round(Number(a.endTime! - a.startTime!) / 1000)} s out of range`}{" "}
          · box <b>{labelOf(a.boxId!)}</b>, batch <b>{labelOf(a.batchId!)}</b> · #{a.counter}
        </>
      );
    }
    case "RoleGranted":
    case "RoleRevoked":
      return (
        <>
          <Badge tone="warn">Role</Badge> {roleName(log.args.role!)} {log.eventName === "RoleGranted" ? "granted to" : "revoked from"}{" "}
          <AddressLink address={log.args.account!} />
        </>
      );
  }
}

/** Every contract event since the deploy, newest first. */
export function ActivityPanel() {
  const { logs, loading, error } = useContractLogs();
  const shown = logs.slice(-SHOWN).reverse();
  // Timestamps only for the rows on screen.
  const times = useBlockTimes(shown.map((l) => l.blockNumber));

  return (
    <section className="panel">
      <h2>Contract activity</h2>
      <p className="muted">
        Every registration and signed event since the contract was deployed (block {DEPLOY_BLOCK.toString()}),
        refreshed every {Math.round(POLL_MS / 1000)} s.
      </p>
      {error && <Notice tone="bad">{error}</Notice>}
      {loading ? (
        <p className="muted">Loading…</p>
      ) : shown.length === 0 ? (
        <p className="muted">No activity yet.</p>
      ) : (
        <ul className="feed">
          {shown.map((log) => (
            <li key={`${log.transactionHash}:${log.logIndex}`}>
              <div className="feed-main">{describe(log)}</div>
              <div className="feed-meta">
                {times.has(log.blockNumber) ? formatDate(times.get(log.blockNumber)!) : `block ${log.blockNumber}`} ·{" "}
                <TxLink hash={log.transactionHash} />
              </div>
            </li>
          ))}
        </ul>
      )}
      {logs.length > SHOWN && <p className="muted small">Showing the latest {SHOWN} of {logs.length} events.</p>}
    </section>
  );
}
