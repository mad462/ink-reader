#include "audio_record_protocol.h"

#include <string.h>

void audio_record_protocol_fill_header(
    audio_record_packet_header_t *header,
    uint32_t sample_rate,
    uint16_t bits_per_sample,
    uint16_t channels,
    uint32_t pcm_data_size,
    uint32_t capture_ms,
    uint32_t sequence)
{
    if (header == NULL) {
        return;
    }

    memset(header, 0, sizeof(*header));
    memcpy(header->magic, AUDIO_RECORD_PROTOCOL_MAGIC, sizeof(header->magic));
    header->header_size = (uint32_t)sizeof(*header);
    header->header_version = AUDIO_RECORD_PROTOCOL_VERSION;
    header->sample_rate = sample_rate;
    header->bits_per_sample = bits_per_sample;
    header->channels = channels;
    header->pcm_data_size = pcm_data_size;
    header->capture_ms = capture_ms;
    header->sequence = sequence;
}
