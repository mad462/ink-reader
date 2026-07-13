#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INK_PHOTO_DIR "/sdcard/photos"
#define INK_PHOTO_MAX_ITEMS 64
#define INK_PHOTO_PATH_MAX 320
#define INK_PHOTO_PLANE_SIZE (480 * 800 / 8)

typedef struct {
  char path[INK_PHOTO_PATH_MAX];
  char name[INK_PHOTO_PATH_MAX];
} ink_photo_item_t;
typedef struct {
  size_t count;
  ink_photo_item_t items[INK_PHOTO_MAX_ITEMS];
} ink_photo_catalog_t;

typedef bool (*ink_photo_try_item_fn)(const ink_photo_item_t *item,
                                     void *context);

bool ink_photo_catalog_load(ink_photo_catalog_t *catalog);
bool ink_photo_catalog_find_decodable(const ink_photo_catalog_t *catalog,
                                      size_t first_index, int direction,
                                      ink_photo_try_item_fn try_item,
                                      void *context, size_t *out_index);
bool ink_photo_bmp_file_looks_supported(const char *path);
bool ink_photo_decode_bmp(const char *path, uint8_t *lsb, size_t lsb_size,
                          uint8_t *msb, size_t msb_size);
bool ink_photo_core_self_test(void);
