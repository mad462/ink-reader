#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *api_key;
    const char *base_url;
    const char *model;
} audio_asr_http_config_t;

const audio_asr_http_config_t *audio_asr_http_get_config(void);
bool audio_asr_http_build_url(const char *base_url, char *buffer, size_t buffer_size);
bool audio_asr_http_config_valid(const audio_asr_http_config_t *config);
size_t audio_asr_http_estimate_request_body_size(const audio_asr_http_config_t *config, size_t wav_bytes);
esp_err_t audio_asr_http_build_request_body(
    const audio_asr_http_config_t *config,
    const uint8_t *wav_data,
    size_t wav_size,
    char *buffer,
    size_t buffer_size,
    size_t *written_out);
esp_err_t audio_asr_http_post_json(
    const audio_asr_http_config_t *config,
    const char *json_body,
    size_t json_body_size,
    char *response_buffer,
    size_t response_buffer_size,
    size_t *response_bytes_out,
    int *http_status_out);
bool audio_asr_http_self_test(void);

#ifdef __cplusplus
}
#endif
