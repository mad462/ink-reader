#include "voice_note/voice_note_service.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ink_wifi_coordinator.h"

#include "voice_note/voice_note_asr.h"
#include "voice_note/voice_note_audio.h"
#include "voice_note/voice_note_capture_logic.h"
#include "voice_note/voice_note_model.h"
#include "voice_note/voice_note_store.h"

static const char *TAG = "voice_note_service";

enum {
    VOICE_NOTE_PDM_CLK_GPIO = GPIO_NUM_17,
    VOICE_NOTE_PDM_DATA_GPIO = GPIO_NUM_18,
    VOICE_NOTE_READ_SAMPLES = 1024U,
    VOICE_NOTE_READ_TIMEOUT_MS = 20U,
    VOICE_NOTE_PRIME_READS = 4U,
    VOICE_NOTE_LOW_SIGNAL_PEAK_THRESHOLD = 1200,
    VOICE_NOTE_TARGET_PEAK_ABS = 24000,
    VOICE_NOTE_MAX_DIGITAL_GAIN_X = 32U,
    VOICE_NOTE_PDM_AMPLIFY_NUM = 2U,
    VOICE_NOTE_CAPTURE_TASK_STACK_BYTES = 4096U,
    VOICE_NOTE_CAPTURE_TASK_PRIORITY = 6U,
};

static voice_note_service_snapshot_t s_snapshot;
static voice_note_capture_session_t s_capture;
static voice_note_note_t s_active_note;
static i2s_chan_handle_t s_rx_chan;
static int16_t *s_read_buffer;
static uint8_t *s_pcm_buffer;
static uint8_t *s_wav_buffer;
static size_t s_pcm_capacity;
static size_t s_pcm_used;
static size_t s_wav_capacity;
static bool s_rx_enabled;
static bool s_i2s_ready;
static TaskHandle_t s_capture_task_handle;
static portMUX_TYPE s_capture_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_capture_stop_requested;
static volatile bool s_capture_finalize_pending;
static volatile bool s_capture_error_pending;
static uint32_t s_last_recording_ui_second;

static void voice_note_copy_text(char *dst, size_t dst_size, const char *src);
static void voice_note_set_status_from_job(voice_note_job_state_t state);
static void voice_note_reset_to_idle(void);
static void *voice_note_alloc_audio_buffer(size_t size);
static esp_err_t voice_note_ensure_audio_buffers(void);
static esp_err_t voice_note_ensure_i2s_ready(void);
static esp_err_t voice_note_ensure_capture_task(void);
static esp_err_t voice_note_enable_capture_io(void);
static void voice_note_disable_capture_io(void);
static void voice_note_prime_capture_io(void);
static void voice_note_capture_task(void *arg);
static esp_err_t voice_note_write_file(const char *path, const uint8_t *data, size_t length);
static esp_err_t voice_note_prepare_processing_note(uint32_t now_ms);
static void voice_note_finalize_capture(uint32_t now_ms);
static uint32_t voice_note_resolve_capture_duration_ms(size_t pcm_bytes, uint32_t started_ms, uint32_t now_ms);
static int32_t voice_note_compute_peak_abs(const int16_t *samples, size_t sample_count);
static uint32_t voice_note_apply_digital_gain(int16_t *samples, size_t sample_count);
static bool voice_note_fail_transient(const char *text, uint32_t now_ms);
static bool voice_note_wifi_result_is_retryable(ink_wifi_coordinator_result_t result);
static bool voice_note_ensure_wifi_for_asr(void);
static void voice_note_release_wifi_for_asr(void);
static bool voice_note_mark_failed_note(const char *error_text, uint32_t now_epoch_s);
static bool voice_note_commit_active_note(uint32_t now_epoch_s);
static const char *voice_note_status_text_for_asr_result(voice_note_asr_result_t result);
static bool voice_note_take_capture_finalize_pending(void);
static bool voice_note_take_capture_error_pending(void);
static bool voice_note_recording_finalize_pending_self_test(void);
static void voice_note_release_i2s(void);

static void voice_note_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static void voice_note_set_status_from_job(voice_note_job_state_t state)
{
    s_snapshot.state = state;
    if (!voice_note_model_status_copy_for_job(
            state,
            s_snapshot.status_text,
            sizeof(s_snapshot.status_text))) {
        voice_note_copy_text(
            s_snapshot.status_text,
            sizeof(s_snapshot.status_text),
            "按住 Confirm 开始录音");
    }
}

static void voice_note_reset_to_idle(void)
{
    s_snapshot.busy = false;
    s_snapshot.stop_due_to_limit = false;
    s_snapshot.started_ms = 0U;
    s_snapshot.capture_duration_ms = 0U;
    s_snapshot.transient_until_ms = 0U;
    s_snapshot.pcm_bytes = 0U;
    s_snapshot.wav_bytes = 0U;
    s_pcm_used = 0U;
    s_capture_stop_requested = false;
    s_capture_finalize_pending = false;
    s_capture_error_pending = false;
    memset(&s_active_note, 0, sizeof(s_active_note));
    memset(s_snapshot.active_note_id, 0, sizeof(s_snapshot.active_note_id));
    voice_note_set_status_from_job(VOICE_NOTE_JOB_IDLE);
}

static void *voice_note_alloc_audio_buffer(size_t size)
{
    void *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buffer;
}

static esp_err_t voice_note_ensure_audio_buffers(void)
{
    if (s_pcm_buffer != NULL && s_wav_buffer != NULL && s_read_buffer != NULL) {
        return ESP_OK;
    }

    s_pcm_capacity =
        (VOICE_NOTE_SAMPLE_RATE
            * VOICE_NOTE_CHANNELS
            * (VOICE_NOTE_BITS_PER_SAMPLE / 8U)
            * VOICE_NOTE_MAX_CAPTURE_MS)
        / 1000U;
    s_wav_capacity = voice_note_audio_wav_size(s_pcm_capacity);

    if (s_pcm_buffer == NULL) {
        s_pcm_buffer = voice_note_alloc_audio_buffer(s_pcm_capacity);
    }
    if (s_wav_buffer == NULL) {
        s_wav_buffer = voice_note_alloc_audio_buffer(s_wav_capacity);
    }
    if (s_read_buffer == NULL) {
        s_read_buffer = heap_caps_malloc(
            VOICE_NOTE_READ_SAMPLES * sizeof(int16_t),
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_pcm_buffer == NULL || s_wav_buffer == NULL || s_read_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t voice_note_ensure_i2s_ready(void)
{
#if !SOC_I2S_SUPPORTS_PDM_RX
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_i2s_ready) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 3;
    chan_cfg.dma_frame_num = 96;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan), TAG, "alloc pdm rx channel failed");

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(VOICE_NOTE_SAMPLE_RATE),
#if SOC_I2S_SUPPORTS_PDM2PCM
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
#else
        .slot_cfg = I2S_PDM_RX_SLOT_RAW_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
#endif
        .gpio_cfg = {
            .clk = VOICE_NOTE_PDM_CLK_GPIO,
            .din = VOICE_NOTE_PDM_DATA_GPIO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    pdm_cfg.clk_cfg.dn_sample_mode = I2S_PDM_DSR_16S;
    pdm_cfg.clk_cfg.bclk_div = 8;
    pdm_cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_RIGHT;
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    pdm_cfg.slot_cfg.hp_en = true;
    pdm_cfg.slot_cfg.hp_cut_off_freq_hz = 35.5f;
    pdm_cfg.slot_cfg.amplify_num = VOICE_NOTE_PDM_AMPLIFY_NUM;
#endif

    if (i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm_cfg) != ESP_OK) {
        voice_note_release_i2s();
        ESP_RETURN_ON_ERROR(ESP_FAIL, TAG, "init pdm rx failed");
    }
    s_i2s_ready = true;
    return ESP_OK;
#endif
}

static void voice_note_release_i2s(void)
{
    if (s_rx_enabled && s_rx_chan != NULL) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_disable(s_rx_chan));
        s_rx_enabled = false;
    }
    if (s_rx_chan != NULL) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_del_channel(s_rx_chan));
        s_rx_chan = NULL;
    }
    s_i2s_ready = false;
}

static esp_err_t voice_note_ensure_capture_task(void)
{
    if (s_capture_task_handle != NULL) {
        return ESP_OK;
    }

    if (xTaskCreate(
            voice_note_capture_task,
            "VoiceNoteCap",
            VOICE_NOTE_CAPTURE_TASK_STACK_BYTES,
            NULL,
            VOICE_NOTE_CAPTURE_TASK_PRIORITY,
            &s_capture_task_handle)
        != pdPASS) {
        s_capture_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t voice_note_enable_capture_io(void)
{
    if (s_rx_enabled) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(voice_note_ensure_i2s_ready(), TAG, "ensure pdm rx failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "enable pdm rx failed");
    s_rx_enabled = true;
    return ESP_OK;
}

static void voice_note_disable_capture_io(void)
{
    if (!s_rx_enabled) {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_disable(s_rx_chan));
    s_rx_enabled = false;
}

static void voice_note_prime_capture_io(void)
{
    if (!s_rx_enabled || s_read_buffer == NULL) {
        return;
    }

    for (uint32_t i = 0U; i < VOICE_NOTE_PRIME_READS; ++i) {
        size_t bytes_read = 0U;
        esp_err_t ret = i2s_channel_read(
            s_rx_chan,
            s_read_buffer,
            VOICE_NOTE_READ_SAMPLES * sizeof(int16_t),
            &bytes_read,
            pdMS_TO_TICKS(VOICE_NOTE_READ_TIMEOUT_MS));
        if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "prime read failed: %s", esp_err_to_name(ret));
            break;
        }
    }
}

static void voice_note_capture_task(void *arg)
{
    (void)arg;

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        for (;;) {
            bool should_capture = false;
            bool stop_requested = false;
            size_t bytes_read = 0U;
            esp_err_t ret = ESP_OK;
            const size_t request_bytes = VOICE_NOTE_READ_SAMPLES * sizeof(int16_t);
            uint32_t now_ms = 0U;

            portENTER_CRITICAL(&s_capture_lock);
            should_capture = s_snapshot.state == VOICE_NOTE_JOB_RECORDING
                && s_rx_enabled
                && s_pcm_buffer != NULL
                && s_read_buffer != NULL
                && s_capture.state == VOICE_NOTE_CAPTURE_RECORDING;
            stop_requested = s_capture_stop_requested;
            portEXIT_CRITICAL(&s_capture_lock);

            if (!should_capture) {
                break;
            }

            if (stop_requested) {
                now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
                portENTER_CRITICAL(&s_capture_lock);
                if (s_capture.state == VOICE_NOTE_CAPTURE_RECORDING) {
                    voice_note_capture_append_bytes(&s_capture, 0U, false, now_ms);
                    s_capture_finalize_pending = true;
                }
                portEXIT_CRITICAL(&s_capture_lock);
                voice_note_disable_capture_io();
                break;
            }

            ret = i2s_channel_read(
                s_rx_chan,
                s_read_buffer,
                request_bytes,
                &bytes_read,
                pdMS_TO_TICKS(VOICE_NOTE_READ_TIMEOUT_MS));
            now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());

            if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
                voice_note_disable_capture_io();
                portENTER_CRITICAL(&s_capture_lock);
                s_capture_error_pending = true;
                s_capture_finalize_pending = false;
                portEXIT_CRITICAL(&s_capture_lock);
                break;
            }

            portENTER_CRITICAL(&s_capture_lock);
            if (s_capture.state == VOICE_NOTE_CAPTURE_RECORDING) {
                if (bytes_read > 0U) {
                    size_t writable = bytes_read;
                    const size_t available = s_pcm_capacity > s_capture.captured_bytes
                        ? s_pcm_capacity - s_capture.captured_bytes
                        : 0U;
                    if (writable > available) {
                        writable = available;
                    }
                    if (writable > 0U) {
                        memcpy(s_pcm_buffer + s_capture.captured_bytes, s_read_buffer, writable);
                    }
                    voice_note_capture_append_bytes(&s_capture, writable, true, now_ms);
                } else {
                    voice_note_capture_append_bytes(&s_capture, 0U, true, now_ms);
                }

                if (s_capture.state != VOICE_NOTE_CAPTURE_RECORDING) {
                    s_capture_finalize_pending = true;
                }
            }
            portEXIT_CRITICAL(&s_capture_lock);

            if (s_capture_finalize_pending) {
                voice_note_disable_capture_io();
                break;
            }
        }
    }
}

static esp_err_t voice_note_write_file(const char *path, const uint8_t *data, size_t length)
{
    FILE *fp = NULL;
    size_t written = 0U;
    int close_result = 0;

    if (path == NULL || data == NULL || length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return ESP_FAIL;
    }
    written = fwrite(data, 1U, length, fp);
    close_result = fclose(fp);
    if (written != length || close_result != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t voice_note_prepare_processing_note(uint32_t now_ms)
{
    voice_note_note_t note;
    char json_path[VOICE_NOTE_PATH_LENGTH];
    time_t current_time = time(NULL);
    uint32_t created_at_epoch_s = 0U;

    memset(&note, 0, sizeof(note));
    if (!voice_note_model_build_note_paths(
            (uint32_t)((current_time > 0) ? current_time : (time_t)(now_ms / 1000U)),
            s_snapshot.sequence,
            note.id,
            sizeof(note.id),
            json_path,
            sizeof(json_path),
            note.wav_path,
            sizeof(note.wav_path))) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (current_time > 1735689600) {
        created_at_epoch_s = (uint32_t)current_time;
    }
    note.created_at_epoch_s = created_at_epoch_s;
    note.updated_at_epoch_s = note.created_at_epoch_s;
    note.status = VOICE_NOTE_STATUS_PENDING;
    note.transcript_state = VOICE_NOTE_TRANSCRIPT_PROCESSING;
    note.duration_ms = s_snapshot.capture_duration_ms;
    note.sample_rate = VOICE_NOTE_SAMPLE_RATE;
    note.channels = VOICE_NOTE_CHANNELS;
    note.bits_per_sample = VOICE_NOTE_BITS_PER_SAMPLE;
    voice_note_model_build_title(
        "",
        note.transcript_state,
        note.title,
        sizeof(note.title));

    s_active_note = note;
    voice_note_copy_text(
        s_snapshot.active_note_id,
        sizeof(s_snapshot.active_note_id),
        s_active_note.id);
    return ESP_OK;
}

static int32_t voice_note_compute_peak_abs(const int16_t *samples, size_t sample_count)
{
    int32_t peak_abs = 0;

    if (samples == NULL || sample_count == 0U) {
        return 0;
    }

    for (size_t i = 0U; i < sample_count; ++i) {
        const int32_t value = samples[i];
        const int32_t abs_value = value == INT16_MIN ? 32768 : abs(value);
        if (abs_value > peak_abs) {
            peak_abs = abs_value;
        }
    }

    return peak_abs;
}

static uint32_t voice_note_apply_digital_gain(int16_t *samples, size_t sample_count)
{
    const int32_t peak_abs = voice_note_compute_peak_abs(samples, sample_count);
    uint32_t gain = 1U;

    if (samples == NULL || sample_count == 0U || peak_abs <= 0) {
        return 1U;
    }

    if (peak_abs >= VOICE_NOTE_TARGET_PEAK_ABS) {
        return 1U;
    }

    gain = (uint32_t)((VOICE_NOTE_TARGET_PEAK_ABS + (uint32_t)peak_abs - 1U) / (uint32_t)peak_abs);
    if (gain < 1U) {
        gain = 1U;
    }
    if (gain > VOICE_NOTE_MAX_DIGITAL_GAIN_X) {
        gain = VOICE_NOTE_MAX_DIGITAL_GAIN_X;
    }
    if (gain == 1U) {
        return gain;
    }

    for (size_t i = 0U; i < sample_count; ++i) {
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

static uint32_t voice_note_resolve_capture_duration_ms(size_t pcm_bytes, uint32_t started_ms, uint32_t now_ms)
{
    const uint32_t pcm_duration_ms = voice_note_audio_pcm_duration_ms(pcm_bytes);
    uint32_t wall_duration_ms = 0U;

    if (started_ms != 0U && now_ms >= started_ms) {
        wall_duration_ms = now_ms - started_ms;
    }

    return pcm_duration_ms > wall_duration_ms ? pcm_duration_ms : wall_duration_ms;
}

static void voice_note_finalize_capture(uint32_t now_ms)
{
    uint32_t duration_ms = 0U;
    uint32_t pcm_duration_ms = 0U;
    uint32_t applied_gain = 1U;

    voice_note_disable_capture_io();

    s_pcm_used = s_capture.captured_bytes;
    pcm_duration_ms = voice_note_audio_pcm_duration_ms(s_pcm_used);
    duration_ms = voice_note_resolve_capture_duration_ms(s_pcm_used, s_snapshot.started_ms, now_ms);
    if (duration_ms > pcm_duration_ms + 250U) {
        ESP_LOGW(
            TAG,
            "capture duration mismatch: wall_ms=%lu pcm_ms=%lu bytes=%u",
            (unsigned long)duration_ms,
            (unsigned long)pcm_duration_ms,
            (unsigned)s_pcm_used);
    }

    if (s_pcm_used > 0U) {
        applied_gain = voice_note_apply_digital_gain((int16_t *)s_pcm_buffer, s_pcm_used / sizeof(int16_t));
        (void)applied_gain;
    }

    voice_note_capture_mark_completed(&s_capture, false);
    s_snapshot.capture_duration_ms = duration_ms;
    s_snapshot.pcm_bytes = s_pcm_used;
    s_snapshot.stop_due_to_limit = s_capture.stop_due_to_limit;

    if (voice_note_model_is_short_recording(duration_ms, VOICE_NOTE_MIN_CAPTURE_MS)) {
        s_snapshot.busy = false;
        s_snapshot.transient_until_ms = now_ms + 5000U;
        voice_note_set_status_from_job(VOICE_NOTE_JOB_INVALID_SHORT_RECORDING);
        return;
    }

    voice_note_set_status_from_job(VOICE_NOTE_JOB_PACKAGING);
}

static bool voice_note_fail_transient(const char *text, uint32_t now_ms)
{
    s_snapshot.busy = false;
    s_snapshot.transient_until_ms = now_ms + 5000U;
    voice_note_set_status_from_job(VOICE_NOTE_JOB_FAILED);
    if (text != NULL && text[0] != '\0') {
        voice_note_copy_text(
            s_snapshot.status_text,
            sizeof(s_snapshot.status_text),
            text);
    }
    return true;
}

static bool voice_note_take_capture_finalize_pending(void)
{
    bool pending = false;

    portENTER_CRITICAL(&s_capture_lock);
    pending = s_capture_finalize_pending;
    s_capture_finalize_pending = false;
    portEXIT_CRITICAL(&s_capture_lock);

    return pending;
}

static bool voice_note_take_capture_error_pending(void)
{
    bool pending = false;

    portENTER_CRITICAL(&s_capture_lock);
    pending = s_capture_error_pending;
    s_capture_error_pending = false;
    portEXIT_CRITICAL(&s_capture_lock);

    return pending;
}

static bool voice_note_wifi_result_is_retryable(ink_wifi_coordinator_result_t result)
{
    return result == INK_WIFI_COORDINATOR_RESULT_CONNECT_FAILED
        || result == INK_WIFI_COORDINATOR_RESULT_TIMEOUT
        || result == INK_WIFI_COORDINATOR_RESULT_BUSY_RETRYABLE;
}

static bool voice_note_ensure_wifi_for_asr(void)
{
    ink_wifi_coordinator_request_t request = {
        .type = INK_WIFI_COORDINATOR_REQUEST_ENSURE_CONNECTED,
        .owner = INK_WIFI_COORDINATOR_OWNER_VOICE_NOTE_ASR,
        .keep_alive = true,
        .best_effort_saved = true,
        .timeout_ms = 12000U,
    };

    for (uint32_t attempt = 1U; attempt <= 2U; ++attempt) {
        ink_wifi_coordinator_result_t result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
        esp_err_t err = ink_wifi_coordinator_request(&request, &result);

        if (err == ESP_OK && result == INK_WIFI_COORDINATOR_RESULT_OK) {
            return true;
        }
        if (attempt >= 2U || err != ESP_OK || !voice_note_wifi_result_is_retryable(result)) {
            ESP_LOGW(
                TAG,
                "wifi ensure failed attempt=%lu result=%d err=%s",
                (unsigned long)attempt,
                (int)result,
                esp_err_to_name(err));
            return false;
        }

        ESP_LOGW(
            TAG,
            "wifi ensure retry attempt=%lu result=%d",
            (unsigned long)attempt,
            (int)result);
        vTaskDelay(pdMS_TO_TICKS(250U));
    }

    return false;
}

static void voice_note_release_wifi_for_asr(void)
{
    (void)ink_wifi_coordinator_release_owner(
        INK_WIFI_COORDINATOR_OWNER_VOICE_NOTE_ASR,
        3000U);
}

static bool voice_note_mark_failed_note(const char *error_text, uint32_t now_epoch_s)
{
    if (s_active_note.id[0] == '\0') {
        return false;
    }

    s_active_note.transcript_state = VOICE_NOTE_TRANSCRIPT_FAILED;
    s_active_note.updated_at_epoch_s = now_epoch_s;
    voice_note_copy_text(
        s_active_note.last_error,
        sizeof(s_active_note.last_error),
        error_text != NULL ? error_text : "failed");
    s_active_note.text[0] = '\0';
    voice_note_model_build_title(
        "",
        s_active_note.transcript_state,
        s_active_note.title,
        sizeof(s_active_note.title));
    return voice_note_commit_active_note(now_epoch_s);
}

static bool voice_note_commit_active_note(uint32_t now_epoch_s)
{
    voice_note_note_t existing_note;

    if (s_active_note.id[0] == '\0') {
        return false;
    }

    if (now_epoch_s != 0U) {
        if (s_active_note.created_at_epoch_s == 0U) {
            s_active_note.created_at_epoch_s = now_epoch_s;
        }
        s_active_note.updated_at_epoch_s = now_epoch_s;
    } else if (s_active_note.updated_at_epoch_s == 0U) {
        s_active_note.updated_at_epoch_s = s_active_note.created_at_epoch_s;
    }

    if (voice_note_store_find_note(s_active_note.id, &existing_note)) {
        return voice_note_store_update_note(&s_active_note) == ESP_OK;
    }
    return voice_note_store_create_processing_note(&s_active_note) == ESP_OK;
}

static const char *voice_note_status_text_for_asr_result(voice_note_asr_result_t result)
{
    switch (result) {
        case VOICE_NOTE_ASR_ERROR_CONFIG:
            return "识别配置错误";
        case VOICE_NOTE_ASR_ERROR_HTTP:
            return "识别网络错误";
        case VOICE_NOTE_ASR_ERROR_STATUS:
            return "识别服务返回异常";
        case VOICE_NOTE_ASR_ERROR_JSON:
            return "识别结果解析失败";
        case VOICE_NOTE_ASR_ERROR_RESPONSE_TOO_LARGE:
            return "识别返回过大";
        case VOICE_NOTE_ASR_ERROR_INVALID_ARG:
        default:
            return "识别服务失败";
    }
}

esp_err_t voice_note_service_prepare_storage(void)
{
    uint32_t prior_sequence = s_capture.sequence;

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(&s_active_note, 0, sizeof(s_active_note));
    voice_note_capture_session_init(
        &s_capture,
        VOICE_NOTE_MAX_CAPTURE_MS,
        (VOICE_NOTE_SAMPLE_RATE
            * VOICE_NOTE_CHANNELS
            * (VOICE_NOTE_BITS_PER_SAMPLE / 8U)
            * VOICE_NOTE_MAX_CAPTURE_MS)
            / 1000U);
    s_capture.sequence = prior_sequence;
    voice_note_set_status_from_job(VOICE_NOTE_JOB_IDLE);

    (void)voice_note_store_init();
    (void)voice_note_store_reload();
    s_snapshot.note_count = voice_note_store_count();
    return ESP_OK;
}

esp_err_t voice_note_service_init(void)
{
    ESP_RETURN_ON_ERROR(voice_note_service_prepare_storage(), TAG, "prepare storage failed");
    if (voice_note_ensure_audio_buffers() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    if (voice_note_ensure_capture_task() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    if (voice_note_ensure_i2s_ready() != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool voice_note_service_start_capture(uint32_t now_ms)
{
    if (s_snapshot.busy) {
        return false;
    }
    if (voice_note_ensure_audio_buffers() != ESP_OK) {
        return false;
    }
    if (voice_note_ensure_i2s_ready() != ESP_OK) {
        voice_note_fail_transient("录音硬件不可用", now_ms);
        return false;
    }
    if (s_capture.state != VOICE_NOTE_CAPTURE_IDLE) {
        voice_note_capture_mark_completed(&s_capture, false);
    }
    if (!voice_note_capture_try_start(&s_capture, now_ms)) {
        return false;
    }
    if (voice_note_ensure_capture_task() != ESP_OK) {
        voice_note_capture_mark_completed(&s_capture, false);
        return false;
    }
    if (voice_note_enable_capture_io() != ESP_OK) {
        voice_note_capture_mark_completed(&s_capture, false);
        return false;
    }

    memset(&s_active_note, 0, sizeof(s_active_note));
    memset(s_snapshot.active_note_id, 0, sizeof(s_snapshot.active_note_id));
    s_pcm_used = 0U;
    memset(s_pcm_buffer, 0, s_pcm_capacity);
    voice_note_prime_capture_io();
    s_snapshot.busy = true;
    s_snapshot.sequence = s_capture.sequence;
    s_snapshot.started_ms = now_ms;
    s_snapshot.capture_duration_ms = 0U;
    s_snapshot.transient_until_ms = 0U;
    s_snapshot.stop_due_to_limit = false;
    s_snapshot.pcm_bytes = 0U;
    s_snapshot.wav_bytes = 0U;
    s_last_recording_ui_second = 0U;
    s_capture_stop_requested = false;
    s_capture_finalize_pending = false;
    s_capture_error_pending = false;
    voice_note_set_status_from_job(VOICE_NOTE_JOB_RECORDING);
    xTaskNotifyGive(s_capture_task_handle);
    return true;
}

bool voice_note_service_stop_capture(uint32_t now_ms)
{
    if (!s_snapshot.busy || s_snapshot.state != VOICE_NOTE_JOB_RECORDING) {
        return false;
    }

    (void)now_ms;
    s_capture_stop_requested = true;
    if (s_capture_task_handle != NULL) {
        xTaskNotifyGive(s_capture_task_handle);
    }
    return true;
}

bool voice_note_service_retry_note(const char *note_id, uint32_t now_ms)
{
    FILE *fp = NULL;
    long wav_size = 0L;
    size_t read_size = 0U;

    if (note_id == NULL || s_snapshot.busy) {
        return false;
    }
    if (voice_note_ensure_audio_buffers() != ESP_OK) {
        return false;
    }
    if (!voice_note_store_find_note(note_id, &s_active_note)) {
        return false;
    }

    fp = fopen(s_active_note.wav_path, "rb");
    if (fp == NULL) {
        return false;
    }
    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    wav_size = ftell(fp);
    if (wav_size <= 0L || (size_t)wav_size > s_wav_capacity || fseek(fp, 0L, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }
    read_size = fread(s_wav_buffer, 1U, (size_t)wav_size, fp);
    fclose(fp);
    if (read_size != (size_t)wav_size) {
        return false;
    }

    s_snapshot.busy = true;
    s_snapshot.sequence++;
    s_snapshot.started_ms = now_ms;
    s_snapshot.capture_duration_ms = s_active_note.duration_ms;
    s_snapshot.pcm_bytes = 0U;
    s_snapshot.wav_bytes = (size_t)wav_size;
    s_snapshot.transient_until_ms = 0U;
    voice_note_copy_text(
        s_snapshot.active_note_id,
        sizeof(s_snapshot.active_note_id),
        note_id);
    voice_note_set_status_from_job(VOICE_NOTE_JOB_WIFI_CONNECTING);
    return true;
}

bool voice_note_service_delete_note(const char *note_id)
{
    if (note_id == NULL) {
        return false;
    }
    if (voice_note_store_delete_note(note_id) != ESP_OK) {
        return false;
    }

    s_snapshot.note_count = voice_note_store_count();
    if (strcmp(s_snapshot.active_note_id, note_id) == 0) {
        memset(&s_active_note, 0, sizeof(s_active_note));
        memset(s_snapshot.active_note_id, 0, sizeof(s_snapshot.active_note_id));
    }
    return true;
}

bool voice_note_service_set_note_status(const char *note_id, voice_note_status_t status)
{
    voice_note_note_t note;
    time_t current_time = time(NULL);

    if (note_id == NULL) {
        return false;
    }
    if (!voice_note_store_find_note(note_id, &note)) {
        return false;
    }

    note.status = status;
    if (current_time > 1735689600) {
        note.updated_at_epoch_s = (uint32_t)current_time;
    } else {
        note.updated_at_epoch_s += 1U;
    }
    return voice_note_store_update_note(&note) == ESP_OK;
}

bool voice_note_service_get_snapshot(voice_note_service_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return false;
    }

    *out_snapshot = s_snapshot;
    return true;
}

bool voice_note_service_copy_note_summaries(
    voice_note_tab_t tab,
    voice_note_note_t *out_notes,
    size_t capacity,
    size_t *count_out)
{
    return voice_note_store_copy_summaries(tab, out_notes, capacity, count_out);
}

bool voice_note_service_load_note(const char *note_id, voice_note_note_t *out_note)
{
    return voice_note_store_find_note(note_id, out_note);
}

bool voice_note_service_tick(uint32_t now_ms)
{
    time_t current_time = time(NULL);
    uint32_t now_epoch_s = current_time > 1735689600 ? (uint32_t)current_time : 0U;

    if (s_snapshot.state == VOICE_NOTE_JOB_RECORDING) {
        uint32_t duration_ms = 0U;
        uint32_t duration_second = 0U;
        bool second_changed = false;

        portENTER_CRITICAL(&s_capture_lock);
        duration_ms = voice_note_resolve_capture_duration_ms(
            s_capture.captured_bytes,
            s_snapshot.started_ms,
            now_ms);
        s_snapshot.capture_duration_ms = duration_ms;
        s_snapshot.pcm_bytes = s_capture.captured_bytes;
        portEXIT_CRITICAL(&s_capture_lock);
        duration_second = duration_ms / 1000U;
        second_changed = duration_second != s_last_recording_ui_second;
        if (second_changed) {
            s_last_recording_ui_second = duration_second;
        }
        if (voice_note_take_capture_error_pending()) {
            voice_note_disable_capture_io();
            return voice_note_fail_transient("录音失败", now_ms);
        }
        if (voice_note_take_capture_finalize_pending()) {
            voice_note_finalize_capture(now_ms);
            return true;
        }
        return second_changed;
    }

    if (s_snapshot.state == VOICE_NOTE_JOB_INVALID_SHORT_RECORDING
        && now_ms >= s_snapshot.transient_until_ms) {
        voice_note_reset_to_idle();
        s_snapshot.note_count = voice_note_store_count();
        return true;
    }

    if (s_snapshot.state == VOICE_NOTE_JOB_PACKAGING) {
        voice_note_set_status_from_job(VOICE_NOTE_JOB_PERSISTING_WAV);
        if (voice_note_audio_build_wav(
                s_pcm_buffer,
                s_pcm_used,
                s_wav_buffer,
                s_wav_capacity,
                &s_snapshot.wav_bytes) != ESP_OK) {
            return voice_note_fail_transient("存储失败", now_ms);
        }
        if (voice_note_prepare_processing_note(now_ms) != ESP_OK) {
            return voice_note_fail_transient("存储失败", now_ms);
        }
        if (voice_note_write_file(s_active_note.wav_path, s_wav_buffer, s_snapshot.wav_bytes) != ESP_OK) {
            return voice_note_fail_transient("存储失败", now_ms);
        }
        voice_note_set_status_from_job(VOICE_NOTE_JOB_WIFI_CONNECTING);
        return true;
    }

    if (s_snapshot.state == VOICE_NOTE_JOB_WIFI_CONNECTING) {
        if (!voice_note_ensure_wifi_for_asr()) {
            (void)voice_note_mark_failed_note("wifi_failed", now_epoch_s);
            s_snapshot.note_count = voice_note_store_count();
            s_snapshot.busy = false;
            s_snapshot.transient_until_ms = now_ms + 1500U;
            voice_note_set_status_from_job(VOICE_NOTE_JOB_FAILED);
            voice_note_copy_text(
                s_snapshot.status_text,
                sizeof(s_snapshot.status_text),
                "WiFi 连接失败");
            return true;
        }

        voice_note_set_status_from_job(VOICE_NOTE_JOB_UPLOADING);
        return true;
    }

    if (s_snapshot.state == VOICE_NOTE_JOB_UPLOADING) {
        char transcript[VOICE_NOTE_TEXT_LENGTH];
        voice_note_asr_result_t asr_result;

        voice_note_set_status_from_job(VOICE_NOTE_JOB_RECOGNIZING);
        transcript[0] = '\0';
        asr_result = voice_note_asr_post_wav(
            s_wav_buffer,
            s_snapshot.wav_bytes,
            transcript,
            sizeof(transcript));
        voice_note_release_wifi_for_asr();
        if (asr_result != VOICE_NOTE_ASR_OK) {
            ESP_LOGW(
                TAG,
                "asr failed result=%s wav_bytes=%u active_note=%s",
                voice_note_asr_result_name(asr_result),
                (unsigned)s_snapshot.wav_bytes,
                s_active_note.id);
            (void)voice_note_mark_failed_note("asr_failed", now_epoch_s);
            s_snapshot.note_count = voice_note_store_count();
            s_snapshot.busy = false;
            s_snapshot.transient_until_ms = now_ms + 1500U;
            voice_note_set_status_from_job(VOICE_NOTE_JOB_FAILED);
            voice_note_copy_text(
                s_snapshot.status_text,
                sizeof(s_snapshot.status_text),
                voice_note_status_text_for_asr_result(asr_result));
            return true;
        }

        voice_note_set_status_from_job(VOICE_NOTE_JOB_PERSISTING_RESULT);
        s_active_note.transcript_state = VOICE_NOTE_TRANSCRIPT_READY;
        s_active_note.updated_at_epoch_s = now_epoch_s;
        if (!voice_note_model_normalize_text(
                transcript,
                s_active_note.text,
                sizeof(s_active_note.text))) {
            s_active_note.text[0] = '\0';
        }
        voice_note_copy_text(
            s_active_note.text,
            sizeof(s_active_note.text),
            s_active_note.text);
        voice_note_model_build_title(
            s_active_note.text,
            s_active_note.transcript_state,
            s_active_note.title,
            sizeof(s_active_note.title));
        if (!voice_note_commit_active_note(now_epoch_s)) {
            (void)voice_note_mark_failed_note("store_update_failed", now_epoch_s);
            s_snapshot.note_count = voice_note_store_count();
            s_snapshot.busy = false;
            s_snapshot.transient_until_ms = now_ms + 1500U;
            voice_note_set_status_from_job(VOICE_NOTE_JOB_FAILED);
            voice_note_copy_text(
                s_snapshot.status_text,
                sizeof(s_snapshot.status_text),
                "存储失败");
            return true;
        }

        s_snapshot.note_count = voice_note_store_count();
        s_snapshot.busy = false;
        s_snapshot.transient_until_ms = now_ms + 1500U;
        voice_note_set_status_from_job(VOICE_NOTE_JOB_COMPLETED);
        return true;
    }

    if ((s_snapshot.state == VOICE_NOTE_JOB_COMPLETED
            || s_snapshot.state == VOICE_NOTE_JOB_FAILED)
        && s_snapshot.transient_until_ms != 0U
        && now_ms >= s_snapshot.transient_until_ms) {
        voice_note_reset_to_idle();
        s_snapshot.note_count = voice_note_store_count();
        return true;
    }

    return false;
}

bool voice_note_service_self_test(void)
{
    voice_note_service_snapshot_t snapshot;
    voice_note_note_t notes[1];
    char normalized[VOICE_NOTE_TEXT_LENGTH];
    size_t count = 1U;

    if (voice_note_service_prepare_storage() != ESP_OK
        || !voice_note_service_get_snapshot(&snapshot)
        || !voice_note_service_copy_note_summaries(
            VOICE_NOTE_TAB_ALL,
            notes,
            1U,
            &count)) {
        return false;
    }

    if (!(snapshot.state == VOICE_NOTE_JOB_IDLE
        && !snapshot.busy
        && count == 0U
        && strcmp(snapshot.status_text, "按住 Confirm 开始录音") == 0)) {
        return false;
    }
    if (!voice_note_recording_finalize_pending_self_test()) {
        return false;
    }

    if (!voice_note_service_start_capture(100U)) {
        return false;
    }
    if (!voice_note_service_stop_capture(1500U)) {
        return false;
    }
    if (!voice_note_service_get_snapshot(&snapshot)) {
        return false;
    }
    if (snapshot.state != VOICE_NOTE_JOB_INVALID_SHORT_RECORDING) {
        return false;
    }
    if (strcmp(snapshot.status_text, "无效标签，请重新录入") != 0) {
        return false;
    }
    if (s_capture.captured_bytes < 2048U) {
        return false;
    }
    if (VOICE_NOTE_MAX_CAPTURE_MS != 30000
        || voice_note_resolve_capture_duration_ms(2048U, 100U, 3000U) != 2900U) {
        return false;
    }
    if (!voice_note_wifi_result_is_retryable(INK_WIFI_COORDINATOR_RESULT_CONNECT_FAILED)
        || !voice_note_wifi_result_is_retryable(INK_WIFI_COORDINATOR_RESULT_TIMEOUT)
        || voice_note_wifi_result_is_retryable(INK_WIFI_COORDINATOR_RESULT_NO_CREDENTIAL)) {
        return false;
    }
    if (!voice_note_model_normalize_text("识别\n结果\r\n测试", normalized, sizeof(normalized))
        || strcmp(normalized, "识别 结果 测试") != 0) {
        return false;
    }
    (void)voice_note_service_tick(7000U);
    if (!voice_note_service_get_snapshot(&snapshot)) {
        return false;
    }
    if (snapshot.state != VOICE_NOTE_JOB_IDLE
        || strcmp(snapshot.status_text, "按住 Confirm 开始录音") != 0) {
        return false;
    }

    snprintf(s_active_note.id, sizeof(s_active_note.id), "%s", "retry_target");
    if (voice_note_service_retry_note("retry_target", 8000U)) {
        return false;
    }

    return snapshot.state == VOICE_NOTE_JOB_IDLE
        && strcmp(snapshot.status_text, "按住 Confirm 开始录音") == 0;
}

static bool voice_note_recording_finalize_pending_self_test(void)
{
    voice_note_service_snapshot_t prior_snapshot = s_snapshot;
    voice_note_capture_session_t prior_capture = s_capture;
    size_t prior_pcm_used = s_pcm_used;
    bool prior_stop_requested = s_capture_stop_requested;
    bool prior_finalize_pending = s_capture_finalize_pending;
    bool prior_error_pending = s_capture_error_pending;
    bool dirty = false;
    bool ok = false;

    s_snapshot.busy = true;
    s_snapshot.state = VOICE_NOTE_JOB_RECORDING;
    s_snapshot.started_ms = 100U;
    s_snapshot.transient_until_ms = 0U;
    s_capture.state = VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE;
    s_capture.started_ms = 100U;
    s_capture.captured_bytes = 2048U;
    s_capture.stop_due_to_limit = false;
    s_capture_finalize_pending = true;
    s_capture_error_pending = false;
    s_capture_stop_requested = false;

    dirty = voice_note_service_tick(3000U);
    ok = dirty
        && s_snapshot.state == VOICE_NOTE_JOB_INVALID_SHORT_RECORDING
        && strcmp(s_snapshot.status_text, "无效标签，请重新录入") == 0
        && s_snapshot.capture_duration_ms == 2900U
        && s_snapshot.pcm_bytes == 2048U
        && !s_snapshot.busy;

    s_snapshot = prior_snapshot;
    s_capture = prior_capture;
    s_pcm_used = prior_pcm_used;
    s_capture_stop_requested = prior_stop_requested;
    s_capture_finalize_pending = prior_finalize_pending;
    s_capture_error_pending = prior_error_pending;
    return ok;
}
