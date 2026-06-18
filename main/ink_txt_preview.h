#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define INK_TXT_PREVIEW_TEXT_LINES 4
#define INK_TXT_PREVIEW_LINE_LENGTH 23
#define INK_TXT_PREVIEW_TITLE_LENGTH 20

typedef struct {
    char title[INK_TXT_PREVIEW_TITLE_LENGTH + 1];
    char lines[INK_TXT_PREVIEW_TEXT_LINES][INK_TXT_PREVIEW_LINE_LENGTH + 1];
    char status[INK_TXT_PREVIEW_LINE_LENGTH + 1];
    size_t non_ascii_bytes;
    bool found_file;
} ink_txt_preview_t;

void ink_txt_preview_prepare_default(ink_txt_preview_t *preview);
esp_err_t ink_txt_preview_load_from_dir(const char *mount_point, ink_txt_preview_t *preview);
