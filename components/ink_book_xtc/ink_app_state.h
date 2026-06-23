#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define INK_APP_STATE_PATH_LENGTH 255

typedef enum {
    INK_APP_STATE_BOOK_KIND_NONE = 0,
    INK_APP_STATE_BOOK_KIND_XTC = 3,
} ink_app_state_book_kind_t;

typedef struct {
    bool has_open_book;
    ink_app_state_book_kind_t open_book_kind;
    size_t open_book_page;
    size_t open_book_chapter;
    size_t open_book_total_pages_snapshot;
    char open_book_path[INK_APP_STATE_PATH_LENGTH + 1];
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
esp_err_t ink_app_state_load_file(const char *path, ink_app_state_t *state);
esp_err_t ink_app_state_save_file(const char *path, const ink_app_state_t *state);
bool ink_app_state_self_test(void);
