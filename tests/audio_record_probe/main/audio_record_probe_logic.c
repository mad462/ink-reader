#include "audio_record_probe_logic.h"

void audio_record_probe_session_init(
    audio_record_probe_session_t *session,
    uint32_t max_capture_ms,
    size_t max_bytes)
{
    if (session == NULL) {
        return;
    }

    session->state = AUDIO_RECORD_PROBE_STATE_IDLE;
    session->sequence = 0U;
    session->started_ms = 0U;
    session->capture_ms = 0U;
    session->max_capture_ms = max_capture_ms;
    session->captured_bytes = 0U;
    session->max_bytes = max_bytes;
    session->stop_due_to_limit = false;
}

bool audio_record_probe_try_start(audio_record_probe_session_t *session, uint32_t now_ms)
{
    if (session == NULL || session->state != AUDIO_RECORD_PROBE_STATE_IDLE) {
        return false;
    }

    session->state = AUDIO_RECORD_PROBE_STATE_RECORDING;
    session->started_ms = now_ms;
    session->capture_ms = 0U;
    session->captured_bytes = 0U;
    session->stop_due_to_limit = false;
    session->sequence += 1U;
    return true;
}

void audio_record_probe_append_bytes(
    audio_record_probe_session_t *session,
    size_t bytes_captured,
    bool button_pressed,
    uint32_t now_ms)
{
    if (session == NULL || session->state != AUDIO_RECORD_PROBE_STATE_RECORDING) {
        return;
    }

    session->captured_bytes += bytes_captured;
    session->capture_ms = now_ms >= session->started_ms ? (now_ms - session->started_ms) : 0U;

    const bool overflowed = session->captured_bytes >= session->max_bytes;
    const bool timeout = session->capture_ms >= session->max_capture_ms;
    if (!button_pressed || overflowed || timeout) {
        session->state = AUDIO_RECORD_PROBE_STATE_EXPORT_PENDING;
        session->stop_due_to_limit = overflowed || timeout;
        if (session->captured_bytes > session->max_bytes) {
            session->captured_bytes = session->max_bytes;
        }
        if (session->capture_ms > session->max_capture_ms) {
            session->capture_ms = session->max_capture_ms;
        }
    }
}

void audio_record_probe_mark_exported(audio_record_probe_session_t *session, bool button_still_pressed)
{
    if (session == NULL || session->state != AUDIO_RECORD_PROBE_STATE_EXPORT_PENDING) {
        return;
    }

    session->state = button_still_pressed ? AUDIO_RECORD_PROBE_STATE_WAIT_FOR_RELEASE : AUDIO_RECORD_PROBE_STATE_IDLE;
}

void audio_record_probe_poll_release(audio_record_probe_session_t *session, bool button_pressed)
{
    if (session == NULL || session->state != AUDIO_RECORD_PROBE_STATE_WAIT_FOR_RELEASE) {
        return;
    }

    if (!button_pressed) {
        session->state = AUDIO_RECORD_PROBE_STATE_IDLE;
    }
}

uint32_t audio_record_probe_pcm_duration_ms(
    size_t pcm_bytes,
    uint32_t sample_rate,
    uint16_t bits_per_sample,
    uint16_t channels)
{
    if (pcm_bytes == 0U || sample_rate == 0U || bits_per_sample == 0U || channels == 0U) {
        return 0U;
    }

    const uint32_t bytes_per_second = sample_rate * (uint32_t)(bits_per_sample / 8U) * (uint32_t)channels;
    if (bytes_per_second == 0U) {
        return 0U;
    }

    return (uint32_t)((pcm_bytes * 1000U) / bytes_per_second);
}

uint32_t audio_record_probe_remaining_ms(const audio_record_probe_session_t *session)
{
    if (session == NULL || session->capture_ms >= session->max_capture_ms) {
        return 0U;
    }
    return session->max_capture_ms - session->capture_ms;
}

bool audio_record_probe_progress_due(
    uint32_t now_ms,
    uint32_t *last_report_ms,
    uint32_t interval_ms)
{
    if (last_report_ms == NULL || interval_ms == 0U) {
        return false;
    }
    if (*last_report_ms == 0U || now_ms - *last_report_ms >= interval_ms) {
        *last_report_ms = now_ms;
        return true;
    }
    return false;
}

audio_record_probe_storage_t audio_record_probe_choose_storage(
    bool psram_available,
    bool psram_alloc_ok,
    bool internal_alloc_ok)
{
    if (psram_available && psram_alloc_ok) {
        return AUDIO_RECORD_PROBE_STORAGE_PSRAM;
    }
    if (internal_alloc_ok) {
        return AUDIO_RECORD_PROBE_STORAGE_INTERNAL;
    }
    return AUDIO_RECORD_PROBE_STORAGE_NONE;
}

size_t audio_record_probe_next_chunk_size(size_t remaining_bytes, size_t max_chunk_bytes)
{
    if (remaining_bytes == 0U || max_chunk_bytes == 0U) {
        return 0U;
    }
    return remaining_bytes < max_chunk_bytes ? remaining_bytes : max_chunk_bytes;
}
