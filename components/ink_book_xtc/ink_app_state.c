#include "ink_app_state.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "ink_app_state";
static const uint32_t kStateMagic = 0x49534150U;
static const uint16_t kStateVersion = 4U;

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
    size_t open_book_chapter;
    size_t open_book_total_pages_snapshot;
    char open_book_path[INK_APP_STATE_PATH_LENGTH + 1];
    ink_app_state_progress_entry_t progress[INK_APP_STATE_PROGRESS_CAPACITY];
    ink_app_state_bookmark_t bookmarks[INK_APP_STATE_BOOKMARK_CAPACITY];
} ink_app_state_disk_v3_state_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    ink_app_state_disk_v3_state_t state;
} ink_app_state_disk_v3_t;

typedef struct {
    bool has_open_book;
    ink_app_state_book_kind_t open_book_kind;
    size_t open_book_page;
    size_t open_book_chapter;
    size_t open_book_total_pages_snapshot;
    char open_book_path[INK_APP_STATE_PATH_LENGTH + 1];
} ink_app_state_disk_v2_state_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    ink_app_state_disk_v2_state_t state;
} ink_app_state_disk_v2_t;

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
static void copy_text(char *dst, size_t dst_size, const char *src);
static bool progress_matches_path(
    const ink_app_state_progress_entry_t *entry,
    const char *path);
static int find_progress_slot(const ink_app_state_t *state, const char *path);
static int find_free_progress_slot(const ink_app_state_t *state);
static bool bookshelf_matches_path(
    const ink_app_state_bookshelf_entry_t *entry,
    const char *path);
static int find_bookshelf_slot(const ink_app_state_t *state, const char *path);
static int find_free_bookshelf_slot(const ink_app_state_t *state);
static int find_oldest_bookshelf_slot(const ink_app_state_t *state);
static ink_app_state_bookshelf_entry_t *upsert_bookshelf_entry(
    ink_app_state_t *state,
    const char *path,
    const char *title);
static bool bookmark_matches_path_and_page(
    const ink_app_state_bookmark_t *bookmark,
    const char *path,
    size_t page_index);
static int find_bookmark_slot(
    const ink_app_state_t *state,
    const char *path,
    size_t page_index);
static int find_free_bookmark_slot(const ink_app_state_t *state);
static int find_oldest_bookmark_slot(const ink_app_state_t *state);
static void migrate_open_book_to_bookshelf(ink_app_state_t *state);
static void migrate_progress_to_bookshelf(ink_app_state_t *state);

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
    if (state->has_open_book
        && state->open_book_kind == INK_APP_STATE_BOOK_KIND_XTC
        && state->open_book_path[0] != '\0') {
        (void)ink_app_state_remember_xtc_progress(
            state,
            state->open_book_path,
            state->open_book_page,
            state->open_book_chapter,
            state->open_book_total_pages_snapshot);
    }
    migrate_progress_to_bookshelf(state);
    migrate_open_book_to_bookshelf(state);
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

static esp_err_t decode_disk_state_v2(const ink_app_state_disk_v2_t *disk, ink_app_state_t *state)
{
    if (disk->magic != kStateMagic || disk->version != 2U) {
        return ESP_ERR_INVALID_VERSION;
    }

    ink_app_state_prepare_default(state);
    state->has_open_book = disk->state.has_open_book;
    state->open_book_kind = disk->state.open_book_kind;
    state->open_book_page = disk->state.open_book_page;
    state->open_book_chapter = disk->state.open_book_chapter;
    state->open_book_total_pages_snapshot = disk->state.open_book_total_pages_snapshot;
    copy_path(state->open_book_path, sizeof(state->open_book_path), disk->state.open_book_path);
    if (state->open_book_kind != INK_APP_STATE_BOOK_KIND_XTC) {
        ink_app_state_clear_open_book(state);
    } else if (state->open_book_path[0] != '\0') {
        (void)ink_app_state_remember_xtc_progress(
            state,
            state->open_book_path,
            state->open_book_page,
            state->open_book_chapter,
            state->open_book_total_pages_snapshot);
    }
    migrate_open_book_to_bookshelf(state);
    return ESP_OK;
}

static esp_err_t decode_disk_state_v3(const ink_app_state_disk_v3_t *disk, ink_app_state_t *state)
{
    if (disk->magic != kStateMagic || disk->version != 3U) {
        return ESP_ERR_INVALID_VERSION;
    }

    ink_app_state_prepare_default(state);
    state->has_open_book = disk->state.has_open_book;
    state->open_book_kind = disk->state.open_book_kind;
    state->open_book_page = disk->state.open_book_page;
    state->open_book_chapter = disk->state.open_book_chapter;
    state->open_book_total_pages_snapshot = disk->state.open_book_total_pages_snapshot;
    copy_path(state->open_book_path, sizeof(state->open_book_path), disk->state.open_book_path);
    memcpy(state->progress, disk->state.progress, sizeof(state->progress));
    memcpy(state->bookmarks, disk->state.bookmarks, sizeof(state->bookmarks));
    if (state->open_book_kind != INK_APP_STATE_BOOK_KIND_XTC) {
        ink_app_state_clear_open_book(state);
    }
    migrate_progress_to_bookshelf(state);
    migrate_open_book_to_bookshelf(state);
    return ESP_OK;
}

static bool progress_matches_path(
    const ink_app_state_progress_entry_t *entry,
    const char *path)
{
    return entry != NULL
        && entry->used
        && entry->book_kind == INK_APP_STATE_BOOK_KIND_XTC
        && path != NULL
        && strcmp(entry->book_path, path) == 0;
}

static int find_progress_slot(const ink_app_state_t *state, const char *path)
{
    if (state == NULL || path == NULL || path[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < INK_APP_STATE_PROGRESS_CAPACITY; ++i) {
        if (progress_matches_path(&state->progress[i], path)) {
            return (int)i;
        }
    }
    return -1;
}

static bool bookshelf_matches_path(
    const ink_app_state_bookshelf_entry_t *entry,
    const char *path)
{
    return entry != NULL
        && entry->used
        && entry->book_kind == INK_APP_STATE_BOOK_KIND_XTC
        && path != NULL
        && strcmp(entry->book_path, path) == 0;
}

static int find_bookshelf_slot(const ink_app_state_t *state, const char *path)
{
    if (state == NULL || path == NULL || path[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < INK_APP_STATE_BOOKSHELF_CAPACITY; ++i) {
        if (bookshelf_matches_path(&state->bookshelf[i], path)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_free_bookshelf_slot(const ink_app_state_t *state)
{
    if (state == NULL) {
        return -1;
    }

    for (size_t i = 0; i < INK_APP_STATE_BOOKSHELF_CAPACITY; ++i) {
        if (!state->bookshelf[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static int find_oldest_bookshelf_slot(const ink_app_state_t *state)
{
    size_t best = 0U;

    if (state == NULL) {
        return -1;
    }

    for (size_t i = 1; i < INK_APP_STATE_BOOKSHELF_CAPACITY; ++i) {
        const bool candidate_opened = state->bookshelf[i].has_opened;
        const bool best_opened = state->bookshelf[best].has_opened;
        if (!candidate_opened && best_opened) {
            best = i;
            continue;
        }
        if (candidate_opened == best_opened
            && state->bookshelf[i].recent_order < state->bookshelf[best].recent_order) {
            best = i;
        }
    }
    return (int)best;
}

static ink_app_state_bookshelf_entry_t *upsert_bookshelf_entry(
    ink_app_state_t *state,
    const char *path,
    const char *title)
{
    int slot;
    ink_app_state_bookshelf_entry_t *entry;

    if (state == NULL || path == NULL || path[0] == '\0') {
        return NULL;
    }

    slot = find_bookshelf_slot(state, path);
    if (slot < 0) {
        slot = find_free_bookshelf_slot(state);
    }
    if (slot < 0) {
        slot = find_oldest_bookshelf_slot(state);
    }
    if (slot < 0) {
        return NULL;
    }

    entry = &state->bookshelf[slot];
    if (!entry->used || strcmp(entry->book_path, path) != 0) {
        memset(entry, 0, sizeof(*entry));
        entry->used = true;
        entry->book_kind = INK_APP_STATE_BOOK_KIND_XTC;
        copy_path(entry->book_path, sizeof(entry->book_path), path);
    }
    if (title != NULL && title[0] != '\0') {
        copy_text(entry->title, sizeof(entry->title), title);
    }
    return entry;
}

static int find_free_progress_slot(const ink_app_state_t *state)
{
    if (state == NULL) {
        return -1;
    }

    for (size_t i = 0; i < INK_APP_STATE_PROGRESS_CAPACITY; ++i) {
        if (!state->progress[i].used) {
            return (int)i;
        }
    }
    return 0;
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

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static bool bookmark_matches_path_and_page(
    const ink_app_state_bookmark_t *bookmark,
    const char *path,
    size_t page_index)
{
    return bookmark != NULL
        && bookmark->used
        && bookmark->book_kind == INK_APP_STATE_BOOK_KIND_XTC
        && bookmark->page_index == page_index
        && path != NULL
        && strcmp(bookmark->book_path, path) == 0;
}

static int find_bookmark_slot(
    const ink_app_state_t *state,
    const char *path,
    size_t page_index)
{
    if (state == NULL || path == NULL || path[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < INK_APP_STATE_BOOKMARK_CAPACITY; ++i) {
        if (bookmark_matches_path_and_page(&state->bookmarks[i], path, page_index)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_free_bookmark_slot(const ink_app_state_t *state)
{
    if (state == NULL) {
        return -1;
    }

    for (size_t i = 0; i < INK_APP_STATE_BOOKMARK_CAPACITY; ++i) {
        if (!state->bookmarks[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static int find_oldest_bookmark_slot(const ink_app_state_t *state)
{
    size_t best = 0U;

    if (state == NULL) {
        return -1;
    }

    for (size_t i = 1; i < INK_APP_STATE_BOOKMARK_CAPACITY; ++i) {
        if (strcmp(
                state->bookmarks[i].timestamp_text,
                state->bookmarks[best].timestamp_text) < 0) {
            best = i;
        }
    }
    return (int)best;
}

static void migrate_open_book_to_bookshelf(ink_app_state_t *state)
{
    if (state == NULL
        || !state->has_open_book
        || state->open_book_kind != INK_APP_STATE_BOOK_KIND_XTC
        || state->open_book_path[0] == '\0') {
        return;
    }

    (void)ink_app_state_note_xtc_opened(
        state,
        state->open_book_path,
        NULL,
        state->open_book_page,
        state->open_book_chapter,
        state->open_book_total_pages_snapshot,
        NULL);
}

static void migrate_progress_to_bookshelf(ink_app_state_t *state)
{
    if (state == NULL) {
        return;
    }

    for (size_t i = 0; i < INK_APP_STATE_PROGRESS_CAPACITY; ++i) {
        const ink_app_state_progress_entry_t *progress = &state->progress[i];
        if (!progress->used
            || progress->book_kind != INK_APP_STATE_BOOK_KIND_XTC
            || progress->book_path[0] == '\0') {
            continue;
        }
        (void)ink_app_state_note_xtc_opened(
            state,
            progress->book_path,
            NULL,
            progress->page_index,
            progress->chapter_index,
            progress->total_pages_snapshot,
            NULL);
    }
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
    if (state == NULL) {
        return;
    }

    state->has_open_book = false;
    state->open_book_kind = INK_APP_STATE_BOOK_KIND_NONE;
    state->open_book_page = 0U;
    state->open_book_chapter = 0U;
    state->open_book_total_pages_snapshot = 0U;
    state->open_book_path[0] = '\0';
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
    (void)ink_app_state_remember_xtc_progress(
        state,
        path,
        page_index,
        chapter_index,
        total_pages_snapshot);
}

bool ink_app_state_remember_xtc_progress(
    ink_app_state_t *state,
    const char *path,
    size_t page_index,
    size_t chapter_index,
    size_t total_pages_snapshot)
{
    int slot;
    ink_app_state_progress_entry_t *entry;

    if (state == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    slot = find_progress_slot(state, path);
    if (slot < 0) {
        slot = find_free_progress_slot(state);
    }
    if (slot < 0) {
        return false;
    }

    entry = &state->progress[slot];
    memset(entry, 0, sizeof(*entry));
    entry->used = true;
    entry->book_kind = INK_APP_STATE_BOOK_KIND_XTC;
    entry->page_index = page_index;
    entry->chapter_index = chapter_index;
    entry->total_pages_snapshot = total_pages_snapshot;
    copy_path(entry->book_path, sizeof(entry->book_path), path);
    return true;
}

bool ink_app_state_find_xtc_progress(
    const ink_app_state_t *state,
    const char *path,
    size_t *page_index_out,
    size_t *chapter_index_out,
    size_t *total_pages_snapshot_out)
{
    const int slot = find_progress_slot(state, path);
    const ink_app_state_progress_entry_t *entry;

    if (slot < 0) {
        return false;
    }

    entry = &state->progress[slot];
    if (page_index_out != NULL) {
        *page_index_out = entry->page_index;
    }
    if (chapter_index_out != NULL) {
        *chapter_index_out = entry->chapter_index;
    }
    if (total_pages_snapshot_out != NULL) {
        *total_pages_snapshot_out = entry->total_pages_snapshot;
    }
    return true;
}

bool ink_app_state_note_xtc_opened(
    ink_app_state_t *state,
    const char *path,
    const char *title,
    size_t page_index,
    size_t chapter_index,
    size_t total_pages_snapshot,
    const char *chapter_title)
{
    ink_app_state_bookshelf_entry_t *entry;

    if (state == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    entry = upsert_bookshelf_entry(state, path, title);
    if (entry == NULL) {
        return false;
    }

    state->recent_order_counter++;
    if (state->recent_order_counter == 0U) {
        state->recent_order_counter = 1U;
    }

    entry->has_opened = true;
    entry->recent_order = state->recent_order_counter;
    entry->page_index = page_index;
    entry->chapter_index = chapter_index;
    entry->total_pages_snapshot = total_pages_snapshot;
    if (chapter_title != NULL && chapter_title[0] != '\0') {
        copy_text(entry->chapter_title, sizeof(entry->chapter_title), chapter_title);
    }
    return true;
}

bool ink_app_state_set_xtc_favorite(
    ink_app_state_t *state,
    const char *path,
    const char *title,
    bool is_favorite)
{
    ink_app_state_bookshelf_entry_t *entry;

    if (state == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    entry = upsert_bookshelf_entry(state, path, title);
    if (entry == NULL) {
        return false;
    }

    entry->is_favorite = is_favorite;
    return true;
}

bool ink_app_state_find_xtc_bookshelf_entry(
    const ink_app_state_t *state,
    const char *path,
    size_t *entry_index_out)
{
    const int slot = find_bookshelf_slot(state, path);

    if (entry_index_out != NULL) {
        *entry_index_out = slot >= 0 ? (size_t)slot : 0U;
    }
    return slot >= 0;
}

const ink_app_state_bookshelf_entry_t *ink_app_state_bookshelf_entry_at(
    const ink_app_state_t *state,
    size_t entry_index)
{
    if (state == NULL || entry_index >= INK_APP_STATE_BOOKSHELF_CAPACITY) {
        return NULL;
    }
    return state->bookshelf[entry_index].used ? &state->bookshelf[entry_index] : NULL;
}

bool ink_app_state_add_or_replace_xtc_bookmark(
    ink_app_state_t *state,
    const char *path,
    size_t page_index,
    size_t chapter_index,
    size_t total_pages_snapshot,
    const char *chapter_title,
    const char *timestamp_text)
{
    int slot;
    ink_app_state_bookmark_t *bookmark;

    if (state == NULL || path == NULL || path[0] == '\0' || timestamp_text == NULL || timestamp_text[0] == '\0') {
        return false;
    }

    slot = find_bookmark_slot(state, path, page_index);
    if (slot < 0) {
        slot = find_free_bookmark_slot(state);
    }
    if (slot < 0) {
        slot = find_oldest_bookmark_slot(state);
    }
    if (slot < 0) {
        return false;
    }

    bookmark = &state->bookmarks[slot];
    memset(bookmark, 0, sizeof(*bookmark));
    bookmark->used = true;
    bookmark->book_kind = INK_APP_STATE_BOOK_KIND_XTC;
    bookmark->page_index = page_index;
    bookmark->chapter_index = chapter_index;
    bookmark->total_pages_snapshot = total_pages_snapshot;
    copy_path(bookmark->book_path, sizeof(bookmark->book_path), path);
    copy_text(bookmark->chapter_title, sizeof(bookmark->chapter_title), chapter_title);
    copy_text(bookmark->timestamp_text, sizeof(bookmark->timestamp_text), timestamp_text);
    return true;
}

bool ink_app_state_overwrite_xtc_bookmark_at(
    ink_app_state_t *state,
    size_t bookmark_index,
    const char *path,
    size_t page_index,
    size_t chapter_index,
    size_t total_pages_snapshot,
    const char *chapter_title,
    const char *timestamp_text)
{
    ink_app_state_bookmark_t *bookmark;

    if (state == NULL
        || bookmark_index >= INK_APP_STATE_BOOKMARK_CAPACITY
        || path == NULL
        || path[0] == '\0'
        || timestamp_text == NULL
        || timestamp_text[0] == '\0') {
        return false;
    }

    bookmark = &state->bookmarks[bookmark_index];
    memset(bookmark, 0, sizeof(*bookmark));
    bookmark->used = true;
    bookmark->book_kind = INK_APP_STATE_BOOK_KIND_XTC;
    bookmark->page_index = page_index;
    bookmark->chapter_index = chapter_index;
    bookmark->total_pages_snapshot = total_pages_snapshot;
    copy_path(bookmark->book_path, sizeof(bookmark->book_path), path);
    copy_text(bookmark->chapter_title, sizeof(bookmark->chapter_title), chapter_title);
    copy_text(bookmark->timestamp_text, sizeof(bookmark->timestamp_text), timestamp_text);
    return true;
}

bool ink_app_state_remove_xtc_bookmark(
    ink_app_state_t *state,
    const char *path,
    size_t page_index)
{
    const int slot = find_bookmark_slot(state, path, page_index);
    if (slot < 0) {
        return false;
    }

    memset(&state->bookmarks[slot], 0, sizeof(state->bookmarks[slot]));
    return true;
}

bool ink_app_state_find_xtc_bookmark(
    const ink_app_state_t *state,
    const char *path,
    size_t page_index,
    size_t *bookmark_index_out)
{
    const int slot = find_bookmark_slot(state, path, page_index);
    if (bookmark_index_out != NULL) {
        *bookmark_index_out = slot >= 0 ? (size_t)slot : 0U;
    }
    return slot >= 0;
}

size_t ink_app_state_count_bookmarks_for_path(
    const ink_app_state_t *state,
    const char *path)
{
    size_t count = 0U;

    if (state == NULL || path == NULL || path[0] == '\0') {
        return 0U;
    }

    for (size_t i = 0; i < INK_APP_STATE_BOOKMARK_CAPACITY; ++i) {
        if (state->bookmarks[i].used && strcmp(state->bookmarks[i].book_path, path) == 0) {
            ++count;
        }
    }
    return count;
}

const ink_app_state_bookmark_t *ink_app_state_bookmark_at(
    const ink_app_state_t *state,
    size_t bookmark_index)
{
    if (state == NULL || bookmark_index >= INK_APP_STATE_BOOKMARK_CAPACITY) {
        return NULL;
    }
    return state->bookmarks[bookmark_index].used ? &state->bookmarks[bookmark_index] : NULL;
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
        ink_app_state_disk_v1_t *disk_v1 = malloc(sizeof(*disk_v1));
        esp_err_t ret;
        if (disk_v1 == NULL) {
            fclose(file);
            return ESP_ERR_NO_MEM;
        }
        if (fread(disk_v1, 1, sizeof(*disk_v1), file) != sizeof(*disk_v1)) {
            free(disk_v1);
            fclose(file);
            return ESP_FAIL;
        }
        fclose(file);
        ret = decode_disk_state_v1(disk_v1, state);
        free(disk_v1);
        return ret;
    }

    if (header.version == 2U) {
        ink_app_state_disk_v2_t *disk_v2 = malloc(sizeof(*disk_v2));
        esp_err_t ret;
        if (disk_v2 == NULL) {
            fclose(file);
            return ESP_ERR_NO_MEM;
        }
        if (fread(disk_v2, 1, sizeof(*disk_v2), file) != sizeof(*disk_v2)) {
            free(disk_v2);
            fclose(file);
            return ESP_FAIL;
        }
        fclose(file);
        ret = decode_disk_state_v2(disk_v2, state);
        free(disk_v2);
        return ret;
    }

    if (header.version == 3U) {
        ink_app_state_disk_v3_t *disk_v3 = malloc(sizeof(*disk_v3));
        esp_err_t ret;
        if (disk_v3 == NULL) {
            fclose(file);
            return ESP_ERR_NO_MEM;
        }
        if (fread(disk_v3, 1, sizeof(*disk_v3), file) != sizeof(*disk_v3)) {
            free(disk_v3);
            fclose(file);
            return ESP_FAIL;
        }
        fclose(file);
        ret = decode_disk_state_v3(disk_v3, state);
        free(disk_v3);
        return ret;
    }

    if (header.version == kStateVersion) {
        ink_app_state_disk_t *disk = malloc(sizeof(*disk));
        esp_err_t ret;
        if (disk == NULL) {
            fclose(file);
            return ESP_ERR_NO_MEM;
        }
        if (fread(disk, 1, sizeof(*disk), file) != sizeof(*disk)) {
            free(disk);
            fclose(file);
            return ESP_FAIL;
        }
        fclose(file);
        ret = decode_disk_state(disk, state);
        free(disk);
        return ret;
    }

    fclose(file);
    return ESP_ERR_INVALID_VERSION;
}

esp_err_t ink_app_state_save_file(const char *path, const ink_app_state_t *state)
{
    FILE *file;
    ink_app_state_disk_t *disk;
    esp_err_t ret = ESP_OK;

    if (state == NULL || path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    disk = malloc(sizeof(*disk));
    if (disk == NULL) {
        return ESP_ERR_NO_MEM;
    }

    encode_disk_state(state, disk);

    file = fopen(path, "wb");
    if (file == NULL) {
        free(disk);
        return ESP_FAIL;
    }

    const size_t written = fwrite(disk, 1, sizeof(*disk), file);
    fclose(file);
    if (written != sizeof(*disk)) {
        ret = ESP_FAIL;
    }

    free(disk);
    return ret;
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
    if (!ink_app_state_find_xtc_progress(&restored, "/sdcard/books/demo.xtc", NULL, NULL, NULL)) {
        return false;
    }
    if (!ink_app_state_note_xtc_opened(
            &state,
            "/sdcard/books/demo.xtc",
            "演示图书",
            11U,
            4U,
            321U,
            "第四章")) {
        return false;
    }
    if (!ink_app_state_set_xtc_favorite(&state, "/sdcard/books/demo.xtc", "演示图书", true)) {
        return false;
    }
    {
        size_t shelf_index = 0U;
        const ink_app_state_bookshelf_entry_t *shelf = NULL;
        if (!ink_app_state_find_xtc_bookshelf_entry(&state, "/sdcard/books/demo.xtc", &shelf_index)) {
            return false;
        }
        shelf = ink_app_state_bookshelf_entry_at(&state, shelf_index);
        if (shelf == NULL
            || !shelf->has_opened
            || !shelf->is_favorite
            || shelf->page_index != 11U
            || strcmp(shelf->title, "演示图书") != 0
            || strcmp(shelf->chapter_title, "第四章") != 0) {
            return false;
        }
    }

    if (!ink_app_state_remember_xtc_progress(&state, "/sdcard/books/other.xtc", 88U, 9U, 654U)) {
        return false;
    }
    {
        size_t page = 0U;
        size_t chapter = 0U;
        size_t total = 0U;
        if (!ink_app_state_find_xtc_progress(&state, "/sdcard/books/other.xtc", &page, &chapter, &total)) {
            return false;
        }
        if (page != 88U || chapter != 9U || total != 654U) {
            return false;
        }
    }

    if (!ink_app_state_add_or_replace_xtc_bookmark(
            &state,
            "/sdcard/books/demo.xtc",
            12U,
            4U,
            321U,
            "第二章",
            "2026-06-23 10:21")) {
        return false;
    }
    if (ink_app_state_count_bookmarks_for_path(&state, "/sdcard/books/demo.xtc") != 1U) {
        return false;
    }
    if (!ink_app_state_find_xtc_bookmark(&state, "/sdcard/books/demo.xtc", 12U, NULL)) {
        return false;
    }
    if (state.bookmarks[0].chapter_index != 4U
        || strcmp(state.bookmarks[0].chapter_title, "第二章") != 0
        || strcmp(state.bookmarks[0].timestamp_text, "2026-06-23 10:21") != 0) {
        return false;
    }
    if (!ink_app_state_remove_xtc_bookmark(&state, "/sdcard/books/demo.xtc", 12U)) {
        return false;
    }
    if (ink_app_state_find_xtc_bookmark(&state, "/sdcard/books/demo.xtc", 12U, NULL)) {
        return false;
    }

    if (!ink_app_state_add_or_replace_xtc_bookmark(
            &state,
            "/sdcard/books/demo.xtc",
            14U,
            5U,
            321U,
            "第三章",
            "2026-06-23 10:30")) {
        return false;
    }
    if (!ink_app_state_overwrite_xtc_bookmark_at(
            &state,
            0U,
            "/sdcard/books/demo.xtc",
            99U,
            8U,
            321U,
            "第九章",
            "2026-06-23 10:45")) {
        return false;
    }
    if (state.bookmarks[0].page_index != 99U
        || state.bookmarks[0].chapter_index != 8U
        || strcmp(state.bookmarks[0].chapter_title, "第九章") != 0
        || strcmp(state.bookmarks[0].timestamp_text, "2026-06-23 10:45") != 0) {
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
