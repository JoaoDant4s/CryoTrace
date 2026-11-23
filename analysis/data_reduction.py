"""Data reduction at the edge: bytes per box per day for each publishing strategy.

Payload sizes are the firmware's own messages: the signed event is the exact
JSON of the shared test vector (relayer/tests/test_payload.py); a reading uses
the telemetry format of the architecture (timestamp, filtered and raw value,
state). Telemetry is not implemented: it is the cloud-centric baseline here. Protocol overheads are estimates, stated
below; keep-alive traffic is shown separately because it is paid whether or
not anything is sent.

Course formulas (lecture 1): R_raw = f_s * N_c * B_s + H_p (per box) and
D_cloud = N_nodes * R_raw * t (fleet).

Writes figures/fig_data_reduction.{pdf,png} and results/data_reduction.md.
"""

import matplotlib.pyplot as plt

import plotstyle as ps

ps.apply()

# ---- messages (exact firmware formats) ----
EVENT_JSON = (
    '{"boxId":"0x8c4dae34670896d924ca9ac8cfde3453f494e0165196f54ab7e9b47322ff8dbb",'
    '"batchId":"0x36c5cfd13ef95136a333b8467f27e5810f10588f87c862682a58cbe406b12fad",'
    '"kind":1,"anomalyType":1,"startTime":1767225600000,"endTime":1767225615000,'
    '"temperature":790,"peak":1121,"counter":42,'
    '"signature":"0x08a7c85b3031fc6059aec942042ef1e10e1bc7b17ecf9fa752a3a1fc44c74cb5'
    '5a2f3eca400efae2f3591feba1e1aad4e5dcd42b02381dad3d2a12883133eee81c"}'
)
READING_JSON = '{"ts":1767225600000,"temp":5.12,"raw":5.03,"state":"NORMAL"}'
EVENT_TOPIC = "cryotrace/box01/anomaly"
READING_TOPIC = "cryotrace/box01/telemetry"  # hypothetical: not implemented

# ---- protocol overheads (assumptions) ----
TLS_RECORD = 5 + 8 + 16   # TLS 1.2 AES-GCM: header + explicit nonce + tag
TCP_IP = 20 + 20          # IPv4 + TCP headers, no options
ACK = TCP_IP              # one pure TCP ACK per data segment not answered at once


def mqtt_publish(topic: str, payload: str, qos: int) -> int:
    variable = 2 + len(topic) + (2 if qos else 0) + len(payload)
    remaining_len_bytes = 1 if variable < 128 else 2
    return 1 + remaining_len_bytes + variable


def wire_publish(topic: str, payload: str, qos: int) -> int:
    data = TCP_IP + TLS_RECORD + mqtt_publish(topic, payload, qos)
    if qos == 0:
        return data + ACK
    puback = TCP_IP + TLS_RECORD + 4   # also acknowledges the PUBLISH segment
    return data + puback + ACK


def wire_ping() -> int:
    ping = TCP_IP + TLS_RECORD + 2
    return ping + ping + ACK            # PINGREQ, PINGRESP (acks the request), ACK


DAY = 86400
SAMPLE_HZ = 2          # firmware samples every 500 ms
TELEMETRY_S = 10

strategies = [
    # label, messages/day, payload bytes/msg, wire bytes/msg
    ("Every reading (2 Hz)", SAMPLE_HZ * DAY, len(READING_JSON), wire_publish(READING_TOPIC, READING_JSON, 0)),
    ("Periodic reading every 10 s", DAY // TELEMETRY_S, len(READING_JSON),
     wire_publish(READING_TOPIC, READING_JSON, 0)),
    ("Signed events, 10 excursions/day", 20, len(EVENT_JSON), wire_publish(EVENT_TOPIC, EVENT_JSON, 1)),
    ("Signed events, 2 excursions/day", 4, len(EVENT_JSON), wire_publish(EVENT_TOPIC, EVENT_JSON, 1)),
    ("Keep-alive only, every 30 s", DAY // 30, 0, wire_ping()),
    ("Keep-alive only, every 5 min", DAY // 300, 0, wire_ping()),
]
rows = [(label, n, n * p, n * w) for label, n, p, w in strategies]
baseline_wire = rows[0][3]


def human(b: float) -> str:
    for unit, size in (("GB", 1e9), ("MB", 1e6), ("kB", 1e3)):
        if b >= size:
            return f"{b / size:.1f} {unit}" if b / size < 100 else f"{b / size:.0f} {unit}"
    return f"{b:.0f} B"


# ---- figure: dot plot, log scale (values span six orders of magnitude) ----
fig, ax = plt.subplots(figsize=(ps.COLUMN_IN, 2.35))
ys = list(range(len(rows)))[::-1]
for y, (label, n, payload, wire) in zip(ys, rows):
    if payload:  # keep-alive has no payload: no range to draw
        ax.plot([payload, wire], [y, y], color=ps.GRID, linewidth=2, zorder=1, solid_capstyle="round")
        ax.plot(payload, y, "o", color=ps.SERIES[0], markersize=5, zorder=2,
                markeredgecolor="white", markeredgewidth=0.6)
    ax.plot(wire, y, "s", color=ps.SERIES[1], markersize=5, zorder=2, markeredgecolor="white",
            markeredgewidth=0.6)
    ax.annotate(human(wire), (wire, y), textcoords="offset points", xytext=(6, 0), va="center",
                fontsize=6.5, color=ps.INK)
ax.set_yticks(ys, [r[0] for r in rows])
ax.set_xscale("log")
ax.set_xlim(50, 4e8)
ax.set_xlabel("Bytes per box per day (log scale)")
ax.grid(axis="y", visible=False)
ax.plot([], [], "o", color=ps.SERIES[0], label="Payload only")
ax.plot([], [], "s", color=ps.SERIES[1], label="On the wire (MQTT + TLS + TCP/IP)")
ax.legend(loc="lower center", bbox_to_anchor=(0.32, 1.0), ncol=2, handletextpad=0.2, columnspacing=1.0)
ps.save(fig, "fig_data_reduction")

# ---- table + course formulas + on-chain view ----
N_NODES, MONTH = 100, 30
GAS_PER_EVENT = 177_675                 # recordEvent, Foundry gas report (first event of a batch)
BLOCK_GAS_LIMIT, BLOCKS_PER_DAY = 36_000_000, 7200   # Ethereum, assumption (2025 values)
with open("results/data_reduction.md", "w") as out:
    out.write(f"Signed event payload: {len(EVENT_JSON)} B; reading payload: {len(READING_JSON)} B.\n")
    out.write(f"Per message on the wire: reading (QoS 0) {wire_publish(READING_TOPIC, READING_JSON, 0)} B, "
              f"event (QoS 1) {wire_publish(EVENT_TOPIC, EVENT_JSON, 1)} B, keep-alive {wire_ping()} B.\n\n")
    out.write("| Strategy | Messages/day | Payload/day | Wire/day | Reduction vs every reading (wire) | "
              f"Fleet of {N_NODES}, {MONTH} days (wire) |\n|---|---|---|---|---|---|\n")
    for label, n, payload, wire in rows:
        out.write(f"| {label} | {n:,} | {human(payload)} | {human(wire)} | {baseline_wire / wire:,.0f}x | "
                  f"{human(N_NODES * wire * MONTH)} |\n")
    capacity = BLOCK_GAS_LIMIT * BLOCKS_PER_DAY
    out.write("\nOn-chain (one transaction per message, recordEvent gas):\n\n")
    for label, n, *_ in rows[:4]:
        out.write(f"- {label}: {n * GAS_PER_EVENT / 1e6:,.1f} Mgas/day = "
                  f"{100 * n * GAS_PER_EVENT / capacity:.4f} % of Ethereum's daily gas capacity "
                  f"(assumed {BLOCK_GAS_LIMIT / 1e6:.0f} M gas per block, {BLOCKS_PER_DAY} blocks/day)\n")
print(open("results/data_reduction.md").read())
