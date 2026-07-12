#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INK_PHOTO_DIR "/sdcard/photos"
#define INK_PHOTO_MAX_ITEMS 64
#define INK_PHOTO_PATH_MAX 320
#define INK_PHOTO_PLANE_SIZE (480 * 800 / 8)

typedef struct { char path[INK_PHOTO_PATH_MAX]; } ink_photo_item_t;
typedef struct { size_t count; ink_photo_item_t items[INK_PHOTO_MAX_ITEMS]; } ink_photo_catalog_t;

bool ink_photo_catalog_load(ink_photo_catalog_t *catalog);
bool ink_photo_decode_bmp(const char *path, uint8_t *lsb, size_t lsb_size, uint8_t *msb, size_t msb_size);
bool ink_photo_core_self_test(void);
