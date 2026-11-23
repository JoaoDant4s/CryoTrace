import { describe, expect, it } from "vitest";
import { type RecordedEvent, buildExcursions, summarize } from "./audit";

const T0 = 1_767_225_600_000n;
const ABOVE = 1;
const BELOW = 0;
const OFFLINE = 2;

function anomaly(counter: number, type: number, start: bigint, peak = 850): RecordedEvent {
  return { kind: 0, anomalyType: type, startTime: start, endTime: 0n, temperature: peak, peak, counter, recordedAt: 0n };
}

function recovery(counter: number, type: number, start: bigint, end: bigint, peak = 1121): RecordedEvent {
  return { kind: 1, anomalyType: type, startTime: start, endTime: end, temperature: 790, peak, counter, recordedAt: 0n };
}

describe("buildExcursions", () => {
  it("returns nothing for a batch without events", () => {
    expect(buildExcursions([], 0)).toEqual([]);
  });

  it("pairs an anomaly with its recovery", () => {
    const [x] = buildExcursions([anomaly(1, ABOVE, T0), recovery(2, ABOVE, T0, T0 + 15_000n)], 0);
    expect(x.status).toBe("recovered");
    expect(x.durationMs).toBe(15_000n);
    expect(x.peak).toBe(1121); // overall peak, from the recovery
  });

  it("pairs events submitted out of order", () => {
    const xs = buildExcursions(
      [
        recovery(4, BELOW, T0 + 60_000n, T0 + 65_000n),
        recovery(2, ABOVE, T0, T0 + 15_000n),
        anomaly(3, BELOW, T0 + 60_000n),
        anomaly(1, ABOVE, T0),
      ],
      0,
    );
    expect(xs.map((x) => [x.anomalyType, x.status, x.durationMs])).toEqual([
      [ABOVE, "recovered", 15_000n],
      [BELOW, "recovered", 5_000n],
    ]);
  });

  it("keeps an excursion open until its recovery arrives, counting time so far", () => {
    const [x] = buildExcursions([anomaly(1, ABOVE, T0)], Number(T0) + 90_000);
    expect(x.status).toBe("open");
    expect(x.durationMs).toBe(90_000n);
  });

  it("still proves the excursion when the anomaly event was lost", () => {
    const [x] = buildExcursions([recovery(2, OFFLINE, T0, T0 + 5_000n)], 0);
    expect(x.status).toBe("anomaly-missing");
    expect(x.durationMs).toBe(5_000n);
  });

  it("does not pair different anomaly types", () => {
    const xs = buildExcursions([anomaly(1, ABOVE, T0), recovery(2, BELOW, T0, T0 + 1_000n)], Number(T0) + 2_000);
    expect(xs.map((x) => x.status).sort()).toEqual(["anomaly-missing", "open"]);
  });

  it("pairs events built before the clock synced, with unknown duration", () => {
    const [x] = buildExcursions([anomaly(1, ABOVE, 0n), recovery(2, ABOVE, 0n, 0n)], 0);
    expect(x.status).toBe("recovered");
    expect(x.durationMs).toBeNull();
  });

  it("uses the recovery times when the clock synced mid-excursion", () => {
    const [x] = buildExcursions([anomaly(1, ABOVE, 0n), recovery(2, ABOVE, T0, T0 + 20_000n)], 0);
    expect(x.status).toBe("recovered");
    expect(x.durationMs).toBe(20_000n);
  });
});

describe("summarize", () => {
  it("adds up exposure and counts per type", () => {
    const events = [
      anomaly(1, ABOVE, T0),
      recovery(2, ABOVE, T0, T0 + 15_000n),
      anomaly(3, OFFLINE, T0 + 100_000n),
      anomaly(4, BELOW, 0n),
    ];
    const s = summarize(events, buildExcursions(events, Number(T0) + 130_000));
    expect(s).toEqual({
      events: 4,
      excursions: 3,
      open: 2,
      exposureMs: 15_000n + 30_000n,
      unknownDurations: 1,
      byType: { [ABOVE]: 1, [OFFLINE]: 1, [BELOW]: 1 },
    });
  });
});
