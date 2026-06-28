#include "audio_record_wav.h"

#include <string.h>

static void write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8) & 0xffU);
}

static void write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8) & 0xffU);
    dst[2] = (uint8_t)((value >> 16) & 0xffU);
    dst[3] = (uint8_t)((value >> 24) & 0xffU);
}

bool audio_record_wav_write_header(
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t sample_rate,
    uint16_t bits_per_sample,
    uint16_t channels,
    uint32_t pcm_data_size)
{
    if (buffer == NULL || buffer_size < AUDIO_RECORD_WAV_HEADER_SIZE || channels == 0U || bits_per_sample == 0U) {
        return false;
    }

    const uint32_t block_align = (uint32_t)channels * (uint32_t)(bits_per_sample / 8U);
    const uint32_t byte_rate = sample_rate * block_align;

    memset(buffer, 0, AUDIO_RECORD_WAV_HEADER_SIZE);
    memcpy(buffer, "RIFF", 4);
    write_le32(buffer + 4, 36U + pcm_data_size);
    memcpy(buffer + 8, "WAVE", 4);
    memcpy(buffer + 12, "fmt ", 4);
    write_le32(buffer + 16, 16U);
    write_le16(buffer + 20, 1U);
    write_le16(buffer + 22, channels);
    write_le32(buffer + 24, sample_rate);
    write_le32(buffer + 28, byte_rate);
    write_le16(buffer + 32, (uint16_t)block_align);
    write_le16(buffer + 34, bits_per_sample);
    memcpy(buffer + 36, "data", 4);
    write_le32(buffer + 40, pcm_data_size);
    return true;
}

size_t audio_record_wav_total_size(uint32_t pcm_data_size)
{
    return AUDIO_RECORD_WAV_HEADER_SIZE + (size_t)pcm_data_size;
}
