#include "audio_record_http_upload.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "audio_record_upload";

#define AUDIO_RECORD_HTTP_UPLOAD_PATH "/api/upload"
#define AUDIO_RECORD_HTTP_TIMEOUT_MS 15000
#define AUDIO_RECORD_HTTP_WRITE_CHUNK 4096U

bool audio_record_http_upload_build_url(const char *server_base_url, char *buffer, size_t buffer_size)
{
    if (server_base_url == NULL || server_base_url[0] == '\0' || buffer == NULL || buffer_size == 0U) {
        return false;
    }

    size_t base_len = strlen(server_base_url);
    while (base_len > 0U && server_base_url[base_len - 1U] == '/') {
        base_len -= 1U;
    }
    if (base_len == 0U) {
        return false;
    }

    int written = snprintf(
        buffer,
        buffer_size,
        "%.*s%s",
        (int)base_len,
        server_base_url,
        AUDIO_RECORD_HTTP_UPLOAD_PATH);
    return written > 0 && (size_t)written < buffer_size;
}

bool audio_record_http_upload_fetch_headers_ok(int64_t result)
{
    return result >= 0;
}

esp_err_t audio_record_http_upload_wav(
    const char *server_base_url,
    const uint8_t *wav_data,
    size_t wav_size,
    uint32_t sequence,
    uint32_t duration_ms,
    int *http_status_out)
{
    char url[192];
    char sequence_header[16];
    char duration_header[16];

    ESP_RETURN_ON_FALSE(audio_record_http_upload_build_url(server_base_url, url, sizeof(url)), ESP_ERR_INVALID_ARG, TAG, "invalid upload url");
    ESP_RETURN_ON_FALSE(wav_data != NULL, ESP_ERR_INVALID_ARG, TAG, "wav data missing");
    ESP_RETURN_ON_FALSE(wav_size > 0U, ESP_ERR_INVALID_ARG, TAG, "wav size missing");

    if (http_status_out != NULL) {
        *http_status_out = 0;
    }

    snprintf(sequence_header, sizeof(sequence_header), "%lu", (unsigned long)sequence);
    snprintf(duration_header, sizeof(duration_header), "%lu", (unsigned long)duration_ms);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AUDIO_RECORD_HTTP_TIMEOUT_MS,
        .buffer_size = AUDIO_RECORD_HTTP_WRITE_CHUNK,
        .buffer_size_tx = AUDIO_RECORD_HTTP_WRITE_CHUNK,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_NO_MEM, TAG, "create http client");

    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Content-Type", "audio/wav"), exit, TAG, "set content-type");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "X-Audio-Sequence", sequence_header), exit, TAG, "set sequence");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "X-Audio-Duration-Ms", duration_header), exit, TAG, "set duration");
    ESP_GOTO_ON_ERROR(esp_http_client_open(client, (int)wav_size), exit, TAG, "open upload");

    size_t sent = 0U;
    while (sent < wav_size) {
        size_t chunk_size = (wav_size - sent) < AUDIO_RECORD_HTTP_WRITE_CHUNK
            ? (wav_size - sent)
            : AUDIO_RECORD_HTTP_WRITE_CHUNK;
        int written = esp_http_client_write(client, (const char *)wav_data + sent, (int)chunk_size);
        if (written < 0) {
            ret = ESP_FAIL;
            goto exit;
        }
        sent += (size_t)written;
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
    }

    int64_t header_result = esp_http_client_fetch_headers(client);
    if (!audio_record_http_upload_fetch_headers_ok(header_result)) {
        ESP_LOGE(TAG, "fetch headers failed: %lld", (long long)header_result);
        ret = ESP_FAIL;
        goto exit;
    }
    if (http_status_out != NULL) {
        *http_status_out = esp_http_client_get_status_code(client);
    }
    if (esp_http_client_get_status_code(client) < 200 || esp_http_client_get_status_code(client) >= 300) {
        ret = ESP_FAIL;
    }

exit:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ret;
}
