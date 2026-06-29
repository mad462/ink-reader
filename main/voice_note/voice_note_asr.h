#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    const char *api_key;
    const char *base_url;
    const char *model;
} voice_note_asr_config_t;

typedef enum {
    VOICE_NOTE_ASR_OK = 0,
    VOICE_NOTE_ASR_ERROR_INVALID_ARG,
    VOICE_NOTE_ASR_ERROR_CONFIG,
    VOICE_NOTE_ASR_ERROR_HTTP,
    VOICE_NOTE_ASR_ERROR_STATUS,
    VOICE_NOTE_ASR_ERROR_JSON,
    VOICE_NOTE_ASR_ERROR_RESPONSE_TOO_LARGE,
} voice_note_asr_result_t;

const voice_note_asr_config_t *voice_note_asr_get_config(void);
const char *voice_note_asr_result_name(voice_note_asr_result_t result);
bool voice_note_asr_config_valid(const voice_note_asr_config_t *config);
size_t voice_note_asr_estimate_body_size(const voice_note_asr_config_t *config, size_t wav_bytes);
esp_err_t voice_note_asr_build_body(
    const voice_note_asr_config_t *config,
    const uint8_t *wav_data,
    size_t wav_size,
    char *buffer,
    size_t buffer_size,
    size_t *written_out);
voice_note_asr_result_t voice_note_asr_post_wav(
    const uint8_t *wav_data,
    size_t wav_size,
    char *transcript,
    size_t transcript_size);
bool voice_note_asr_self_test(void);
