#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_ASR_WAV_HEADER_SIZE 44U

size_t audio_asr_wav_total_size(uint32_t pcm_bytes);
bool audio_asr_wav_write_header(
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t sample_rate,
    uint16_t bits_per_sample,
    uint16_t channels,
    uint32_t pcm_bytes);
size_t audio_asr_base64_encoded_size(size_t input_bytes);
bool audio_asr_wav_self_test(void);

#ifdef __cplusplus
}
#endif
