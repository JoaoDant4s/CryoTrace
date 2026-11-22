#pragma once

#include <stdint.h>

// Simple moving average over the last N valid samples.
// Plain C (no ESP-IDF) so it can be tested on the host.

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
