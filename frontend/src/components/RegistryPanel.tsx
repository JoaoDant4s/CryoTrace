import { useMemo } from "react";
import type { Hex } from "viem";
import { formatDate } from "../lib/format";
import { labelOf, nameOf } from "../lib/names";
import { buildRegistry } from "../lib/registry";
import { useBlockTimes, useContractLogs } from "../lib/useContractLogs";
import { AddressLink, Badge, Notice, TxLink } from "./ui";

function IdLabel({ id }: { id: Hex }) {
  const known = nameOf(id) !== undefined;
  return (
    <span className={known ? "" : "mono"} title={id}>
      {labelOf(id)}
    </span>
  );
}

function auditHref(batchId: Hex): string {
  return `#audit?batch=${encodeURIComponent(nameOf(batchId) ?? batchId)}`;
}

/** Every registered box (with its device keys) and every batch (with the keys that signed its events). */
export function RegistryPanel() {
  const { logs, loading, error } = useContractLogs();
  const { boxes, batches } = useMemo(() => buildRegistry(logs), [logs]);
  const times = useBlockTimes([...boxes.flatMap((b) => b.keys.map((k) => k.blockNumber)), ...batches.map((b) => b.blockNumber)]);
  const when = (block: bigint) => (times.has(block) ? formatDate(times.get(block)!) : `block ${block}`);
  const hasUnknownNames = [...boxes.map((b) => b.boxId), ...batches.map((b) => b.batchId)].some((id) => !nameOf(id));

  return (
    <section className="panel">
      <h2>Registry</h2>
      <p className="muted">Every box and batch registered on the contract, rebuilt from its public event log.</p>
      {error && <Notice tone="bad">{error}</Notice>}
      {loading ? (
        <p className="muted">Loading…</p>
      ) : (
        <>
          <h3>
            Boxes and devices <span className="muted">({boxes.length})</span>
          </h3>
          {boxes.length === 0 ? (
            <p className="muted">No device registered yet.</p>
          ) : (
            <div className="table-wrap">
              <table>
                <thead>
                  <tr>
                    <th>Box</th>
                    <th>Device key (accepted now)</th>
                    <th>Registered</th>
                    <th>Previous keys</th>
                    <th>Batches</th>
                  </tr>
                </thead>
                <tbody>
                  {boxes.map((box) => {
                    const current = box.keys[box.keys.length - 1];
                    const previous = box.keys.slice(0, -1);
                    return (
                      <tr key={box.boxId}>
                        <td>
                          <IdLabel id={box.boxId} />
                        </td>
                        <td>
                          <AddressLink address={box.device} full />
                        </td>
                        <td>
                          {when(current.blockNumber)} · <TxLink hash={current.transactionHash} />
                        </td>
                        <td>
                          {previous.length === 0 ? (
                            "—"
                          ) : (
                            <span title="Rotated keys: events they sign are refused from the rotation on">
                              {previous.map((k, i) => (
                                <span key={i}>
                                  {i > 0 && ", "}
                                  <AddressLink address={k.device} />
                                </span>
                              ))}
                            </span>
                          )}
                        </td>
                        <td>{box.batches.length}</td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
          )}

          <h3>
            Batches <span className="muted">({batches.length})</span>
          </h3>
          {batches.length === 0 ? (
            <p className="muted">No batch registered yet.</p>
          ) : (
            <div className="table-wrap">
              <table>
                <thead>
                  <tr>
                    <th>Batch</th>
                    <th>Box</th>
                    <th>Tracked by (device keys)</th>
                    <th>Assigned</th>
                    <th>Events</th>
                    <th></th>
                  </tr>
                </thead>
                <tbody>
                  {batches.map((batch) => {
                    const box = boxes.find((b) => b.boxId === batch.boxId);
                    // With events: the keys that actually signed them. Without: the key that will sign.
                    const trackers = batch.signers.length ? batch.signers : box ? [box.device] : [];
                    return (
                      <tr key={batch.batchId}>
                        <td>
                          <IdLabel id={batch.batchId} />
                        </td>
                        <td>
                          <IdLabel id={batch.boxId} />
                        </td>
                        <td>
                          {trackers.map((d, i) => (
                            <span key={d}>
                              {i > 0 && ", "}
                              <AddressLink address={d} />
                            </span>
                          ))}
                          {batch.signers.length === 0 && <span className="muted small"> (no events yet)</span>}
                        </td>
                        <td>
                          {when(batch.blockNumber)} · <TxLink hash={batch.transactionHash} />
                        </td>
                        <td>
                          {batch.events}{" "}
                          {batch.anomalies > 0 && (
                            <Badge tone="warn">
                              {batch.anomalies} anomal{batch.anomalies > 1 ? "ies" : "y"}
                            </Badge>
                          )}
                        </td>
                        <td>
                          <a href={auditHref(batch.batchId)}>Audit →</a>
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
          )}

          {hasUnknownNames && (
            <p className="muted small">
              The contract stores only the id (hash) of each box and batch name. Names appear for those typed in this browser (in
              Audit or Manage); the others show as hashes (hover for the full value).
            </p>
          )}
        </>
      )}
    </section>
  );
}
