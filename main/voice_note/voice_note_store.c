#include "voice_note/voice_note_store.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_check.h"

#include "voice_note/voice_note_model.h"

static voice_note_note_t s_notes[VOICE_NOTE_MAX_NOTES];
static size_t s_note_count;

static esp_err_t ensure_voice_note_dir(void);
static bool note_matches_tab(const voice_note_note_t *note, voice_note_tab_t tab);
static cJSON *note_to_json(const voice_note_note_t *note);
static bool note_from_json(const cJSON *root, voice_note_note_t *note);
static esp_err_t write_note_file(const char *path, const voice_note_note_t *note);

static esp_err_t ensure_voice_note_dir(void)
{
    struct stat st = {0};

    if (stat("/sdcard/.ink-reader", &st) != 0) {
        if (mkdir("/sdcard/.ink-reader", 0775) != 0) {
            return ESP_FAIL;
        }
    }
    if (stat(VOICE_NOTE_ROOT_DIR, &st) != 0) {
        if (mkdir(VOICE_NOTE_ROOT_DIR, 0775) != 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static bool note_matches_tab(const voice_note_note_t *note, voice_note_tab_t tab)
{
    if (note == NULL) {
        return false;
    }

    switch (tab) {
        case VOICE_NOTE_TAB_PENDING:
            return note->status == VOICE_NOTE_STATUS_PENDING;
        case VOICE_NOTE_TAB_DONE:
            return note->status == VOICE_NOTE_STATUS_DONE;
        case VOICE_NOTE_TAB_ALL:
        default:
            return true;
    }
}

static cJSON *note_to_json(const voice_note_note_t *note)
{
    cJSON *root = NULL;

    if (note == NULL) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "id", note->id);
    cJSON_AddNumberToObject(root, "created_at_epoch_s", note->created_at_epoch_s);
    cJSON_AddNumberToObject(root, "updated_at_epoch_s", note->updated_at_epoch_s);
    cJSON_AddNumberToObject(root, "status", note->status);
    cJSON_AddNumberToObject(root, "transcript_state", note->transcript_state);
    cJSON_AddStringToObject(root, "title", note->title);
    cJSON_AddStringToObject(root, "text", note->text);
    cJSON_AddStringToObject(root, "wav_path", note->wav_path);
    cJSON_AddNumberToObject(root, "duration_ms", note->duration_ms);
    cJSON_AddNumberToObject(root, "sample_rate", note->sample_rate);
    cJSON_AddNumberToObject(root, "channels", note->channels);
    cJSON_AddNumberToObject(root, "bits_per_sample", note->bits_per_sample);
    cJSON_AddStringToObject(root, "last_error", note->last_error);
    return root;
}

static bool note_from_json(const cJSON *root, voice_note_note_t *note)
{
    const cJSON *id = NULL;
    const cJSON *title = NULL;
    const cJSON *wav_path = NULL;
    const cJSON *text = NULL;
    const cJSON *last_error = NULL;

    if (root == NULL || note == NULL) {
        return false;
    }

    id = cJSON_GetObjectItemCaseSensitive(root, "id");
    title = cJSON_GetObjectItemCaseSensitive(root, "title");
    wav_path = cJSON_GetObjectItemCaseSensitive(root, "wav_path");
    if (!cJSON_IsString(id) || !cJSON_IsString(title) || !cJSON_IsString(wav_path)) {
        return false;
    }

    memset(note, 0, sizeof(*note));
    snprintf(note->id, sizeof(note->id), "%s", id->valuestring);
    snprintf(note->title, sizeof(note->title), "%s", title->valuestring);
    snprintf(note->wav_path, sizeof(note->wav_path), "%s", wav_path->valuestring);

    text = cJSON_GetObjectItemCaseSensitive(root, "text");
    last_error = cJSON_GetObjectItemCaseSensitive(root, "last_error");
    note->created_at_epoch_s = (uint32_t)cJSON_GetNumberValue(
        cJSON_GetObjectItemCaseSensitive(root, "created_at_epoch_s"));
    note->updated_at_epoch_s = (uint32_t)cJSON_GetNumberValue(
        cJSON_GetObjectItemCaseSensitive(root, "updated_at_epoch_s"));
    note->status = (voice_note_status_t)cJSON_GetNumberValue(
        cJSON_GetObjectItemCaseSensitive(root, "status"));
    note->transcript_state = (voice_note_transcript_state_t)cJSON_GetNumberValue(
        cJSON_GetObjectItemCaseSensitive(root, "transcript_state"));
    note->duration_ms = (uint32_t)cJSON_GetNumberValue(
        cJSON_GetObjectItemCaseSensitive(root, "duration_ms"));
    note->sample_rate = (uint32_t)cJSON_GetNumberValue(
        cJSON_GetObjectItemCaseSensitive(root, "sample_rate"));
    note->channels = (uint16_t)cJSON_GetNumberValue(
        cJSON_GetObjectItemCaseSensitive(root, "channels"));
    note->bits_per_sample = (uint16_t)cJSON_GetNumberValue(
        cJSON_GetObjectItemCaseSensitive(root, "bits_per_sample"));
    if (cJSON_IsString(text)) {
        snprintf(note->text, sizeof(note->text), "%s", text->valuestring);
    }
    if (cJSON_IsString(last_error)) {
        snprintf(note->last_error, sizeof(note->last_error), "%s", last_error->valuestring);
    }
    return true;
}

static esp_err_t write_note_file(const char *path, const voice_note_note_t *note)
{
    FILE *fp = NULL;
    cJSON *root = NULL;
    char *text = NULL;

    root = note_to_json(note);
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    text = cJSON_PrintUnformatted(root);
    if (text == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    fp = fopen(path, "wb");
    if (fp == NULL) {
        cJSON_free(text);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    fwrite(text, 1U, strlen(text), fp);
    fclose(fp);
    cJSON_free(text);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t voice_note_store_init(void)
{
    s_note_count = 0U;
    memset(s_notes, 0, sizeof(s_notes));
    return ensure_voice_note_dir();
}

esp_err_t voice_note_store_reload(void)
{
    return voice_note_store_init();
}

size_t voice_note_store_count(void)
{
    return s_note_count;
}

bool voice_note_store_copy_summaries(
    voice_note_tab_t tab,
    voice_note_note_t *notes,
    size_t capacity,
    size_t *count_out)
{
    size_t count = 0U;
    size_t i = 0U;

    if (count_out != NULL) {
        *count_out = 0U;
    }
    if (notes == NULL) {
        return false;
    }
    for (i = 0U; i < s_note_count && count < capacity; ++i) {
        if (!note_matches_tab(&s_notes[i], tab)) {
            continue;
        }
        notes[count++] = s_notes[i];
    }
    if (count_out != NULL) {
        *count_out = count;
    }
    return true;
}

bool voice_note_store_find_note(const char *note_id, voice_note_note_t *out_note)
{
    size_t i = 0U;

    if (note_id == NULL || out_note == NULL) {
        return false;
    }
    for (i = 0U; i < s_note_count; ++i) {
        if (strcmp(s_notes[i].id, note_id) == 0) {
            *out_note = s_notes[i];
            return true;
        }
    }
    return false;
}

esp_err_t voice_note_store_create_processing_note(const voice_note_note_t *note)
{
    char path[VOICE_NOTE_PATH_LENGTH];

    if (note == NULL || s_note_count >= VOICE_NOTE_MAX_NOTES) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(path, sizeof(path), "%s/%s.json", VOICE_NOTE_ROOT_DIR, note->id);
    s_notes[s_note_count++] = *note;
    return write_note_file(path, note);
}

esp_err_t voice_note_store_update_note(const voice_note_note_t *note)
{
    size_t i = 0U;
    char path[VOICE_NOTE_PATH_LENGTH];

    if (note == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (i = 0U; i < s_note_count; ++i) {
        if (strcmp(s_notes[i].id, note->id) == 0) {
            s_notes[i] = *note;
            snprintf(path, sizeof(path), "%s/%s.json", VOICE_NOTE_ROOT_DIR, note->id);
            return write_note_file(path, note);
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t voice_note_store_delete_note(const char *note_id)
{
    size_t i = 0U;
    char json_path[VOICE_NOTE_PATH_LENGTH];

    if (note_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (i = 0U; i < s_note_count; ++i) {
        if (strcmp(s_notes[i].id, note_id) != 0) {
            continue;
        }
        snprintf(json_path, sizeof(json_path), "%s/%s.json", VOICE_NOTE_ROOT_DIR, note_id);
        remove(json_path);
        remove(s_notes[i].wav_path);
        memmove(&s_notes[i], &s_notes[i + 1U], (s_note_count - i - 1U) * sizeof(s_notes[0]));
        s_note_count--;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

bool voice_note_store_self_test(void)
{
    voice_note_note_t note;
    voice_note_note_t copy;
    cJSON *json = NULL;
    char *json_text = NULL;

    memset(&note, 0, sizeof(note));
    snprintf(note.id, sizeof(note.id), "%s", "note_test");
    snprintf(note.title, sizeof(note.title), "%s", "测试便签");
    snprintf(note.wav_path, sizeof(note.wav_path), "%s", VOICE_NOTE_ROOT_DIR "/note_test.wav");
    note.status = VOICE_NOTE_STATUS_PENDING;
    note.transcript_state = VOICE_NOTE_TRANSCRIPT_PROCESSING;

    json = note_to_json(&note);
    if (json == NULL || !note_from_json(json, &copy)) {
        cJSON_Delete(json);
        return false;
    }
    json_text = cJSON_PrintUnformatted(json);
    cJSON_free(json_text);
    cJSON_Delete(json);

    if (strcmp(copy.id, "note_test") != 0 || strcmp(copy.title, "测试便签") != 0) {
        return false;
    }
    if (voice_note_store_init() != ESP_OK) {
        return false;
    }
    if (voice_note_store_create_processing_note(&note) != ESP_OK) {
        return false;
    }
    if (!voice_note_store_find_note("note_test", &copy)) {
        return false;
    }
    if (strcmp(copy.title, "测试便签") != 0) {
        return false;
    }
    return voice_note_store_delete_note("note_test") == ESP_OK;
}
