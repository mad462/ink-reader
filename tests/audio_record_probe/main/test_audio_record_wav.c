#include "audio_record_wav.h"

#include <string.h>

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
        && audio_record_wav_total_size(32000U) == (AUDIO_RECORD_WAV_HEADER_SIZE + 32000U)
        && !audio_record_wav_write_header(header, 8U, 16000U, 16U, 1U, 32000U);
}
