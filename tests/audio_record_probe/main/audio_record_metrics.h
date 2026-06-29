#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t sample_count;
    int16_t min_sample;
    int16_t max_sample;
    int32_t mean;
    int32_t avg_abs;
    int32_t rms;
    int32_t peak_abs;
    uint32_t clipped_samples;
} audio_record_metrics_t;

void audio_record_metrics_compute(
    const int16_t *samples,
    size_t sample_count,
    audio_record_metrics_t *metrics);

bool audio_record_metrics_self_test(void);

#ifdef __cplusplus
}
#endif
