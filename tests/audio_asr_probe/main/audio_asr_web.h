#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "audio_asr_preview_state.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_asr_web_start(void);
bool audio_asr_web_escape_json_string(const char *src, char *dst, size_t dst_size);
bool audio_asr_web_build_latest_json(
    const audio_asr_preview_snapshot_t *snapshot,
    char *buffer,
    size_t buffer_size);
bool audio_asr_web_is_running(void);
bool audio_asr_web_self_test(void);

#ifdef __cplusplus
}
#endif
