#include "audio_record_wav.h"

#include <limits.h>
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

static bool add_u32(uint32_t left, uint32_t right, uint32_t *result)
{
    if (result == NULL || left > (UINT32_MAX - right)) {
        return false;
    }

    *result = left + right;
    return true;
}

bool audio_record_wav_write_header(
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t sample_rate,
    uint16_t bits_per_sample,
    uint16_t channels,
    uint32_t pcm_data_size)
{
    if (buffer == NULL
        || buffer_size < AUDIO_RECORD_WAV_HEADER_SIZE
        || sample_rate == 0U
        || channels == 0U
        || bits_per_sample == 0U
        || (bits_per_sample % 8U) != 0U) {
        return false;
    }

    const uint32_t bytes_per_sample = (uint32_t)(bits_per_sample / 8U);
    if (bytes_per_sample == 0U || (uint32_t)channels > (uint32_t)(UINT16_MAX / bytes_per_sample)) {
        return false;
    }

    const uint32_t block_align = (uint32_t)channels * bytes_per_sample;
    if (block_align == 0U || sample_rate > (UINT32_MAX / block_align)) {
        return false;
    }

    const uint32_t byte_rate = sample_rate * block_align;
    uint32_t riff_chunk_size = 0U;
    if (!add_u32(36U, pcm_data_size, &riff_chunk_size) || audio_record_wav_total_size(pcm_data_size) == 0U) {
        return false;
    }

    memset(buffer, 0, AUDIO_RECORD_WAV_HEADER_SIZE);
    memcpy(buffer, "RIFF", 4);
    write_le32(buffer + 4, riff_chunk_size);
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
    if ((size_t)pcm_data_size > ((size_t)-1) - AUDIO_RECORD_WAV_HEADER_SIZE) {
        return 0U;
    }

    return AUDIO_RECORD_WAV_HEADER_SIZE + (size_t)pcm_data_size;
}
