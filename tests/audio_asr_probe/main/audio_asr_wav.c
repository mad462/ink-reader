#include "audio_asr_wav.h"

#include <string.h>

static void write_le16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void write_le32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
    buffer[2] = (uint8_t)((value >> 16U) & 0xFFU);
    buffer[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

size_t audio_asr_wav_total_size(uint32_t pcm_bytes)
{
    return AUDIO_ASR_WAV_HEADER_SIZE + (size_t)pcm_bytes;
}

bool audio_asr_wav_write_header(
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t sample_rate,
    uint16_t bits_per_sample,
    uint16_t channels,
    uint32_t pcm_bytes)
{
    if (buffer == NULL || buffer_size < AUDIO_ASR_WAV_HEADER_SIZE || sample_rate == 0U || bits_per_sample == 0U || channels == 0U) {
        return false;
    }

    const uint32_t byte_rate = sample_rate * (uint32_t)channels * ((uint32_t)bits_per_sample / 8U);
    const uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8U));

    memcpy(buffer + 0, "RIFF", 4);
    write_le32(buffer + 4, 36U + pcm_bytes);
    memcpy(buffer + 8, "WAVE", 4);
    memcpy(buffer + 12, "fmt ", 4);
    write_le32(buffer + 16, 16U);
    write_le16(buffer + 20, 1U);
    write_le16(buffer + 22, channels);
    write_le32(buffer + 24, sample_rate);
    write_le32(buffer + 28, byte_rate);
    write_le16(buffer + 32, block_align);
    write_le16(buffer + 34, bits_per_sample);
    memcpy(buffer + 36, "data", 4);
    write_le32(buffer + 40, pcm_bytes);
    return true;
}

size_t audio_asr_base64_encoded_size(size_t input_bytes)
{
    if (input_bytes == 0U) {
        return 0U;
    }
    return ((input_bytes + 2U) / 3U) * 4U;
}

bool audio_asr_wav_self_test(void)
{
    uint8_t header[AUDIO_ASR_WAV_HEADER_SIZE];
    if (!audio_asr_wav_write_header(header, sizeof(header), 16000U, 16U, 1U, 32000U)) {
        return false;
    }
    return audio_asr_wav_total_size(32000U) == 32044U
        && audio_asr_base64_encoded_size(0U) == 0U
        && audio_asr_base64_encoded_size(1U) == 4U
        && audio_asr_base64_encoded_size(3U) == 4U
        && audio_asr_base64_encoded_size(4U) == 8U
        && header[0] == 'R'
        && header[8] == 'W'
        && header[36] == 'd';
}
