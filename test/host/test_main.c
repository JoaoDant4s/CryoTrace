// Host (gcc) tests for the logic that does not depend on ESP-IDF.
// Run: make -C test/host

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "filter.h"
#include "fsm.h"
#include "sensor_sim.h"

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                 \
        }                                                               \
    } while (0)

static const fsm_config_t CFG = {
    .temp_min = 2.0f,
    .temp_max = 8.0f,
    .enter_time_ms = 3000,
    .exit_time_ms = 3000,
};

#define STEP_MS 500

// Feeds `temp` to the FSM from t0 to t1 (exclusive) every STEP_MS.
// Returns the number of events and stores the last one in *last.
static int feed(fsm_t *fsm, float temp, int64_t t0, int64_t t1, fsm_event_t *last)
{
    int n = 0;
    for (int64_t t = t0; t < t1; t += STEP_MS) {
        fsm_event_t ev;
        if (anomaly_fsm_process(fsm, temp, t, &ev)) {
            *last = ev;
            n++;
        }
    }
    return n;
}

static void test_short_excursion_does_not_trigger(void)
{
    puts("2.5 s excursion does not trigger");
    fsm_t fsm;
    fsm_event_t ev;
    anomaly_fsm_init(&fsm, &CFG);
    // Readings at 0, 500, ..., 2500 ms: the last is 2.5 s after the first.
    CHECK(feed(&fsm, 9.0f, 0, 3000, &ev) == 0);
    CHECK(feed(&fsm, 5.0f, 3000, 10000, &ev) == 0);
    CHECK(fsm.state == FSM_NORMAL);
}

static void test_triggers_at_3s(void)
{
    puts("triggers exactly when 3 s out of range is reached");
    fsm_t fsm;
    fsm_event_t ev;
    anomaly_fsm_init(&fsm, &CFG);
    feed(&fsm, 5.0f, 0, 1000, &ev);
    CHECK(feed(&fsm, 9.0f, 1000, 4000, &ev) == 0); // up to 3500 ms
    CHECK(anomaly_fsm_process(&fsm, 9.0f, 4000, &ev));     // 3 s since 1000 ms
    CHECK(ev.type == FSM_EVT_ANOMALY);
    CHECK(ev.anomaly == ANOMALY_ABOVE);
    CHECK(ev.start_ms == 1000);
    CHECK(fsm.state == FSM_ANOMALY);
}

static void test_interruption_resets_timer(void)
{
    puts("a single in-range reading resets the timer");
    fsm_t fsm;
    fsm_event_t ev;
    anomaly_fsm_init(&fsm, &CFG);
    feed(&fsm, 9.0f, 0, 2500, &ev);
    anomaly_fsm_process(&fsm, 5.0f, 2500, &ev);
    CHECK(feed(&fsm, 9.0f, 3000, 6000, &ev) == 0);
    CHECK(anomaly_fsm_process(&fsm, 9.0f, 6000, &ev));
    CHECK(ev.start_ms == 3000);
}

static void test_recovery(void)
{
    puts("recovery after 3 s in range, with duration and peak");
    fsm_t fsm;
    fsm_event_t ev;
    anomaly_fsm_init(&fsm, &CFG);
    feed(&fsm, 9.0f, 0, 5000, &ev);
    anomaly_fsm_process(&fsm, 12.5f, 5000, &ev);
    feed(&fsm, 9.0f, 5500, 10000, &ev);
    CHECK(feed(&fsm, 5.0f, 10000, 13000, &ev) == 0);
    CHECK(anomaly_fsm_process(&fsm, 5.0f, 13000, &ev));
    CHECK(ev.type == FSM_EVT_RECOVERY);
    CHECK(ev.start_ms == 0);
    CHECK(ev.duration_ms == 10000);
    CHECK(ev.peak == 12.5f);
    CHECK(fsm.state == FSM_NORMAL);
}

static void test_relapse_during_exit(void)
{
    puts("an out-of-range reading during exit keeps ANOMALY");
    fsm_t fsm;
    fsm_event_t ev;
    anomaly_fsm_init(&fsm, &CFG);
    feed(&fsm, 9.0f, 0, 3500, &ev);
    feed(&fsm, 5.0f, 3500, 6000, &ev);
    anomaly_fsm_process(&fsm, 9.0f, 6000, &ev);
    CHECK(feed(&fsm, 5.0f, 6500, 9500, &ev) == 0);
    CHECK(fsm.state == FSM_ANOMALY);
    CHECK(anomaly_fsm_process(&fsm, 5.0f, 9500, &ev));
}

static void test_types(void)
{
    puts("BELOW and SENSOR_OFFLINE types; range limits are inclusive");
    fsm_t fsm;
    fsm_event_t ev;

    anomaly_fsm_init(&fsm, &CFG);
    CHECK(feed(&fsm, 1.0f, 0, 3500, &ev) == 1);
    CHECK(ev.anomaly == ANOMALY_BELOW);

    anomaly_fsm_init(&fsm, &CFG);
    CHECK(feed(&fsm, NAN, 0, 3500, &ev) == 1);
    CHECK(ev.anomaly == ANOMALY_SENSOR_OFFLINE);
    CHECK(isnan(ev.peak));

    anomaly_fsm_init(&fsm, &CFG);
    CHECK(feed(&fsm, 8.0f, 0, 10000, &ev) == 0);
    CHECK(feed(&fsm, 2.0f, 10000, 20000, &ev) == 0);
}

static void test_filter(void)
{
    puts("moving average");
    filter_avg_t f;
    filter_init(&f, 4);
    CHECK(filter_add(&f, 4.0f) == 4.0f);
    CHECK(filter_add(&f, 6.0f) == 5.0f);
    filter_add(&f, 6.0f);
    CHECK(filter_add(&f, 8.0f) == 6.0f);
    CHECK(filter_add(&f, 8.0f) == 7.0f); // dropped the 4
    filter_reset(&f);
    CHECK(filter_add(&f, 3.0f) == 3.0f);
}

// Runs the full pipeline (simulated sensor -> filter -> FSM), as the
// firmware does, and checks the events of one cycle.
static void test_ema_and_kalman(void)
{
    printf("EMA, Kalman and median filters\n");
    filter_ema_t e;
    filter_ema_init(&e, 0.5f);
    CHECK(filter_ema_add(&e, 4.0f) == 4.0f);  // first sample taken as is
    CHECK(filter_ema_add(&e, 8.0f) == 6.0f);
    CHECK(filter_ema_add(&e, 8.0f) == 7.0f);
    filter_ema_reset(&e);
    CHECK(filter_ema_add(&e, 3.0f) == 3.0f);

    filter_kalman_t k;
    filter_kalman_init(&k, 0.001f, 0.09f);
    CHECK(filter_kalman_add(&k, 5.0f) == 5.0f);
    float y = 5.0f;
    for (int i = 0; i < 200; i++) {
        y = filter_kalman_add(&k, 7.0f);  // step: converges, never overshoots
        CHECK(y <= 7.0f);
    }
    CHECK(fabsf(y - 7.0f) < 0.01f);
    // steady-state gain = EMA alpha: k = p/(p+r), with p the prior variance
    float p_post = k.p, prior = p_post + k.q;
    float gain = prior / (prior + k.r);
    CHECK(gain > 0.05f && gain < 0.2f);
    filter_kalman_reset(&k);
    CHECK(filter_kalman_add(&k, 2.0f) == 2.0f);

    filter_median_t m;
    filter_median_init(&m, 5);
    CHECK(filter_median_add(&m, 5.0f) == 5.0f);
    CHECK(filter_median_add(&m, 7.0f) == 6.0f);   // two values: their mean
    CHECK(filter_median_add(&m, 5.2f) == 5.2f);
    CHECK(filter_median_add(&m, 12.0f) == 6.1f);  // (5.2 + 7) / 2
    CHECK(filter_median_add(&m, 5.1f) == 5.2f);   // glitches 7 and 12 removed
    CHECK(filter_median_add(&m, 5.3f) == 5.3f);   // window slid past the 5.0
    filter_median_reset(&m);
    CHECK(filter_median_add(&m, 3.0f) == 3.0f);
}

// Same pipeline as main.c: the median (firmware default) or the moving average.
static void test_simulation_cycle(bool median)
{
    printf("simulated sensor cycle produces only the expected anomalies (%s):\n",
           median ? "median N=5" : "moving average N=4");
    filter_avg_t f;
    filter_median_t fm;
    fsm_t fsm;
    filter_init(&f, 4);
    filter_median_init(&fm, 5);
    anomaly_fsm_init(&fsm, &CFG);

    fsm_event_t events[8];
    int n = 0;
    for (int64_t t = 0; t < SENSOR_SIM_CYCLE_MS; t += STEP_MS) {
        float raw = sensor_sim_temperature(t);
        float filtered = NAN;
        if (isnan(raw)) {
            filter_reset(&f);
            filter_median_reset(&fm);
        } else {
            filtered = median ? filter_median_add(&fm, raw) : filter_add(&f, raw);
        }
        fsm_event_t ev;
        if (anomaly_fsm_process(&fsm, filtered, t, &ev) && n < 8) {
            printf("    t=%6.1fs %-8s %-14s start=%.1fs duration=%.1fs peak=%.2f\n",
                   t / 1000.0, ev.type == FSM_EVT_ANOMALY ? "ANOMALY" : "RECOVERY",
                   anomaly_fsm_anomaly_name(ev.anomaly), ev.start_ms / 1000.0,
                   ev.duration_ms / 1000.0, ev.peak);
            events[n++] = ev;
        }
    }

    CHECK(n == 4);
    if (n != 4) {
        return;
    }
    CHECK(events[0].type == FSM_EVT_ANOMALY && events[0].anomaly == ANOMALY_ABOVE);
    CHECK(events[0].start_ms > 60000 && events[0].start_ms < 65000);
    CHECK(events[1].type == FSM_EVT_RECOVERY && events[1].anomaly == ANOMALY_ABOVE);
    CHECK(events[1].peak > 10.5f);
    CHECK(events[2].type == FSM_EVT_ANOMALY &&
          events[2].anomaly == ANOMALY_SENSOR_OFFLINE);
    CHECK(events[2].start_ms == 95000);
    CHECK(events[3].type == FSM_EVT_RECOVERY);
    CHECK(events[3].duration_ms == 5000);
    CHECK(fsm.state == FSM_NORMAL);
}

int main(void)
{
    test_filter();
    test_short_excursion_does_not_trigger();
    test_triggers_at_3s();
    test_interruption_resets_timer();
    test_recovery();
    test_relapse_during_exit();
    test_types();
    test_ema_and_kalman();
    test_simulation_cycle(true);
    test_simulation_cycle(false);

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    puts("\nOK: all tests passed");
    return 0;
}
