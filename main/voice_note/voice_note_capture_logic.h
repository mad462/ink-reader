#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    VOICE_NOTE_CAPTURE_IDLE = 0,
    VOICE_NOTE_CAPTURE_RECORDING,
    VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE,
} voice_note_capture_state_t;

typedef struct {
    voice_note_capture_state_t state;
    uint32_t sequence;
    uint32_t started_ms;
    uint32_t capture_ms;
    uint32_t max_capture_ms;
    size_t captured_bytes;
    size_t max_bytes;
    bool stop_due_to_limit;
} voice_note_capture_session_t;

void voice_note_capture_session_init(
    voice_note_capture_session_t *session,
    uint32_t max_capture_ms,
    size_t max_bytes);
bool voice_note_capture_try_start(voice_note_capture_session_t *session, uint32_t now_ms);
void voice_note_capture_append_bytes(
    voice_note_capture_session_t *session,
    size_t bytes_captured,
    bool button_pressed,
    uint32_t now_ms);
void voice_note_capture_mark_completed(
    voice_note_capture_session_t *session,
    bool button_still_pressed);
void voice_note_capture_poll_release(
    voice_note_capture_session_t *session,
    bool button_pressed);
bool voice_note_capture_logic_self_test(void);
