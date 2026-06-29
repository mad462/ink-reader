#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RECORD_PROTOCOL_MAGIC "ARPROBE1"
#define AUDIO_RECORD_PROTOCOL_VERSION 1U

typedef struct __attribute__((packed)) {
    char magic[8];
    uint32_t header_size;
    uint32_t header_version;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t channels;
    uint32_t pcm_data_size;
    uint32_t capture_ms;
    uint32_t sequence;
    uint32_t reserved0;
} audio_record_packet_header_t;

void audio_record_protocol_fill_header(
    audio_record_packet_header_t *header,
    uint32_t sample_rate,
    uint16_t bits_per_sample,
    uint16_t channels,
    uint32_t pcm_data_size,
    uint32_t capture_ms,
    uint32_t sequence);

bool audio_record_protocol_self_test(void);

#ifdef __cplusplus
}
#endif
