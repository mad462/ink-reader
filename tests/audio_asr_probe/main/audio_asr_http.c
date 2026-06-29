#include "audio_asr_http.h"

#include "local_asr_config.h"
#include "audio_asr_wav.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include "mbedtls/base64.h"

static const char *const AUDIO_ASR_HTTP_BODY_PREFIX = "{\"model\":\"";
static const char *const AUDIO_ASR_HTTP_BODY_MID = "\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"data:audio/wav;base64,";
static const char *const AUDIO_ASR_HTTP_BODY_SUFFIX = "\"}}]}],\"stream\":false,\"asr_options\":{\"enable_itn\":false}}";

static const audio_asr_http_config_t s_config = {
    .api_key = AUDIO_ASR_API_KEY,
    .base_url = AUDIO_ASR_BASE_URL,
    .model = AUDIO_ASR_MODEL,
};

typedef struct {
    char *buffer;
    size_t capacity;
    size_t used;
    bool overflow;
} audio_asr_http_response_accumulator_t;

static bool audio_asr_http_value_is_placeholder(const char *value)
{
    return value != NULL
        && strcmp(value, "replace-with-your-dashscope-api-key") == 0;
}

static esp_err_t audio_asr_http_event_handler(esp_http_client_event_t *event)
{
    audio_asr_http_response_accumulator_t *accumulator = event != NULL
        ? (audio_asr_http_response_accumulator_t *)event->user_data
        : NULL;

    if (event == NULL || accumulator == NULL) {
        return ESP_OK;
    }

    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    if (accumulator->used + (size_t)event->data_len + 1U > accumulator->capacity) {
        accumulator->overflow = true;
        return ESP_OK;
    }

    memcpy(accumulator->buffer + accumulator->used, event->data, (size_t)event->data_len);
    accumulator->used += (size_t)event->data_len;
    accumulator->buffer[accumulator->used] = '\0';
    return ESP_OK;
}

const audio_asr_http_config_t *audio_asr_http_get_config(void)
{
    return &s_config;
}

bool audio_asr_http_build_url(const char *base_url, char *buffer, size_t buffer_size)
{
    if (base_url == NULL || base_url[0] == '\0' || buffer == NULL || buffer_size == 0U) {
        return false;
    }

    size_t base_len = strlen(base_url);
    while (base_len > 0U && base_url[base_len - 1U] == '/') {
        base_len -= 1U;
    }
    if (base_len == 0U) {
        return false;
    }

    int written = snprintf(buffer, buffer_size, "%.*s/chat/completions", (int)base_len, base_url);
    return written > 0 && (size_t)written < buffer_size;
}

bool audio_asr_http_config_valid(const audio_asr_http_config_t *config)
{
    return config != NULL
        && config->api_key != NULL
        && config->base_url != NULL
        && config->model != NULL
        && config->api_key[0] != '\0'
        && config->base_url[0] != '\0'
        && config->model[0] != '\0'
        && !audio_asr_http_value_is_placeholder(config->api_key);
}

size_t audio_asr_http_estimate_request_body_size(const audio_asr_http_config_t *config, size_t wav_bytes)
{
    if (!audio_asr_http_config_valid(config) || wav_bytes == 0U) {
        return 0U;
    }

    return strlen(AUDIO_ASR_HTTP_BODY_PREFIX)
        + strlen(config->model)
        + strlen(AUDIO_ASR_HTTP_BODY_MID)
        + audio_asr_base64_encoded_size(wav_bytes)
        + strlen(AUDIO_ASR_HTTP_BODY_SUFFIX);
}

esp_err_t audio_asr_http_build_request_body(
    const audio_asr_http_config_t *config,
    const uint8_t *wav_data,
    size_t wav_size,
    char *buffer,
    size_t buffer_size,
    size_t *written_out)
{
    char *cursor = buffer;
    size_t remaining = buffer_size;
    size_t base64_written = 0U;
    int ret = 0;

    ESP_RETURN_ON_FALSE(audio_asr_http_config_valid(config), ESP_ERR_INVALID_ARG, "audio_asr_http", "invalid config");
    ESP_RETURN_ON_FALSE(wav_data != NULL && wav_size > 0U, ESP_ERR_INVALID_ARG, "audio_asr_http", "invalid wav");
    ESP_RETURN_ON_FALSE(buffer != NULL && buffer_size > 0U, ESP_ERR_INVALID_ARG, "audio_asr_http", "invalid buffer");

    const size_t required = audio_asr_http_estimate_request_body_size(config, wav_size);
    ESP_RETURN_ON_FALSE(required > 0U && required + 1U <= buffer_size, ESP_ERR_INVALID_SIZE, "audio_asr_http", "body buffer too small");

    memcpy(cursor, AUDIO_ASR_HTTP_BODY_PREFIX, strlen(AUDIO_ASR_HTTP_BODY_PREFIX));
    cursor += strlen(AUDIO_ASR_HTTP_BODY_PREFIX);
    memcpy(cursor, config->model, strlen(config->model));
    cursor += strlen(config->model);
    memcpy(cursor, AUDIO_ASR_HTTP_BODY_MID, strlen(AUDIO_ASR_HTTP_BODY_MID));
    cursor += strlen(AUDIO_ASR_HTTP_BODY_MID);

    remaining = buffer_size - (size_t)(cursor - buffer);
    ret = mbedtls_base64_encode((unsigned char *)cursor, remaining, &base64_written, wav_data, wav_size);
    ESP_RETURN_ON_FALSE(ret == 0, ESP_FAIL, "audio_asr_http", "base64 encode failed");
    cursor += base64_written;

    memcpy(cursor, AUDIO_ASR_HTTP_BODY_SUFFIX, strlen(AUDIO_ASR_HTTP_BODY_SUFFIX));
    cursor += strlen(AUDIO_ASR_HTTP_BODY_SUFFIX);
    *cursor = '\0';

    if (written_out != NULL) {
        *written_out = (size_t)(cursor - buffer);
    }
    return ESP_OK;
}

esp_err_t audio_asr_http_post_json(
    const audio_asr_http_config_t *config,
    const char *json_body,
    size_t json_body_size,
    char *response_buffer,
    size_t response_buffer_size,
    size_t *response_bytes_out,
    int *http_status_out)
{
    static const char *TAG = "audio_asr_http";
    char url[192];
    char auth_header[256];
    audio_asr_http_response_accumulator_t accumulator = {
        .buffer = response_buffer,
        .capacity = response_buffer_size,
        .used = 0U,
        .overflow = false,
    };

    ESP_RETURN_ON_FALSE(audio_asr_http_config_valid(config), ESP_ERR_INVALID_ARG, TAG, "invalid config");
    ESP_RETURN_ON_FALSE(json_body != NULL && json_body_size > 0U, ESP_ERR_INVALID_ARG, TAG, "invalid body");
    ESP_RETURN_ON_FALSE(response_buffer != NULL && response_buffer_size > 0U, ESP_ERR_INVALID_ARG, TAG, "invalid response buffer");
    ESP_RETURN_ON_FALSE(audio_asr_http_build_url(config->base_url, url, sizeof(url)), ESP_ERR_INVALID_ARG, TAG, "invalid url");

    response_buffer[0] = '\0';
    if (response_bytes_out != NULL) {
        *response_bytes_out = 0U;
    }
    if (http_status_out != NULL) {
        *http_status_out = 0;
    }

    const int auth_written = snprintf(auth_header, sizeof(auth_header), "Bearer %s", config->api_key);
    ESP_RETURN_ON_FALSE(auth_written > 0 && (size_t)auth_written < sizeof(auth_header), ESP_ERR_INVALID_SIZE, TAG, "auth header too large");

    esp_http_client_config_t client_config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 45000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .event_handler = audio_asr_http_event_handler,
        .user_data = &accumulator,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_NO_MEM, TAG, "create http client");

    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Authorization", auth_header), cleanup, TAG, "set auth header");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Content-Type", "application/json"), cleanup, TAG, "set content-type");
    ESP_GOTO_ON_ERROR(esp_http_client_set_post_field(client, json_body, (int)json_body_size), cleanup, TAG, "set post field");
    ret = esp_http_client_perform(client);

    if (http_status_out != NULL) {
        *http_status_out = esp_http_client_get_status_code(client);
    }
    if (response_bytes_out != NULL) {
        *response_bytes_out = accumulator.used;
    }
    if (accumulator.overflow) {
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }
    if (ret == ESP_OK && (esp_http_client_get_status_code(client) < 200 || esp_http_client_get_status_code(client) >= 300)) {
        ret = ESP_FAIL;
    }

cleanup:
    esp_http_client_cleanup(client);
    return ret;
}

bool audio_asr_http_self_test(void)
{
    uint8_t wav_data[1] = {0x66};
    char url[128];
    char body[256];
    size_t body_size = 0U;
    audio_asr_http_config_t cfg = {
        .api_key = "k",
        .base_url = "https://example.com/compatible-mode/v1",
        .model = "qwen3-asr-flash",
    };

    return audio_asr_http_config_valid(&cfg)
        && !audio_asr_http_config_valid(&(audio_asr_http_config_t){0})
        && audio_asr_http_build_url(cfg.base_url, url, sizeof(url))
        && strcmp(url, "https://example.com/compatible-mode/v1/chat/completions") == 0
        && audio_asr_http_estimate_request_body_size(&cfg, sizeof(wav_data)) > 0U
        && audio_asr_http_build_request_body(&cfg, wav_data, sizeof(wav_data), body, sizeof(body), &body_size) == ESP_OK
        && body_size == strlen(body)
        && strstr(body, "\"type\":\"input_audio\"") != NULL
        && strstr(body, "data:audio/wav;base64,Zg==") != NULL;
}
