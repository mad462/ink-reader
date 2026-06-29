#include "audio_record_wav.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *src)
{
    return (uint16_t)src[0] | (uint16_t)((uint16_t)src[1] << 8);
}

static uint32_t read_le32(const uint8_t *src)
{
    return (uint32_t)src[0]
        | ((uint32_t)src[1] << 8)
        | ((uint32_t)src[2] << 16)
        | ((uint32_t)src[3] << 24);
}

bool audio_record_wav_self_test(void)
{
    uint8_t header[AUDIO_RECORD_WAV_HEADER_SIZE];
    memset(header, 0, sizeof(header));

    if (!audio_record_wav_write_header(header, sizeof(header), 16000U, 16U, 1U, 32000U)) {
        return false;
    }

    return memcmp(header, "RIFF", 4) == 0
        && memcmp(header + 8, "WAVE", 4) == 0
        && memcmp(header + 12, "fmt ", 4) == 0
        && memcmp(header + 36, "data", 4) == 0
        && read_le32(header + 4) == 32036U
        && read_le16(header + 20) == 1U
        && read_le16(header + 22) == 1U
        && read_le32(header + 24) == 16000U
        && read_le32(header + 28) == 32000U
        && read_le16(header + 32) == 2U
        && read_le16(header + 34) == 16U
        && read_le32(header + 40) == 32000U
        && audio_record_wav_total_size(32000U) == (AUDIO_RECORD_WAV_HEADER_SIZE + 32000U)
        && !audio_record_wav_write_header(header, 8U, 16000U, 16U, 1U, 32000U)
        && !audio_record_wav_write_header(header, sizeof(header), 0U, 16U, 1U, 32000U)
        && !audio_record_wav_write_header(header, sizeof(header), 16000U, 0U, 1U, 32000U)
        && !audio_record_wav_write_header(header, sizeof(header), 16000U, 12U, 1U, 32000U)
        && !audio_record_wav_write_header(header, sizeof(header), 16000U, 16U, 0U, 32000U)
        && !audio_record_wav_write_header(header, sizeof(header), 0xffffffffU, 16U, 2U, 0xffffffffU)
        && !audio_record_wav_write_header(header, sizeof(header), 1U, 65528U, 65535U, 0U)
        && !audio_record_wav_total_size(0xffffffffU);
}
