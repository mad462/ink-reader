#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_ASR_WEB_STATUS_IDLE = 0,
    AUDIO_ASR_WEB_STATUS_RECORDING,
    AUDIO_ASR_WEB_STATUS_WIFI_CONNECTING,
    AUDIO_ASR_WEB_STATUS_ASR_REQUESTING,
    AUDIO_ASR_WEB_STATUS_DONE,
    AUDIO_ASR_WEB_STATUS_ERROR,
} audio_asr_web_status_t;

typedef struct {
    audio_asr_web_status_t status;
    uint32_t sequence;
    uint32_t duration_ms;
    uint32_t pcm_bytes;
    uint32_t wav_bytes;
    uint32_t wifi_elapsed_ms;
    uint32_t request_elapsed_ms;
    bool has_audio;
    const uint8_t *wav_data;
    char transcript_text[2048];
    char error_text[256];
} audio_asr_preview_snapshot_t;

void audio_asr_preview_state_init(void);
void audio_asr_preview_state_set_status(audio_asr_web_status_t status);
void audio_asr_preview_state_set_recording(uint32_t sequence);
void audio_asr_preview_state_set_result_text(const char *text);
void audio_asr_preview_state_set_error_text(const char *text);
void audio_asr_preview_state_set_metrics(
    uint32_t sequence,
    uint32_t duration_ms,
    uint32_t pcm_bytes,
    uint32_t wav_bytes,
    uint32_t wifi_elapsed_ms,
    uint32_t request_elapsed_ms);
esp_err_t audio_asr_preview_state_store_wav_copy(
    const uint8_t *wav_data,
    size_t wav_size,
    bool prefer_psram);
void audio_asr_preview_state_clear_audio(void);
void audio_asr_preview_state_get_snapshot(audio_asr_preview_snapshot_t *snapshot);
const char *audio_asr_preview_state_status_name(audio_asr_web_status_t status);
bool audio_asr_preview_state_self_test(void);

#ifdef __cplusplus
}
#endif
