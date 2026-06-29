#include "audio_record_metrics.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

void audio_record_metrics_compute(
    const int16_t *samples,
    size_t sample_count,
    audio_record_metrics_t *metrics)
{
    if (metrics == NULL) {
        return;
    }

    metrics->sample_count = sample_count;
    metrics->min_sample = 0;
    metrics->max_sample = 0;
    metrics->mean = 0;
    metrics->avg_abs = 0;
    metrics->rms = 0;
    metrics->peak_abs = 0;
    metrics->clipped_samples = 0U;

    if (samples == NULL || sample_count == 0U) {
        return;
    }

    int16_t min_sample = INT16_MAX;
    int16_t max_sample = INT16_MIN;
    int32_t peak_abs = 0;
    int64_t sum = 0;
    int64_t sum_abs = 0;
    int64_t sum_sq = 0;
    uint32_t clipped = 0U;

    for (size_t i = 0; i < sample_count; ++i) {
        const int32_t value = samples[i];
        const int32_t abs_value = value == INT16_MIN ? 32768 : abs(value);

        if (value < min_sample) {
            min_sample = (int16_t)value;
        }
        if (value > max_sample) {
            max_sample = (int16_t)value;
        }
        if (abs_value > peak_abs) {
            peak_abs = abs_value;
        }
        if (value == INT16_MIN || value == INT16_MAX) {
            clipped += 1U;
        }

        sum += value;
        sum_abs += abs_value;
        sum_sq += (int64_t)value * (int64_t)value;
    }

    metrics->min_sample = min_sample;
    metrics->max_sample = max_sample;
    metrics->mean = (int32_t)(sum / (int64_t)sample_count);
    metrics->avg_abs = (int32_t)(sum_abs / (int64_t)sample_count);
    metrics->rms = (int32_t)lround(sqrt((double)sum_sq / (double)sample_count));
    metrics->peak_abs = peak_abs;
    metrics->clipped_samples = clipped;
}
