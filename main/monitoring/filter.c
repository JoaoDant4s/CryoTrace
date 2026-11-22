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
