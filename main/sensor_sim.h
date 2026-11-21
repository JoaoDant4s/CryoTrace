#pragma once

#include <stdint.h>

// Deterministic simulated temperature profile (120 s cycle), used while
// there is no physical sensor. Each cycle exercises the FSM:
//   30–31 s  spike to 9.5 °C    -> too short, must NOT trigger
//   45–47 s  drop to 1.0 °C     -> too short, must NOT trigger
//   60–80 s  excursion to 11 °C -> ANOMALY (ABOVE) + RECOVERY
//   95–100 s sensor not replying -> ANOMALY (SENSOR_OFFLINE) + RECOVERY
// Plain C (no ESP-IDF) so it can be tested on the host.

#define SENSOR_SIM_CYCLE_MS 120000

// Temperature in °C at t_ms; NAN simulates a read failure.
float sensor_sim_temperature(int64_t t_ms);
