#include "sensor_sim.h"

#include <math.h>

#define BASE_C 5.0f

// Pseudo-random noise in [-0.3, +0.3] °C, a function of time only, so
// the same simulation repeats on the ESP32 and in the tests.
static float noise(int64_t t_ms)
{
    uint32_t x = (uint32_t)(t_ms / 100) * 2654435761u;
    x ^= x >> 16;
    return ((int32_t)(x % 61) - 30) / 100.0f;
}

float sensor_sim_temperature(int64_t t_ms)
{
    int64_t t = t_ms % SENSOR_SIM_CYCLE_MS;
    float base = BASE_C;

    if (t >= 30000 && t < 31000) {
        base = 9.5f;
    } else if (t >= 45000 && t < 47000) {
        base = 1.0f;
    } else if (t >= 60000 && t < 65000) {
        base = BASE_C + (11.0f - BASE_C) * (t - 60000) / 5000.0f; // ramp up
    } else if (t >= 65000 && t < 75000) {
        base = 11.0f;
    } else if (t >= 75000 && t < 80000) {
        base = 11.0f - (11.0f - BASE_C) * (t - 75000) / 5000.0f;  // ramp down
    } else if (t >= 95000 && t < 100000) {
        return NAN;
    }
    return base + noise(t_ms);
}
