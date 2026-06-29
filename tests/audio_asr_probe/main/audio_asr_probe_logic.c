#include "audio_asr_probe_logic.h"

void audio_asr_probe_session_init(audio_asr_probe_session_t *session, uint32_t max_capture_ms, size_t max_bytes)
{
    if (session == NULL) {
        return;
    }

    session->state = AUDIO_ASR_PROBE_STATE_IDLE;
    session->sequence = 0U;
    session->started_ms = 0U;
    session->capture_ms = 0U;
    session->max_capture_ms = max_capture_ms;
    session->captured_bytes = 0U;
    session->max_bytes = max_bytes;
    session->stop_due_to_limit = false;
}

bool audio_asr_probe_try_start(audio_asr_probe_session_t *session, uint32_t now_ms)
{
    if (session == NULL || session->state != AUDIO_ASR_PROBE_STATE_IDLE) {
        return false;
    }

    session->state = AUDIO_ASR_PROBE_STATE_RECORDING;
    session->started_ms = now_ms;
    session->capture_ms = 0U;
    session->captured_bytes = 0U;
    session->stop_due_to_limit = false;
    session->sequence += 1U;
    return true;
}

void audio_asr_probe_append_bytes(audio_asr_probe_session_t *session, size_t bytes_captured, bool button_pressed, uint32_t now_ms)
{
    if (session == NULL || session->state != AUDIO_ASR_PROBE_STATE_RECORDING) {
        return;
    }

    session->captured_bytes += bytes_captured;
    session->capture_ms = now_ms >= session->started_ms ? (now_ms - session->started_ms) : 0U;

    const bool overflowed = session->captured_bytes >= session->max_bytes;
    const bool timeout = session->capture_ms >= session->max_capture_ms;
    if (!button_pressed || overflowed || timeout) {
        session->state = AUDIO_ASR_PROBE_STATE_REQUEST_PENDING;
        session->stop_due_to_limit = overflowed || timeout;
        if (session->captured_bytes > session->max_bytes) {
            session->captured_bytes = session->max_bytes;
        }
        if (session->capture_ms > session->max_capture_ms) {
            session->capture_ms = session->max_capture_ms;
        }
    }
}

void audio_asr_probe_mark_completed(audio_asr_probe_session_t *session, bool button_still_pressed)
{
    if (session == NULL || session->state != AUDIO_ASR_PROBE_STATE_REQUEST_PENDING) {
        return;
    }

    session->state = button_still_pressed ? AUDIO_ASR_PROBE_STATE_WAIT_FOR_RELEASE : AUDIO_ASR_PROBE_STATE_IDLE;
}

void audio_asr_probe_poll_release(audio_asr_probe_session_t *session, bool button_pressed)
{
    if (session == NULL || session->state != AUDIO_ASR_PROBE_STATE_WAIT_FOR_RELEASE) {
        return;
    }
    if (!button_pressed) {
        session->state = AUDIO_ASR_PROBE_STATE_IDLE;
    }
}

uint32_t audio_asr_probe_pcm_duration_ms(size_t pcm_bytes, uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels)
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

bool audio_asr_probe_meets_min_duration(uint32_t duration_ms, uint32_t min_duration_ms)
{
    return min_duration_ms == 0U || duration_ms >= min_duration_ms;
}

bool audio_asr_probe_logic_self_test(void)
{
    audio_asr_probe_session_t session;
    audio_asr_probe_session_init(&session, 10000U, 320000U);

    if (!audio_asr_probe_try_start(&session, 100U)) {
        return false;
    }
    if (session.state != AUDIO_ASR_PROBE_STATE_RECORDING) {
        return false;
    }

    audio_asr_probe_append_bytes(&session, 1024U, true, 250U);
    if (session.captured_bytes != 1024U || session.state != AUDIO_ASR_PROBE_STATE_RECORDING) {
        return false;
    }

    audio_asr_probe_append_bytes(&session, 0U, false, 300U);
    if (session.state != AUDIO_ASR_PROBE_STATE_REQUEST_PENDING) {
        return false;
    }

    audio_asr_probe_mark_completed(&session, false);
    if (session.state != AUDIO_ASR_PROBE_STATE_IDLE) {
        return false;
    }

    audio_asr_probe_session_init(&session, 10000U, 320000U);
    if (!audio_asr_probe_try_start(&session, 500U)) {
        return false;
    }
    audio_asr_probe_append_bytes(&session, 320000U, true, 11000U);
    if (session.state != AUDIO_ASR_PROBE_STATE_REQUEST_PENDING || !session.stop_due_to_limit) {
        return false;
    }

    audio_asr_probe_mark_completed(&session, true);
    if (session.state != AUDIO_ASR_PROBE_STATE_WAIT_FOR_RELEASE) {
        return false;
    }

    audio_asr_probe_poll_release(&session, false);
    if (session.state != AUDIO_ASR_PROBE_STATE_IDLE) {
        return false;
    }

    return audio_asr_probe_pcm_duration_ms(32000U, 16000U, 16U, 1U) == 1000U
        && !audio_asr_probe_meets_min_duration(1999U, 2000U)
        && audio_asr_probe_meets_min_duration(2000U, 2000U)
        && audio_asr_probe_meets_min_duration(2500U, 2000U);
}
