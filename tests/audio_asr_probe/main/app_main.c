#include "audio_asr_http.h"
#include "audio_asr_json.h"
#include "audio_asr_metrics.h"
#include "audio_asr_preview_state.h"
#include "audio_asr_probe_logic.h"
#include "audio_asr_wav.h"
#include "audio_asr_web.h"
#include "audio_asr_wifi.h"
#include "local_asr_config.h"

#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_rom_uart.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "soc/soc_caps.h"

static const char *TAG = "audio_asr_probe";

#define AUDIO_ASR_CONFIRM_GPIO GPIO_NUM_10
#define AUDIO_ASR_PDM_CLK_GPIO GPIO_NUM_17
#define AUDIO_ASR_PDM_DATA_GPIO GPIO_NUM_18
#define AUDIO_ASR_SAMPLE_RATE_HZ 16000U
#define AUDIO_ASR_BITS_PER_SAMPLE 16U
#define AUDIO_ASR_CHANNELS 1U
#define AUDIO_ASR_MIN_CAPTURE_MS 2000U
#define AUDIO_ASR_MAX_CAPTURE_MS 10000U
#define AUDIO_ASR_WIFI_CONNECT_TIMEOUT_MS 15000U
#define AUDIO_ASR_MAX_PCM_BYTES ((AUDIO_ASR_SAMPLE_RATE_HZ * (AUDIO_ASR_BITS_PER_SAMPLE / 8U) * AUDIO_ASR_CHANNELS * AUDIO_ASR_MAX_CAPTURE_MS) / 1000U)
#define AUDIO_ASR_READ_SAMPLES 1024U
#define AUDIO_ASR_READ_TIMEOUT_MS 40U
#define AUDIO_ASR_IDLE_POLL_MS 10U
#define AUDIO_ASR_PRIME_READS 4U
#define AUDIO_ASR_RESPONSE_BUFFER_BYTES 16384U
#define AUDIO_ASR_LOW_SIGNAL_PEAK_THRESHOLD 1200
#define AUDIO_ASR_TARGET_PEAK_ABS 24000
#define AUDIO_ASR_MAX_DIGITAL_GAIN_X 32
#define AUDIO_ASR_PDM_AMPLIFY_NUM 2U

#define AUDIO_ASR_EVENT_PREFIX "AUDIO_ASR_EVENT "
#define AUDIO_ASR_STATUS_PREFIX "AUDIO_ASR_STATUS "
#define AUDIO_ASR_RESULT_PREFIX "AUDIO_ASR_RESULT "
#define AUDIO_ASR_ERROR_PREFIX "AUDIO_ASR_ERROR "

typedef enum {
    AUDIO_ASR_PDM_PROFILE_STABLE_RIGHT = 0,
} audio_asr_pdm_profile_t;

typedef enum {
    AUDIO_ASR_STORAGE_NONE = 0,
    AUDIO_ASR_STORAGE_PSRAM,
    AUDIO_ASR_STORAGE_INTERNAL,
} audio_asr_storage_t;

typedef struct {
    i2s_chan_handle_t rx_chan;
    int16_t *capture_buffer;
    int16_t *read_buffer;
    audio_asr_probe_session_t session;
    audio_asr_storage_t storage;
    bool rx_enabled;
} audio_asr_probe_context_t;

static const audio_asr_pdm_profile_t AUDIO_ASR_PDM_PROFILE = AUDIO_ASR_PDM_PROFILE_STABLE_RIGHT;

static bool audio_asr_confirm_pressed(void)
{
    return gpio_get_level(AUDIO_ASR_CONFIRM_GPIO) == 0;
}

static uint32_t audio_asr_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static audio_asr_storage_t audio_asr_choose_storage(bool psram_available, bool psram_alloc_ok, bool internal_alloc_ok)
{
    if (psram_available && psram_alloc_ok) {
        return AUDIO_ASR_STORAGE_PSRAM;
    }
    if (internal_alloc_ok) {
        return AUDIO_ASR_STORAGE_INTERNAL;
    }
    return AUDIO_ASR_STORAGE_NONE;
}

static esp_err_t audio_asr_uart_write_all(const void *data, size_t size)
{
    const uint8_t *cursor = (const uint8_t *)data;
    for (size_t i = 0; i < size; ++i) {
        if (esp_rom_output_tx_one_char(cursor[i]) != 0) {
            return ESP_FAIL;
        }
    }
    esp_rom_output_tx_wait_idle(CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM);
    return ESP_OK;
}

static esp_err_t audio_asr_write_text_line(const char *prefix, const char *message)
{
    char line[256];
    const int written = snprintf(line, sizeof(line), "%s%s\r\n", prefix, message);
    if (written < 0 || (size_t)written >= sizeof(line)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return audio_asr_uart_write_all(line, (size_t)written);
}

static void audio_asr_emit_event(const char *message)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(audio_asr_write_text_line(AUDIO_ASR_EVENT_PREFIX, message));
}

static void audio_asr_emit_status(const char *message)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(audio_asr_write_text_line(AUDIO_ASR_STATUS_PREFIX, message));
}

static void audio_asr_emit_error(const char *message)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(audio_asr_write_text_line(AUDIO_ASR_ERROR_PREFIX, message));
}

static void audio_asr_emit_result(const char *text)
{
    if (text == NULL) {
        return;
    }

    size_t len = strlen(text);
    char *line = heap_caps_malloc(len + 1U, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (line == NULL) {
        audio_asr_emit_error("reason=result_alloc_failed");
        return;
    }

    memcpy(line, text, len + 1U);
    for (size_t i = 0; i < len; ++i) {
        if (line[i] == '\r' || line[i] == '\n') {
            line[i] = ' ';
        }
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(audio_asr_uart_write_all(AUDIO_ASR_RESULT_PREFIX, strlen(AUDIO_ASR_RESULT_PREFIX)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(audio_asr_uart_write_all("text=", 5U));
    ESP_ERROR_CHECK_WITHOUT_ABORT(audio_asr_uart_write_all(line, strlen(line)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(audio_asr_uart_write_all("\r\n", 2U));
    free(line);
}

static void audio_asr_preview_set_error(const char *text)
{
    audio_asr_preview_state_set_error_text(text);
    audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_ERROR);
}

static bool audio_asr_pdm_profile_self_test(void)
{
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    return AUDIO_ASR_PDM_AMPLIFY_NUM >= 1U && AUDIO_ASR_PDM_AMPLIFY_NUM <= 8U;
#else
    return true;
#endif
}

static void audio_asr_log_capture_metrics(const int16_t *samples, size_t sample_count)
{
    audio_asr_metrics_t metrics;
    audio_asr_metrics_compute(samples, sample_count, &metrics);
    ESP_LOGI(
        TAG,
        "capture metrics: samples=%u min=%d max=%d mean=%ld avg_abs=%ld rms=%ld peak_abs=%ld clipped=%u",
        (unsigned)metrics.sample_count,
        metrics.min_sample,
        metrics.max_sample,
        (long)metrics.mean,
        (long)metrics.avg_abs,
        (long)metrics.rms,
        (long)metrics.peak_abs,
        (unsigned)metrics.clipped_samples);
    if (metrics.sample_count > 0U && metrics.peak_abs < AUDIO_ASR_LOW_SIGNAL_PEAK_THRESHOLD) {
        ESP_LOGW(
            TAG,
            "capture signal is very low: peak_abs=%ld threshold=%d",
            (long)metrics.peak_abs,
            AUDIO_ASR_LOW_SIGNAL_PEAK_THRESHOLD);
    }
}

static uint32_t audio_asr_apply_digital_gain(int16_t *samples, size_t sample_count)
{
    audio_asr_metrics_t metrics;
    audio_asr_metrics_compute(samples, sample_count, &metrics);
    if (sample_count == 0U || metrics.peak_abs <= 0 || metrics.peak_abs >= AUDIO_ASR_TARGET_PEAK_ABS) {
        return 1U;
    }

    uint32_t gain = (uint32_t)((AUDIO_ASR_TARGET_PEAK_ABS + (uint32_t)metrics.peak_abs - 1U) / (uint32_t)metrics.peak_abs);
    if (gain < 1U) {
        gain = 1U;
    }
    if (gain > AUDIO_ASR_MAX_DIGITAL_GAIN_X) {
        gain = AUDIO_ASR_MAX_DIGITAL_GAIN_X;
    }
    if (gain == 1U) {
        return gain;
    }

    for (size_t i = 0; i < sample_count; ++i) {
        int32_t scaled = (int32_t)samples[i] * (int32_t)gain;
        if (scaled > INT16_MAX) {
            scaled = INT16_MAX;
        } else if (scaled < INT16_MIN) {
            scaled = INT16_MIN;
        }
        samples[i] = (int16_t)scaled;
    }
    return gain;
}

static void audio_asr_configure_pdm_profile(i2s_pdm_rx_config_t *pdm_cfg)
{
    if (pdm_cfg == NULL) {
        return;
    }

    if (AUDIO_ASR_PDM_PROFILE == AUDIO_ASR_PDM_PROFILE_STABLE_RIGHT) {
        pdm_cfg->clk_cfg.dn_sample_mode = I2S_PDM_DSR_16S;
        pdm_cfg->clk_cfg.bclk_div = 8;
        pdm_cfg->slot_cfg.slot_mask = I2S_PDM_SLOT_RIGHT;
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
        pdm_cfg->slot_cfg.hp_en = true;
        pdm_cfg->slot_cfg.hp_cut_off_freq_hz = 35.5f;
        pdm_cfg->slot_cfg.amplify_num = AUDIO_ASR_PDM_AMPLIFY_NUM;
#endif
    }
}

static void audio_asr_log_pdm_profile(const i2s_pdm_rx_config_t *pdm_cfg)
{
    const char *slot_name = "both";
    if (pdm_cfg == NULL) {
        return;
    }

    if (pdm_cfg->slot_cfg.slot_mask == I2S_PDM_SLOT_LEFT) {
        slot_name = "left";
    } else if (pdm_cfg->slot_cfg.slot_mask == I2S_PDM_SLOT_RIGHT) {
        slot_name = "right";
    }

    ESP_LOGI(
        TAG,
        "pdm profile=stable-right dn_sample_mode=%s slot_mask=%s clk_inv=%s",
        pdm_cfg->clk_cfg.dn_sample_mode == I2S_PDM_DSR_16S ? "16S" : "8S",
        slot_name,
        pdm_cfg->gpio_cfg.invert_flags.clk_inv ? "true" : "false");
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    ESP_LOGI(
        TAG,
        "pdm hp_filter=%s cutoff_hz=%.1f amplify_num=%lu",
        pdm_cfg->slot_cfg.hp_en ? "on" : "off",
        (double)pdm_cfg->slot_cfg.hp_cut_off_freq_hz,
        (unsigned long)pdm_cfg->slot_cfg.amplify_num);
#endif
}

static esp_err_t audio_asr_init_button(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << AUDIO_ASR_CONFIRM_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static esp_err_t audio_asr_init_i2s(audio_asr_probe_context_t *ctx)
{
#if !SOC_I2S_SUPPORTS_PDM_RX
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
#else
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &ctx->rx_chan), TAG, "alloc pdm rx channel failed");

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(AUDIO_ASR_SAMPLE_RATE_HZ),
#if SOC_I2S_SUPPORTS_PDM2PCM
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
#else
        .slot_cfg = I2S_PDM_RX_SLOT_RAW_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
#endif
        .gpio_cfg = {
            .clk = AUDIO_ASR_PDM_CLK_GPIO,
            .din = AUDIO_ASR_PDM_DATA_GPIO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    audio_asr_configure_pdm_profile(&pdm_cfg);
    audio_asr_log_pdm_profile(&pdm_cfg);
    return i2s_channel_init_pdm_rx_mode(ctx->rx_chan, &pdm_cfg);
#endif
}

static esp_err_t audio_asr_enable_i2s(audio_asr_probe_context_t *ctx)
{
    if (ctx->rx_enabled) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2s_channel_enable(ctx->rx_chan), TAG, "enable pdm rx failed");
    ctx->rx_enabled = true;
    return ESP_OK;
}

static void audio_asr_disable_i2s(audio_asr_probe_context_t *ctx)
{
    if (!ctx->rx_enabled) {
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_disable(ctx->rx_chan));
    ctx->rx_enabled = false;
}

static void audio_asr_prime_i2s(audio_asr_probe_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    for (uint32_t i = 0; i < AUDIO_ASR_PRIME_READS; ++i) {
        size_t bytes_read = 0U;
        esp_err_t ret = i2s_channel_read(
            ctx->rx_chan,
            ctx->read_buffer,
            AUDIO_ASR_READ_SAMPLES * sizeof(int16_t),
            &bytes_read,
            pdMS_TO_TICKS(AUDIO_ASR_READ_TIMEOUT_MS));
        if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "prime read failed: %s", esp_err_to_name(ret));
            break;
        }
    }
}

static esp_err_t audio_asr_alloc_buffers(audio_asr_probe_context_t *ctx)
{
    const bool psram_available = esp_psram_is_initialized();
    void *psram_capture = NULL;
    void *internal_capture = NULL;

    if (psram_available) {
        psram_capture = heap_caps_malloc(AUDIO_ASR_MAX_PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (psram_capture == NULL) {
        internal_capture = heap_caps_malloc(AUDIO_ASR_MAX_PCM_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    ctx->storage = audio_asr_choose_storage(psram_available, psram_capture != NULL, internal_capture != NULL);
    switch (ctx->storage) {
        case AUDIO_ASR_STORAGE_PSRAM:
            ctx->capture_buffer = (int16_t *)psram_capture;
            ESP_LOGI(TAG, "capture buffer allocated in PSRAM: %u bytes", (unsigned)AUDIO_ASR_MAX_PCM_BYTES);
            break;
        case AUDIO_ASR_STORAGE_INTERNAL:
            ctx->capture_buffer = (int16_t *)internal_capture;
            if (!psram_available) {
                ESP_LOGW(TAG, "PSRAM not available, falling back to internal RAM for capture buffer");
            } else {
                ESP_LOGW(TAG, "PSRAM capture alloc failed, falling back to internal RAM");
            }
            break;
        case AUDIO_ASR_STORAGE_NONE:
        default:
            ESP_LOGE(TAG, "failed to allocate capture buffer in PSRAM or internal RAM");
            return ESP_ERR_NO_MEM;
    }

    ctx->read_buffer = heap_caps_malloc(AUDIO_ASR_READ_SAMPLES * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(ctx->read_buffer != NULL, ESP_ERR_NO_MEM, TAG, "alloc read buffer");
    return ESP_OK;
}

static esp_err_t audio_asr_capture_loop(audio_asr_probe_context_t *ctx)
{
    ESP_RETURN_ON_ERROR(audio_asr_wifi_quiet(), TAG, "quiet wifi before capture");
    ESP_RETURN_ON_ERROR(audio_asr_enable_i2s(ctx), TAG, "enable recording");

    memset(ctx->capture_buffer, 0, AUDIO_ASR_MAX_PCM_BYTES);
    audio_asr_prime_i2s(ctx);
    ESP_LOGI(TAG, "recording started: hold Confirm to continue, release to stop");
    audio_asr_emit_event("pressed");
    audio_asr_preview_state_set_recording(ctx->session.sequence);
    audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_RECORDING);

    while (ctx->session.state == AUDIO_ASR_PROBE_STATE_RECORDING) {
        size_t bytes_read = 0U;
        esp_err_t ret = i2s_channel_read(
            ctx->rx_chan,
            ctx->read_buffer,
            AUDIO_ASR_READ_SAMPLES * sizeof(int16_t),
            &bytes_read,
            pdMS_TO_TICKS(AUDIO_ASR_READ_TIMEOUT_MS));
        if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
            audio_asr_disable_i2s(ctx);
            ESP_LOGE(TAG, "record read failed: %s", esp_err_to_name(ret));
            return ret;
        }

        size_t bytes_to_copy = bytes_read;
        const size_t remaining = ctx->session.max_bytes - ctx->session.captured_bytes;
        if (bytes_to_copy > remaining) {
            bytes_to_copy = remaining;
        }
        if (bytes_to_copy > 0U) {
            memcpy(
                ((uint8_t *)ctx->capture_buffer) + ctx->session.captured_bytes,
                ctx->read_buffer,
                bytes_to_copy);
        }

        const bool button_pressed = audio_asr_confirm_pressed();
        audio_asr_probe_append_bytes(&ctx->session, bytes_to_copy, button_pressed, audio_asr_now_ms());

        if (!button_pressed && ctx->session.state != AUDIO_ASR_PROBE_STATE_RECORDING) {
            audio_asr_emit_event("released");
        }
    }

    audio_asr_disable_i2s(ctx);
    return ESP_OK;
}

static void audio_asr_free_buffer(void *buffer)
{
    if (buffer != NULL) {
        free(buffer);
    }
}

static esp_err_t audio_asr_process_capture(audio_asr_probe_context_t *ctx)
{
    const audio_asr_wifi_config_t *wifi_config = audio_asr_wifi_get_config();
    const audio_asr_http_config_t *http_config = audio_asr_http_get_config();
    const uint32_t duration_ms = audio_asr_probe_pcm_duration_ms(
        ctx->session.captured_bytes,
        AUDIO_ASR_SAMPLE_RATE_HZ,
        AUDIO_ASR_BITS_PER_SAMPLE,
        AUDIO_ASR_CHANNELS);
    char message[192];
    esp_err_t ret = ESP_OK;
    uint8_t *wav_buffer = NULL;
    char *request_body = NULL;
    char *response_buffer = NULL;

    if (ctx->session.captured_bytes == 0U || duration_ms == 0U) {
        audio_asr_preview_set_error("empty_audio");
        audio_asr_emit_error("reason=empty_audio");
        return ESP_ERR_INVALID_SIZE;
    }

    if (!audio_asr_probe_meets_min_duration(duration_ms, AUDIO_ASR_MIN_CAPTURE_MS)) {
        int short_written = snprintf(
            message,
            sizeof(message),
            "reason=recording_too_short duration_ms=%u min_ms=%u",
            (unsigned)duration_ms,
            (unsigned)AUDIO_ASR_MIN_CAPTURE_MS);
        if (short_written > 0 && (size_t)short_written < sizeof(message)) {
            audio_asr_preview_set_error(message);
            audio_asr_emit_error(message);
        } else {
            audio_asr_preview_set_error("reason=recording_too_short");
            audio_asr_emit_error("reason=recording_too_short");
        }
        return ESP_ERR_INVALID_SIZE;
    }

    int written = snprintf(
        message,
        sizeof(message),
        "recording_stopped bytes=%u duration_ms=%u stop_due_to_limit=%s",
        (unsigned)ctx->session.captured_bytes,
        (unsigned)duration_ms,
        ctx->session.stop_due_to_limit ? "true" : "false");
    if (written > 0 && (size_t)written < sizeof(message)) {
        audio_asr_emit_status(message);
    }

    const size_t sample_count = ctx->session.captured_bytes / sizeof(int16_t);
    audio_asr_log_capture_metrics(ctx->capture_buffer, sample_count);
    const uint32_t applied_gain = audio_asr_apply_digital_gain(ctx->capture_buffer, sample_count);
    if (applied_gain > 1U) {
        ESP_LOGI(TAG, "applied digital gain: x%u", (unsigned)applied_gain);
        audio_asr_log_capture_metrics(ctx->capture_buffer, sample_count);
    }

    ESP_RETURN_ON_FALSE(audio_asr_wifi_config_valid(wifi_config), ESP_ERR_INVALID_STATE, TAG, "wifi config missing");
    ESP_RETURN_ON_FALSE(audio_asr_http_config_valid(http_config), ESP_ERR_INVALID_STATE, TAG, "http config missing");

    const size_t wav_size = audio_asr_wav_total_size((uint32_t)ctx->session.captured_bytes);
    const size_t estimated_base64_size = audio_asr_base64_encoded_size(wav_size);
    const size_t estimated_body_size = audio_asr_http_estimate_request_body_size(http_config, wav_size);

    wav_buffer = heap_caps_malloc(wav_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (wav_buffer == NULL) {
        wav_buffer = heap_caps_malloc(wav_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    ESP_GOTO_ON_FALSE(wav_buffer != NULL, ESP_ERR_NO_MEM, cleanup, TAG, "alloc wav buffer");
    ESP_GOTO_ON_FALSE(
        audio_asr_wav_write_header(
            wav_buffer,
            wav_size,
            AUDIO_ASR_SAMPLE_RATE_HZ,
            AUDIO_ASR_BITS_PER_SAMPLE,
            AUDIO_ASR_CHANNELS,
            (uint32_t)ctx->session.captured_bytes),
        ESP_ERR_INVALID_ARG,
        cleanup,
        TAG,
        "write wav header");
    memcpy(wav_buffer + AUDIO_ASR_WAV_HEADER_SIZE, ctx->capture_buffer, ctx->session.captured_bytes);
    audio_asr_preview_state_set_metrics(
        ctx->session.sequence,
        duration_ms,
        (uint32_t)ctx->session.captured_bytes,
        (uint32_t)wav_size,
        0U,
        0U);
    audio_asr_preview_state_set_result_text(NULL);
    audio_asr_preview_state_set_error_text(NULL);
    esp_err_t preview_ret = audio_asr_preview_state_store_wav_copy(
        wav_buffer,
        wav_size,
        ctx->storage == AUDIO_ASR_STORAGE_PSRAM);
    if (preview_ret != ESP_OK) {
        ESP_LOGW(TAG, "preview wav retention failed: %s", esp_err_to_name(preview_ret));
    }

    written = snprintf(
        message,
        sizeof(message),
        "wav_ready pcm_bytes=%u wav_bytes=%u base64_bytes=%u body_estimate=%u",
        (unsigned)ctx->session.captured_bytes,
        (unsigned)wav_size,
        (unsigned)estimated_base64_size,
        (unsigned)estimated_body_size);
    if (written > 0 && (size_t)written < sizeof(message)) {
        audio_asr_emit_status(message);
    }

    request_body = heap_caps_malloc(estimated_body_size + 1U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (request_body == NULL) {
        request_body = heap_caps_malloc(estimated_body_size + 1U, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    ESP_GOTO_ON_FALSE(request_body != NULL, ESP_ERR_NO_MEM, cleanup, TAG, "alloc request body");

    size_t request_body_size = 0U;
    ESP_GOTO_ON_ERROR(
        audio_asr_http_build_request_body(
            http_config,
            wav_buffer,
            wav_size,
            request_body,
            estimated_body_size + 1U,
            &request_body_size),
        cleanup,
        TAG,
        "build request body");

    audio_asr_emit_status("wifi_connecting");
    const uint32_t wifi_started_ms = audio_asr_now_ms();
    audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_WIFI_CONNECTING);
    ret = audio_asr_wifi_connect(wifi_config, AUDIO_ASR_WIFI_CONNECT_TIMEOUT_MS);
    if (ret != ESP_OK) {
        snprintf(message, sizeof(message), "reason=wifi_failed err=%s", esp_err_to_name(ret));
        audio_asr_preview_set_error(message);
        audio_asr_emit_error(message);
        goto cleanup;
    }

    const uint32_t wifi_elapsed_ms = (uint32_t)(audio_asr_now_ms() - wifi_started_ms);
    written = snprintf(
        message,
        sizeof(message),
        "wifi_connected elapsed_ms=%u",
        (unsigned)wifi_elapsed_ms);
    if (written > 0 && (size_t)written < sizeof(message)) {
        audio_asr_emit_status(message);
    }
    audio_asr_preview_state_set_metrics(
        ctx->session.sequence,
        duration_ms,
        (uint32_t)ctx->session.captured_bytes,
        (uint32_t)wav_size,
        wifi_elapsed_ms,
        0U);
    audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_ASR_REQUESTING);

    if (!audio_asr_web_is_running()) {
        ESP_GOTO_ON_ERROR(audio_asr_web_start(), cleanup, TAG, "start web preview server");
        ESP_LOGI(TAG, "web preview server started");
    }

    char ip[32];
    if (audio_asr_wifi_get_ipv4_string(ip, sizeof(ip))) {
        ESP_LOGI(TAG, "web preview url: http://%s/", ip);
    }

    response_buffer = heap_caps_malloc(AUDIO_ASR_RESPONSE_BUFFER_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(response_buffer != NULL, ESP_ERR_NO_MEM, cleanup, TAG, "alloc response buffer");

    written = snprintf(message, sizeof(message), "asr_request_started body_bytes=%u", (unsigned)request_body_size);
    if (written > 0 && (size_t)written < sizeof(message)) {
        audio_asr_emit_status(message);
    }

    size_t response_bytes = 0U;
    int http_status = 0;
    const uint32_t request_started_ms = audio_asr_now_ms();
    ret = audio_asr_http_post_json(
        http_config,
        request_body,
        request_body_size,
        response_buffer,
        AUDIO_ASR_RESPONSE_BUFFER_BYTES,
        &response_bytes,
        &http_status);
    const uint32_t request_elapsed_ms = (uint32_t)(audio_asr_now_ms() - request_started_ms);

    written = snprintf(
        message,
        sizeof(message),
        "asr_request_done elapsed_ms=%u http_status=%d response_bytes=%u",
        (unsigned)request_elapsed_ms,
        http_status,
        (unsigned)response_bytes);
    if (written > 0 && (size_t)written < sizeof(message)) {
        audio_asr_emit_status(message);
    }
    audio_asr_preview_state_set_metrics(
        ctx->session.sequence,
        duration_ms,
        (uint32_t)ctx->session.captured_bytes,
        (uint32_t)wav_size,
        wifi_elapsed_ms,
        request_elapsed_ms);

    if (ret != ESP_OK) {
        snprintf(
            message,
            sizeof(message),
            "reason=http_failed status=%d err=%s",
            http_status,
            esp_err_to_name(ret));
        audio_asr_preview_set_error(message);
        audio_asr_emit_error(message);
        goto cleanup;
    }

    char transcript[2048];
    const audio_asr_json_result_t parse_result = audio_asr_json_extract_transcript(
        response_buffer,
        transcript,
        sizeof(transcript));
    if (parse_result != AUDIO_ASR_JSON_OK) {
        snprintf(
            message,
            sizeof(message),
            "reason=json_parse_failed detail=%s",
            audio_asr_json_result_name(parse_result));
        audio_asr_preview_set_error(message);
        audio_asr_emit_error(message);
        ret = ESP_FAIL;
        goto cleanup;
    }

    audio_asr_preview_state_set_result_text(transcript);
    audio_asr_preview_state_set_error_text(NULL);
    audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_DONE);
    audio_asr_emit_result(transcript);

cleanup:
    audio_asr_free_buffer(response_buffer);
    audio_asr_free_buffer(request_body);
    audio_asr_free_buffer(wav_buffer);
    return ret;
}

static void audio_asr_log_config_summary(void)
{
    const audio_asr_wifi_config_t *wifi_config = audio_asr_wifi_get_config();
    const audio_asr_http_config_t *http_config = audio_asr_http_get_config();

    ESP_LOGI(
        TAG,
        "config source=%s wifi_valid=%s asr_valid=%s ssid=%s base_url=%s model=%s",
        AUDIO_ASR_LOCAL_OVERRIDE_PRESENT ? "override" : "example",
        audio_asr_wifi_config_valid(wifi_config) ? "true" : "false",
        audio_asr_http_config_valid(http_config) ? "true" : "false",
        wifi_config != NULL && wifi_config->ssid != NULL ? wifi_config->ssid : "",
        http_config != NULL && http_config->base_url != NULL ? http_config->base_url : "",
        http_config != NULL && http_config->model != NULL ? http_config->model : "");
}

static void audio_asr_run(audio_asr_probe_context_t *ctx)
{
    for (;;) {
        if (ctx->session.state == AUDIO_ASR_PROBE_STATE_WAIT_FOR_RELEASE) {
            audio_asr_probe_poll_release(&ctx->session, audio_asr_confirm_pressed());
            if (ctx->session.state == AUDIO_ASR_PROBE_STATE_IDLE) {
                audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_IDLE);
            }
            vTaskDelay(pdMS_TO_TICKS(AUDIO_ASR_IDLE_POLL_MS));
            continue;
        }

        if (ctx->session.state == AUDIO_ASR_PROBE_STATE_IDLE) {
            if (!audio_asr_confirm_pressed()) {
                vTaskDelay(pdMS_TO_TICKS(AUDIO_ASR_IDLE_POLL_MS));
                continue;
            }

            if (!audio_asr_probe_try_start(&ctx->session, audio_asr_now_ms())) {
                vTaskDelay(pdMS_TO_TICKS(AUDIO_ASR_IDLE_POLL_MS));
                continue;
            }

            if (audio_asr_capture_loop(ctx) != ESP_OK) {
                audio_asr_emit_error("reason=capture_failed");
                ctx->session.state = AUDIO_ASR_PROBE_STATE_WAIT_FOR_RELEASE;
                continue;
            }
        }

        if (ctx->session.state == AUDIO_ASR_PROBE_STATE_REQUEST_PENDING) {
            esp_err_t ret = audio_asr_process_capture(ctx);
            if (ret == ESP_OK) {
                ESP_LOGI(
                    TAG,
                    "audio asr complete: sequence=%u bytes=%u duration_ms=%u",
                    (unsigned)ctx->session.sequence,
                    (unsigned)ctx->session.captured_bytes,
                    (unsigned)ctx->session.capture_ms);
            } else {
                ESP_LOGE(TAG, "audio asr failed: %s", esp_err_to_name(ret));
            }
            audio_asr_probe_mark_completed(&ctx->session, audio_asr_confirm_pressed());
            if (ctx->session.state == AUDIO_ASR_PROBE_STATE_IDLE) {
                audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_IDLE);
            }
        }
    }
}

void app_main(void)
{
    audio_asr_probe_context_t ctx = {0};

    ESP_LOGI(TAG, "audio asr probe boot");
    ESP_LOGI(TAG, "Confirm GPIO=%d active_low, mic PDM CLK=%d DATA=%d", AUDIO_ASR_CONFIRM_GPIO, AUDIO_ASR_PDM_CLK_GPIO, AUDIO_ASR_PDM_DATA_GPIO);
    ESP_LOGI(TAG, "format=%u Hz %u-bit mono min_ms=%u max_ms=%u", AUDIO_ASR_SAMPLE_RATE_HZ, AUDIO_ASR_BITS_PER_SAMPLE, AUDIO_ASR_MIN_CAPTURE_MS, AUDIO_ASR_MAX_CAPTURE_MS);
    ESP_ERROR_CHECK(audio_asr_probe_logic_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_asr_wav_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_asr_wifi_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_asr_http_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_asr_json_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_asr_metrics_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_asr_pdm_profile_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_asr_preview_state_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_asr_web_self_test() ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "self-tests passed");
    audio_asr_log_config_summary();

    audio_asr_probe_session_init(&ctx.session, AUDIO_ASR_MAX_CAPTURE_MS, AUDIO_ASR_MAX_PCM_BYTES);
    audio_asr_preview_state_init();
    audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_IDLE);
    ESP_ERROR_CHECK(audio_asr_init_button());
    ESP_ERROR_CHECK(audio_asr_alloc_buffers(&ctx));
    ESP_ERROR_CHECK(audio_asr_init_i2s(&ctx));

    ESP_LOGI(TAG, "ready: hold Confirm to record, release to send audio to ASR over Wi-Fi");
    audio_asr_run(&ctx);
}
