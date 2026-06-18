#include "ink_txt_preview.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "ink_txt_preview";

enum {
    INK_TXT_PREVIEW_READ_BYTES = 512,
};

static bool is_ascii_printable(unsigned char c)
{
    return c >= 32 && c <= 126;
}

static char sanitize_preview_char(unsigned char c)
{
    if (c == '\t') {
        return ' ';
    }
    if (is_ascii_printable(c)) {
        return (char)c;
    }
    return '#';
}

static bool ascii_char_equal_ignore_case(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
    }
    return a == b;
}

static bool has_txt_extension(const char *name)
{
    size_t len = strlen(name);
    if (len < 4) {
        return false;
    }

    const char *ext = name + len - 4;
    return ext[0] == '.'
        && ascii_char_equal_ignore_case(ext[1], 't')
        && ascii_char_equal_ignore_case(ext[2], 'x')
        && ascii_char_equal_ignore_case(ext[3], 't');
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
        dst[out++] = sanitize_preview_char((unsigned char)src[i]);
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

void ink_txt_preview_prepare_default(ink_txt_preview_t *preview)
{
    memset(preview, 0, sizeof(*preview));
    strcpy(preview->title, "TXT PREVIEW");
    strcpy(preview->lines[0], "NO TXT FILE FOUND");
    strcpy(preview->lines[1], "PUT A FAT32 TXT IN TF");
    strcpy(preview->lines[2], "UTF-8 CHARS -> #");
    strcpy(preview->status, "WAITING FOR SAMPLE");
}

static esp_err_t find_first_txt_file(
    const char *mount_point,
    char *path,
    size_t path_size,
    char *name,
    size_t name_size)
{
    DIR *dir = opendir(mount_point);
    if (dir == NULL) {
        ESP_LOGE(TAG, "opendir(%s) failed during TXT scan: errno=%d", mount_point, errno);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!has_txt_extension(entry->d_name)) {
            continue;
        }

        snprintf(path, path_size, "%s/%s", mount_point, entry->d_name);
        sanitize_ascii_snippet(entry->d_name, name, name_size);
        ret = ESP_OK;
        break;
    }

    closedir(dir);
    return ret;
}

static void append_preview_char(ink_txt_preview_t *preview, size_t *line_index, size_t *column, char c)
{
    if (*line_index >= INK_TXT_PREVIEW_TEXT_LINES) {
        return;
    }

    if (c == '\r') {
        return;
    }

    if (c == '\n') {
        trim_trailing_spaces(preview->lines[*line_index]);
        ++(*line_index);
        *column = 0;
        return;
    }

    if (*column >= INK_TXT_PREVIEW_LINE_LENGTH) {
        trim_trailing_spaces(preview->lines[*line_index]);
        ++(*line_index);
        *column = 0;
        if (*line_index >= INK_TXT_PREVIEW_TEXT_LINES) {
            return;
        }
    }

    preview->lines[*line_index][(*column)++] = c;
    preview->lines[*line_index][*column] = '\0';
}

esp_err_t ink_txt_preview_load_from_file(const char *path, ink_txt_preview_t *preview)
{
    ink_txt_preview_prepare_default(preview);
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(preview->status, sizeof(preview->status), "OPEN FAIL ERR %d", errno);
        ESP_LOGE(TAG, "fopen(%s) failed: errno=%d", path, errno);
        return ESP_FAIL;
    }

    preview->found_file = true;
    const char *name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;
    sanitize_ascii_snippet(name, preview->title, sizeof(preview->title));
    strcpy(preview->lines[0], "FILE FOUND");

    uint8_t raw[INK_TXT_PREVIEW_READ_BYTES];
    const size_t read_bytes = fread(raw, 1, sizeof(raw), file);
    fclose(file);

    size_t line_index = 1;
    size_t column = 0;
    for (size_t i = 0; i < read_bytes; ++i) {
        const unsigned char c = raw[i];
        if (c >= 128) {
            ++preview->non_ascii_bytes;
        }
        append_preview_char(preview, &line_index, &column, sanitize_preview_char(c));
        if (line_index >= INK_TXT_PREVIEW_TEXT_LINES) {
            break;
        }
    }

    for (size_t i = 0; i < INK_TXT_PREVIEW_TEXT_LINES; ++i) {
        trim_trailing_spaces(preview->lines[i]);
    }

    if (preview->lines[1][0] == '\0') {
        strcpy(preview->lines[1], "EMPTY OR BINARY FILE");
    }

    if (preview->non_ascii_bytes > 0) {
        strcpy(preview->status, "UTF8 CHARS -> #");
    } else {
        strcpy(preview->status, "ASCII TEXT OK");
    }

    ESP_LOGI(TAG, "TXT preview file: %s", path);
    ESP_LOGI(TAG, "TXT preview bytes read: %u", (unsigned)read_bytes);
    ESP_LOGI(TAG, "TXT preview non-ASCII bytes: %u", (unsigned)preview->non_ascii_bytes);
    return ESP_OK;
}

esp_err_t ink_txt_preview_load_from_dir(const char *mount_point, ink_txt_preview_t *preview)
{
    char txt_path[256];
    char txt_name[INK_TXT_PREVIEW_TITLE_LENGTH + 1];

    esp_err_t ret = find_first_txt_file(mount_point, txt_path, sizeof(txt_path), txt_name, sizeof(txt_name));
    if (ret != ESP_OK) {
        ink_txt_preview_prepare_default(preview);
        return ret;
    }

    return ink_txt_preview_load_from_file(txt_path, preview);
}
