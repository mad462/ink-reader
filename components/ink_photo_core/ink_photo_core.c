#include "ink_photo_core.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
  uint32_t image_offset;
  uint32_t width;
  int32_t height;
  uint32_t colors;
} bmp_info_t;
static uint16_t le16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static bool extension_ok(const char *name) {
  const char *dot = name ? strrchr(name, '.') : NULL;
  return dot && !strcasecmp(dot, ".bmp");
}

static void copy_photo_name(char *destination, size_t destination_size,
                            const char *filename) {
  if (!destination || destination_size == 0) return;
  destination[0] = '\0';
  if (!filename) return;

  const char *dot = strrchr(filename, '.');
  size_t length = (dot && dot != filename) ? (size_t)(dot - filename)
                                           : strlen(filename);
  if (length >= destination_size) length = destination_size - 1;
  memcpy(destination, filename, length);
  destination[length] = '\0';
}

static int compare_items(const void *a, const void *b) {
  const char *a_path = ((const ink_photo_item_t *)a)->path;
  const char *b_path = ((const ink_photo_item_t *)b)->path;
  const int case_insensitive_order = strcasecmp(a_path, b_path);
  return case_insensitive_order != 0 ? case_insensitive_order
                                     : strcmp(a_path, b_path);
}

bool ink_photo_catalog_load(ink_photo_catalog_t *catalog) {
  if (!catalog) return false;
  memset(catalog, 0, sizeof(*catalog));
  DIR *dir = opendir(INK_PHOTO_DIR);
  if (!dir) return false;
  struct dirent *entry;
  while (catalog->count < INK_PHOTO_MAX_ITEMS &&
         (entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.' || !extension_ok(entry->d_name)) continue;
    ink_photo_item_t *item = &catalog->items[catalog->count];
    if (snprintf(item->path, sizeof(item->path), "%s/%s", INK_PHOTO_DIR,
                 entry->d_name) < (int)sizeof(item->path)) {
      copy_photo_name(item->name, sizeof(item->name), entry->d_name);
      ++catalog->count;
    }
  }
  closedir(dir);
  qsort(catalog->items, catalog->count, sizeof(catalog->items[0]),
        compare_items);
  return true;
}

static bool parse_headers(const uint8_t file_h[14], const uint8_t dib[40],
                          bmp_info_t *out) {
  if (!file_h || !dib || !out || file_h[0] != 'B' || file_h[1] != 'M' ||
      le32(dib) < 40)
    return false;
  out->image_offset = le32(file_h + 10);
  out->width = le32(dib + 4);
  out->height = (int32_t)le32(dib + 8);
  out->colors = le32(dib + 32);
  if (out->colors == 0) out->colors = 16;
  return le16(dib + 12) == 1 && le16(dib + 14) == 4 && le32(dib + 16) == 0 &&
         out->width == 480 && out->height == 800 && out->colors >= 4 &&
         out->colors <= 16;
}

static bool classify(const uint8_t *palette, uint32_t count, uint8_t map[4]) {
  if (!palette || count < 4 || count > 16 || !map) return false;
  uint8_t ranked[16];
  uint32_t lum[16];
  for (uint32_t i = 0; i < count; ++i) {
    ranked[i] = (uint8_t)i;
    lum[i] = (uint32_t)palette[i * 4 + 2] * 30 +
             (uint32_t)palette[i * 4 + 1] * 59 + (uint32_t)palette[i * 4] * 11;
  }
  for (uint32_t i = 0; i + 1 < count; ++i)
    for (uint32_t j = i + 1; j < count; ++j)
      if (lum[ranked[i]] > lum[ranked[j]]) {
        uint8_t t = ranked[i];
        ranked[i] = ranked[j];
        ranked[j] = t;
      }
  map[3] = ranked[0];
  map[2] = ranked[1];
  map[1] = ranked[count - 2];
  map[0] = ranked[count - 1];
  return true;
}
static uint8_t gray_for(uint8_t index, const uint8_t map[4]) {
  for (uint8_t gray = 0; gray < 4; ++gray)
    if (map[gray] == index) return gray;
  return 0;
}
static void set_gray(uint8_t *lsb, uint8_t *msb, uint16_t x, uint16_t y,
                     uint8_t gray) {
  size_t i = (size_t)y * 60 + x / 8;
  uint8_t mask = (uint8_t)(0x80u >> (x & 7));
  if (gray & 1)
    lsb[i] |= mask;
  else
    lsb[i] &= (uint8_t)~mask;
  if (gray & 2)
    msb[i] |= mask;
  else
    msb[i] &= (uint8_t)~mask;
}

bool ink_photo_decode_bmp(const char *path, uint8_t *lsb, size_t ll,
                          uint8_t *msb, size_t ml) {
  if (!path || !lsb || !msb || ll < INK_PHOTO_PLANE_SIZE ||
      ml < INK_PHOTO_PLANE_SIZE)
    return false;
  memset(lsb, 0, INK_PHOTO_PLANE_SIZE);
  memset(msb, 0, INK_PHOTO_PLANE_SIZE);
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  uint8_t fh[14], dib[40], palette[64], map[4], row[240];
  bmp_info_t info;
  bool ok = false;
  if (fread(fh, 1, sizeof(fh), f) != sizeof(fh) ||
      fread(dib, 1, sizeof(dib), f) != sizeof(dib) ||
      !parse_headers(fh, dib, &info))
    goto done;
  const uint32_t dib_size = le32(dib);
  if (dib_size > LONG_MAX - 14 || fseek(f, 14 + (long)dib_size, SEEK_SET) != 0)
    goto done;
  if (fread(palette, 4, info.colors, f) != info.colors ||
      !classify(palette, info.colors, map) ||
      fseek(f, (long)info.image_offset, SEEK_SET) != 0)
    goto done;
  for (uint32_t src_y = 0; src_y < 800; ++src_y) {
    if (fread(row, 1, sizeof(row), f) != sizeof(row)) goto done;
    uint16_t y = (uint16_t)(799 - src_y);
    for (uint16_t x = 0; x < 480; x += 2) {
      uint8_t packed = row[x / 2];
      set_gray(lsb, msb, x, y, gray_for((uint8_t)(packed >> 4), map));
      set_gray(lsb, msb, (uint16_t)(x + 1), y,
               gray_for((uint8_t)(packed & 15), map));
    }
  }
  ok = true;
done:
  fclose(f);
  return ok;
}

bool ink_photo_core_self_test(void) {
  uint8_t fh[14] = {'B', 'M'}, dib[40] = {40},
          palette[16] = {0,   0,   0,   0, 85,  85,  85,  0,
                         170, 170, 170, 0, 255, 255, 255, 0};
  bmp_info_t info;
  uint8_t map[4];
  fh[10] = 118;
  dib[4] = 0xe0;
  dib[5] = 1;
  dib[8] = 0x20;
  dib[9] = 3;
  dib[12] = 1;
  dib[14] = 4;
  dib[32] = 4;
  char bmp_name[sizeof("holiday")];
  char extensionless_name[sizeof("README")];
  char hidden_name[sizeof(".hidden")];
  char hidden_without_extension[sizeof(".hidden")];
  char boundary_name[4];
  static const ink_photo_item_t lowercase_item = {
      .path = INK_PHOTO_DIR "/a.bmp"};
  static const ink_photo_item_t uppercase_item = {
      .path = INK_PHOTO_DIR "/A.bmp"};
  copy_photo_name(bmp_name, sizeof(bmp_name), "holiday.bmp");
  copy_photo_name(extensionless_name, sizeof(extensionless_name), "README");
  copy_photo_name(hidden_name, sizeof(hidden_name), ".hidden.bmp");
  copy_photo_name(hidden_without_extension, sizeof(hidden_without_extension),
                  ".hidden");
  copy_photo_name(boundary_name, sizeof(boundary_name), "abcdef.bmp");
  if (strcmp(bmp_name, "holiday") != 0 ||
      strcmp(extensionless_name, "README") != 0 ||
      strcmp(hidden_name, ".hidden") != 0 || strcmp(boundary_name, "abc") != 0 ||
      strcmp(hidden_without_extension, ".hidden") != 0 ||
      boundary_name[sizeof(boundary_name) - 1] != '\0' ||
      compare_items(&lowercase_item, &uppercase_item) <= 0 ||
      compare_items(&uppercase_item, &lowercase_item) >= 0 ||
      !parse_headers(fh, dib, &info) || !classify(palette, 4, map) ||
      map[0] != 3 || map[3] != 0)
    return false;
  uint8_t lsb[60] = {0}, msb[60] = {0};
  set_gray(lsb, msb, 0, 0, 0);
  set_gray(lsb, msb, 1, 0, 1);
  set_gray(lsb, msb, 2, 0, 2);
  set_gray(lsb, msb, 3, 0, 3);
  return (lsb[0] & 0x40) && (msb[0] & 0x20) && (lsb[0] & 0x10) &&
         (msb[0] & 0x10);
}
