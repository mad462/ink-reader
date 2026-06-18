#include "ink_file_browser.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool is_ascii_printable(unsigned char c)
{
    return c >= 32 && c <= 126;
}

static char sanitize_browser_char(unsigned char c)
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
        dst[out++] = sanitize_browser_char((unsigned char)src[i]);
    }
    dst[out] = '\0';
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

static int ascii_casecmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
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

static bool should_skip_name(const char *name)
{
    return name == NULL
        || name[0] == '\0'
        || strcmp(name, ".") == 0
        || strcmp(name, "..") == 0
        || name[0] == '.'
        || strcmp(name, "System Volume Information") == 0;
}

static void join_path(const char *base, const char *name, char *dst, size_t dst_size)
{
    if (strcmp(base, "/") == 0) {
        snprintf(dst, dst_size, "/%s", name);
        return;
    }

    if (base[strlen(base) - 1] == '/') {
        snprintf(dst, dst_size, "%s%s", base, name);
        return;
    }

    snprintf(dst, dst_size, "%s/%s", base, name);
}

static bool browser_is_root(const ink_file_browser_t *browser)
{
    return strcmp(browser->current_path, browser->mount_point) == 0;
}

static void update_visible_offset(ink_file_browser_t *browser)
{
    if (browser->entry_count <= INK_FILE_BROWSER_VISIBLE_LINES) {
        browser->visible_offset = 0;
        return;
    }

    if (browser->selected_index < browser->visible_offset) {
        browser->visible_offset = browser->selected_index;
        return;
    }

    if (browser->selected_index >= browser->visible_offset + INK_FILE_BROWSER_VISIBLE_LINES) {
        browser->visible_offset = browser->selected_index - (INK_FILE_BROWSER_VISIBLE_LINES - 1);
    }
}

static void sort_entries(ink_file_browser_t *browser)
{
    for (size_t i = 0; i < browser->entry_count; ++i) {
        for (size_t j = i + 1; j < browser->entry_count; ++j) {
            bool swap = false;
            if (browser->entries[j].type != browser->entries[i].type) {
                swap = browser->entries[j].type < browser->entries[i].type;
            } else if (ascii_casecmp(browser->entries[j].name, browser->entries[i].name) < 0) {
                swap = true;
            }

            if (swap) {
                ink_file_browser_entry_t temp = browser->entries[i];
                browser->entries[i] = browser->entries[j];
                browser->entries[j] = temp;
            }
        }
    }
}

static esp_err_t load_entries(ink_file_browser_t *browser)
{
    DIR *dir = opendir(browser->current_path);
    if (dir == NULL) {
        browser->entry_count = 0;
        browser->selected_index = 0;
        browser->visible_offset = 0;
        return ESP_FAIL;
    }

    browser->entry_count = 0;
    browser->selected_index = 0;
    browser->visible_offset = 0;
    browser->selected_file_ready = false;
    browser->selected_file_path[0] = '\0';

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (should_skip_name(entry->d_name)) {
            continue;
        }
        if (browser->entry_count >= INK_FILE_BROWSER_MAX_ENTRIES) {
            break;
        }

        char full_path[INK_FILE_BROWSER_PATH_LENGTH + 1];
        struct stat st;
        join_path(browser->current_path, entry->d_name, full_path, sizeof(full_path));
        if (stat(full_path, &st) != 0) {
            continue;
        }

        ink_file_browser_entry_type_t type = INK_FILE_BROWSER_ENTRY_NONE;
        if ((st.st_mode & S_IFDIR) != 0) {
            type = INK_FILE_BROWSER_ENTRY_DIRECTORY;
        } else if (has_txt_extension(entry->d_name)) {
            type = INK_FILE_BROWSER_ENTRY_TXT;
        }

        if (type == INK_FILE_BROWSER_ENTRY_NONE) {
            continue;
        }

        ink_file_browser_entry_t *dst = &browser->entries[browser->entry_count++];
        memset(dst, 0, sizeof(*dst));
        dst->type = type;
        sanitize_ascii_snippet(entry->d_name, dst->name, sizeof(dst->name));
        snprintf(dst->full_path, sizeof(dst->full_path), "%s", full_path);
    }

    closedir(dir);
    sort_entries(browser);
    return ESP_OK;
}

static bool set_parent_path(char *path, const char *root)
{
    if (strcmp(path, root) == 0) {
        return false;
    }

    size_t root_len = strlen(root);
    size_t len = strlen(path);

    while (len > root_len && path[len - 1] == '/') {
        path[--len] = '\0';
    }

    while (len > root_len && path[len - 1] != '/') {
        path[--len] = '\0';
    }

    while (len > root_len && path[len - 1] == '/') {
        path[--len] = '\0';
    }

    if (len < root_len) {
        snprintf(path, INK_FILE_BROWSER_PATH_LENGTH + 1, "%s", root);
    }
    return true;
}

esp_err_t ink_file_browser_init(
    ink_file_browser_t *browser,
    const char *mount_point,
    const char *initial_path)
{
    if (browser == NULL || mount_point == NULL || mount_point[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    memset(browser, 0, sizeof(*browser));
    snprintf(browser->mount_point, sizeof(browser->mount_point), "%s", mount_point);
    snprintf(
        browser->current_path,
        sizeof(browser->current_path),
        "%s",
        (initial_path != NULL && initial_path[0] != '\0') ? initial_path : mount_point
    );
    return load_entries(browser);
}

bool ink_file_browser_move_previous(ink_file_browser_t *browser)
{
    if (browser == NULL || browser->entry_count == 0 || browser->selected_index == 0) {
        return false;
    }

    --browser->selected_index;
    update_visible_offset(browser);
    return true;
}

bool ink_file_browser_move_next(ink_file_browser_t *browser)
{
    if (browser == NULL || browser->entry_count == 0 || browser->selected_index + 1 >= browser->entry_count) {
        return false;
    }

    ++browser->selected_index;
    update_visible_offset(browser);
    return true;
}

esp_err_t ink_file_browser_confirm(ink_file_browser_t *browser, bool *entered_directory, bool *selected_file)
{
    if (browser == NULL || browser->entry_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (entered_directory != NULL) {
        *entered_directory = false;
    }
    if (selected_file != NULL) {
        *selected_file = false;
    }

    ink_file_browser_entry_t *entry = &browser->entries[browser->selected_index];
    if (entry->type == INK_FILE_BROWSER_ENTRY_DIRECTORY) {
        snprintf(browser->current_path, sizeof(browser->current_path), "%s", entry->full_path);
        if (entered_directory != NULL) {
            *entered_directory = true;
        }
        return load_entries(browser);
    }

    if (entry->type == INK_FILE_BROWSER_ENTRY_TXT) {
        browser->selected_file_ready = true;
        snprintf(browser->selected_file_path, sizeof(browser->selected_file_path), "%s", entry->full_path);
        if (selected_file != NULL) {
            *selected_file = true;
        }
        return ESP_OK;
    }

    return ESP_OK;
}

bool ink_file_browser_go_parent(ink_file_browser_t *browser)
{
    if (browser == NULL) {
        return false;
    }

    if (!set_parent_path(browser->current_path, browser->mount_point)) {
        return false;
    }

    return load_entries(browser) == ESP_OK;
}

void ink_file_browser_render(const ink_file_browser_t *browser, ink_file_browser_view_t *view)
{
    memset(view, 0, sizeof(*view));

    if (browser == NULL) {
        snprintf(view->title, sizeof(view->title), "%s", "FILE BROWSER");
        snprintf(view->lines[0], sizeof(view->lines[0]), "%s", "NO BROWSER DATA");
        return;
    }

    if (browser_is_root(browser)) {
        snprintf(view->title, sizeof(view->title), "%s", "TF ROOT");
    } else {
        const char *leaf = strrchr(browser->current_path, '/');
        leaf = leaf != NULL ? leaf + 1 : browser->current_path;
        sanitize_ascii_snippet(leaf, view->title, sizeof(view->title));
    }

    if (browser->entry_count == 0) {
        snprintf(view->lines[0], sizeof(view->lines[0]), "%s", "EMPTY FOLDER");
        snprintf(view->status, sizeof(view->status), "%s", browser_is_root(browser) ? "BACK HOME" : "BACK UP");
        return;
    }

    for (size_t row = 0; row < INK_FILE_BROWSER_VISIBLE_LINES; ++row) {
        size_t index = browser->visible_offset + row;
        if (index >= browser->entry_count) {
            view->lines[row][0] = '\0';
            continue;
        }

        const ink_file_browser_entry_t *entry = &browser->entries[index];
        const char *prefix = index == browser->selected_index ? ">" : " ";
        const char *suffix = entry->type == INK_FILE_BROWSER_ENTRY_DIRECTORY ? "/" : "";
        const size_t suffix_len = suffix[0] != '\0' ? 1U : 0U;
        const int name_limit = (int)(sizeof(view->lines[row]) - 2U - suffix_len);
        snprintf(view->lines[row], sizeof(view->lines[row]), "%s%.*s%s", prefix, name_limit, entry->name, suffix);
    }

    const ink_file_browser_entry_t *selected = &browser->entries[browser->selected_index];
    if (selected->type == INK_FILE_BROWSER_ENTRY_DIRECTORY) {
        snprintf(view->status, sizeof(view->status), "%s", browser_is_root(browser) ? "CONF ENTER BK HOME" : "CONF ENTER BK UP");
    } else {
        snprintf(view->status, sizeof(view->status), "%s", browser_is_root(browser) ? "CONF OPEN BK HOME" : "CONF OPEN BK UP");
    }
}

bool ink_file_browser_self_test(void)
{
    static ink_file_browser_t browser;
    static ink_file_browser_view_t view;

    memset(&browser, 0, sizeof(browser));
    snprintf(browser.mount_point, sizeof(browser.mount_point), "%s", "/sdcard");
    snprintf(browser.current_path, sizeof(browser.current_path), "%s", "/sdcard/books");
    browser.entry_count = 3;
    browser.selected_index = 0;
    browser.entries[0].type = INK_FILE_BROWSER_ENTRY_DIRECTORY;
    snprintf(browser.entries[0].name, sizeof(browser.entries[0].name), "%s", "AUTHOR");
    browser.entries[1].type = INK_FILE_BROWSER_ENTRY_TXT;
    snprintf(browser.entries[1].name, sizeof(browser.entries[1].name), "%s", "A.TXT");
    browser.entries[2].type = INK_FILE_BROWSER_ENTRY_TXT;
    snprintf(browser.entries[2].name, sizeof(browser.entries[2].name), "%s", "B.TXT");

    ink_file_browser_render(&browser, &view);
    if (strcmp(view.title, "books") != 0) {
        return false;
    }
    if (strcmp(view.lines[0], ">AUTHOR/") != 0) {
        return false;
    }

    if (!ink_file_browser_move_next(&browser)) {
        return false;
    }
    ink_file_browser_render(&browser, &view);
    if (strcmp(view.lines[1], ">A.TXT") != 0) {
        return false;
    }

    if (!set_parent_path(browser.current_path, browser.mount_point)) {
        return false;
    }
    if (strcmp(browser.current_path, "/sdcard") != 0) {
        return false;
    }

    return true;
}
