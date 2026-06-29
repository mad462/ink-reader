#include "audio_asr_preview_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

typedef struct {
    audio_asr_preview_snapshot_t snapshot;
    uint8_t *owned_wav;
    size_t owned_wav_size;
} audio_asr_preview_state_t;

static audio_asr_preview_state_t s_preview_state;

static void audio_asr_preview_state_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    snprintf(dst, dst_size, "%s", src != NULL ? src : "");
}

static void audio_asr_preview_state_reset_snapshot(audio_asr_preview_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->status = AUDIO_ASR_WEB_STATUS_IDLE;
}

void audio_asr_preview_state_init(void)
{
    audio_asr_preview_state_clear_audio();
    audio_asr_preview_state_reset_snapshot(&s_preview_state.snapshot);
}

void audio_asr_preview_state_set_status(audio_asr_web_status_t status)
{
    s_preview_state.snapshot.status = status;
}

void audio_asr_preview_state_set_recording(uint32_t sequence)
{
    s_preview_state.snapshot.sequence = sequence;
    s_preview_state.snapshot.status = AUDIO_ASR_WEB_STATUS_RECORDING;
}

void audio_asr_preview_state_set_result_text(const char *text)
{
    audio_asr_preview_state_copy_text(
        s_preview_state.snapshot.transcript_text,
        sizeof(s_preview_state.snapshot.transcript_text),
        text);
}

void audio_asr_preview_state_set_error_text(const char *text)
{
    audio_asr_preview_state_copy_text(
        s_preview_state.snapshot.error_text,
        sizeof(s_preview_state.snapshot.error_text),
        text);
}

void audio_asr_preview_state_set_metrics(
    uint32_t sequence,
    uint32_t duration_ms,
    uint32_t pcm_bytes,
    uint32_t wav_bytes,
    uint32_t wifi_elapsed_ms,
    uint32_t request_elapsed_ms)
{
    s_preview_state.snapshot.sequence = sequence;
    s_preview_state.snapshot.duration_ms = duration_ms;
    s_preview_state.snapshot.pcm_bytes = pcm_bytes;
    s_preview_state.snapshot.wav_bytes = wav_bytes;
    s_preview_state.snapshot.wifi_elapsed_ms = wifi_elapsed_ms;
    s_preview_state.snapshot.request_elapsed_ms = request_elapsed_ms;
}

esp_err_t audio_asr_preview_state_store_wav_copy(
    const uint8_t *wav_data,
    size_t wav_size,
    bool prefer_psram)
{
    uint8_t *new_wav = NULL;

    if (wav_data == NULL || wav_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (prefer_psram) {
        new_wav = heap_caps_malloc(wav_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (new_wav == NULL) {
        new_wav = heap_caps_malloc(wav_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (new_wav == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(new_wav, wav_data, wav_size);
    free(s_preview_state.owned_wav);
    s_preview_state.owned_wav = new_wav;
    s_preview_state.owned_wav_size = wav_size;
    s_preview_state.snapshot.wav_data = s_preview_state.owned_wav;
    s_preview_state.snapshot.wav_bytes = (uint32_t)wav_size;
    s_preview_state.snapshot.has_audio = true;
    return ESP_OK;
}

void audio_asr_preview_state_clear_audio(void)
{
    free(s_preview_state.owned_wav);
    s_preview_state.owned_wav = NULL;
    s_preview_state.owned_wav_size = 0U;
    s_preview_state.snapshot.wav_data = NULL;
    s_preview_state.snapshot.wav_bytes = 0U;
    s_preview_state.snapshot.has_audio = false;
}

void audio_asr_preview_state_get_snapshot(audio_asr_preview_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    *snapshot = s_preview_state.snapshot;
}

const char *audio_asr_preview_state_status_name(audio_asr_web_status_t status)
{
    switch (status) {
        case AUDIO_ASR_WEB_STATUS_IDLE:
            return "idle";
        case AUDIO_ASR_WEB_STATUS_RECORDING:
            return "recording";
        case AUDIO_ASR_WEB_STATUS_WIFI_CONNECTING:
            return "wifi_connecting";
        case AUDIO_ASR_WEB_STATUS_ASR_REQUESTING:
            return "asr_requesting";
        case AUDIO_ASR_WEB_STATUS_DONE:
            return "done";
        case AUDIO_ASR_WEB_STATUS_ERROR:
            return "error";
        default:
            return "unknown";
    }
}

bool audio_asr_preview_state_self_test(void)
{
    static const uint8_t fake_wav[] = {'R', 'I', 'F', 'F'};
    audio_asr_preview_snapshot_t snapshot;

    audio_asr_preview_state_init();
    audio_asr_preview_state_get_snapshot(&snapshot);
    if (snapshot.status != AUDIO_ASR_WEB_STATUS_IDLE || snapshot.has_audio) {
        return false;
    }

    audio_asr_preview_state_set_recording(3U);
    audio_asr_preview_state_set_metrics(3U, 1200U, 32000U, 32044U, 111U, 222U);
    audio_asr_preview_state_set_result_text("hello");
    audio_asr_preview_state_set_error_text("none");
    if (audio_asr_preview_state_store_wav_copy(fake_wav, sizeof(fake_wav), false) != ESP_OK) {
        return false;
    }

    audio_asr_preview_state_get_snapshot(&snapshot);
    if (snapshot.status != AUDIO_ASR_WEB_STATUS_RECORDING
        || snapshot.sequence != 3U
        || !snapshot.has_audio
        || snapshot.wav_bytes != sizeof(fake_wav)
        || strcmp(snapshot.transcript_text, "hello") != 0
        || strcmp(snapshot.error_text, "none") != 0) {
        return false;
    }

    if (strcmp(audio_asr_preview_state_status_name(AUDIO_ASR_WEB_STATUS_ASR_REQUESTING), "asr_requesting") != 0) {
        return false;
    }

    audio_asr_preview_state_clear_audio();
    audio_asr_preview_state_get_snapshot(&snapshot);
    if (snapshot.has_audio || snapshot.wav_data != NULL || snapshot.wav_bytes != 0U) {
        return false;
    }

    audio_asr_preview_state_init();
    return true;
}
