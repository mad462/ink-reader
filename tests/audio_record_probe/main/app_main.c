#include "audio_record_http_upload.h"
#include "audio_record_metrics.h"
#include "audio_record_probe_logic.h"
#include "audio_record_wifi.h"
#include "audio_record_wav.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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

static const char *TAG = "audio_record_probe";

#define AUDIO_RECORD_CONFIRM_GPIO GPIO_NUM_10
#define AUDIO_RECORD_SAMPLE_RATE_HZ 16000U
#define AUDIO_RECORD_BITS_PER_SAMPLE 16U
#define AUDIO_RECORD_CHANNELS 1U
#define AUDIO_RECORD_MAX_CAPTURE_MS 10000U
#define AUDIO_RECORD_WIFI_CONNECT_TIMEOUT_MS 15000U
#define AUDIO_RECORD_MAX_PCM_BYTES ((AUDIO_RECORD_SAMPLE_RATE_HZ * (AUDIO_RECORD_BITS_PER_SAMPLE / 8U) * AUDIO_RECORD_CHANNELS * AUDIO_RECORD_MAX_CAPTURE_MS) / 1000U)
#define AUDIO_RECORD_PDM_CLK_GPIO GPIO_NUM_17
#define AUDIO_RECORD_PDM_DATA_GPIO GPIO_NUM_18
#define AUDIO_RECORD_READ_SAMPLES 1024U
#define AUDIO_RECORD_EVENT_PREFIX "AUDIO_RECORD_EVENT "
#define AUDIO_RECORD_STATUS_PREFIX "AUDIO_RECORD_STATUS "
#define AUDIO_RECORD_IDLE_POLL_MS 10U
#define AUDIO_RECORD_READ_TIMEOUT_MS 40U
#define AUDIO_RECORD_PROGRESS_REPORT_MS 0U
#define AUDIO_RECORD_PRIME_READS 4U
#define AUDIO_RECORD_LOW_SIGNAL_PEAK_THRESHOLD 1000

typedef enum {
    AUDIO_RECORD_PDM_PROFILE_STABLE_RIGHT = 0,
    AUDIO_RECORD_PDM_PROFILE_DEFAULT,
} audio_record_pdm_profile_t;

typedef struct {
    i2s_chan_handle_t rx_chan;
    int16_t *capture_buffer;
    int16_t *read_buffer;
    audio_record_probe_storage_t storage;
    audio_record_probe_session_t session;
    bool rx_enabled;
    uint32_t last_progress_report_ms;
    bool last_button_pressed;
} audio_record_probe_context_t;

static const audio_record_pdm_profile_t AUDIO_RECORD_PDM_PROFILE = AUDIO_RECORD_PDM_PROFILE_STABLE_RIGHT;

static bool audio_record_confirm_pressed(void)
{
    return gpio_get_level(AUDIO_RECORD_CONFIRM_GPIO) == 0;
}

static uint32_t audio_record_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static const char *audio_record_pdm_profile_name(audio_record_pdm_profile_t profile)
{
    switch (profile) {
        case AUDIO_RECORD_PDM_PROFILE_STABLE_RIGHT:
            return "stable-right";
        case AUDIO_RECORD_PDM_PROFILE_DEFAULT:
            return "default";
        default:
            return "unknown";
    }
}

static void audio_record_configure_pdm_profile(i2s_pdm_rx_config_t *pdm_cfg)
{
    if (pdm_cfg == NULL) {
        return;
    }

    if (AUDIO_RECORD_PDM_PROFILE == AUDIO_RECORD_PDM_PROFILE_STABLE_RIGHT) {
        pdm_cfg->clk_cfg.dn_sample_mode = I2S_PDM_DSR_16S;
        pdm_cfg->clk_cfg.bclk_div = 8;
        pdm_cfg->slot_cfg.slot_mask = I2S_PDM_SLOT_RIGHT;
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
        pdm_cfg->slot_cfg.hp_en = true;
        pdm_cfg->slot_cfg.hp_cut_off_freq_hz = 35.5f;
        pdm_cfg->slot_cfg.amplify_num = 1;
#endif
    }
}

static void audio_record_log_pdm_profile(const i2s_pdm_rx_config_t *pdm_cfg)
{
    if (pdm_cfg == NULL) {
        return;
    }

    const char *slot_name = "both";
    if (pdm_cfg->slot_cfg.slot_mask == I2S_PDM_SLOT_LEFT) {
        slot_name = "left";
    } else if (pdm_cfg->slot_cfg.slot_mask == I2S_PDM_SLOT_RIGHT) {
        slot_name = "right";
    }

    ESP_LOGI(
        TAG,
        "pdm profile=%s dn_sample_mode=%s slot_mask=%s clk_inv=%s",
        audio_record_pdm_profile_name(AUDIO_RECORD_PDM_PROFILE),
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

static void audio_record_log_capture_metrics(const audio_record_probe_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    audio_record_metrics_t metrics;
    const size_t sample_count = ctx->session.captured_bytes / sizeof(int16_t);
    audio_record_metrics_compute(ctx->capture_buffer, sample_count, &metrics);
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
    if (metrics.sample_count > 0U && metrics.peak_abs < AUDIO_RECORD_LOW_SIGNAL_PEAK_THRESHOLD) {
        ESP_LOGW(
            TAG,
            "capture signal is very low: peak_abs=%ld threshold=%d, uploaded WAV may sound silent",
            (long)metrics.peak_abs,
            AUDIO_RECORD_LOW_SIGNAL_PEAK_THRESHOLD);
    }
}

static void audio_record_prime_i2s(audio_record_probe_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    for (uint32_t i = 0; i < AUDIO_RECORD_PRIME_READS; ++i) {
        size_t bytes_read = 0U;
        esp_err_t ret = i2s_channel_read(
            ctx->rx_chan,
            ctx->read_buffer,
            AUDIO_RECORD_READ_SAMPLES * sizeof(int16_t),
            &bytes_read,
            pdMS_TO_TICKS(AUDIO_RECORD_READ_TIMEOUT_MS));
        if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "prime read failed: %s", esp_err_to_name(ret));
            break;
        }
        (void)bytes_read;
    }
}

static esp_err_t audio_record_init_button(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << AUDIO_RECORD_CONFIRM_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static esp_err_t audio_record_init_i2s(audio_record_probe_context_t *ctx)
{
#if !SOC_I2S_SUPPORTS_PDM_RX
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
#else
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &ctx->rx_chan), TAG, "alloc pdm rx channel failed");

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(AUDIO_RECORD_SAMPLE_RATE_HZ),
#if SOC_I2S_SUPPORTS_PDM2PCM
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
#else
        .slot_cfg = I2S_PDM_RX_SLOT_RAW_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
#endif
        .gpio_cfg = {
            .clk = AUDIO_RECORD_PDM_CLK_GPIO,
            .din = AUDIO_RECORD_PDM_DATA_GPIO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    audio_record_configure_pdm_profile(&pdm_cfg);
    audio_record_log_pdm_profile(&pdm_cfg);
    return i2s_channel_init_pdm_rx_mode(ctx->rx_chan, &pdm_cfg);
#endif
}

static esp_err_t audio_record_enable_i2s(audio_record_probe_context_t *ctx)
{
    if (ctx->rx_enabled) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2s_channel_enable(ctx->rx_chan), TAG, "enable pdm rx failed");
    ctx->rx_enabled = true;
    return ESP_OK;
}

static void audio_record_disable_i2s(audio_record_probe_context_t *ctx)
{
    if (!ctx->rx_enabled) {
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_disable(ctx->rx_chan));
    ctx->rx_enabled = false;
}

static esp_err_t audio_record_alloc_buffers(audio_record_probe_context_t *ctx)
{
    const bool psram_available = esp_psram_is_initialized();
    void *psram_capture = NULL;
    void *internal_capture = NULL;

    if (psram_available) {
        psram_capture = heap_caps_malloc(AUDIO_RECORD_MAX_PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (psram_capture == NULL) {
        internal_capture = heap_caps_malloc(AUDIO_RECORD_MAX_PCM_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    ctx->storage = audio_record_probe_choose_storage(psram_available, psram_capture != NULL, internal_capture != NULL);
    switch (ctx->storage) {
        case AUDIO_RECORD_PROBE_STORAGE_PSRAM:
            ctx->capture_buffer = psram_capture;
            ESP_LOGI(TAG, "capture buffer allocated in PSRAM: %u bytes", (unsigned)AUDIO_RECORD_MAX_PCM_BYTES);
            break;
        case AUDIO_RECORD_PROBE_STORAGE_INTERNAL:
            ctx->capture_buffer = internal_capture;
            if (!psram_available) {
                ESP_LOGW(TAG, "PSRAM not available, falling back to internal RAM for capture buffer");
            } else {
                ESP_LOGW(TAG, "PSRAM capture alloc failed, falling back to internal RAM");
            }
            break;
        case AUDIO_RECORD_PROBE_STORAGE_NONE:
        default:
            ESP_LOGE(TAG, "failed to allocate capture buffer in PSRAM or internal RAM");
            return ESP_ERR_NO_MEM;
    }

    ctx->read_buffer = heap_caps_malloc(AUDIO_RECORD_READ_SAMPLES * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ctx->read_buffer == NULL) {
        ESP_LOGE(TAG, "failed to allocate read buffer");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t audio_record_uart_write_all(const void *data, size_t size)
{
    const uint8_t *cursor = data;
    for (size_t i = 0; i < size; ++i) {
        if (esp_rom_output_tx_one_char(cursor[i]) != 0) {
            return ESP_FAIL;
        }
    }
    esp_rom_output_tx_wait_idle(CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM);
    return ESP_OK;
}

static esp_err_t audio_record_write_text_line(const char *prefix, const char *message)
{
    char line[160];
    const int written = snprintf(line, sizeof(line), "%s%s\r\n", prefix, message);
    if (written < 0 || (size_t)written >= sizeof(line)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return audio_record_uart_write_all(line, (size_t)written);
}

static void audio_record_emit_event(const char *message)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(audio_record_write_text_line(AUDIO_RECORD_EVENT_PREFIX, message));
}

static void audio_record_emit_progress(audio_record_probe_context_t *ctx)
{
    char message[128];
    const int written = snprintf(
        message,
        sizeof(message),
        "recording elapsed_ms=%u remaining_ms=%u bytes=%u",
        (unsigned)ctx->session.capture_ms,
        (unsigned)audio_record_probe_remaining_ms(&ctx->session),
        (unsigned)ctx->session.captured_bytes);
    if (written < 0 || (size_t)written >= sizeof(message)) {
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(audio_record_write_text_line(AUDIO_RECORD_STATUS_PREFIX, message));
}

static esp_err_t audio_record_export_capture(audio_record_probe_context_t *ctx)
{
    const audio_record_wifi_config_t *wifi_config = audio_record_wifi_get_config();
    ESP_RETURN_ON_FALSE(audio_record_wifi_config_valid(wifi_config), ESP_ERR_INVALID_STATE, TAG, "wifi config missing");

    const size_t wav_size = audio_record_wav_total_size((uint32_t)ctx->session.captured_bytes);
    ESP_RETURN_ON_FALSE(wav_size >= AUDIO_RECORD_WAV_HEADER_SIZE, ESP_ERR_INVALID_SIZE, TAG, "invalid wav size");

    uint8_t *wav_buffer = heap_caps_malloc(wav_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (wav_buffer == NULL) {
        wav_buffer = heap_caps_malloc(wav_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    ESP_RETURN_ON_FALSE(wav_buffer != NULL, ESP_ERR_NO_MEM, TAG, "alloc wav buffer");

    esp_err_t ret = ESP_OK;
    if (!audio_record_wav_write_header(
            wav_buffer,
            wav_size,
            AUDIO_RECORD_SAMPLE_RATE_HZ,
            AUDIO_RECORD_BITS_PER_SAMPLE,
            AUDIO_RECORD_CHANNELS,
            (uint32_t)ctx->session.captured_bytes)) {
        free(wav_buffer);
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->session.captured_bytes > 0U) {
        memcpy(wav_buffer + AUDIO_RECORD_WAV_HEADER_SIZE, ctx->capture_buffer, ctx->session.captured_bytes);
    }

    ESP_LOGI(TAG, "connecting wifi for upload: ssid=%s server=%s", wifi_config->ssid, wifi_config->server_base_url);
    ret = audio_record_wifi_connect(wifi_config, AUDIO_RECORD_WIFI_CONNECT_TIMEOUT_MS);
    if (ret == ESP_OK) {
        int http_status = 0;
        ESP_LOGI(TAG, "uploading wav bytes=%u total_wav_bytes=%u", (unsigned)ctx->session.captured_bytes, (unsigned)wav_size);
        ret = audio_record_http_upload_wav(
            wifi_config->server_base_url,
            wav_buffer,
            wav_size,
            ctx->session.sequence,
            ctx->session.capture_ms,
            &http_status);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "http upload failed status=%d err=%s", http_status, esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "http upload success status=%d sequence=%u", http_status, (unsigned)ctx->session.sequence);
        }
    }

    esp_err_t quiet_ret = audio_record_wifi_quiet();
    if (quiet_ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to quiet wifi after upload: %s", esp_err_to_name(quiet_ret));
    }

    free(wav_buffer);
    return ret;
}

static esp_err_t audio_record_capture_loop(audio_record_probe_context_t *ctx)
{
    ESP_RETURN_ON_ERROR(audio_record_enable_i2s(ctx), TAG, "failed to enable recording");

    memset(ctx->capture_buffer, 0, AUDIO_RECORD_MAX_PCM_BYTES);
    audio_record_prime_i2s(ctx);
    ctx->last_progress_report_ms = 0U;
    ESP_LOGI(TAG, "recording started: hold Confirm to continue, release to stop");
    audio_record_emit_event("pressed");

    while (ctx->session.state == AUDIO_RECORD_PROBE_STATE_RECORDING) {
        size_t bytes_read = 0U;
        esp_err_t ret = i2s_channel_read(
            ctx->rx_chan,
            ctx->read_buffer,
            AUDIO_RECORD_READ_SAMPLES * sizeof(int16_t),
            &bytes_read,
            pdMS_TO_TICKS(AUDIO_RECORD_READ_TIMEOUT_MS));
        if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
            audio_record_disable_i2s(ctx);
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

        const bool button_pressed = audio_record_confirm_pressed();
        audio_record_probe_append_bytes(
            &ctx->session,
            bytes_to_copy,
            button_pressed,
            audio_record_now_ms());

        if (ctx->session.state == AUDIO_RECORD_PROBE_STATE_RECORDING
            && audio_record_probe_progress_due(
                ctx->session.capture_ms,
                &ctx->last_progress_report_ms,
                AUDIO_RECORD_PROGRESS_REPORT_MS)) {
            audio_record_emit_progress(ctx);
        }

        if (!button_pressed && ctx->session.state != AUDIO_RECORD_PROBE_STATE_RECORDING) {
            audio_record_emit_event("released");
        }
    }

    audio_record_disable_i2s(ctx);
    return ESP_OK;
}

static void audio_record_run(audio_record_probe_context_t *ctx)
{
    for (;;) {
        if (ctx->session.state == AUDIO_RECORD_PROBE_STATE_WAIT_FOR_RELEASE) {
            audio_record_probe_poll_release(&ctx->session, audio_record_confirm_pressed());
            vTaskDelay(pdMS_TO_TICKS(AUDIO_RECORD_IDLE_POLL_MS));
            continue;
        }

        if (ctx->session.state == AUDIO_RECORD_PROBE_STATE_IDLE) {
            if (!audio_record_confirm_pressed()) {
                ctx->last_button_pressed = false;
                vTaskDelay(pdMS_TO_TICKS(AUDIO_RECORD_IDLE_POLL_MS));
                continue;
            }

            if (!audio_record_probe_try_start(&ctx->session, audio_record_now_ms())) {
                vTaskDelay(pdMS_TO_TICKS(AUDIO_RECORD_IDLE_POLL_MS));
                continue;
            }

            if (audio_record_capture_loop(ctx) != ESP_OK) {
                ctx->session.state = AUDIO_RECORD_PROBE_STATE_WAIT_FOR_RELEASE;
                continue;
            }
        }

        if (ctx->session.state == AUDIO_RECORD_PROBE_STATE_EXPORT_PENDING) {
            const uint32_t duration_ms = audio_record_probe_pcm_duration_ms(
                ctx->session.captured_bytes,
                AUDIO_RECORD_SAMPLE_RATE_HZ,
                AUDIO_RECORD_BITS_PER_SAMPLE,
                AUDIO_RECORD_CHANNELS);
            char exporting_message[128];
            const int exporting_written = snprintf(
                exporting_message,
                sizeof(exporting_message),
                "uploading bytes=%u duration_ms=%u",
                (unsigned)ctx->session.captured_bytes,
                (unsigned)duration_ms);
            if (exporting_written > 0 && (size_t)exporting_written < sizeof(exporting_message)) {
                audio_record_write_text_line(AUDIO_RECORD_STATUS_PREFIX, exporting_message);
            }
            ESP_LOGI(
                TAG,
                "recording stopped: bytes=%u duration_ms=%u stop_due_to_limit=%s",
                (unsigned)ctx->session.captured_bytes,
                (unsigned)duration_ms,
                ctx->session.stop_due_to_limit ? "true" : "false");
            audio_record_log_capture_metrics(ctx);
            esp_err_t ret = audio_record_export_capture(ctx);
            if (ret != ESP_OK) {
                char failed_message[128];
                int failed_written = snprintf(
                    failed_message,
                    sizeof(failed_message),
                    "upload_failed sequence=%u err=%s",
                    (unsigned)ctx->session.sequence,
                    esp_err_to_name(ret));
                if (failed_written > 0 && (size_t)failed_written < sizeof(failed_message)) {
                    audio_record_write_text_line(AUDIO_RECORD_STATUS_PREFIX, failed_message);
                }
                ESP_LOGE(TAG, "audio upload failed: %s", esp_err_to_name(ret));
            } else {
                char exported_message[128];
                const int exported_written = snprintf(
                    exported_message,
                    sizeof(exported_message),
                    "upload_done sequence=%u bytes=%u duration_ms=%u",
                    (unsigned)ctx->session.sequence,
                    (unsigned)ctx->session.captured_bytes,
                    (unsigned)duration_ms);
                if (exported_written > 0 && (size_t)exported_written < sizeof(exported_message)) {
                    audio_record_write_text_line(AUDIO_RECORD_STATUS_PREFIX, exported_message);
                }
                ESP_LOGI(
                    TAG,
                    "audio upload complete: sequence=%u bytes=%u duration_ms=%u",
                    (unsigned)ctx->session.sequence,
                    (unsigned)ctx->session.captured_bytes,
                    (unsigned)duration_ms);
            }
            audio_record_probe_mark_exported(&ctx->session, audio_record_confirm_pressed());
        }
    }
}

void app_main(void)
{
    audio_record_probe_context_t ctx = {0};

    ESP_LOGI(TAG, "audio record probe boot");
    ESP_LOGI(TAG, "Confirm GPIO=%d active_low, mic PDM CLK=%d DATA=%d", AUDIO_RECORD_CONFIRM_GPIO, AUDIO_RECORD_PDM_CLK_GPIO, AUDIO_RECORD_PDM_DATA_GPIO);
    ESP_LOGI(TAG, "format=%u Hz %u-bit mono max_ms=%u", AUDIO_RECORD_SAMPLE_RATE_HZ, AUDIO_RECORD_BITS_PER_SAMPLE, AUDIO_RECORD_MAX_CAPTURE_MS);
    const audio_record_wifi_config_t *wifi_config = audio_record_wifi_get_config();
    ESP_ERROR_CHECK(audio_record_probe_logic_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_record_metrics_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_record_wav_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_record_wifi_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio_record_http_upload_self_test() ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "self-tests passed");
    ESP_LOGI(
        TAG,
        "wifi config present=%s server=%s ssid=%s",
        audio_record_wifi_config_valid(wifi_config) ? "true" : "false",
        wifi_config->server_base_url != NULL ? wifi_config->server_base_url : "",
        wifi_config->ssid != NULL ? wifi_config->ssid : "");

    audio_record_probe_session_init(&ctx.session, AUDIO_RECORD_MAX_CAPTURE_MS, AUDIO_RECORD_MAX_PCM_BYTES);
    ESP_ERROR_CHECK(audio_record_init_button());
    ESP_ERROR_CHECK(audio_record_alloc_buffers(&ctx));
    ESP_ERROR_CHECK(audio_record_init_i2s(&ctx));

    ESP_LOGI(TAG, "ready: hold Confirm to record, release to upload WAV over Wi-Fi HTTP");
    audio_record_run(&ctx);
}
