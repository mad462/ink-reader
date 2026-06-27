#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define INK_APP_STATE_PATH_LENGTH 255
#define INK_APP_STATE_BOOKMARK_CAPACITY 12
#define INK_APP_STATE_PROGRESS_CAPACITY 16
#define INK_APP_STATE_BOOKSHELF_CAPACITY 32
#define INK_APP_STATE_BOOKMARK_TITLE_LENGTH 80
#define INK_APP_STATE_BOOKMARK_TIME_LENGTH 31
#define INK_APP_STATE_BOOK_TITLE_LENGTH 80

typedef enum {
    INK_APP_STATE_BOOK_KIND_NONE = 0,
    INK_APP_STATE_BOOK_KIND_XTC = 3,
} ink_app_state_book_kind_t;

typedef struct {
    bool used;
    ink_app_state_book_kind_t book_kind;
    size_t page_index;
    size_t chapter_index;
    size_t total_pages_snapshot;
    char book_path[INK_APP_STATE_PATH_LENGTH + 1];
} ink_app_state_progress_entry_t;

typedef struct {
    bool used;
    ink_app_state_book_kind_t book_kind;
    size_t page_index;
    size_t chapter_index;
    size_t total_pages_snapshot;
    char book_path[INK_APP_STATE_PATH_LENGTH + 1];
    char chapter_title[INK_APP_STATE_BOOKMARK_TITLE_LENGTH + 1];
    char timestamp_text[INK_APP_STATE_BOOKMARK_TIME_LENGTH + 1];
} ink_app_state_bookmark_t;

typedef struct {
    bool used;
    bool has_opened;
    bool is_favorite;
    ink_app_state_book_kind_t book_kind;
    uint32_t recent_order;
    size_t page_index;
    size_t chapter_index;
    size_t total_pages_snapshot;
    char book_path[INK_APP_STATE_PATH_LENGTH + 1];
    char title[INK_APP_STATE_BOOK_TITLE_LENGTH + 1];
    char chapter_title[INK_APP_STATE_BOOK_TITLE_LENGTH + 1];
} ink_app_state_bookshelf_entry_t;

typedef struct {
    bool has_open_book;
    ink_app_state_book_kind_t open_book_kind;
    size_t open_book_page;
    size_t open_book_chapter;
    size_t open_book_total_pages_snapshot;
    char open_book_path[INK_APP_STATE_PATH_LENGTH + 1];
    ink_app_state_progress_entry_t progress[INK_APP_STATE_PROGRESS_CAPACITY];
    ink_app_state_bookmark_t bookmarks[INK_APP_STATE_BOOKMARK_CAPACITY];
    uint32_t recent_order_counter;
    ink_app_state_bookshelf_entry_t bookshelf[INK_APP_STATE_BOOKSHELF_CAPACITY];
} ink_app_state_t;

void ink_app_state_prepare_default(ink_app_state_t *state);
void ink_app_state_clear_open_book(ink_app_state_t *state);
void ink_app_state_remember_xtc_open_book(
    ink_app_state_t *state,
    const char *path,
    size_t page_index,
    size_t chapter_index,
    size_t total_pages_snapshot
);
bool ink_app_state_remember_xtc_progress(
    ink_app_state_t *state,
    const char *path,
    size_t page_index,
    size_t chapter_index,
    size_t total_pages_snapshot
);
bool ink_app_state_find_xtc_progress(
    const ink_app_state_t *state,
    const char *path,
    size_t *page_index_out,
    size_t *chapter_index_out,
    size_t *total_pages_snapshot_out
);
bool ink_app_state_add_or_replace_xtc_bookmark(
    ink_app_state_t *state,
    const char *path,
    size_t page_index,
    size_t chapter_index,
    size_t total_pages_snapshot,
    const char *chapter_title,
    const char *timestamp_text
);
bool ink_app_state_overwrite_xtc_bookmark_at(
    ink_app_state_t *state,
    size_t bookmark_index,
    const char *path,
    size_t page_index,
    size_t chapter_index,
    size_t total_pages_snapshot,
    const char *chapter_title,
    const char *timestamp_text
);
bool ink_app_state_remove_xtc_bookmark(
    ink_app_state_t *state,
    const char *path,
    size_t page_index
);
bool ink_app_state_find_xtc_bookmark(
    const ink_app_state_t *state,
    const char *path,
    size_t page_index,
    size_t *bookmark_index_out
);
size_t ink_app_state_count_bookmarks_for_path(
    const ink_app_state_t *state,
    const char *path
);
const ink_app_state_bookmark_t *ink_app_state_bookmark_at(
    const ink_app_state_t *state,
    size_t bookmark_index
);
bool ink_app_state_note_xtc_opened(
    ink_app_state_t *state,
    const char *path,
    const char *title,
    size_t page_index,
    size_t chapter_index,
    size_t total_pages_snapshot,
    const char *chapter_title
);
bool ink_app_state_set_xtc_favorite(
    ink_app_state_t *state,
    const char *path,
    const char *title,
    bool is_favorite
);
bool ink_app_state_find_xtc_bookshelf_entry(
    const ink_app_state_t *state,
    const char *path,
    size_t *entry_index_out
);
const ink_app_state_bookshelf_entry_t *ink_app_state_bookshelf_entry_at(
    const ink_app_state_t *state,
    size_t entry_index
);
esp_err_t ink_app_state_load_file(const char *path, ink_app_state_t *state);
esp_err_t ink_app_state_save_file(const char *path, const ink_app_state_t *state);
bool ink_app_state_self_test(void);
