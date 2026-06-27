#include "ink_photo_catalog.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "ink_app_boot.h"
#include "ink_photo_bmp_parser.h"

static bool has_bmp_extension(const char *name);
static int compare_catalog_entries(const void *lhs, const void *rhs);
static bool catalog_store_entry(ink_photo_catalog_t *catalog, const char *name);
static void catalog_set_error(ink_photo_catalog_t *catalog, const char *text);
static bool photo_catalog_extension_self_test(void);
static bool photo_catalog_sort_self_test(void);
static bool photo_catalog_init_self_test(void);

void ink_photo_catalog_init(ink_photo_catalog_t *catalog)
{
    if (catalog == NULL) {
        return;
    }

    memset(catalog, 0, sizeof(*catalog));
    catalog->initialized = true;
}

static void catalog_set_error(ink_photo_catalog_t *catalog, const char *text)
{
    if (catalog == NULL) {
        return;
    }

    snprintf(
        catalog->last_error,
        sizeof(catalog->last_error),
        "%s",
        text != NULL ? text : "");
}

static bool has_bmp_extension(const char *name)
{
    const char *dot = NULL;

    if (name == NULL || name[0] == '\0') {
        return false;
    }

    dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }

    return strcasecmp(dot, ".bmp") == 0;
}

static int compare_catalog_entries(const void *lhs, const void *rhs)
{
    const ink_photo_catalog_entry_t *left = (const ink_photo_catalog_entry_t *)lhs;
    const ink_photo_catalog_entry_t *right = (const ink_photo_catalog_entry_t *)rhs;

    return strcasecmp(left->name, right->name);
}

static bool catalog_store_entry(ink_photo_catalog_t *catalog, const char *name)
{
    ink_photo_catalog_entry_t *entry = NULL;
    struct stat st;

    if (catalog == NULL
        || name == NULL
        || name[0] == '\0'
        || catalog->count >= INK_PHOTO_CATALOG_MAX_ITEMS) {
        return false;
    }

    entry = &catalog->entries[catalog->count];
    if (snprintf(entry->name, sizeof(entry->name), "%s", name) >= (int)sizeof(entry->name)) {
        return false;
    }
    if (snprintf(entry->path, sizeof(entry->path), "%s/%s", INK_PHOTO_ALBUM_DIR, name)
        >= (int)sizeof(entry->path)) {
        return false;
    }
    if (stat(entry->path, &st) != 0 || (st.st_mode & S_IFDIR) != 0) {
        memset(entry, 0, sizeof(*entry));
        return false;
    }
    if (!ink_photo_bmp_file_looks_supported(entry->path)) {
        memset(entry, 0, sizeof(*entry));
        return false;
    }

    catalog->count++;
    return true;
}

esp_err_t ink_photo_catalog_reload(ink_photo_catalog_t *catalog)
{
    DIR *dir = NULL;
    struct dirent *entry = NULL;

    if (catalog == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ink_photo_catalog_init(catalog);
    if (ink_app_ensure_photo_directory() != ESP_OK) {
        catalog->directory_create_failed = true;
        catalog_set_error(catalog, "TF/相册目录不可用");
        return ESP_FAIL;
    }

    dir = opendir(INK_PHOTO_ALBUM_DIR);
    if (dir == NULL) {
        catalog->directory_create_failed = true;
        catalog_set_error(catalog, "TF/相册目录不可用");
        return ESP_FAIL;
    }

    catalog->directory_ready = true;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '\0'
            || strcmp(entry->d_name, ".") == 0
            || strcmp(entry->d_name, "..") == 0
            || entry->d_name[0] == '.'
            || !has_bmp_extension(entry->d_name)) {
            continue;
        }

        (void)catalog_store_entry(catalog, entry->d_name);
    }
    closedir(dir);

    if (catalog->count > 1U) {
        qsort(
            catalog->entries,
            catalog->count,
            sizeof(catalog->entries[0]),
            compare_catalog_entries);
    }
    if (catalog->count == 0U) {
        catalog_set_error(catalog, "photos 目录为空");
    }

    return ESP_OK;
}

size_t ink_photo_catalog_count(const ink_photo_catalog_t *catalog)
{
    return catalog != NULL ? catalog->count : 0U;
}

const ink_photo_catalog_entry_t *ink_photo_catalog_entry_at(
    const ink_photo_catalog_t *catalog,
    size_t index)
{
    if (catalog == NULL || index >= catalog->count) {
        return NULL;
    }

    return &catalog->entries[index];
}

bool ink_photo_catalog_copy_name(
    const ink_photo_catalog_t *catalog,
    size_t index,
    char *out_name,
    size_t out_size)
{
    const ink_photo_catalog_entry_t *entry = ink_photo_catalog_entry_at(catalog, index);

    if (out_name == NULL || out_size == 0U || entry == NULL) {
        return false;
    }

    snprintf(out_name, out_size, "%s", entry->name);
    return true;
}

bool ink_photo_catalog_copy_path(
    const ink_photo_catalog_t *catalog,
    size_t index,
    char *out_path,
    size_t out_size)
{
    const ink_photo_catalog_entry_t *entry = ink_photo_catalog_entry_at(catalog, index);

    if (out_path == NULL || out_size == 0U || entry == NULL) {
        return false;
    }

    snprintf(out_path, out_size, "%s", entry->path);
    return true;
}

static bool photo_catalog_init_self_test(void)
{
    ink_photo_catalog_t catalog;

    memset(&catalog, 0xA5, sizeof(catalog));
    ink_photo_catalog_init(&catalog);
    return catalog.initialized
        && !catalog.directory_ready
        && !catalog.directory_create_failed
        && catalog.count == 0U
        && catalog.last_error[0] == '\0';
}

static bool photo_catalog_extension_self_test(void)
{
    return has_bmp_extension("a.bmp")
        && has_bmp_extension("A.BMP")
        && !has_bmp_extension("a.png")
        && !has_bmp_extension("bmp")
        && !has_bmp_extension(NULL);
}

static bool photo_catalog_sort_self_test(void)
{
    ink_photo_catalog_entry_t entries[3];

    memset(entries, 0, sizeof(entries));
    snprintf(entries[0].name, sizeof(entries[0].name), "%s", "c.bmp");
    snprintf(entries[1].name, sizeof(entries[1].name), "%s", "A.bmp");
    snprintf(entries[2].name, sizeof(entries[2].name), "%s", "b.BMP");

    qsort(entries, 3U, sizeof(entries[0]), compare_catalog_entries);
    return strcmp(entries[0].name, "A.bmp") == 0
        && strcmp(entries[1].name, "b.BMP") == 0
        && strcmp(entries[2].name, "c.bmp") == 0;
}

bool ink_photo_catalog_self_test(void)
{
    return photo_catalog_init_self_test()
        && photo_catalog_extension_self_test()
        && photo_catalog_sort_self_test();
}
