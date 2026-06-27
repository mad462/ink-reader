#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define INK_FILE_BROWSER_MAX_ENTRIES 32
#define INK_FILE_BROWSER_NAME_LENGTH 95
#define INK_FILE_BROWSER_PATH_LENGTH 255
#define INK_FILE_BROWSER_VISIBLE_LINES 4

typedef enum {
    INK_FILE_BROWSER_ENTRY_NONE = 0,
    INK_FILE_BROWSER_ENTRY_DIRECTORY,
    INK_FILE_BROWSER_ENTRY_XTC
} ink_file_browser_entry_type_t;

typedef struct {
    ink_file_browser_entry_type_t type;
    char name[INK_FILE_BROWSER_NAME_LENGTH + 1];
    char full_path[INK_FILE_BROWSER_PATH_LENGTH + 1];
} ink_file_browser_entry_t;

typedef struct {
    char mount_point[32];
    char current_path[INK_FILE_BROWSER_PATH_LENGTH + 1];
    ink_file_browser_entry_t entries[INK_FILE_BROWSER_MAX_ENTRIES];
    size_t entry_count;
    size_t selected_index;
    size_t visible_offset;
    bool selected_file_ready;
    ink_file_browser_entry_type_t selected_file_type;
    char selected_file_path[INK_FILE_BROWSER_PATH_LENGTH + 1];
} ink_file_browser_t;

typedef struct {
    char title[INK_FILE_BROWSER_NAME_LENGTH + 1];
    char lines[INK_FILE_BROWSER_VISIBLE_LINES][INK_FILE_BROWSER_NAME_LENGTH + 1];
    char status[INK_FILE_BROWSER_NAME_LENGTH + 1];
} ink_file_browser_view_t;

esp_err_t ink_file_browser_init(
    ink_file_browser_t *browser,
    const char *mount_point,
    const char *initial_path
);
bool ink_file_browser_is_root(const ink_file_browser_t *browser);
bool ink_file_browser_select_index(ink_file_browser_t *browser, size_t index);
const ink_file_browser_entry_t *ink_file_browser_current_entry(const ink_file_browser_t *browser);
bool ink_file_browser_move_previous(ink_file_browser_t *browser);
bool ink_file_browser_move_next(ink_file_browser_t *browser);
esp_err_t ink_file_browser_confirm(ink_file_browser_t *browser, bool *entered_directory, bool *selected_file);
bool ink_file_browser_go_parent(ink_file_browser_t *browser);
void ink_file_browser_render(const ink_file_browser_t *browser, ink_file_browser_view_t *view);
bool ink_file_browser_self_test(void);
