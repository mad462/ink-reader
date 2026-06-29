#include "voice_note/voice_note_capture_logic.h"

#include <string.h>

void voice_note_capture_session_init(
    voice_note_capture_session_t *session,
    uint32_t max_capture_ms,
    size_t max_bytes)
{
    if (session == NULL) {
        return;
    }

    memset(session, 0, sizeof(*session));
    session->max_capture_ms = max_capture_ms;
    session->max_bytes = max_bytes;
}

bool voice_note_capture_try_start(voice_note_capture_session_t *session, uint32_t now_ms)
{
    if (session == NULL || session->state != VOICE_NOTE_CAPTURE_IDLE) {
        return false;
    }

    session->state = VOICE_NOTE_CAPTURE_RECORDING;
    session->sequence++;
    session->started_ms = now_ms;
    session->capture_ms = 0U;
    session->captured_bytes = 0U;
    session->stop_due_to_limit = false;
    return true;
}

void voice_note_capture_append_bytes(
    voice_note_capture_session_t *session,
    size_t bytes_captured,
    bool button_pressed,
    uint32_t now_ms)
{
    if (session == NULL || session->state != VOICE_NOTE_CAPTURE_RECORDING) {
        return;
    }

    session->captured_bytes += bytes_captured;
    session->capture_ms = now_ms - session->started_ms;
    if (!button_pressed
        || session->capture_ms >= session->max_capture_ms
        || session->captured_bytes >= session->max_bytes) {
        session->stop_due_to_limit =
            button_pressed
            && (session->capture_ms >= session->max_capture_ms
                || session->captured_bytes >= session->max_bytes);
        session->state = VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE;
    }
}

void voice_note_capture_mark_completed(
    voice_note_capture_session_t *session,
    bool button_still_pressed)
{
    if (session == NULL) {
        return;
    }

    session->state = button_still_pressed
        ? VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE
        : VOICE_NOTE_CAPTURE_IDLE;
}

void voice_note_capture_poll_release(
    voice_note_capture_session_t *session,
    bool button_pressed)
{
    if (session == NULL || session->state != VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE) {
        return;
    }

    if (!button_pressed) {
        session->state = VOICE_NOTE_CAPTURE_IDLE;
    }
}

bool voice_note_capture_logic_self_test(void)
{
    voice_note_capture_session_t session;

    voice_note_capture_session_init(&session, 10000U, 320000U);
    if (!voice_note_capture_try_start(&session, 100U)) {
        return false;
    }
    voice_note_capture_append_bytes(&session, 2048U, true, 150U);
    if (session.state != VOICE_NOTE_CAPTURE_RECORDING) {
        return false;
    }
    voice_note_capture_append_bytes(&session, 2048U, false, 250U);
    if (session.state != VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE) {
        return false;
    }
    voice_note_capture_poll_release(&session, false);
    return session.state == VOICE_NOTE_CAPTURE_IDLE;
}
