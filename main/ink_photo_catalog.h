#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define INK_PHOTO_ALBUM_DIR "/sdcard/photos"
#define INK_PHOTO_CATALOG_MAX_ITEMS 128
#define INK_PHOTO_CATALOG_NAME_MAX 192
#define INK_PHOTO_CATALOG_PATH_MAX 320

typedef struct {
    char name[INK_PHOTO_CATALOG_NAME_MAX];
    char path[INK_PHOTO_CATALOG_PATH_MAX];
} ink_photo_catalog_entry_t;

typedef struct {
    bool initialized;
    bool directory_ready;
    bool directory_create_failed;
    size_t count;
    char last_error[64];
    ink_photo_catalog_entry_t entries[INK_PHOTO_CATALOG_MAX_ITEMS];
} ink_photo_catalog_t;

void ink_photo_catalog_init(ink_photo_catalog_t *catalog);
esp_err_t ink_photo_catalog_reload(ink_photo_catalog_t *catalog);
size_t ink_photo_catalog_count(const ink_photo_catalog_t *catalog);
const ink_photo_catalog_entry_t *ink_photo_catalog_entry_at(
    const ink_photo_catalog_t *catalog,
    size_t index);
bool ink_photo_catalog_copy_name(
    const ink_photo_catalog_t *catalog,
    size_t index,
    char *out_name,
    size_t out_size);
bool ink_photo_catalog_copy_path(
    const ink_photo_catalog_t *catalog,
    size_t index,
    char *out_path,
    size_t out_size);
bool ink_photo_catalog_self_test(void);
