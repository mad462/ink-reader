#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_ASR_PROBE_STATE_IDLE = 0,
    AUDIO_ASR_PROBE_STATE_RECORDING,
    AUDIO_ASR_PROBE_STATE_REQUEST_PENDING,
    AUDIO_ASR_PROBE_STATE_WAIT_FOR_RELEASE,
} audio_asr_probe_state_t;

typedef struct {
    audio_asr_probe_state_t state;
    uint32_t sequence;
    uint32_t started_ms;
    uint32_t capture_ms;
    uint32_t max_capture_ms;
    size_t captured_bytes;
    size_t max_bytes;
    bool stop_due_to_limit;
} audio_asr_probe_session_t;

void audio_asr_probe_session_init(audio_asr_probe_session_t *session, uint32_t max_capture_ms, size_t max_bytes);
bool audio_asr_probe_try_start(audio_asr_probe_session_t *session, uint32_t now_ms);
void audio_asr_probe_append_bytes(audio_asr_probe_session_t *session, size_t bytes_captured, bool button_pressed, uint32_t now_ms);
void audio_asr_probe_mark_completed(audio_asr_probe_session_t *session, bool button_still_pressed);
void audio_asr_probe_poll_release(audio_asr_probe_session_t *session, bool button_pressed);
uint32_t audio_asr_probe_pcm_duration_ms(size_t pcm_bytes, uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels);
bool audio_asr_probe_meets_min_duration(uint32_t duration_ms, uint32_t min_duration_ms);
bool audio_asr_probe_logic_self_test(void);

#ifdef __cplusplus
}
#endif
