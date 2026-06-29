#include "audio_record_metrics.h"

#include <limits.h>

bool audio_record_metrics_self_test(void)
{
    audio_record_metrics_t metrics = {
        .sample_count = 123U,
        .min_sample = 5,
        .max_sample = 6,
        .mean = 7,
        .avg_abs = 8,
        .rms = 9,
        .peak_abs = 10,
        .clipped_samples = 11U,
    };
    const int16_t samples[] = {-32768, -1000, 0, 1000, 32767};

    audio_record_metrics_compute(samples, sizeof(samples) / sizeof(samples[0]), &metrics);

    if (metrics.sample_count != 5U) {
        return false;
    }
    if (metrics.min_sample != INT16_MIN || metrics.max_sample != INT16_MAX) {
        return false;
    }
    if (metrics.mean != 0) {
        return false;
    }
    if (metrics.avg_abs != 13507) {
        return false;
    }
    if (metrics.rms != 20734) {
        return false;
    }
    if (metrics.peak_abs != 32768) {
        return false;
    }
    if (metrics.clipped_samples != 2U) {
        return false;
    }

    audio_record_metrics_compute(NULL, 0U, &metrics);
    if (metrics.sample_count != 0U) {
        return false;
    }
    if (metrics.min_sample != 0 || metrics.max_sample != 0) {
        return false;
    }
    if (metrics.mean != 0 || metrics.avg_abs != 0 || metrics.rms != 0 || metrics.peak_abs != 0) {
        return false;
    }
    if (metrics.clipped_samples != 0U) {
        return false;
    }

    return true;
}
