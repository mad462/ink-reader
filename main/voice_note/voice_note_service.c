#include "voice_note/voice_note_service.h"

#include <stddef.h>
#include <string.h>

#include "esp_err.h"

static voice_note_service_snapshot_t s_snapshot;

static void voice_note_copy_text(char *dst, size_t dst_size, const char *src);

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

esp_err_t voice_note_service_init(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = VOICE_NOTE_JOB_IDLE;
    voice_note_copy_text(
        s_snapshot.status_text,
        sizeof(s_snapshot.status_text),
        "按住 Confirm 开始录音");
    return ESP_OK;
}

bool voice_note_service_start_capture(uint32_t now_ms)
{
    if (s_snapshot.busy) {
        return false;
    }

    s_snapshot.busy = true;
    s_snapshot.state = VOICE_NOTE_JOB_RECORDING;
    s_snapshot.started_ms = now_ms;
    s_snapshot.capture_duration_ms = 0U;
    s_snapshot.stop_due_to_limit = false;
    s_snapshot.pcm_bytes = 0U;
    s_snapshot.wav_bytes = 0U;
    voice_note_copy_text(
        s_snapshot.status_text,
        sizeof(s_snapshot.status_text),
        "正在录音");
    return true;
}

bool voice_note_service_stop_capture(uint32_t now_ms)
{
    if (!s_snapshot.busy || s_snapshot.state != VOICE_NOTE_JOB_RECORDING) {
        return false;
    }

    s_snapshot.capture_duration_ms = now_ms - s_snapshot.started_ms;
    s_snapshot.state = VOICE_NOTE_JOB_COMPLETED;
    s_snapshot.busy = false;
    voice_note_copy_text(
        s_snapshot.status_text,
        sizeof(s_snapshot.status_text),
        "识别完成");
    return true;
}

bool voice_note_service_retry_note(const char *note_id, uint32_t now_ms)
{
    (void)note_id;
    (void)now_ms;
    return false;
}

bool voice_note_service_delete_note(const char *note_id)
{
    (void)note_id;
    return false;
}

bool voice_note_service_set_note_status(const char *note_id, voice_note_status_t status)
{
    (void)note_id;
    (void)status;
    return false;
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
    (void)tab;
    (void)out_notes;
    (void)capacity;
    if (count_out != NULL) {
        *count_out = 0U;
    }
    return true;
}

bool voice_note_service_load_note(const char *note_id, voice_note_note_t *out_note)
{
    (void)note_id;
    (void)out_note;
    return false;
}

bool voice_note_service_tick(uint32_t now_ms)
{
    (void)now_ms;
    return false;
}

bool voice_note_service_self_test(void)
{
    voice_note_service_snapshot_t snapshot;
    voice_note_note_t notes[1];
    size_t count = 1U;

    if (voice_note_service_init() != ESP_OK
        || !voice_note_service_get_snapshot(&snapshot)
        || !voice_note_service_copy_note_summaries(
            VOICE_NOTE_TAB_ALL,
            notes,
            1U,
            &count)) {
        return false;
    }

    return snapshot.state == VOICE_NOTE_JOB_IDLE
        && !snapshot.busy
        && count == 0U
        && strcmp(snapshot.status_text, "按住 Confirm 开始录音") == 0;
}
