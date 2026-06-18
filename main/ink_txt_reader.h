#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define INK_TXT_READER_TITLE_LENGTH 20
#define INK_TXT_READER_BODY_LINES 4
#define INK_TXT_READER_LINE_LENGTH 23
#define INK_TXT_READER_MAX_PAGES 128
#define INK_TXT_READER_PATH_LENGTH 255

typedef struct {
    char title[INK_TXT_READER_TITLE_LENGTH + 1];
    char lines[INK_TXT_READER_BODY_LINES][INK_TXT_READER_LINE_LENGTH + 1];
    char status[INK_TXT_READER_LINE_LENGTH + 1];
} ink_txt_reader_view_t;

typedef struct {
    char path[INK_TXT_READER_PATH_LENGTH + 1];
    char title[INK_TXT_READER_TITLE_LENGTH + 1];
    char pages[INK_TXT_READER_MAX_PAGES][INK_TXT_READER_BODY_LINES][INK_TXT_READER_LINE_LENGTH + 1];
    size_t page_count;
    size_t current_page;
    size_t non_ascii_bytes;
    bool truncated;
    bool loaded;
} ink_txt_reader_t;

void ink_txt_reader_prepare_default(ink_txt_reader_t *reader);
esp_err_t ink_txt_reader_load_file(ink_txt_reader_t *reader, const char *path);
bool ink_txt_reader_page_previous(ink_txt_reader_t *reader);
bool ink_txt_reader_page_next(ink_txt_reader_t *reader);
void ink_txt_reader_render(const ink_txt_reader_t *reader, ink_txt_reader_view_t *view);
bool ink_txt_reader_self_test(void);
