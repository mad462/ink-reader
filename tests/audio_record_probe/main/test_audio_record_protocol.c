#include "audio_record_protocol.h"

#include <string.h>

bool audio_record_protocol_self_test(void)
{
    audio_record_packet_header_t header;
    memset(&header, 0, sizeof(header));

    audio_record_protocol_fill_header(&header, 16000U, 16U, 1U, 320U, 10U, 3U);

    return memcmp(header.magic, AUDIO_RECORD_PROTOCOL_MAGIC, sizeof(header.magic)) == 0
        && header.header_size == sizeof(audio_record_packet_header_t)
        && header.header_version == AUDIO_RECORD_PROTOCOL_VERSION
        && header.sample_rate == 16000U
        && header.bits_per_sample == 16U
        && header.channels == 1U
        && header.pcm_data_size == 320U
        && header.capture_ms == 10U
        && header.sequence == 3U
        && header.reserved0 == 0U;
}
