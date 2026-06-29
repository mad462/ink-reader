#include "audio_asr_json.h"

#include <string.h>

#include "cJSON.h"

audio_asr_json_result_t audio_asr_json_extract_transcript(
    const char *json,
    char *buffer,
    size_t buffer_size)
{
    cJSON *root = NULL;
    cJSON *choices = NULL;
    cJSON *choice0 = NULL;
    cJSON *message = NULL;
    cJSON *content = NULL;
    const char *transcript = NULL;
    size_t transcript_len = 0U;

    if (json == NULL || buffer == NULL || buffer_size == 0U) {
        return AUDIO_ASR_JSON_ERROR_INVALID_ARG;
    }

    root = cJSON_Parse(json);
    if (root == NULL) {
        return AUDIO_ASR_JSON_ERROR_PARSE_FAILED;
    }

    choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) <= 0) {
        cJSON_Delete(root);
        return AUDIO_ASR_JSON_ERROR_MISSING_CHOICES;
    }

    choice0 = cJSON_GetArrayItem(choices, 0);
    if (!cJSON_IsObject(choice0)) {
        cJSON_Delete(root);
        return AUDIO_ASR_JSON_ERROR_MISSING_CHOICES;
    }

    message = cJSON_GetObjectItemCaseSensitive(choice0, "message");
    if (!cJSON_IsObject(message)) {
        cJSON_Delete(root);
        return AUDIO_ASR_JSON_ERROR_MISSING_MESSAGE;
    }

    content = cJSON_GetObjectItemCaseSensitive(message, "content");
    if (!cJSON_IsString(content) || content->valuestring == NULL) {
        cJSON_Delete(root);
        return AUDIO_ASR_JSON_ERROR_MISSING_CONTENT;
    }

    transcript = content->valuestring;
    transcript_len = strlen(transcript);
    if (transcript_len + 1U > buffer_size) {
        cJSON_Delete(root);
        return AUDIO_ASR_JSON_ERROR_BUFFER_TOO_SMALL;
    }

    memcpy(buffer, transcript, transcript_len + 1U);
    cJSON_Delete(root);
    return AUDIO_ASR_JSON_OK;
}

const char *audio_asr_json_result_name(audio_asr_json_result_t result)
{
    switch (result) {
        case AUDIO_ASR_JSON_OK:
            return "ok";
        case AUDIO_ASR_JSON_ERROR_INVALID_ARG:
            return "invalid_arg";
        case AUDIO_ASR_JSON_ERROR_PARSE_FAILED:
            return "parse_failed";
        case AUDIO_ASR_JSON_ERROR_MISSING_CHOICES:
            return "missing_choices";
        case AUDIO_ASR_JSON_ERROR_MISSING_MESSAGE:
            return "missing_message";
        case AUDIO_ASR_JSON_ERROR_MISSING_CONTENT:
            return "missing_content";
        case AUDIO_ASR_JSON_ERROR_BUFFER_TOO_SMALL:
            return "buffer_too_small";
        default:
            return "unknown";
    }
}

bool audio_asr_json_self_test(void)
{
    char buffer[64];
    const char *json = "{\"choices\":[{\"message\":{\"content\":\"hello \\u4f60\\u597d\"}}]}";

    return audio_asr_json_extract_transcript(json, buffer, sizeof(buffer)) == AUDIO_ASR_JSON_OK
        && strcmp(buffer, "hello 你好") == 0
        && audio_asr_json_extract_transcript("{}", buffer, sizeof(buffer)) == AUDIO_ASR_JSON_ERROR_MISSING_CHOICES
        && audio_asr_json_extract_transcript("{", buffer, sizeof(buffer)) == AUDIO_ASR_JSON_ERROR_PARSE_FAILED
        && audio_asr_json_extract_transcript(json, buffer, 4U) == AUDIO_ASR_JSON_ERROR_BUFFER_TOO_SMALL
        && strcmp(audio_asr_json_result_name(AUDIO_ASR_JSON_ERROR_MISSING_CONTENT), "missing_content") == 0;
}
