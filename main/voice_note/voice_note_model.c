#include "voice_note/voice_note_model.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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

static bool is_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static uint32_t clamp_u32(uint32_t value, uint32_t max_value)
{
    return value > max_value ? max_value : value;
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

bool voice_note_model_format_timestamp(
    uint32_t epoch_s,
    char *buffer,
    size_t buffer_size)
{
    time_t raw_time = (time_t)epoch_s;
    struct tm local_tm = {0};

    if (buffer == NULL || buffer_size == 0U) {
        return false;
    }

    if (epoch_s == 0U || localtime_r(&raw_time, &local_tm) == NULL) {
        snprintf(buffer, buffer_size, "%s", "时间未同步");
        return false;
    }

    snprintf(
        buffer,
        buffer_size,
        "%02d-%02d %02d:%02d",
        local_tm.tm_mon + 1,
        local_tm.tm_mday,
        local_tm.tm_hour,
        local_tm.tm_min);
    return true;
}

bool voice_note_model_compose_meta_line(
    uint32_t created_at_epoch_s,
    uint32_t duration_ms,
    voice_note_status_t status,
    char *buffer,
    size_t buffer_size)
{
    char time_text[24];

    if (buffer == NULL || buffer_size == 0U) {
        return false;
    }

    (void)voice_note_model_format_timestamp(created_at_epoch_s, time_text, sizeof(time_text));
    snprintf(
        buffer,
        buffer_size,
        "%s  %lus  %s",
        time_text,
        (unsigned long)((duration_ms + 500U) / 1000U),
        status == VOICE_NOTE_STATUS_DONE ? "已完成" : "未完成");
    return true;
}

bool voice_note_model_compose_playback_line(
    voice_note_playback_state_t state,
    uint32_t total_ms,
    uint32_t position_ms,
    char *buffer,
    size_t buffer_size)
{
    const char *label = "";
    uint32_t remaining_ms = 0U;
    uint32_t remaining_s = 0U;
    uint32_t minutes = 0U;
    uint32_t seconds = 0U;

    if (buffer == NULL || buffer_size == 0U) {
        return false;
    }

    switch (state) {
        case VOICE_NOTE_PLAYBACK_PLAYING:
            label = "播放中";
            break;
        case VOICE_NOTE_PLAYBACK_PAUSED:
            label = "已暂停";
            break;
        case VOICE_NOTE_PLAYBACK_COMPLETED:
            label = "播放完成";
            break;
        case VOICE_NOTE_PLAYBACK_FAILED:
            label = "播放失败";
            break;
        case VOICE_NOTE_PLAYBACK_IDLE:
        default:
            buffer[0] = '\0';
            return false;
    }

    if (state == VOICE_NOTE_PLAYBACK_FAILED) {
        return snprintf(buffer, buffer_size, "%s", label) > 0;
    }

    if (state == VOICE_NOTE_PLAYBACK_COMPLETED) {
        remaining_ms = 0U;
    } else {
        remaining_ms = total_ms > clamp_u32(position_ms, total_ms)
            ? (total_ms - clamp_u32(position_ms, total_ms))
            : 0U;
    }
    remaining_s = (remaining_ms + 999U) / 1000U;
    minutes = remaining_s / 60U;
    seconds = remaining_s % 60U;

    return snprintf(
               buffer,
               buffer_size,
               "%s  剩余 %02lu:%02lu",
               label,
               (unsigned long)minutes,
               (unsigned long)seconds)
        > 0;
}

bool voice_note_model_normalize_text(
    const char *src,
    char *dst,
    size_t dst_size)
{
    size_t src_index = 0U;
    size_t dst_index = 0U;
    bool pending_space = false;

    if (dst == NULL || dst_size == 0U) {
        return false;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return false;
    }

    while (src[src_index] != '\0' && dst_index + 1U < dst_size) {
        const unsigned char ch = (unsigned char)src[src_index];

        if (is_ascii_space((char)ch)) {
            pending_space = dst_index > 0U;
            ++src_index;
            continue;
        }

        if (pending_space && dst_index + 1U < dst_size) {
            dst[dst_index++] = ' ';
            pending_space = false;
        }

        if (ch < 0x80U) {
            dst[dst_index++] = (char)ch;
            ++src_index;
            continue;
        }

        {
            size_t cp_len = 1U;
            if ((ch & 0xE0U) == 0xC0U) {
                cp_len = 2U;
            } else if ((ch & 0xF0U) == 0xE0U) {
                cp_len = 3U;
            } else if ((ch & 0xF8U) == 0xF0U) {
                cp_len = 4U;
            }
            if (dst_index + cp_len >= dst_size) {
                break;
            }
            for (size_t i = 0U; i < cp_len && src[src_index] != '\0'; ++i) {
                dst[dst_index++] = src[src_index++];
            }
        }
    }

    while (dst_index > 0U && dst[dst_index - 1U] == ' ') {
        --dst_index;
    }
    dst[dst_index] = '\0';
    return dst_index > 0U;
}

bool voice_note_model_self_test(void)
{
    char note_id[VOICE_NOTE_ID_LENGTH];
    char json_path[VOICE_NOTE_PATH_LENGTH];
    char wav_path[VOICE_NOTE_PATH_LENGTH];
    char title[VOICE_NOTE_TITLE_LENGTH];
    char tiny_title[8];
    char status[VOICE_NOTE_STATUS_COPY_LENGTH];
    char time_text[24];
    char meta_text[48];
    char normalized[VOICE_NOTE_TEXT_LENGTH];

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
    voice_note_model_build_title(
        "abcdefghi",
        VOICE_NOTE_TRANSCRIPT_READY,
        tiny_title,
        sizeof(tiny_title));
    if (strcmp(tiny_title, "abcd...") != 0) {
        return false;
    }
    if (!voice_note_model_format_timestamp(1735689600U, time_text, sizeof(time_text))) {
        return false;
    }
    if (strcmp(time_text, "01-01 08:00") != 0) {
        return false;
    }
    if (!voice_note_model_compose_meta_line(
            1735689600U,
            3200U,
            VOICE_NOTE_STATUS_DONE,
            meta_text,
            sizeof(meta_text))) {
        return false;
    }
    if (strcmp(meta_text, "01-01 08:00  3s  已完成") != 0) {
        return false;
    }
    if (!voice_note_model_compose_playback_line(
            VOICE_NOTE_PLAYBACK_PLAYING,
            12000U,
            3000U,
            meta_text,
            sizeof(meta_text))) {
        return false;
    }
    if (strcmp(meta_text, "播放中  剩余 00:09") != 0) {
        return false;
    }
    if (!voice_note_model_normalize_text(
            "  第一行\r\n第二行 \n\n  第三行\t结尾  ",
            normalized,
            sizeof(normalized))) {
        return false;
    }
    if (strcmp(normalized, "第一行 第二行 第三行 结尾") != 0) {
        return false;
    }
    return strcmp(status, "无效标签，请重新录入") == 0
        && voice_note_model_is_short_recording(1999U, 2000U)
        && !voice_note_model_is_short_recording(2000U, 2000U);
}
