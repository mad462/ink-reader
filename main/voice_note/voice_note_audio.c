#include "voice_note/voice_note_audio.h"

#include <string.h>

static void voice_note_write_u16le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void voice_note_write_u32le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16U) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

size_t voice_note_audio_wav_size(size_t pcm_bytes)
{
    return VOICE_NOTE_WAV_HEADER_BYTES + pcm_bytes;
}

esp_err_t voice_note_audio_build_wav(
    const uint8_t *pcm_data,
    size_t pcm_size,
    uint8_t *wav_buffer,
    size_t wav_buffer_size,
    size_t *wav_bytes_out)
{
    const uint32_t byte_rate =
        VOICE_NOTE_SAMPLE_RATE * VOICE_NOTE_CHANNELS * (VOICE_NOTE_BITS_PER_SAMPLE / 8U);
    const uint16_t block_align =
        (uint16_t)(VOICE_NOTE_CHANNELS * (VOICE_NOTE_BITS_PER_SAMPLE / 8U));
    const size_t wav_size = voice_note_audio_wav_size(pcm_size);

    if (pcm_data == NULL || wav_buffer == NULL || wav_buffer_size < wav_size) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(wav_buffer + 0, "RIFF", 4U);
    voice_note_write_u32le(wav_buffer + 4, (uint32_t)(36U + pcm_size));
    memcpy(wav_buffer + 8, "WAVE", 4U);
    memcpy(wav_buffer + 12, "fmt ", 4U);
    voice_note_write_u32le(wav_buffer + 16, 16U);
    voice_note_write_u16le(wav_buffer + 20, 1U);
    voice_note_write_u16le(wav_buffer + 22, VOICE_NOTE_CHANNELS);
    voice_note_write_u32le(wav_buffer + 24, VOICE_NOTE_SAMPLE_RATE);
    voice_note_write_u32le(wav_buffer + 28, byte_rate);
    voice_note_write_u16le(wav_buffer + 32, block_align);
    voice_note_write_u16le(wav_buffer + 34, VOICE_NOTE_BITS_PER_SAMPLE);
    memcpy(wav_buffer + 36, "data", 4U);
    voice_note_write_u32le(wav_buffer + 40, (uint32_t)pcm_size);
    memcpy(wav_buffer + VOICE_NOTE_WAV_HEADER_BYTES, pcm_data, pcm_size);

    if (wav_bytes_out != NULL) {
        *wav_bytes_out = wav_size;
    }
    return ESP_OK;
}

uint32_t voice_note_audio_pcm_duration_ms(size_t pcm_bytes)
{
    const size_t bytes_per_ms =
        (VOICE_NOTE_SAMPLE_RATE * VOICE_NOTE_CHANNELS * (VOICE_NOTE_BITS_PER_SAMPLE / 8U)) / 1000U;

    if (bytes_per_ms == 0U) {
        return 0U;
    }
    return (uint32_t)(pcm_bytes / bytes_per_ms);
}

bool voice_note_audio_self_test(void)
{
    uint8_t pcm[4] = {1, 2, 3, 4};
    uint8_t wav[48];
    size_t written = 0U;

    return VOICE_NOTE_MAX_CAPTURE_MS == 30000
        && voice_note_audio_wav_size(sizeof(pcm)) == 48U
        && voice_note_audio_build_wav(pcm, sizeof(pcm), wav, sizeof(wav), &written) == ESP_OK
        && written == sizeof(wav)
        && memcmp(wav, "RIFF", 4U) == 0
        && memcmp(wav + 8, "WAVE", 4U) == 0
        && memcmp(wav + VOICE_NOTE_WAV_HEADER_BYTES, pcm, sizeof(pcm)) == 0
        && voice_note_audio_pcm_duration_ms(32000U) == 1000U;
}
