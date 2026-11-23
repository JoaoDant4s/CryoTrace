#pragma once

#include <stdint.h>

// Smoothing filters for the temperature readings. The firmware uses the
// moving average; EMA and a 1D Kalman filter are here for comparison
// (analysis/filter_bench.c) and as drop-in alternatives.
// Plain C (no ESP-IDF) so it can be tested on the host.

#include <stdbool.h>

#define FILTER_WINDOW_MAX 16

typedef struct {
    float samples[FILTER_WINDOW_MAX];
    uint8_t window; // configured size
    uint8_t count;  // samples accumulated (saturates at window)
    uint8_t next;   // next write position
} filter_avg_t;

void filter_init(filter_avg_t *f, uint8_t window);

// Drops the history, e.g. after the sensor recovers from a failure.
void filter_reset(filter_avg_t *f);

// Adds a sample and returns the average of the available samples.
float filter_add(filter_avg_t *f, float sample);

// Running median over the last N samples (N odd, up to FILTER_MEDIAN_MAX).
// Removes isolated glitches entirely, which averaging only spreads out.
#define FILTER_MEDIAN_MAX 9

typedef struct {
    float samples[FILTER_MEDIAN_MAX];
    uint8_t window;
    uint8_t count;
    uint8_t next;
} filter_median_t;

void filter_median_init(filter_median_t *f, uint8_t window);
void filter_median_reset(filter_median_t *f);
float filter_median_add(filter_median_t *f, float sample);

// Exponential moving average: y += alpha * (x - y). The first sample after
// init/reset is taken as is. alpha in (0, 1]; smaller = smoother, slower.
typedef struct {
    float alpha;
    float value;
    bool primed;
} filter_ema_t;

void filter_ema_init(filter_ema_t *f, float alpha);
void filter_ema_reset(filter_ema_t *f);
float filter_ema_add(filter_ema_t *f, float sample);

// 1D Kalman filter with a random-walk model (the temperature drifts slowly):
//   predict: p += q;  update: k = p / (p + r); x += k (z - x); p *= 1 - k.
// q = process noise variance per sample, r = measurement noise variance
// (sensor noise squared). The gain k settles to a constant, so in steady
// state it behaves as an EMA with alpha = k, chosen from q and r.
typedef struct {
    float q;
    float r;
    float x;
    float p;
    bool primed;
} filter_kalman_t;

void filter_kalman_init(filter_kalman_t *f, float q, float r);
void filter_kalman_reset(filter_kalman_t *f);
float filter_kalman_add(filter_kalman_t *f, float sample);
