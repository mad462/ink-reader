#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_RECORD_PROBE_STATE_IDLE = 0,
    AUDIO_RECORD_PROBE_STATE_RECORDING,
    AUDIO_RECORD_PROBE_STATE_EXPORT_PENDING,
    AUDIO_RECORD_PROBE_STATE_WAIT_FOR_RELEASE,
} audio_record_probe_state_t;

typedef enum {
    AUDIO_RECORD_PROBE_STORAGE_NONE = 0,
    AUDIO_RECORD_PROBE_STORAGE_PSRAM,
    AUDIO_RECORD_PROBE_STORAGE_INTERNAL,
} audio_record_probe_storage_t;

typedef struct {
    audio_record_probe_state_t state;
    uint32_t sequence;
    uint32_t started_ms;
    uint32_t capture_ms;
    uint32_t max_capture_ms;
    size_t captured_bytes;
    size_t max_bytes;
    bool stop_due_to_limit;
} audio_record_probe_session_t;

void audio_record_probe_session_init(
    audio_record_probe_session_t *session,
    uint32_t max_capture_ms,
    size_t max_bytes);

bool audio_record_probe_try_start(audio_record_probe_session_t *session, uint32_t now_ms);

void audio_record_probe_append_bytes(
    audio_record_probe_session_t *session,
    size_t bytes_captured,
    bool button_pressed,
    uint32_t now_ms);

void audio_record_probe_mark_exported(audio_record_probe_session_t *session, bool button_still_pressed);

void audio_record_probe_poll_release(audio_record_probe_session_t *session, bool button_pressed);

uint32_t audio_record_probe_pcm_duration_ms(
    size_t pcm_bytes,
    uint32_t sample_rate,
    uint16_t bits_per_sample,
    uint16_t channels);

uint32_t audio_record_probe_remaining_ms(const audio_record_probe_session_t *session);

bool audio_record_probe_progress_due(
    uint32_t now_ms,
    uint32_t *last_report_ms,
    uint32_t interval_ms);

audio_record_probe_storage_t audio_record_probe_choose_storage(
    bool psram_available,
    bool psram_alloc_ok,
    bool internal_alloc_ok);

size_t audio_record_probe_next_chunk_size(size_t remaining_bytes, size_t max_chunk_bytes);

bool audio_record_probe_logic_self_test(void);

#ifdef __cplusplus
}
#endif
