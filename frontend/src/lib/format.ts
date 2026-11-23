import { TEMP_MAX_C, TEMP_MIN_C } from "./config";

export const NO_READING = -32768;

export const KIND_ANOMALY = 0;
export const KIND_RECOVERY = 1;

export function kindName(kind: number): string {
  return kind === KIND_ANOMALY ? "Anomaly" : kind === KIND_RECOVERY ? "Recovery" : `kind ${kind}`;
}

/** anomalyType: 0 = BELOW, 1 = ABOVE, 2 = SENSOR_OFFLINE. */
export function anomalyName(type: number): string {
  switch (type) {
    case 0:
      return `Below ${TEMP_MIN_C} °C`;
    case 1:
      return `Above ${TEMP_MAX_C} °C`;
    case 2:
      return "Sensor offline";
    default:
      return `type ${type}`;
  }
}

/** Centi-degrees C -> "8.12 °C". */
export function formatTemp(centi: number): string {
  if (centi === NO_READING) return "no reading";
  return `${(centi / 100).toFixed(2)} °C`;
}

/** Epoch ms from the device clock; 0 means the clock was never synced. */
export function formatDeviceTime(ms: bigint): string {
  if (ms === 0n) return "clock not synced";
  return formatDate(Number(ms));
}

export function formatDate(ms: number): string {
  return new Date(ms).toLocaleString(undefined, {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

/** 15000 -> "15 s", 754000 -> "12 min 34 s", 7_200_000 -> "2 h 0 min". */
export function formatDuration(ms: bigint | number): string {
  const total = Math.round(Number(ms) / 1000);
  if (total < 60) return `${total} s`;
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  return h > 0 ? `${h} h ${m} min` : `${m} min ${s} s`;
}

export function shortHex(value: string, head = 6, tail = 4): string {
  return value.length <= head + tail + 2 ? value : `${value.slice(0, head + 2)}…${value.slice(-tail)}`;
}
