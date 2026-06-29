#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

enum {
    VOICE_NOTE_SAMPLE_RATE = 16000,
    VOICE_NOTE_BITS_PER_SAMPLE = 16,
    VOICE_NOTE_CHANNELS = 1,
    VOICE_NOTE_MAX_CAPTURE_MS = 30000,
    VOICE_NOTE_MIN_CAPTURE_MS = 2000,
    VOICE_NOTE_WAV_HEADER_BYTES = 44,
};

size_t voice_note_audio_wav_size(size_t pcm_bytes);
esp_err_t voice_note_audio_build_wav(
    const uint8_t *pcm_data,
    size_t pcm_size,
    uint8_t *wav_buffer,
    size_t wav_buffer_size,
    size_t *wav_bytes_out);
uint32_t voice_note_audio_pcm_duration_ms(size_t pcm_bytes);
bool voice_note_audio_self_test(void);
