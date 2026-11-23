#include "filter.h"

void filter_init(filter_avg_t *f, uint8_t window)
{
    if (window == 0) {
        window = 1;
    } else if (window > FILTER_WINDOW_MAX) {
        window = FILTER_WINDOW_MAX;
    }
    f->window = window;
    filter_reset(f);
}

void filter_reset(filter_avg_t *f)
{
    f->count = 0;
    f->next = 0;
}

float filter_add(filter_avg_t *f, float sample)
{
    f->samples[f->next] = sample;
    f->next = (f->next + 1) % f->window;
    if (f->count < f->window) {
        f->count++;
    }

    // Sum recomputed on every call (window <= 16): avoids the error a
    // running float sum would accumulate.
    float sum = 0.0f;
    for (uint8_t i = 0; i < f->count; i++) {
        sum += f->samples[i];
    }
    return sum / f->count;
}

void filter_median_init(filter_median_t *f, uint8_t window)
{
    if (window == 0) {
        window = 1;
    } else if (window > FILTER_MEDIAN_MAX) {
        window = FILTER_MEDIAN_MAX;
    }
    f->window = window;
    filter_median_reset(f);
}

void filter_median_reset(filter_median_t *f)
{
    f->count = 0;
    f->next = 0;
}

float filter_median_add(filter_median_t *f, float sample)
{
    f->samples[f->next] = sample;
    f->next = (f->next + 1) % f->window;
    if (f->count < f->window) {
        f->count++;
    }
    // Insertion sort of a copy: at most 9 values.
    float sorted[FILTER_MEDIAN_MAX];
    for (uint8_t i = 0; i < f->count; i++) {
        float v = f->samples[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > v) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = v;
    }
    // Even count (still filling up): mean of the two middle values.
    uint8_t mid = f->count / 2;
    return f->count % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2.0f;
}

void filter_ema_init(filter_ema_t *f, float alpha)
{
    if (!(alpha > 0.0f)) {
        alpha = 1.0f;
    } else if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    f->alpha = alpha;
    filter_ema_reset(f);
}

void filter_ema_reset(filter_ema_t *f)
{
    f->primed = false;
}

float filter_ema_add(filter_ema_t *f, float sample)
{
    if (!f->primed) {
        f->value = sample;
        f->primed = true;
    } else {
        f->value += f->alpha * (sample - f->value);
    }
    return f->value;
}

void filter_kalman_init(filter_kalman_t *f, float q, float r)
{
    f->q = q;
    f->r = r > 0.0f ? r : 1e-6f;
    filter_kalman_reset(f);
}

void filter_kalman_reset(filter_kalman_t *f)
{
    f->primed = false;
}

float filter_kalman_add(filter_kalman_t *f, float sample)
{
    if (!f->primed) {
        // First reading: trust it as much as the sensor allows.
        f->x = sample;
        f->p = f->r;
        f->primed = true;
        return f->x;
    }
    f->p += f->q;
    float k = f->p / (f->p + f->r);
    f->x += k * (sample - f->x);
    f->p *= 1.0f - k;
    return f->x;
}
