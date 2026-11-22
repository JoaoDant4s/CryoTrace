#include "fsm.h"

#include <math.h>

static bool out_of_range(const fsm_config_t *cfg, float t, anomaly_type_t *type)
{
    if (isnan(t)) {
        *type = ANOMALY_SENSOR_OFFLINE;
        return true;
    }
    if (t > cfg->temp_max) {
        *type = ANOMALY_ABOVE;
        return true;
    }
    if (t < cfg->temp_min) {
        *type = ANOMALY_BELOW;
        return true;
    }
    return false;
}

static float deviation(const fsm_config_t *cfg, float t)
{
    return t > cfg->temp_max ? t - cfg->temp_max : cfg->temp_min - t;
}

static void update_peak(fsm_t *fsm, float t)
{
    if (isnan(t)) {
        return;
    }
    if (isnan(fsm->peak) || deviation(&fsm->cfg, t) > deviation(&fsm->cfg, fsm->peak)) {
        fsm->peak = t;
    }
}

void anomaly_fsm_init(fsm_t *fsm, const fsm_config_t *cfg)
{
    fsm->cfg = *cfg;
    fsm->state = FSM_NORMAL;
    fsm->pending = false;
    fsm->pending_since_ms = 0;
    fsm->anomaly = ANOMALY_ABOVE;
    fsm->start_ms = 0;
    fsm->peak = NAN;
}

bool anomaly_fsm_process(fsm_t *fsm, float temperature, int64_t now_ms, fsm_event_t *event)
{
    anomaly_type_t type;
    bool out = out_of_range(&fsm->cfg, temperature, &type);

    if (fsm->state == FSM_NORMAL) {
        if (!out) {
            fsm->pending = false;
            return false;
        }
        if (!fsm->pending) {
            fsm->pending = true;
            fsm->pending_since_ms = now_ms;
            fsm->peak = NAN;
        }
        fsm->anomaly = type;
        update_peak(fsm, temperature);

        if (now_ms - fsm->pending_since_ms < fsm->cfg.enter_time_ms) {
            return false;
        }
        fsm->state = FSM_ANOMALY;
        fsm->pending = false;
        fsm->start_ms = fsm->pending_since_ms;
        *event = (fsm_event_t){
            .type = FSM_EVT_ANOMALY,
            .anomaly = fsm->anomaly,
            .start_ms = fsm->start_ms,
            .duration_ms = 0,
            .temperature = temperature,
            .peak = fsm->peak,
        };
        return true;
    }

    // FSM_ANOMALY: the type stays fixed at what was published on entry;
    // only the peak keeps being tracked.
    if (out) {
        fsm->pending = false;
        update_peak(fsm, temperature);
        return false;
    }
    if (!fsm->pending) {
        fsm->pending = true;
        fsm->pending_since_ms = now_ms;
    }
    if (now_ms - fsm->pending_since_ms < fsm->cfg.exit_time_ms) {
        return false;
    }
    fsm->state = FSM_NORMAL;
    fsm->pending = false;
    *event = (fsm_event_t){
        .type = FSM_EVT_RECOVERY,
        .anomaly = fsm->anomaly,
        .start_ms = fsm->start_ms,
        // Actual exposure: up to the first reading back in range, not
        // up to the recovery confirmation.
        .duration_ms = fsm->pending_since_ms - fsm->start_ms,
        .temperature = temperature,
        .peak = fsm->peak,
    };
    return true;
}

const char *anomaly_fsm_state_name(fsm_state_t state)
{
    return state == FSM_ANOMALY ? "ANOMALY" : "NORMAL";
}

const char *anomaly_fsm_anomaly_name(anomaly_type_t type)
{
    switch (type) {
    case ANOMALY_ABOVE:
        return "ABOVE";
    case ANOMALY_BELOW:
        return "BELOW";
    case ANOMALY_SENSOR_OFFLINE:
        return "SENSOR_OFFLINE";
    }
    return "?";
}
