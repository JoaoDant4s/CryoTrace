// Filter comparison bench: runs the firmware's own filters and anomaly FSM
// (main/monitoring) over synthetic temperature traces with sensor noise and
// glitches, and measures what matters for the cold chain:
//
//   - detection delay: time from the true temperature crossing 8 °C to the
//     ANOMALY event, for a fast and a slow warm-up;
//   - false alarms: ANOMALY events per day while the true temperature stays
//     inside the range, close to the limit;
//   - peak error: reported peak (sent on-chain) minus the true peak.
//
// Same parameters as the firmware: 0.5 s sampling, range [2, 8] °C, 3 s to
// enter and to leave the anomaly state. Deterministic (fixed seeds).
//
// Output (CSV on stdout):
//   ./filter_bench summary  -> one row per filter configuration
//   ./filter_bench trace    -> one warm-up excursion, per-sample readings

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "fsm.h"

#define SAMPLE_MS 500
#define T_MAX 8.0f
#define T_MIN 2.0f

// ---- deterministic noise ----

static uint64_t s_rng;

static void rng_seed(uint64_t seed)
{
    s_rng = seed * 0x9E3779B97F4A7C15ull + 1;
}

static double uniform(void)  // (0, 1)
{
    s_rng ^= s_rng >> 12;
    s_rng ^= s_rng << 25;
    s_rng ^= s_rng >> 27;
    return ((s_rng * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0) + 1e-17;
}

static double gaussian(void)
{
    return sqrt(-2.0 * log(uniform())) * cos(2.0 * M_PI * uniform());
}

// Sensor model: Gaussian noise plus rare single-sample glitches (+2..5 °C),
// e.g. electrical interference on a long sensor cable.
typedef struct {
    double sigma;
    double glitch_per_sample;
} sensor_model_t;

static float read_sensor(const sensor_model_t *m, double truth)
{
    double x = truth + m->sigma * gaussian();
    if (uniform() < m->glitch_per_sample) {
        x += 2.0 + 3.0 * uniform();
    }
    return (float)x;
}

// ---- filters under test ----

typedef enum { F_NONE, F_MA, F_EMA, F_KALMAN, F_MEDIAN, F_MEDIAN_EMA } family_t;

typedef struct {
    family_t family;
    double param;  // window, alpha or q/r (median + EMA: alpha, after a median of 3)
    const char *label;
} config_t;

typedef struct {
    config_t cfg;
    filter_avg_t ma;
    filter_ema_t ema;
    filter_kalman_t kf;
    filter_median_t med;
} filter_t;

static void filter_setup(filter_t *f, const config_t *cfg, double sigma)
{
    memset(f, 0, sizeof(*f));
    f->cfg = *cfg;
    float r = (float)(sigma * sigma);
    switch (cfg->family) {
    case F_MA: filter_init(&f->ma, (uint8_t)cfg->param); break;
    case F_EMA: filter_ema_init(&f->ema, (float)cfg->param); break;
    case F_KALMAN: filter_kalman_init(&f->kf, (float)(cfg->param * r), r); break;
    case F_MEDIAN: filter_median_init(&f->med, (uint8_t)cfg->param); break;
    case F_MEDIAN_EMA:
        filter_median_init(&f->med, 3);
        filter_ema_init(&f->ema, (float)cfg->param);
        break;
    case F_NONE: break;
    }
}

static float filter_step(filter_t *f, float x)
{
    switch (f->cfg.family) {
    case F_MA: return filter_add(&f->ma, x);
    case F_EMA: return filter_ema_add(&f->ema, x);
    case F_KALMAN: return filter_kalman_add(&f->kf, x);
    case F_MEDIAN: return filter_median_add(&f->med, x);
    case F_MEDIAN_EMA: return filter_ema_add(&f->ema, filter_median_add(&f->med, x));
    case F_NONE: break;
    }
    return x;
}

static const char *family_name(family_t f)
{
    static const char *names[] = { "none", "moving average", "EMA", "Kalman", "median", "median3+EMA" };
    return names[f];
}

// Steady-state gain of an EMA-like filter (1 for no filter); for the moving
// average, the EMA with the same noise reduction: 2 / (N + 1).
static double equivalent_alpha(const config_t *c)
{
    switch (c->family) {
    case F_NONE: return 1.0;
    case F_MA: return 2.0 / (c->param + 1.0);
    case F_EMA:
    case F_MEDIAN_EMA: return c->param;
    case F_MEDIAN: return 2.0 / (c->param + 1.0);
    case F_KALMAN: {
        double q = c->param;  // with r = 1: prior p solves p = (p r / (p + r)) + q
        double p = (q + sqrt(q * q + 4.0 * q)) / 2.0;
        return p / (p + 1.0);
    }
    }
    return 1.0;
}

static const fsm_config_t FSM_CFG = {
    .temp_min = T_MIN,
    .temp_max = T_MAX,
    .enter_time_ms = 3000,
    .exit_time_ms = 3000,
};

// ---- scenarios ----

// True temperature: 5 °C, warms at rate_c_per_min up to 10 °C, stays there
// 10 min, cools back. Returns the time (ms) it crosses 8 °C.
static double warmup_truth(double t_ms, double rate_c_per_min, double *crossing_ms)
{
    const double start = 60000, base = 5.0, top = 10.0, hold = 600000;
    double rise = (top - base) / rate_c_per_min * 60000.0;
    *crossing_ms = start + (T_MAX - base) / rate_c_per_min * 60000.0;
    if (t_ms < start) return base;
    if (t_ms < start + rise) return base + (t_ms - start) / 60000.0 * rate_c_per_min;
    if (t_ms < start + rise + hold) return top;
    double down = t_ms - (start + rise + hold);
    double v = top - down / 60000.0 * rate_c_per_min;
    return v > base ? v : base;
}

typedef struct {
    double delay_ms;  // NAN if not detected
    double peak_error;
} warmup_result_t;

static warmup_result_t run_warmup(const config_t *cfg, const sensor_model_t *m, double rate)
{
    filter_t f;
    filter_setup(&f, cfg, m->sigma);
    fsm_t fsm;
    anomaly_fsm_init(&fsm, &FSM_CFG);
    warmup_result_t r = { NAN, NAN };
    double crossing;
    warmup_truth(0, rate, &crossing);
    for (double t = 0; t < 60 * 60000.0; t += SAMPLE_MS) {
        double truth = warmup_truth(t, rate, &crossing);
        float y = filter_step(&f, read_sensor(m, truth));
        fsm_event_t ev;
        if (anomaly_fsm_process(&fsm, y, (int64_t)t, &ev)) {
            if (ev.type == FSM_EVT_ANOMALY && isnan(r.delay_ms)) {
                r.delay_ms = t - crossing;
            } else if (ev.type == FSM_EVT_RECOVERY && ev.anomaly == ANOMALY_ABOVE) {
                r.peak_error = ev.peak - 10.0;
                break;
            }
        }
    }
    return r;
}

// True temperature constant at `level`, inside the range: every ANOMALY is false.
static int run_steady(const config_t *cfg, const sensor_model_t *m, double level, double hours)
{
    filter_t f;
    filter_setup(&f, cfg, m->sigma);
    fsm_t fsm;
    anomaly_fsm_init(&fsm, &FSM_CFG);
    int alarms = 0;
    for (double t = 0; t < hours * 3600000.0; t += SAMPLE_MS) {
        float y = filter_step(&f, read_sensor(m, level));
        fsm_event_t ev;
        if (anomaly_fsm_process(&fsm, y, (int64_t)t, &ev) && ev.type == FSM_EVT_ANOMALY) {
            alarms++;
        }
    }
    return alarms;
}

static const config_t CONFIGS[] = {
    { F_NONE, 0, "raw" },
    { F_MA, 2, "N=2" },
    { F_MA, 4, "N=4" },
    { F_MA, 8, "N=8" },
    { F_MA, 16, "N=16" },
    { F_EMA, 0.5, "a=0.5" },
    { F_EMA, 0.3, "a=0.3" },
    { F_EMA, 0.15, "a=0.15" },
    { F_EMA, 0.07, "a=0.07" },
    { F_KALMAN, 1.0, "q/r=1" },
    { F_KALMAN, 0.1, "q/r=0.1" },
    { F_KALMAN, 0.02, "q/r=0.02" },
    { F_KALMAN, 0.005, "q/r=0.005" },
    { F_MEDIAN, 3, "N=3" },
    { F_MEDIAN, 5, "N=5" },
    { F_MEDIAN, 7, "N=7" },
    { F_MEDIAN_EMA, 0.5, "a=0.5" },
    { F_MEDIAN_EMA, 0.3, "a=0.3" },
    { F_MEDIAN_EMA, 0.15, "a=0.15" },
};
#define N_CONFIGS (sizeof(CONFIGS) / sizeof(CONFIGS[0]))

static double mean_of(const double *v, int n, int *valid)
{
    double s = 0;
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (!isnan(v[i])) {
            s += v[i];
            k++;
        }
    }
    *valid = k;
    return k ? s / k : NAN;
}

static void summary_for(const char *sensor_name, const sensor_model_t *sensor)
{
    const int runs = 200;
    const double steady_level = 7.5, steady_hours = 24, steady_runs = 10;

    for (size_t c = 0; c < N_CONFIGS; c++) {
        const config_t *cfg = &CONFIGS[c];
        double fast[runs], slow[runs], peak[runs], peak_abs[runs];
        for (int i = 0; i < runs; i++) {
            rng_seed(1000 + i);
            warmup_result_t a = run_warmup(cfg, sensor, 3.0);   // door open: 3 °C/min
            rng_seed(5000 + i);
            warmup_result_t b = run_warmup(cfg, sensor, 0.5);   // cooling lost: 0.5 °C/min
            fast[i] = a.delay_ms / 1000.0;
            slow[i] = b.delay_ms / 1000.0;
            peak[i] = a.peak_error;
            peak_abs[i] = fabs(a.peak_error);
        }
        int nf, ns, np, npa;
        double mf = mean_of(fast, runs, &nf), ms = mean_of(slow, runs, &ns);
        double mp = mean_of(peak, runs, &np), mpa = mean_of(peak_abs, runs, &npa);
        int alarms = 0;
        for (int i = 0; i < steady_runs; i++) {
            rng_seed(9000 + i);
            alarms += run_steady(cfg, sensor, steady_level, steady_hours);
        }
        printf("%s,%s,%s,%g,%.4f,%.3f,%.3f,%d,%.3f,%.3f,%.3f\n", sensor_name, family_name(cfg->family),
               cfg->label, cfg->param, equivalent_alpha(cfg), mf, ms, 2 * runs - nf - ns, mp, mpa,
               alarms / (steady_runs * steady_hours / 24.0));
    }
}

static void summary(void)
{
    printf("sensor,family,label,param,alpha_equiv,delay_fast_s,delay_slow_s,missed,peak_error_c,"
           "peak_error_abs_c,false_alarms_per_day\n");
    const sensor_model_t noise = { .sigma = 0.3, .glitch_per_sample = 0 };
    const sensor_model_t glitchy = { .sigma = 0.3, .glitch_per_sample = 1.0 / 240 };  // 1 glitch / 2 min
    summary_for("noise", &noise);
    summary_for("noise+glitches", &glitchy);
}

// One fast warm-up, per sample, for the time-series figure.
static void trace(void)
{
    const sensor_model_t sensor = { .sigma = 0.3, .glitch_per_sample = 1.0 / 240 };
    const config_t shown[] = { { F_NONE, 0, "raw" }, { F_MA, 4, "N=4" }, { F_EMA, 0.3, "a=0.3" },
                               { F_MEDIAN, 5, "N=5" } };
    const int n = sizeof(shown) / sizeof(shown[0]);
    filter_t f[4];
    fsm_t fsm[4];
    for (int i = 0; i < n; i++) {
        filter_setup(&f[i], &shown[i], sensor.sigma);
        anomaly_fsm_init(&fsm[i], &FSM_CFG);
    }
    rng_seed(42);
    printf("t_s,truth,raw,ma4,ema03,med5,state_raw,state_ma4,state_ema03,state_med5\n");
    double crossing;
    for (double t = 0; t < 8 * 60000.0; t += SAMPLE_MS) {
        double truth = warmup_truth(t, 3.0, &crossing);
        float x = read_sensor(&sensor, truth);
        float y[4];
        for (int i = 0; i < n; i++) {
            y[i] = filter_step(&f[i], x);
            fsm_event_t ev;
            anomaly_fsm_process(&fsm[i], y[i], (int64_t)t, &ev);
        }
        printf("%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d\n", t / 1000.0, truth, y[0], y[1], y[2], y[3],
               fsm[0].state, fsm[1].state, fsm[2].state, fsm[3].state);
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "summary") == 0) {
        summary();
    } else if (argc == 2 && strcmp(argv[1], "trace") == 0) {
        trace();
    } else {
        fprintf(stderr, "usage: %s summary|trace\n", argv[0]);
        return 2;
    }
    return 0;
}
