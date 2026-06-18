#include "ink_txt_reader.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    INK_TXT_READER_READ_BYTES = 256,
};

static bool is_ascii_printable(unsigned char c)
{
    return c >= 32 && c <= 126;
}

static char sanitize_reader_char(unsigned char c)
{
    if (c == '\t') {
        return ' ';
    }
    if (is_ascii_printable(c)) {
        return (char)c;
    }
    return '#';
}

static void sanitize_ascii_snippet(const char *src, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    size_t out = 0;
    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; ++i) {
        dst[out++] = sanitize_reader_char((unsigned char)src[i]);
    }
    dst[out] = '\0';
}

static void trim_trailing_spaces(char *text)
{
    size_t len = strlen(text);
    while (len > 0 && text[len - 1] == ' ') {
        text[--len] = '\0';
    }
}

static bool advance_position(
    ink_txt_reader_t *reader,
    size_t *page_index,
    size_t *line_index,
    size_t *column)
{
    trim_trailing_spaces(reader->pages[*page_index][*line_index]);

    ++(*line_index);
    *column = 0;
    if (*line_index < INK_TXT_READER_BODY_LINES) {
        return true;
    }

    ++(*page_index);
    *line_index = 0;
    if (*page_index >= INK_TXT_READER_MAX_PAGES) {
        reader->truncated = true;
        *page_index = INK_TXT_READER_MAX_PAGES - 1;
        *line_index = INK_TXT_READER_BODY_LINES - 1;
        *column = strlen(reader->pages[*page_index][*line_index]);
        return false;
    }

    if (reader->page_count < *page_index + 1) {
        reader->page_count = *page_index + 1;
    }
    return true;
}

static bool append_reader_char(
    ink_txt_reader_t *reader,
    size_t *page_index,
    size_t *line_index,
    size_t *column,
    unsigned char raw)
{
    if (raw == '\r') {
        return true;
    }

    if (raw == '\n') {
        return advance_position(reader, page_index, line_index, column);
    }

    if (*column >= INK_TXT_READER_LINE_LENGTH
        && !advance_position(reader, page_index, line_index, column)) {
        return false;
    }

    char *line = reader->pages[*page_index][*line_index];
    line[(*column)++] = sanitize_reader_char(raw);
    line[*column] = '\0';
    return true;
}

static void finish_reader_load(ink_txt_reader_t *reader, bool exhausted)
{
    for (size_t page = 0; page < reader->page_count; ++page) {
        for (size_t row = 0; row < INK_TXT_READER_BODY_LINES; ++row) {
            trim_trailing_spaces(reader->pages[page][row]);
        }
    }

    if (reader->pages[0][0][0] == '\0') {
        strcpy(reader->pages[0][0], exhausted ? "EMPTY TXT FILE" : "NO DISPLAY DATA");
    }
}

static void load_reader_bytes(
    ink_txt_reader_t *reader,
    const uint8_t *raw,
    size_t length,
    size_t *page_index,
    size_t *line_index,
    size_t *column)
{
    for (size_t i = 0; i < length && !reader->truncated; ++i) {
        if (raw[i] >= 128) {
            ++reader->non_ascii_bytes;
        }
        if (!append_reader_char(reader, page_index, line_index, column, raw[i])) {
            break;
        }
    }
}

void ink_txt_reader_prepare_default(ink_txt_reader_t *reader)
{
    if (reader == NULL) {
        return;
    }

    memset(reader, 0, sizeof(*reader));
    strcpy(reader->title, "TXT READER");
    strcpy(reader->pages[0][0], "NO TXT LOADED");
    reader->page_count = 1;
}

esp_err_t ink_txt_reader_load_file(ink_txt_reader_t *reader, const char *path)
{
    if (reader == NULL || path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ink_txt_reader_prepare_default(reader);

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        strcpy(reader->pages[0][0], "OPEN FAILED");
        snprintf(reader->pages[0][1], sizeof(reader->pages[0][1]), "ERR %d", errno);
        return ESP_FAIL;
    }

    snprintf(reader->path, sizeof(reader->path), "%s", path);
    const char *name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;
    sanitize_ascii_snippet(name, reader->title, sizeof(reader->title));
    reader->loaded = true;
    reader->page_count = 1;

    uint8_t raw[INK_TXT_READER_READ_BYTES];
    size_t page_index = 0;
    size_t line_index = 0;
    size_t column = 0;
    bool exhausted = false;

    while (!reader->truncated) {
        size_t read_bytes = fread(raw, 1, sizeof(raw), file);
        if (read_bytes == 0) {
            exhausted = true;
            break;
        }

        load_reader_bytes(reader, raw, read_bytes, &page_index, &line_index, &column);

        if (read_bytes < sizeof(raw)) {
            exhausted = true;
            break;
        }
    }

    fclose(file);
    finish_reader_load(reader, exhausted);

    return ESP_OK;
}

bool ink_txt_reader_page_previous(ink_txt_reader_t *reader)
{
    if (reader == NULL || reader->current_page == 0) {
        return false;
    }

    --reader->current_page;
    return true;
}

bool ink_txt_reader_page_next(ink_txt_reader_t *reader)
{
    if (reader == NULL || reader->current_page + 1 >= reader->page_count) {
        return false;
    }

    ++reader->current_page;
    return true;
}

void ink_txt_reader_render(const ink_txt_reader_t *reader, ink_txt_reader_view_t *view)
{
    if (reader == NULL || view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    strcpy(view->title, reader->title);
    for (size_t row = 0; row < INK_TXT_READER_BODY_LINES; ++row) {
        strcpy(view->lines[row], reader->pages[reader->current_page][row]);
    }
    if (!reader->loaded) {
        strcpy(view->status, "BACK TO BROWSER");
    } else if (reader->truncated) {
        snprintf(
            view->status,
            sizeof(view->status),
            "P%03u/%03u TRUNC",
            (unsigned)(reader->current_page + 1),
            (unsigned)reader->page_count);
    } else if (reader->non_ascii_bytes > 0) {
        snprintf(
            view->status,
            sizeof(view->status),
            "P%03u/%03u UTF8->#",
            (unsigned)(reader->current_page + 1),
            (unsigned)reader->page_count);
    } else {
        snprintf(
            view->status,
            sizeof(view->status),
            "P%03u/%03u ASCII",
            (unsigned)(reader->current_page + 1),
            (unsigned)reader->page_count);
    }
}

bool ink_txt_reader_self_test(void)
{
    static ink_txt_reader_t reader;
    static ink_txt_reader_view_t view;
    static const uint8_t sample[] =
        "Page 1 line A\n"
        "Page 1 line B\n"
        "Page 1 line C\n"
        "Page 1 line D\n"
        "Page 2 line E\n"
        "7 and UTF-8: \xE4\xB8\xAD\n";

    ink_txt_reader_prepare_default(&reader);
    ink_txt_reader_render(&reader, &view);
    if (strcmp(view.title, "TXT READER") != 0) {
        return false;
    }
    if (strcmp(view.lines[0], "NO TXT LOADED") != 0) {
        return false;
    }
    if (strcmp(view.status, "BACK TO BROWSER") != 0) {
        return false;
    }

    ink_txt_reader_prepare_default(&reader);
    strcpy(reader.title, "SAMPLE.TXT");
    reader.loaded = true;
    size_t page_index = 0;
    size_t line_index = 0;
    size_t column = 0;
    load_reader_bytes(&reader, sample, sizeof(sample) - 1U, &page_index, &line_index, &column);
    finish_reader_load(&reader, true);

    ink_txt_reader_render(&reader, &view);
    if (strcmp(view.title, "SAMPLE.TXT") != 0) {
        return false;
    }
    if (strcmp(view.lines[0], "Page 1 line A") != 0) {
        return false;
    }
    if (strcmp(view.lines[3], "Page 1 line D") != 0) {
        return false;
    }
    if (strcmp(view.status, "P001/002 UTF8->#") != 0) {
        return false;
    }

    if (!ink_txt_reader_page_next(&reader)) {
        return false;
    }
    ink_txt_reader_render(&reader, &view);
    if (strcmp(view.lines[0], "Page 2 line E") != 0) {
        return false;
    }
    if (strcmp(view.lines[1], "7 and UTF-8: ###") != 0) {
        return false;
    }
    if (strcmp(view.status, "P002/002 UTF8->#") != 0) {
        return false;
    }

    if (!ink_txt_reader_page_previous(&reader)) {
        return false;
    }
    if (ink_txt_reader_page_previous(&reader)) {
        return false;
    }

    return true;
}
