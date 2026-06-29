#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_ASR_JSON_OK = 0,
    AUDIO_ASR_JSON_ERROR_INVALID_ARG,
    AUDIO_ASR_JSON_ERROR_PARSE_FAILED,
    AUDIO_ASR_JSON_ERROR_MISSING_CHOICES,
    AUDIO_ASR_JSON_ERROR_MISSING_MESSAGE,
    AUDIO_ASR_JSON_ERROR_MISSING_CONTENT,
    AUDIO_ASR_JSON_ERROR_BUFFER_TOO_SMALL,
} audio_asr_json_result_t;

audio_asr_json_result_t audio_asr_json_extract_transcript(
    const char *json,
    char *buffer,
    size_t buffer_size);
const char *audio_asr_json_result_name(audio_asr_json_result_t result);
bool audio_asr_json_self_test(void);

#ifdef __cplusplus
}
#endif
