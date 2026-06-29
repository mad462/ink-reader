#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool audio_record_http_upload_build_url(const char *server_base_url, char *buffer, size_t buffer_size);

bool audio_record_http_upload_fetch_headers_ok(int64_t result);

esp_err_t audio_record_http_upload_wav(
    const char *server_base_url,
    const uint8_t *wav_data,
    size_t wav_size,
    uint32_t sequence,
    uint32_t duration_ms,
    int *http_status_out);

bool audio_record_http_upload_self_test(void);

#ifdef __cplusplus
}
#endif
