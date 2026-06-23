#include "ink_app_state.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "ink_app_state";
static const uint32_t kStateMagic = 0x49534150U;
static const uint16_t kStateVersion = 2U;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    ink_app_state_t state;
} ink_app_state_disk_t;

typedef struct {
    bool has_open_book;
    ink_app_state_book_kind_t open_book_kind;
    size_t open_book_page;
    char open_book_path[INK_APP_STATE_PATH_LENGTH + 1];
} ink_app_state_disk_v1_state_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    ink_app_state_disk_v1_state_t state;
} ink_app_state_disk_v1_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
} ink_app_state_disk_header_t;

static void copy_path(char *dst, size_t dst_size, const char *src);

static void encode_disk_state(const ink_app_state_t *state, ink_app_state_disk_t *disk)
{
    disk->magic = kStateMagic;
    disk->version = kStateVersion;
    disk->reserved = 0;
    disk->state = *state;
}

static esp_err_t decode_disk_state(const ink_app_state_disk_t *disk, ink_app_state_t *state)
{
    if (disk->magic != kStateMagic || disk->version != kStateVersion) {
        return ESP_ERR_INVALID_VERSION;
    }

    *state = disk->state;
    if (state->open_book_kind != INK_APP_STATE_BOOK_KIND_NONE
        && state->open_book_kind != INK_APP_STATE_BOOK_KIND_XTC) {
        ink_app_state_clear_open_book(state);
    }
    return ESP_OK;
}

static esp_err_t decode_disk_state_v1(const ink_app_state_disk_v1_t *disk, ink_app_state_t *state)
{
    if (disk->magic != kStateMagic || disk->version != 1U) {
        return ESP_ERR_INVALID_VERSION;
    }

    ink_app_state_prepare_default(state);
    state->has_open_book = disk->state.has_open_book;
    state->open_book_kind = disk->state.open_book_kind == INK_APP_STATE_BOOK_KIND_XTC
        ? INK_APP_STATE_BOOK_KIND_XTC
        : INK_APP_STATE_BOOK_KIND_NONE;
    state->open_book_page = disk->state.open_book_page;
    copy_path(state->open_book_path, sizeof(state->open_book_path), disk->state.open_book_path);
    if (state->open_book_kind == INK_APP_STATE_BOOK_KIND_NONE) {
        state->has_open_book = false;
        state->open_book_page = 0U;
        state->open_book_path[0] = '\0';
    }
    return ESP_OK;
}

static void copy_path(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

void ink_app_state_prepare_default(ink_app_state_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
}

void ink_app_state_clear_open_book(ink_app_state_t *state)
{
    ink_app_state_prepare_default(state);
}

void ink_app_state_remember_xtc_open_book(
    ink_app_state_t *state,
    const char *path,
    size_t page_index,
    size_t chapter_index,
    size_t total_pages_snapshot)
{
    if (state == NULL) {
        return;
    }

    state->has_open_book = true;
    state->open_book_kind = INK_APP_STATE_BOOK_KIND_XTC;
    state->open_book_page = page_index;
    state->open_book_chapter = chapter_index;
    state->open_book_total_pages_snapshot = total_pages_snapshot;
    copy_path(state->open_book_path, sizeof(state->open_book_path), path);
}

esp_err_t ink_app_state_load_file(const char *path, ink_app_state_t *state)
{
    FILE *file;
    ink_app_state_disk_header_t header;

    if (state == NULL || path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ink_app_state_prepare_default(state);

    file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (fread(&header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return ESP_FAIL;
    }

    rewind(file);
    if (header.magic != kStateMagic) {
        fclose(file);
        return ESP_ERR_INVALID_VERSION;
    }

    if (header.version == 1U) {
        ink_app_state_disk_v1_t disk_v1;
        if (fread(&disk_v1, 1, sizeof(disk_v1), file) != sizeof(disk_v1)) {
            fclose(file);
            return ESP_FAIL;
        }
        fclose(file);
        return decode_disk_state_v1(&disk_v1, state);
    }

    if (header.version == kStateVersion) {
        ink_app_state_disk_t disk;
        if (fread(&disk, 1, sizeof(disk), file) != sizeof(disk)) {
            fclose(file);
            return ESP_FAIL;
        }
        fclose(file);
        return decode_disk_state(&disk, state);
    }

    fclose(file);
    return ESP_ERR_INVALID_VERSION;
}

esp_err_t ink_app_state_save_file(const char *path, const ink_app_state_t *state)
{
    FILE *file;
    ink_app_state_disk_t disk;

    if (state == NULL || path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    encode_disk_state(state, &disk);

    file = fopen(path, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }

    const size_t written = fwrite(&disk, 1, sizeof(disk), file);
    fclose(file);
    if (written != sizeof(disk)) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool ink_app_state_self_test(void)
{
    ink_app_state_t state;
    ink_app_state_t restored;
    ink_app_state_disk_t disk;
    ink_app_state_disk_v1_t legacy_disk;

    ink_app_state_prepare_default(&state);
    ink_app_state_remember_xtc_open_book(&state, "/sdcard/books/demo.xtc", 11, 4, 321);
    if (!state.has_open_book || state.open_book_kind != INK_APP_STATE_BOOK_KIND_XTC) {
        return false;
    }
    if (state.open_book_page != 11 || state.open_book_chapter != 4) {
        return false;
    }
    if (state.open_book_total_pages_snapshot != 321) {
        return false;
    }
    if (strcmp(state.open_book_path, "/sdcard/books/demo.xtc") != 0) {
        return false;
    }

    encode_disk_state(&state, &disk);
    if (decode_disk_state(&disk, &restored) != ESP_OK) {
        return false;
    }
    if (!restored.has_open_book || restored.open_book_page != 11) {
        return false;
    }
    if (restored.open_book_kind != INK_APP_STATE_BOOK_KIND_XTC) {
        return false;
    }
    if (restored.open_book_chapter != 4 || restored.open_book_total_pages_snapshot != 321) {
        return false;
    }
    if (strcmp(restored.open_book_path, "/sdcard/books/demo.xtc") != 0) {
        return false;
    }

    memset(&legacy_disk, 0, sizeof(legacy_disk));
    legacy_disk.magic = kStateMagic;
    legacy_disk.version = 1U;
    legacy_disk.state.has_open_book = true;
    legacy_disk.state.open_book_kind = (ink_app_state_book_kind_t)1;
    legacy_disk.state.open_book_page = 7U;
    snprintf(legacy_disk.state.open_book_path, sizeof(legacy_disk.state.open_book_path), "%s", "/sdcard/books/demo.txt");
    if (decode_disk_state_v1(&legacy_disk, &restored) != ESP_OK) {
        return false;
    }
    if (restored.has_open_book || restored.open_book_kind != INK_APP_STATE_BOOK_KIND_NONE) {
        return false;
    }
    ESP_LOGI(TAG, "state self-test passed");
    return true;
}
