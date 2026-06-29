#include "voice_note/voice_note_model.h"

#include <stdio.h>
#include <string.h>

static void truncate_utf8ish_copy(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0U;

    if (dst == NULL || dst_size == 0U) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }
    for (; src[i] != '\0' && i + 1U < dst_size; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
    if (src[i] != '\0' && i >= 3U) {
        dst[i - 3U] = '.';
        dst[i - 2U] = '.';
        dst[i - 1U] = '.';
        dst[i] = '\0';
    }
}

bool voice_note_model_build_note_paths(
    uint32_t epoch_s,
    uint32_t sequence,
    char *note_id,
    size_t note_id_size,
    char *json_path,
    size_t json_path_size,
    char *wav_path,
    size_t wav_path_size)
{
    int note_id_written = 0;
    int json_written = 0;
    int wav_written = 0;

    if (note_id == NULL || json_path == NULL || wav_path == NULL) {
        return false;
    }

    note_id_written = snprintf(
        note_id,
        note_id_size,
        "note_%010u_%03u",
        (unsigned)epoch_s,
        (unsigned)sequence);
    json_written = snprintf(
        json_path,
        json_path_size,
        "%s/%s.json",
        VOICE_NOTE_ROOT_DIR,
        note_id);
    wav_written = snprintf(
        wav_path,
        wav_path_size,
        "%s/%s.wav",
        VOICE_NOTE_ROOT_DIR,
        note_id);
    return note_id_written > 0
        && json_written > 0
        && wav_written > 0
        && (size_t)note_id_written < note_id_size
        && (size_t)json_written < json_path_size
        && (size_t)wav_written < wav_path_size;
}

void voice_note_model_build_title(
    const char *text,
    voice_note_transcript_state_t transcript_state,
    char *title,
    size_t title_size)
{
    if (title == NULL || title_size == 0U) {
        return;
    }

    if (transcript_state != VOICE_NOTE_TRANSCRIPT_READY || text == NULL || text[0] == '\0') {
        snprintf(title, title_size, "%s", "这是一条语音标签");
        return;
    }

    truncate_utf8ish_copy(text, title, title_size);
}

bool voice_note_model_is_short_recording(uint32_t duration_ms, uint32_t min_duration_ms)
{
    return duration_ms < min_duration_ms;
}

bool voice_note_model_status_copy_for_job(
    voice_note_job_state_t state,
    char *buffer,
    size_t buffer_size)
{
    const char *text = "按住 Confirm 开始录音";

    if (buffer == NULL || buffer_size == 0U) {
        return false;
    }

    switch (state) {
        case VOICE_NOTE_JOB_RECORDING:
            text = "正在录音";
            break;
        case VOICE_NOTE_JOB_PACKAGING:
            text = "正在打包";
            break;
        case VOICE_NOTE_JOB_PERSISTING_WAV:
            text = "结束录音";
            break;
        case VOICE_NOTE_JOB_WIFI_CONNECTING:
            text = "正在连接网络";
            break;
        case VOICE_NOTE_JOB_UPLOADING:
            text = "上传中";
            break;
        case VOICE_NOTE_JOB_RECOGNIZING:
            text = "上传识别中";
            break;
        case VOICE_NOTE_JOB_COMPLETED:
            text = "识别完成";
            break;
        case VOICE_NOTE_JOB_FAILED:
            text = "识别失败";
            break;
        case VOICE_NOTE_JOB_INVALID_SHORT_RECORDING:
            text = "无效标签，请重新录入";
            break;
        case VOICE_NOTE_JOB_PERSISTING_RESULT:
            text = "正在保存结果";
            break;
        case VOICE_NOTE_JOB_IDLE:
        default:
            break;
    }

    return snprintf(buffer, buffer_size, "%s", text) > 0;
}

bool voice_note_model_self_test(void)
{
    char note_id[VOICE_NOTE_ID_LENGTH];
    char json_path[VOICE_NOTE_PATH_LENGTH];
    char wav_path[VOICE_NOTE_PATH_LENGTH];
    char title[VOICE_NOTE_TITLE_LENGTH];
    char status[VOICE_NOTE_STATUS_COPY_LENGTH];

    if (!voice_note_model_build_note_paths(
            1719651000U,
            1U,
            note_id,
            sizeof(note_id),
            json_path,
            sizeof(json_path),
            wav_path,
            sizeof(wav_path))) {
        return false;
    }
    if (strcmp(note_id, "note_1719651000_001") != 0) {
        return false;
    }
    voice_note_model_build_title(
        "记得买小葱，还有蒜头。",
        VOICE_NOTE_TRANSCRIPT_READY,
        title,
        sizeof(title));
    if (strcmp(title, "记得买小葱，还有蒜头。") != 0) {
        return false;
    }
    voice_note_model_build_title(
        "",
        VOICE_NOTE_TRANSCRIPT_FAILED,
        title,
        sizeof(title));
    if (strcmp(title, "这是一条语音标签") != 0) {
        return false;
    }
    if (!voice_note_model_status_copy_for_job(
            VOICE_NOTE_JOB_INVALID_SHORT_RECORDING,
            status,
            sizeof(status))) {
        return false;
    }
    return strcmp(status, "无效标签，请重新录入") == 0
        && voice_note_model_is_short_recording(1999U, 2000U)
        && !voice_note_model_is_short_recording(2000U, 2000U);
}
