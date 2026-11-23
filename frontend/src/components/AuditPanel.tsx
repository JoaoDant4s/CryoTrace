import { useCallback, useEffect, useRef, useState } from "react";
import { type Address, type Hash, type Hex, zeroAddress, zeroHash } from "viem";
import { type AuditSummary, type Excursion, type RecordedEvent, buildExcursions, summarize } from "../lib/audit";
import { contract, describeError, fetchContractLogs, publicClient } from "../lib/chain";
import { POLL_MS, TEMP_MAX_C, TEMP_MIN_C } from "../lib/config";
import {
  anomalyName,
  formatDate,
  formatDeviceTime,
  formatDuration,
  formatTemp,
  kindName,
} from "../lib/format";
import { idOf, labelOf, remember } from "../lib/names";
import { AddressLink, Badge, Notice, TxLink } from "./ui";

interface AuditResult {
  batchId: Hex;
  boxId: Hex;
  device: Address;
  events: RecordedEvent[];
  excursions: Excursion[];
  summary: AuditSummary;
  /** transaction of each event, keyed by `${counter}:${kind}` */
  txOf: Map<string, Hash>;
  checkedAt: number;
}

const LAST_BATCH_KEY = "cryotrace.lastBatch";

function eventKey(ev: { counter: number; kind: number }): string {
  return `${ev.counter}:${ev.kind}`;
}

async function loadAudit(batchId: Hex): Promise<AuditResult | null> {
  const boxId = await publicClient.readContract({ ...contract, functionName: "boxOfBatch", args: [batchId] });
  if (boxId === zeroHash) return null;

  const [device, stored, latest] = await Promise.all([
    publicClient.readContract({ ...contract, functionName: "deviceOf", args: [boxId] }),
    publicClient.readContract({ ...contract, functionName: "getBatchEvents", args: [batchId] }),
    publicClient.getBlockNumber(),
  ]);
  const events: RecordedEvent[] = stored.map((s) => ({ ...s.data, recordedAt: s.recordedAt }));

  // Transaction hashes come from the logs; the stored events do not keep them.
  const txOf = new Map<string, Hash>();
  if (events.length > 0) {
    const logs = await fetchContractLogs(0n, latest, batchId);
    for (const log of logs) {
      if (log.eventName === "EventRecorded") txOf.set(eventKey(log.args as never), log.transactionHash);
    }
  }

  const now = Date.now();
  const excursions = buildExcursions(events, now);
  return { batchId, boxId, device, events, excursions, summary: summarize(events, excursions), txOf, checkedAt: now };
}

export function AuditPanel() {
  const [input, setInput] = useState(() => {
    try {
      return localStorage.getItem(LAST_BATCH_KEY) ?? "batch-demo-001";
    } catch {
      return "batch-demo-001";
    }
  });
  const [result, setResult] = useState<AuditResult | null>(null);
  const [notFound, setNotFound] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [live, setLive] = useState(true);
  const current = useRef<Hex | null>(null);

  const audit = useCallback(async (batchId: Hex, quiet = false) => {
    if (!quiet) setLoading(true);
    setError(null);
    try {
      const r = await loadAudit(batchId);
      if (current.current !== batchId) return; // a newer search replaced this one
      setResult(r);
      setNotFound(r ? null : labelOf(batchId));
    } catch (err) {
      setError(describeError(err));
    } finally {
      if (!quiet) setLoading(false);
    }
  }, []);

  function submit(e?: React.FormEvent, batch = input) {
    e?.preventDefault();
    if (!batch.trim()) return;
    const id = idOf(batch);
    remember(batch);
    current.current = id;
    try {
      localStorage.setItem(LAST_BATCH_KEY, batch.trim());
    } catch {
      /* not persisted */
    }
    void audit(id);
  }

  // Links like #audit?batch=batch-demo-001 (from the Registry) open that batch.
  useEffect(() => {
    function fromHash() {
      const batch = new URLSearchParams(window.location.hash.split("?")[1] ?? "").get("batch");
      if (batch) {
        setInput(batch);
        submit(undefined, batch);
      }
    }
    fromHash();
    window.addEventListener("hashchange", fromHash);
    return () => window.removeEventListener("hashchange", fromHash);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Live mode: one cheap eventCount call per poll; reload only when it changes.
  useEffect(() => {
    if (!live || !result) return;
    const timer = setInterval(async () => {
      try {
        const n = await publicClient.readContract({ ...contract, functionName: "eventCount", args: [result.batchId] });
        if (Number(n) !== result.events.length || result.summary.open > 0) void audit(result.batchId, true);
      } catch {
        /* transient RPC error: try again on the next tick */
      }
    }, POLL_MS);
    return () => clearInterval(timer);
  }, [live, result, audit]);

  return (
    <section className="panel">
      <h2>Batch audit</h2>
      <p className="muted">
        Everything a box reported for a vaccine batch, read straight from the contract. Anyone can check it: no wallet
        needed.
      </p>
      <form className="row" onSubmit={submit}>
        <input
          value={input}
          onChange={(e) => setInput(e.target.value)}
          placeholder="Batch ID (e.g. batch-demo-001) or its 0x hash"
          aria-label="Batch ID"
        />
        <button type="submit" disabled={loading}>
          {loading ? "Loading…" : "Audit"}
        </button>
      </form>

      {error && <Notice tone="bad">{error}</Notice>}
      {notFound && (
        <Notice tone="warn">
          Batch <b>{notFound}</b> is not registered on the contract. Batch IDs are case-sensitive and must match the
          firmware configuration exactly.
        </Notice>
      )}
      {result && <AuditView result={result} live={live} onLive={setLive} />}
    </section>
  );
}

function Verdict({ s }: { s: AuditSummary }) {
  if (s.excursions === 0) {
    return (
      <Notice tone="ok">
        <b>No excursion reported.</b> The box did not report any reading outside {TEMP_MIN_C}–{TEMP_MAX_C} °C (or a
        sensor failure) for this batch.
      </Notice>
    );
  }
  return (
    <Notice tone={s.open > 0 ? "bad" : "warn"}>
      <b>
        {s.excursions} excursion{s.excursions > 1 ? "s" : ""}
        {s.open > 0 ? `, ${s.open} still open` : ""}.
      </b>{" "}
      Total time out of range: {formatDuration(s.exposureMs)}
      {s.unknownDurations > 0 && ` (+ ${s.unknownDurations} with unknown duration: device clock not synced)`}.
    </Notice>
  );
}

function statusBadge(x: Excursion) {
  switch (x.status) {
    case "recovered":
      return <Badge tone="ok">recovered</Badge>;
    case "open":
      return <Badge tone="bad">open</Badge>;
    case "anomaly-missing":
      return (
        <span title="Only the recovery event arrived. It carries the start and end times, so the excursion is still proven.">
          <Badge tone="warn">recovered · start event missing</Badge>
        </span>
      );
  }
}

function AuditView({ result, live, onLive }: { result: AuditResult; live: boolean; onLive: (v: boolean) => void }) {
  const { summary: s } = result;
  return (
    <>
      <Verdict s={s} />

      <div className="cards">
        <div className="card">
          <div className="label">Batch</div>
          <div className="value">{labelOf(result.batchId)}</div>
        </div>
        <div className="card">
          <div className="label">Box</div>
          <div className="value">{labelOf(result.boxId)}</div>
          <div className="sub">
            device {result.device === zeroAddress ? "not registered" : <AddressLink address={result.device} />}
          </div>
        </div>
        <div className="card">
          <div className="label">Signed events</div>
          <div className="value">{s.events}</div>
        </div>
        <div className="card">
          <div className="label">Excursions</div>
          <div className="value">{s.excursions}</div>
          <div className="sub">
            {[0, 1, 2]
              .filter((t) => s.byType[t])
              .map((t) => `${s.byType[t]} ${anomalyName(t).toLowerCase()}`)
              .join(" · ") || "—"}
          </div>
        </div>
        <div className="card">
          <div className="label">Time out of range</div>
          <div className="value">{s.excursions ? formatDuration(s.exposureMs) : "0 s"}</div>
        </div>
      </div>

      <div className="row between">
        <h3>Excursions</h3>
        <label className="toggle">
          <input type="checkbox" checked={live} onChange={(e) => onLive(e.target.checked)} />
          Live (refresh every {Math.round(POLL_MS / 1000)} s) · checked {formatDate(result.checkedAt)}
        </label>
      </div>
      {result.excursions.length === 0 ? (
        <p className="muted">None.</p>
      ) : (
        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>#</th>
                <th>Type</th>
                <th>Start</th>
                <th>End</th>
                <th>Duration</th>
                <th>Peak</th>
                <th>Status</th>
              </tr>
            </thead>
            <tbody>
              {result.excursions.map((x, i) => (
                <tr key={i}>
                  <td>{i + 1}</td>
                  <td>{anomalyName(x.anomalyType)}</td>
                  <td>{formatDeviceTime(x.startTime)}</td>
                  <td>{x.status === "open" ? "—" : formatDeviceTime(x.endTime)}</td>
                  <td>
                    {x.durationMs === null
                      ? "unknown"
                      : `${x.status === "open" ? "≥ " : ""}${formatDuration(x.durationMs)}`}
                  </td>
                  <td>{formatTemp(x.peak)}</td>
                  <td>{statusBadge(x)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      <h3>Signed events on-chain</h3>
      {result.events.length === 0 ? (
        <p className="muted">No events recorded for this batch yet.</p>
      ) : (
        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>Counter</th>
                <th>Event</th>
                <th>Type</th>
                <th>Start (device)</th>
                <th>End (device)</th>
                <th>Reading</th>
                <th>Peak</th>
                <th>Recorded (block)</th>
                <th>Tx</th>
              </tr>
            </thead>
            <tbody>
              {[...result.events]
                .sort((a, b) => a.counter - b.counter)
                .map((ev) => {
                  const tx = result.txOf.get(eventKey(ev));
                  return (
                    <tr key={eventKey(ev)}>
                      <td>{ev.counter}</td>
                      <td>
                        <Badge tone={ev.kind === 0 ? "bad" : "ok"}>{kindName(ev.kind)}</Badge>
                      </td>
                      <td>{anomalyName(ev.anomalyType)}</td>
                      <td>{formatDeviceTime(ev.startTime)}</td>
                      <td>{ev.kind === 0 ? "—" : formatDeviceTime(ev.endTime)}</td>
                      <td>{formatTemp(ev.temperature)}</td>
                      <td>{formatTemp(ev.peak)}</td>
                      <td>{formatDate(Number(ev.recordedAt) * 1000)}</td>
                      <td>{tx ? <TxLink hash={tx} /> : "—"}</td>
                    </tr>
                  );
                })}
            </tbody>
          </table>
        </div>
      )}
      <p className="muted small">
        Each event was signed inside the box by its device key and checked by the contract before being stored, so
        neither the relayer nor anyone else can alter or invent it. Times come from the box clock (NTP); the block time
        is an upper bound. Events can reach the chain late (no connectivity, slow relayer), so a missing event can mean it
        has not arrived yet.
      </p>
    </>
  );
}
