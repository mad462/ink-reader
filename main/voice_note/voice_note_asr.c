#include "voice_note/voice_note_asr.h"

#include "local_voice_note_asr_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "mbedtls/base64.h"

static const char *const TAG = "voice_note_asr";
static const char *const kBodyPrefix = "{\"model\":\"";
static const char *const kBodyMid =
    "\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"data:audio/wav;base64,";
static const char *const kBodySuffix =
    "\"}}]}],\"stream\":false,\"asr_options\":{\"enable_itn\":false}}";

static const voice_note_asr_config_t s_config = {
    .api_key = VOICE_NOTE_ASR_API_KEY,
    .base_url = VOICE_NOTE_ASR_BASE_URL,
    .model = VOICE_NOTE_ASR_MODEL,
};

typedef struct {
    char *buffer;
    size_t capacity;
    size_t used;
    bool overflow;
} voice_note_http_accumulator_t;

static size_t voice_note_base64_encoded_size(size_t input_bytes);
static bool voice_note_asr_value_is_placeholder(const char *value);
static bool voice_note_asr_build_url(const char *base_url, char *buffer, size_t buffer_size);
static void *voice_note_asr_alloc(size_t size);
static voice_note_asr_result_t voice_note_asr_extract_transcript(
    const char *json,
    char *buffer,
    size_t buffer_size);
static const char *voice_note_asr_config_source_name(void);
static esp_err_t voice_note_http_event_handler(esp_http_client_event_t *event);

static size_t voice_note_base64_encoded_size(size_t input_bytes)
{
    if (input_bytes == 0U) {
        return 0U;
    }
    return ((input_bytes + 2U) / 3U) * 4U;
}

static bool voice_note_asr_value_is_placeholder(const char *value)
{
    return value != NULL && strcmp(value, "replace-with-your-dashscope-api-key") == 0;
}

static bool voice_note_asr_build_url(const char *base_url, char *buffer, size_t buffer_size)
{
    size_t base_len = 0U;
    int written = 0;

    if (base_url == NULL || base_url[0] == '\0' || buffer == NULL || buffer_size == 0U) {
        return false;
    }

    base_len = strlen(base_url);
    while (base_len > 0U && base_url[base_len - 1U] == '/') {
        base_len -= 1U;
    }
    if (base_len == 0U) {
        return false;
    }

    written = snprintf(buffer, buffer_size, "%.*s/chat/completions", (int)base_len, base_url);
    return written > 0 && (size_t)written < buffer_size;
}

static void *voice_note_asr_alloc(size_t size)
{
    void *buffer = NULL;

    if (size == 0U) {
        return NULL;
    }

    buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buffer;
}

static const char *voice_note_asr_config_source_name(void)
{
    return VOICE_NOTE_ASR_LOCAL_OVERRIDE_PRESENT ? "override" : "example";
}

static voice_note_asr_result_t voice_note_asr_extract_transcript(
    const char *json,
    char *buffer,
    size_t buffer_size)
{
    cJSON *root = NULL;
    cJSON *choices = NULL;
    cJSON *choice0 = NULL;
    cJSON *message = NULL;
    cJSON *content = NULL;
    const char *transcript = NULL;
    size_t transcript_len = 0U;

    if (json == NULL || buffer == NULL || buffer_size == 0U) {
        return VOICE_NOTE_ASR_ERROR_INVALID_ARG;
    }

    root = cJSON_Parse(json);
    if (root == NULL) {
        return VOICE_NOTE_ASR_ERROR_JSON;
    }

    choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    choice0 = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
    message = cJSON_IsObject(choice0) ? cJSON_GetObjectItemCaseSensitive(choice0, "message") : NULL;
    content = cJSON_IsObject(message) ? cJSON_GetObjectItemCaseSensitive(message, "content") : NULL;
    if (!cJSON_IsString(content) || content->valuestring == NULL) {
        cJSON_Delete(root);
        return VOICE_NOTE_ASR_ERROR_JSON;
    }

    transcript = content->valuestring;
    transcript_len = strlen(transcript);
    if (transcript_len + 1U > buffer_size) {
        cJSON_Delete(root);
        return VOICE_NOTE_ASR_ERROR_RESPONSE_TOO_LARGE;
    }

    memcpy(buffer, transcript, transcript_len + 1U);
    cJSON_Delete(root);
    return VOICE_NOTE_ASR_OK;
}

static esp_err_t voice_note_http_event_handler(esp_http_client_event_t *event)
{
    voice_note_http_accumulator_t *acc = event != NULL
        ? (voice_note_http_accumulator_t *)event->user_data
        : NULL;

    if (event == NULL || acc == NULL) {
        return ESP_OK;
    }
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }
    if (acc->used + (size_t)event->data_len + 1U > acc->capacity) {
        acc->overflow = true;
        return ESP_OK;
    }

    memcpy(acc->buffer + acc->used, event->data, (size_t)event->data_len);
    acc->used += (size_t)event->data_len;
    acc->buffer[acc->used] = '\0';
    return ESP_OK;
}

const voice_note_asr_config_t *voice_note_asr_get_config(void)
{
    return &s_config;
}

const char *voice_note_asr_result_name(voice_note_asr_result_t result)
{
    switch (result) {
        case VOICE_NOTE_ASR_OK:
            return "ok";
        case VOICE_NOTE_ASR_ERROR_INVALID_ARG:
            return "invalid_arg";
        case VOICE_NOTE_ASR_ERROR_CONFIG:
            return "config";
        case VOICE_NOTE_ASR_ERROR_HTTP:
            return "http";
        case VOICE_NOTE_ASR_ERROR_STATUS:
            return "http_status";
        case VOICE_NOTE_ASR_ERROR_JSON:
            return "json";
        case VOICE_NOTE_ASR_ERROR_RESPONSE_TOO_LARGE:
            return "response_too_large";
        default:
            return "unknown";
    }
}

bool voice_note_asr_config_valid(const voice_note_asr_config_t *config)
{
    return config != NULL
        && config->api_key != NULL
        && config->base_url != NULL
        && config->model != NULL
        && config->api_key[0] != '\0'
        && config->base_url[0] != '\0'
        && config->model[0] != '\0'
        && !voice_note_asr_value_is_placeholder(config->api_key);
}

size_t voice_note_asr_estimate_body_size(const voice_note_asr_config_t *config, size_t wav_bytes)
{
    if (!voice_note_asr_config_valid(config) || wav_bytes == 0U) {
        return 0U;
    }

    return strlen(kBodyPrefix)
        + strlen(config->model)
        + strlen(kBodyMid)
        + voice_note_base64_encoded_size(wav_bytes)
        + strlen(kBodySuffix);
}

esp_err_t voice_note_asr_build_body(
    const voice_note_asr_config_t *config,
    const uint8_t *wav_data,
    size_t wav_size,
    char *buffer,
    size_t buffer_size,
    size_t *written_out)
{
    char *cursor = buffer;
    size_t required = 0U;
    size_t remaining = 0U;
    size_t base64_written = 0U;
    int ret = 0;

    ESP_RETURN_ON_FALSE(voice_note_asr_config_valid(config), ESP_ERR_INVALID_ARG, TAG, "invalid config");
    ESP_RETURN_ON_FALSE(wav_data != NULL && wav_size > 0U, ESP_ERR_INVALID_ARG, TAG, "invalid wav");
    ESP_RETURN_ON_FALSE(buffer != NULL && buffer_size > 0U, ESP_ERR_INVALID_ARG, TAG, "invalid buffer");

    required = voice_note_asr_estimate_body_size(config, wav_size);
    ESP_RETURN_ON_FALSE(
        required > 0U && required + 1U <= buffer_size,
        ESP_ERR_INVALID_SIZE,
        TAG,
        "body buffer too small");

    memcpy(cursor, kBodyPrefix, strlen(kBodyPrefix));
    cursor += strlen(kBodyPrefix);
    memcpy(cursor, config->model, strlen(config->model));
    cursor += strlen(config->model);
    memcpy(cursor, kBodyMid, strlen(kBodyMid));
    cursor += strlen(kBodyMid);

    remaining = buffer_size - (size_t)(cursor - buffer);
    ret = mbedtls_base64_encode((unsigned char *)cursor, remaining, &base64_written, wav_data, wav_size);
    ESP_RETURN_ON_FALSE(ret == 0, ESP_FAIL, TAG, "base64 encode failed");
    cursor += base64_written;

    memcpy(cursor, kBodySuffix, strlen(kBodySuffix));
    cursor += strlen(kBodySuffix);
    *cursor = '\0';

    if (written_out != NULL) {
        *written_out = (size_t)(cursor - buffer);
    }
    return ESP_OK;
}

voice_note_asr_result_t voice_note_asr_post_wav(
    const uint8_t *wav_data,
    size_t wav_size,
    char *transcript,
    size_t transcript_size)
{
    char url[192];
    char auth_header[256];
    char *body = NULL;
    char response[1024];
    size_t body_capacity = 0U;
    size_t body_size = 0U;
    int auth_written = 0;
    voice_note_http_accumulator_t acc = {
        .buffer = response,
        .capacity = sizeof(response),
        .used = 0U,
        .overflow = false,
    };
    esp_http_client_config_t client_cfg = {0};
    esp_http_client_handle_t client = NULL;
    esp_err_t ret = ESP_OK;
    esp_err_t http_err = ESP_OK;
    int http_status = 0;
    voice_note_asr_result_t result = VOICE_NOTE_ASR_ERROR_HTTP;
    const bool config_valid = voice_note_asr_config_valid(&s_config);

    if (!config_valid) {
        ESP_LOGW(
            TAG,
            "asr config invalid source=%s base_url=%s model=%s api_key_set=%d",
            voice_note_asr_config_source_name(),
            s_config.base_url != NULL ? s_config.base_url : "(null)",
            s_config.model != NULL ? s_config.model : "(null)",
            s_config.api_key != NULL && s_config.api_key[0] != '\0' ? 1 : 0);
        return VOICE_NOTE_ASR_ERROR_CONFIG;
    }
    if (wav_data == NULL || wav_size == 0U || transcript == NULL || transcript_size == 0U) {
        return VOICE_NOTE_ASR_ERROR_INVALID_ARG;
    }
    if (!voice_note_asr_build_url(s_config.base_url, url, sizeof(url))) {
        return VOICE_NOTE_ASR_ERROR_CONFIG;
    }

    auth_written = snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_config.api_key);
    if (auth_written <= 0 || (size_t)auth_written >= sizeof(auth_header)) {
        ESP_LOGW(TAG, "asr auth header build failed");
        return VOICE_NOTE_ASR_ERROR_CONFIG;
    }

    body_capacity = voice_note_asr_estimate_body_size(&s_config, wav_size) + 1U;
    body = voice_note_asr_alloc(body_capacity);
    if (body == NULL) {
        ESP_LOGW(TAG, "asr body alloc failed bytes=%u", (unsigned)body_capacity);
        return VOICE_NOTE_ASR_ERROR_INVALID_ARG;
    }
    if (voice_note_asr_build_body(&s_config, wav_data, wav_size, body, body_capacity, &body_size) != ESP_OK) {
        ESP_LOGW(TAG, "asr build body failed");
    heap_caps_free(body);
    return VOICE_NOTE_ASR_ERROR_INVALID_ARG;
    }

    response[0] = '\0';
    client_cfg.url = url;
    client_cfg.method = HTTP_METHOD_POST;
    client_cfg.timeout_ms = 45000;
    client_cfg.buffer_size = 4096;
    client_cfg.buffer_size_tx = 4096;
    client_cfg.event_handler = voice_note_http_event_handler;
    client_cfg.user_data = &acc;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    client_cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    client = esp_http_client_init(&client_cfg);
    if (client == NULL) {
        free(body);
        return VOICE_NOTE_ASR_ERROR_HTTP;
    }

    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Authorization", auth_header), cleanup, TAG, "set auth header");
    ESP_GOTO_ON_ERROR(esp_http_client_set_header(client, "Content-Type", "application/json"), cleanup, TAG, "set content-type");
    ESP_GOTO_ON_ERROR(esp_http_client_set_post_field(client, body, (int)body_size), cleanup, TAG, "set post field");

    http_err = esp_http_client_perform(client);
    if (http_err != ESP_OK) {
        ESP_LOGW(TAG, "asr http perform failed err=%s", esp_err_to_name(http_err));
        result = VOICE_NOTE_ASR_ERROR_HTTP;
        goto cleanup;
    }

    http_status = esp_http_client_get_status_code(client);
    if (acc.overflow) {
        ESP_LOGW(TAG, "asr response overflow capacity=%u", (unsigned)sizeof(response));
        result = VOICE_NOTE_ASR_ERROR_RESPONSE_TOO_LARGE;
        goto cleanup;
    }
    if (http_status < 200 || http_status >= 300) {
        ESP_LOGW(
            TAG,
            "asr http status=%d response=%s",
            http_status,
            response[0] != '\0' ? response : "(empty)");
        result = VOICE_NOTE_ASR_ERROR_STATUS;
        goto cleanup;
    }

    result = voice_note_asr_extract_transcript(response, transcript, transcript_size);
    if (result != VOICE_NOTE_ASR_OK) {
        ESP_LOGW(
            TAG,
            "asr parse failed result=%s response=%s",
            voice_note_asr_result_name(result),
            response[0] != '\0' ? response : "(empty)");
        goto cleanup;
    }
cleanup:
    (void)ret;
    if (client != NULL) {
        esp_http_client_cleanup(client);
    }
    heap_caps_free(body);
    return result;
}

bool voice_note_asr_self_test(void)
{
    uint8_t wav_data[1] = {0x66};
    char body[256];
    char transcript[32];
    size_t written = 0U;
    const voice_note_asr_config_t config = {
        .api_key = "k",
        .base_url = "https://example.com/compatible-mode/v1",
        .model = "qwen3-asr-flash",
    };

    transcript[0] = '\0';
    return voice_note_asr_config_valid(&config)
        && strcmp(voice_note_asr_result_name(VOICE_NOTE_ASR_ERROR_STATUS), "http_status") == 0
        && !voice_note_asr_config_valid(&(voice_note_asr_config_t){0})
        && voice_note_asr_estimate_body_size(&config, sizeof(wav_data)) > 0U
        && voice_note_asr_build_body(&config, wav_data, sizeof(wav_data), body, sizeof(body), &written) == ESP_OK
        && written == strlen(body)
        && strstr(body, "\"type\":\"input_audio\"") != NULL
        && strstr(body, "data:audio/wav;base64,Zg==") != NULL
        && voice_note_asr_extract_transcript(
               "{\"choices\":[{\"message\":{\"content\":\"hello \\u4f60\\u597d\"}}]}",
               transcript,
               sizeof(transcript))
            == VOICE_NOTE_ASR_OK
        && strcmp(transcript, "hello 你好") == 0
        && voice_note_asr_extract_transcript("{}", transcript, sizeof(transcript)) == VOICE_NOTE_ASR_ERROR_JSON;
}
