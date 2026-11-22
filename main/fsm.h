#pragma once

#include <stdbool.h>
#include <stdint.h>

// Time-based thermal anomaly state machine:
//   NORMAL  -> ANOMALY  when the temperature stays out of range
//                       continuously for enter_time_ms;
//   ANOMALY -> NORMAL   when it stays back in range continuously for
//                       exit_time_ms (recovery event).
// Any reading that breaks the condition resets the timer.
//
// Timestamps are ms from a monotonic clock (esp_timer on the ESP32),
// never the wall clock, which can jump when NTP syncs.
// Plain C (no ESP-IDF) so it can be tested on the host.

typedef enum {
    FSM_NORMAL = 0,
    FSM_ANOMALY,
} fsm_state_t;

// Same values as the `tipo` field in the smart contract.
typedef enum {
    ANOMALY_BELOW = 0,
    ANOMALY_ABOVE = 1,
    ANOMALY_SENSOR_OFFLINE = 2,
} anomaly_type_t;

typedef enum {
    FSM_EVT_NONE = 0,
    FSM_EVT_ANOMALY,  // entered ANOMALY
    FSM_EVT_RECOVERY, // back to NORMAL
} fsm_event_type_t;

typedef struct {
    float temp_min; // allowed range, inclusive [temp_min, temp_max]
    float temp_max;
    uint32_t enter_time_ms;
    uint32_t exit_time_ms;
} fsm_config_t;

typedef struct {
    fsm_event_type_t type;
    anomaly_type_t anomaly;
    int64_t start_ms;    // first out-of-range reading
    int64_t duration_ms; // RECOVERY: total time out of range
    float temperature;   // reading that triggered the event (NAN if offline)
    float peak;          // reading furthest from the range (NAN if only offline)
} fsm_event_t;

typedef struct {
    fsm_config_t cfg;
    fsm_state_t state;
    bool pending;          // state change condition in progress
    int64_t pending_since_ms;
    anomaly_type_t anomaly;
    int64_t start_ms;
    float peak;
} fsm_t;

void fsm_init(fsm_t *fsm, const fsm_config_t *cfg);

// Processes one reading (NAN = sensor read failure) at now_ms.
// Returns true and fills *event when the state changes.
bool fsm_process(fsm_t *fsm, float temperature, int64_t now_ms, fsm_event_t *event);

const char *fsm_state_name(fsm_state_t state);
const char *fsm_anomaly_name(anomaly_type_t type);
