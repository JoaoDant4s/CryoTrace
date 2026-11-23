import { KIND_ANOMALY, KIND_RECOVERY } from "./format";

/** An event as stored on-chain for a batch (StoredEvent). */
export interface RecordedEvent {
  kind: number;
  anomalyType: number;
  startTime: bigint; // epoch ms, 0 = device clock not synced
  endTime: bigint;
  temperature: number; // centi-degrees C
  peak: number;
  counter: number;
  recordedAt: bigint; // block timestamp, seconds
}

export type ExcursionStatus =
  /** anomaly and recovery both on-chain */
  | "recovered"
  /** anomaly on-chain, no recovery yet: still out of range, or the recovery is in transit */
  | "open"
  /** only the recovery arrived; it alone proves the excursion and its duration */
  | "anomaly-missing";

export interface Excursion {
  anomalyType: number;
  anomaly?: RecordedEvent;
  recovery?: RecordedEvent;
  status: ExcursionStatus;
  startTime: bigint; // 0 = unknown
  endTime: bigint; // 0 = unknown or still open
  /** Time out of range in ms; for an open excursion, the time so far. null = unknown (no clock). */
  durationMs: bigint | null;
  /** Reading furthest from the range (centi °C), from the recovery when available. */
  peak: number;
}

export interface AuditSummary {
  events: number;
  excursions: number;
  open: number;
  /** Sum of the known durations (open excursions count until now). */
  exposureMs: bigint;
  /** Excursions whose duration is unknown because the device clock was not synced. */
  unknownDurations: number;
  byType: Record<number, number>;
}

function sameStart(a: bigint, b: bigint): boolean {
  // 0 = clock not synced when that event was built: it matches any start.
  return a === b || a === 0n || b === 0n;
}

/**
 * Pairs anomaly and recovery events into excursions.
 *
 * Events are sorted by device counter, since relayers may submit them out of
 * order. Each recovery closes the earliest open anomaly of the same type and
 * start time. Because a recovery carries both start and end, an excursion is
 * still visible, with its duration, if its anomaly event was lost or censored.
 */
export function buildExcursions(events: RecordedEvent[], nowMs: number): Excursion[] {
  const sorted = [...events].sort((a, b) => a.counter - b.counter);
  const open: RecordedEvent[] = [];
  const result: { order: number; excursion: Excursion }[] = [];

  for (const ev of sorted) {
    if (ev.kind === KIND_ANOMALY) {
      open.push(ev);
      continue;
    }
    if (ev.kind !== KIND_RECOVERY) continue;

    const i = open.findIndex(
      (a) => a.anomalyType === ev.anomalyType && a.counter < ev.counter && sameStart(a.startTime, ev.startTime),
    );
    const anomaly = i >= 0 ? open.splice(i, 1)[0] : undefined;
    const start = ev.startTime !== 0n ? ev.startTime : (anomaly?.startTime ?? 0n);
    const known = start !== 0n && ev.endTime !== 0n;
    result.push({
      order: anomaly?.counter ?? ev.counter,
      excursion: {
        anomalyType: ev.anomalyType,
        anomaly,
        recovery: ev,
        status: anomaly ? "recovered" : "anomaly-missing",
        startTime: start,
        endTime: ev.endTime,
        durationMs: known ? ev.endTime - start : null,
        peak: ev.peak,
      },
    });
  }

  for (const a of open) {
    const known = a.startTime !== 0n;
    const sinceStart = BigInt(nowMs) - a.startTime;
    result.push({
      order: a.counter,
      excursion: {
        anomalyType: a.anomalyType,
        anomaly: a,
        status: "open",
        startTime: a.startTime,
        endTime: 0n,
        durationMs: known ? (sinceStart > 0n ? sinceStart : 0n) : null,
        peak: a.peak,
      },
    });
  }

  return result.sort((x, y) => x.order - y.order).map((r) => r.excursion);
}

export function summarize(events: RecordedEvent[], excursions: Excursion[]): AuditSummary {
  const byType: Record<number, number> = {};
  let exposureMs = 0n;
  let unknownDurations = 0;
  for (const x of excursions) {
    byType[x.anomalyType] = (byType[x.anomalyType] ?? 0) + 1;
    if (x.durationMs === null) unknownDurations++;
    else exposureMs += x.durationMs;
  }
  return {
    events: events.length,
    excursions: excursions.length,
    open: excursions.filter((x) => x.status === "open").length,
    exposureMs,
    unknownDurations,
    byType,
  };
}
