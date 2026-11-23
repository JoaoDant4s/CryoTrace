# Analysis

Metrics that do not need the board, with the figures and tables for the paper.
Everything is deterministic: `make` rebuilds the same numbers.

```sh
pip install matplotlib        # Python 3.10+
make                          # builds the bench, writes results/ and figures/
```

## Filter comparison (`filter_bench.c`, `filter_comparison.py`)

A C program that runs **the firmware's own filters and anomaly FSM**
(`main/monitoring/`) over synthetic temperature traces, with the firmware's
settings: 0.5 s sampling, range 2–8 °C, 3 s to enter and to leave the anomaly
state. Two sensor models: Gaussian noise (σ = 0.3 °C), and the same plus a
single-sample glitch (+2 to +5 °C) every 2 minutes on average.

| Metric | Scenario |
|---|---|
| Detection delay | true temperature warms from 5 to 10 °C at 3 °C/min (door open) and 0.5 °C/min (cooling lost); time from crossing 8 °C to the ANOMALY event; 200 runs each |
| Peak error | reported peak (the value signed and stored on-chain) minus the true 10 °C |
| False alarms | true temperature constant at 7.5 °C (inside the range) for 10 × 24 h; every ANOMALY is false |

Filters: none, moving average (N = 2…16), EMA (α = 0.5…0.07), 1D Kalman
(q/r = 1…0.005, r = σ²), running median (N = 3, 5, 7), and median-3 followed
by EMA.

Outputs: `results/filters.csv`, `results/filters_table.md`,
`figures/fig_filter_tradeoff` (delay × peak error, false-alarm configurations
circled) and `figures/fig_filter_trace` (one warm-up with glitches).

## Data reduction (`data_reduction.py`)

Bytes per box per day for each publishing strategy, from the exact firmware
messages (signed event 422 B, reading 60 B) plus estimated MQTT, TLS 1.2 and
TCP/IP overheads (assumptions in the script), with the course formulas
`R_raw = f_s × N_c × B_s + H_p` and `D_cloud = N_nodes × R_raw × t`. Keep-alive
traffic is shown on its own: it is paid even when nothing is published.

Outputs: `results/data_reduction.md`, `figures/fig_data_reduction`.

Figures are PDF (for the paper) and PNG, sized for IEEE columns, with a palette
checked for color-vision deficiency; every series also has its own marker, so
they read in grayscale.
