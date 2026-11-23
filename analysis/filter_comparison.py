"""Figures and table from the filter bench (results/*.csv, made by `make`).

fig_filter_tradeoff: detection delay vs peak error per filter family
fig_filter_trace:    one warm-up with sensor glitches, raw vs filtered + FSM state
results/filters_table.md: the numbers behind them
"""

import csv
from collections import defaultdict

import matplotlib.pyplot as plt

import plotstyle as ps

ps.apply()

rows = list(csv.DictReader(open("results/filters.csv")))
for r in rows:
    for k in ("param", "alpha_equiv", "delay_fast_s", "delay_slow_s", "peak_error_c",
              "peak_error_abs_c", "false_alarms_per_day"):
        r[k] = float(r[k])

# Fixed order = fixed color per filter in every figure (the two that the text
# compares get the strongest slots).
FAMILIES = ["moving average", "median", "EMA", "Kalman", "median3+EMA"]
COLOR = {fam: ps.SERIES[i] for i, fam in enumerate(FAMILIES)}
MARKER = {fam: ps.MARKERS[i] for i, fam in enumerate(FAMILIES)}
FAMILY_LABEL = {
    "moving average": "Moving average",
    "EMA": "EMA",
    "Kalman": "Kalman 1D",
    "median": "Median",
    "median3+EMA": "Median-3 + EMA",
}

# ---- trade-off: delay vs peak error, per sensor model ----
fig, axes = plt.subplots(1, 2, figsize=(ps.PAGE_IN, 2.5), sharey=False)
for ax, sensor, title in zip(axes, ["noise", "noise+glitches"],
                             ["(a) Gaussian noise, $\\sigma$ = 0.3 °C",
                              "(b) noise + one glitch every 2 min"]):
    by_family = defaultdict(list)
    raw = None
    for r in rows:
        if r["sensor"] != sensor:
            continue
        if r["family"] == "none":
            raw = r
        else:
            by_family[r["family"]].append(r)
    for fam in FAMILIES:
        pts = sorted(by_family[fam], key=lambda r: -r["alpha_equiv"])  # light -> heavy smoothing
        ax.plot([p["delay_fast_s"] for p in pts], [p["peak_error_c"] for p in pts],
                color=COLOR[fam], marker=MARKER[fam], markersize=4, label=FAMILY_LABEL[fam],
                markeredgecolor="white", markeredgewidth=0.6)
        # Mark configs that raise false alarms (only happens with glitches).
        for p in pts:
            if p["false_alarms_per_day"] >= 1:
                ax.plot(p["delay_fast_s"], p["peak_error_c"], marker="o", markersize=8,
                        markerfacecolor="none", markeredgecolor=ps.INK, markeredgewidth=0.7)
    ax.plot(raw["delay_fast_s"], raw["peak_error_c"], marker="X", color=ps.INK_2, markersize=5,
            linestyle="none", label="No filter")
    ax.annotate("no filter", (raw["delay_fast_s"], raw["peak_error_c"]), textcoords="offset points",
                xytext=(-4, -9), ha="right", color=ps.INK_2, fontsize=7)
    # Firmware default (MA, N=4) and the recommended filter, labels placed in empty space.
    top = raw["peak_error_c"]
    for fam, label, text, y_frac in [("moving average", "N=4", "before: moving average N=4", 0.62),
                                     ("median", "N=5", "adopted: median N=5", 0.45)]:
        p = next(r for r in by_family[fam] if r["label"] == label)
        ax.annotate(text, (p["delay_fast_s"], p["peak_error_c"]), xytext=(7.3, top * y_frac),
                    textcoords="data", fontsize=6.5, color=ps.INK, va="center",
                    arrowprops=dict(arrowstyle="-", color=ps.INK_2, linewidth=0.5,
                                    shrinkA=1, shrinkB=3))
    ax.set_title(title, loc="left")
    ax.set_xlabel("Detection delay after crossing 8 °C (s)")
    ax.set_ylabel("Reported peak $-$ true peak (°C)")
    ax.set_ylim(bottom=0)
axes[1].plot([], [], marker="o", markersize=8, markerfacecolor="none", markeredgecolor=ps.INK,
             linestyle="none", label="$\\geq$1 false alarm/day")
handles, labels = axes[1].get_legend_handles_labels()
fig.legend(handles, labels, loc="lower center", ncol=7, bbox_to_anchor=(0.5, -0.06), handletextpad=0.3,
           columnspacing=1.0)
fig.tight_layout(rect=(0, 0.06, 1, 1))
ps.save(fig, "fig_filter_tradeoff")

# ---- one excursion, raw vs filtered ----
trace = list(csv.DictReader(open("results/trace.csv")))
t = [float(r["t_s"]) for r in trace]
fig, (ax, ax_state) = plt.subplots(2, 1, figsize=(ps.COLUMN_IN, 2.6), sharex=True,
                                   gridspec_kw={"height_ratios": [4, 1.1], "hspace": 0.08})
ax.plot(t, [float(r["truth"]) for r in trace], color=ps.INK, linestyle="--", linewidth=0.9,
        label="True temperature")
ax.plot(t, [float(r["raw"]) for r in trace], color=ps.MUTED, linewidth=0.6, label="Sensor reading")
ax.plot(t, [float(r["ma4"]) for r in trace], color=COLOR["moving average"], linewidth=1.1,
        label="Moving average N=4 (before)")
ax.plot(t, [float(r["med5"]) for r in trace], color=COLOR["median"], linewidth=1.1,
        label="Median N=5 (adopted)")
ax.axhline(8.0, color=ps.INK_2, linewidth=0.7)
ax.text(62, 8.2, "limit 8 °C", fontsize=6.5, color=ps.INK_2)
ax.set_ylabel("Temperature (°C)")
ax.legend(loc="lower right", fontsize=6.2, handlelength=1.5, bbox_to_anchor=(1.0, 0.0))
ax.set_xlim(40, 260)

# FSM state per filter: a bar while in ANOMALY.
for lane, (key, name, color) in enumerate([("state_raw", "No filter", ps.MUTED),
                                           ("state_ma4", "MA N=4", COLOR["moving average"]),
                                           ("state_med5", "Median N=5", COLOR["median"])]):
    y = 2 - lane
    start = None
    for i, r in enumerate(trace):
        on = r[key] == "1"
        if on and start is None:
            start = t[i]
        if (not on or i == len(trace) - 1) and start is not None:
            ax_state.plot([start, t[i]], [y, y], color=color, linewidth=3, solid_capstyle="butt")
            start = None
    ax_state.text(42, y, name, fontsize=6, va="center", color=ps.INK_2)
ax_state.set_yticks([])
ax_state.set_ylim(-0.7, 2.7)
ax_state.grid(False)
ax_state.set_ylabel("Anomaly", fontsize=7)
ax_state.set_xlabel("Time (s)")
ps.save(fig, "fig_filter_trace")

# ---- table ----
with open("results/filters_table.md", "w") as out:
    out.write("| Sensor | Filter | Parameter | Delay, 3 °C/min (s) | Delay, 0.5 °C/min (s) | "
              "Peak error (°C) | False alarms/day at 7.5 °C |\n|---|---|---|---|---|---|---|\n")
    for r in rows:
        out.write(f"| {r['sensor']} | {r['family']} | {r['label']} | {r['delay_fast_s']:.1f} | "
                  f"{r['delay_slow_s']:.1f} | {r['peak_error_c']:+.2f} | {r['false_alarms_per_day']:.1f} |\n")
print("figures/fig_filter_tradeoff, figures/fig_filter_trace, results/filters_table.md")
