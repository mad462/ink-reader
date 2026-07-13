#include "ink_cpfont.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

enum {
  INK_CPFONT_MAX_BITMAP_SCRATCH = 8192,
  INK_CPFONT_GLYPH_CACHE_CAPACITY = 96,
};

static const char kCpfontMagic[8] = {'C', 'P', 'F', 'O', 'N', 'T', '\0', '\0'};
static const uint16_t kCpfontVersion = 4;

typedef struct {
  bool valid;
  uint32_t glyph_index;
  uint32_t last_used_tick;
  ink_cpfont_glyph_t glyph;
  uint8_t *bitmap;
  uint16_t bitmap_length;
} ink_cpfont_cache_entry_t;

static void *cpfont_malloc(size_t size) {
  if (size == 0U) return NULL;
  void *ptr = heap_caps_malloc_prefer(
      size, 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM,
      MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
  return ptr != NULL ? ptr : malloc(size);
}

static void *cpfont_calloc(size_t count, size_t size) {
  if (count == 0U || size == 0U) return NULL;
  void *ptr = heap_caps_calloc_prefer(
      count, size, 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM,
      MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
  return ptr != NULL ? ptr : calloc(count, size);
}

static void *cpfont_realloc(void *ptr, size_t size) {
  if (size == 0U) {
    free(ptr);
    return NULL;
  }
  void *resized = heap_caps_realloc_prefer(
      ptr, size, 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM,
      MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
  return resized != NULL ? resized : realloc(ptr, size);
}

static uint16_t read_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int16_t read_i16(const uint8_t *p) { return (int16_t)read_u16(p); }

static uint32_t read_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool checked_add_u32(uint32_t left, uint32_t right,
                            uint32_t *result) {
  if (UINT32_MAX - left < right) return false;
  *result = left + right;
  return true;
}

static bool checked_mul_u32(uint32_t left, uint32_t right,
                            uint32_t *result) {
  if (left != 0U && right > UINT32_MAX / left) return false;
  *result = left * right;
  return true;
}

static uint32_t utf8_next_codepoint(const char **text) {
  const uint8_t *p = (const uint8_t *)*text;
  uint32_t cp;
  size_t length;
  uint32_t minimum;

  if (p == NULL || p[0] == 0U) return 0U;
  if (p[0] < 0x80U) {
    *text += 1;
    return p[0];
  }
  if (p[0] >= 0xC2U && p[0] <= 0xDFU) {
    cp = p[0] & 0x1FU;
    length = 2U;
    minimum = 0x80U;
  } else if (p[0] >= 0xE0U && p[0] <= 0xEFU) {
    cp = p[0] & 0x0FU;
    length = 3U;
    minimum = 0x800U;
  } else if (p[0] >= 0xF0U && p[0] <= 0xF4U) {
    cp = p[0] & 0x07U;
    length = 4U;
    minimum = 0x10000U;
  } else {
    *text += 1;
    return 0xFFFDU;
  }

  for (size_t i = 1U; i < length; ++i) {
    if (p[i] == 0U || (p[i] & 0xC0U) != 0x80U) {
      *text += 1;
      return 0xFFFDU;
    }
    cp = (cp << 6) | (p[i] & 0x3FU);
  }
  if (cp < minimum || (cp >= 0xD800U && cp <= 0xDFFFU) ||
      cp > 0x10FFFFU) {
    *text += 1;
    return 0xFFFDU;
  }
  *text += length;
  return cp;
}

static bool interval_contains(const ink_cpfont_interval_t *intervals,
                              uint32_t count, uint32_t cp,
                              uint32_t *glyph_index) {
  int left = 0;
  int right = (int)count - 1;
  while (left <= right) {
    const int mid = left + (right - left) / 2;
    const ink_cpfont_interval_t *interval = &intervals[mid];
    if (cp < interval->first) {
      right = mid - 1;
    } else if (cp > interval->last) {
      left = mid + 1;
    } else {
      if (glyph_index != NULL)
        *glyph_index = interval->offset + (cp - interval->first);
      return true;
    }
  }
  return false;
}

static esp_err_t cpfont_read_at(ink_cpfont_t *font, long offset, void *dst,
                                size_t size) {
  if (font == NULL || (dst == NULL && size > 0U)) return ESP_ERR_INVALID_ARG;
  if (size == 0U) return ESP_OK;
  if (font->file == NULL || fseek(font->file, offset, SEEK_SET) != 0 ||
      fread(dst, 1, size, font->file) != size)
    return ESP_FAIL;
  return ESP_OK;
}

static bool read_glyph(ink_cpfont_t *font, uint32_t glyph_index,
                       ink_cpfont_glyph_t *glyph) {
  if (font->last_glyph_valid && font->last_glyph_index == glyph_index) {
    *glyph = font->last_glyph;
    return true;
  }
  if (glyph_index >= font->glyph_count) return false;
  uint8_t buf[16];
  uint32_t glyph_delta;
  uint32_t offset;
  uint32_t glyph_end;
  if (!checked_mul_u32(glyph_index, 16U, &glyph_delta) ||
      !checked_add_u32(font->glyphs_file_offset, glyph_delta, &offset) ||
      !checked_add_u32(offset, sizeof(buf), &glyph_end) ||
      glyph_end > font->file_size)
    return false;
  if (cpfont_read_at(font, (long)offset, buf, sizeof(buf)) != ESP_OK)
    return false;
  glyph->width = buf[0];
  glyph->height = buf[1];
  glyph->advance_x = read_u16(buf + 2);
  glyph->left = read_i16(buf + 4);
  glyph->top = read_i16(buf + 6);
  glyph->data_length = read_u16(buf + 8);
  glyph->data_offset = read_u32(buf + 12);
  const uint32_t pixel_count = (uint32_t)glyph->width * glyph->height;
  const uint32_t required_bytes = (pixel_count + 3U) / 4U;
  uint32_t bitmap_offset;
  uint32_t bitmap_end;
  if (glyph->data_length < required_bytes ||
      !checked_add_u32(font->bitmap_file_offset, glyph->data_offset,
                       &bitmap_offset) ||
      !checked_add_u32(bitmap_offset, glyph->data_length, &bitmap_end) ||
      bitmap_end > font->file_size)
    return false;
  font->last_glyph_index = glyph_index;
  font->last_glyph = *glyph;
  font->last_glyph_valid = true;
  return true;
}

static ink_cpfont_cache_entry_t *cache_entries(ink_cpfont_t *font) {
  return (ink_cpfont_cache_entry_t *)font->glyph_cache_entries;
}

static ink_cpfont_cache_entry_t *cache_find(ink_cpfont_t *font,
                                             uint32_t glyph_index) {
  ink_cpfont_cache_entry_t *entries = cache_entries(font);
  for (uint32_t i = 0; entries != NULL && i < font->glyph_cache_capacity;
       ++i) {
    if (entries[i].valid && entries[i].glyph_index == glyph_index) {
      entries[i].last_used_tick = ++font->glyph_cache_tick;
      return &entries[i];
    }
  }
  return NULL;
}

static ink_cpfont_cache_entry_t *cache_select(ink_cpfont_t *font) {
  ink_cpfont_cache_entry_t *entries = cache_entries(font);
  if (entries == NULL) return NULL;
  for (uint32_t i = 0; i < font->glyph_cache_capacity; ++i) {
    if (!entries[i].valid) return &entries[i];
  }
  ink_cpfont_cache_entry_t *lru = &entries[0];
  for (uint32_t i = 1; i < font->glyph_cache_capacity; ++i)
    if (entries[i].last_used_tick < lru->last_used_tick) lru = &entries[i];
  return lru;
}

static uint16_t glyph_1bit_size(const ink_cpfont_glyph_t *glyph) {
  return glyph == NULL
             ? 0U
             : (uint16_t)(((uint32_t)glyph->width * glyph->height + 7U) >> 3);
}

static void decode_glyph_2bit(const ink_cpfont_glyph_t *glyph,
                              const uint8_t *src, uint8_t *dst) {
  const uint32_t pixel_count = (uint32_t)glyph->width * glyph->height;
  memset(dst, 0, glyph_1bit_size(glyph));
  for (uint32_t pixel = 0; pixel < pixel_count; ++pixel) {
    const uint8_t packed = src[pixel >> 2];
    const uint8_t shift = (uint8_t)((3U - (pixel & 3U)) * 2U);
    if (((packed >> shift) & 3U) != 0U)
      dst[pixel >> 3] |= (uint8_t)(0x80U >> (pixel & 7U));
  }
}

static bool load_glyph_cached(ink_cpfont_t *font, uint32_t glyph_index,
                              ink_cpfont_glyph_t *glyph,
                              const uint8_t **bitmap_out) {
  ink_cpfont_cache_entry_t *entry = cache_find(font, glyph_index);
  if (entry != NULL) {
    *glyph = entry->glyph;
    *bitmap_out = entry->bitmap;
    return true;
  }

  ink_cpfont_glyph_t loaded;
  if (!read_glyph(font, glyph_index, &loaded)) return false;
  if (loaded.data_length > font->bitmap_scratch_size) return false;
  if (loaded.data_length > 0U &&
      cpfont_read_at(font,
                     (long)(font->bitmap_file_offset + loaded.data_offset),
                     font->bitmap_scratch, loaded.data_length) != ESP_OK)
    return false;

  entry = cache_select(font);
  if (entry == NULL) return false;
  const uint16_t bitmap_length = glyph_1bit_size(&loaded);
  uint8_t *resized = cpfont_realloc(entry->bitmap, bitmap_length);
  if (bitmap_length > 0U && resized == NULL) return false;
  if (!entry->valid) ++font->glyph_cache_used;
  entry->bitmap = resized;
  entry->bitmap_length = bitmap_length;
  entry->glyph_index = glyph_index;
  entry->glyph = loaded;
  if (bitmap_length > 0U)
    decode_glyph_2bit(&loaded, font->bitmap_scratch, entry->bitmap);
  entry->last_used_tick = ++font->glyph_cache_tick;
  entry->valid = true;
  *glyph = loaded;
  *bitmap_out = entry->bitmap;
  return true;
}

static inline void set_pixel(uint8_t *buffer, int x, int y) {
  const size_t index = (size_t)y * (INK_CPFONT_FB_WIDTH / 8) + (size_t)(x / 8);
  buffer[index] &= (uint8_t)~(0x80U >> (x % 8));
}

static void draw_glyph_scaled(uint8_t *buffer, int x, int baseline_y,
                              const ink_cpfont_glyph_t *glyph,
                              const uint8_t *bitmap,
                              uint8_t scale_divisor) {
  const int base_x = x + glyph->left / (int16_t)scale_divisor;
  const int base_y = baseline_y - glyph->top / (int16_t)scale_divisor;
  const uint32_t pixel_count = (uint32_t)glyph->width * glyph->height;
  for (uint32_t pixel = 0; pixel < pixel_count; ++pixel) {
    if ((bitmap[pixel >> 3] & (uint8_t)(0x80U >> (pixel & 7U))) == 0U)
      continue;
    const int gx = (int)(pixel % glyph->width);
    const int gy = (int)(pixel / glyph->width);
    if (gx % scale_divisor != 0 || gy % scale_divisor != 0) continue;
    const int px = base_x + gx / scale_divisor;
    const int py = base_y + gy / scale_divisor;
    if (px >= 0 && px < INK_CPFONT_FB_WIDTH && py >= 0 &&
        py < INK_CPFONT_FB_HEIGHT)
      set_pixel(buffer, px, py);
  }
}

void ink_cpfont_init(ink_cpfont_t *font) {
  if (font != NULL) memset(font, 0, sizeof(*font));
}

void ink_cpfont_close(ink_cpfont_t *font) {
  if (font == NULL) return;
  if (font->file != NULL) fclose(font->file);
  free(font->intervals);
  free(font->bitmap_scratch);
  ink_cpfont_cache_entry_t *entries = cache_entries(font);
  for (uint32_t i = 0; entries != NULL && i < font->glyph_cache_capacity;
       ++i)
    free(entries[i].bitmap);
  free(entries);
  memset(font, 0, sizeof(*font));
}

bool ink_cpfont_is_loaded(const ink_cpfont_t *font) {
  return font != NULL && font->loaded && font->file != NULL &&
         font->intervals != NULL;
}

esp_err_t ink_cpfont_load(ink_cpfont_t *font, const char *path) {
  uint8_t header[32];
  uint8_t toc[32];
  if (font == NULL || path == NULL || path[0] == '\0')
    return ESP_ERR_INVALID_ARG;

  ink_cpfont_close(font);
  if (snprintf(font->path, sizeof(font->path), "%s", path) >=
      (int)sizeof(font->path))
    return ESP_ERR_INVALID_ARG;
  font->file = fopen(path, "rb");
  if (font->file == NULL) return ESP_FAIL;
  if (fseek(font->file, 0L, SEEK_END) != 0) {
    ink_cpfont_close(font);
    return ESP_FAIL;
  }
  const long file_size = ftell(font->file);
  if (file_size < (long)sizeof(header) || (uint64_t)file_size > UINT32_MAX ||
      fseek(font->file, 0L, SEEK_SET) != 0) {
    ink_cpfont_close(font);
    return ESP_ERR_INVALID_SIZE;
  }
  font->file_size = (uint32_t)file_size;
  if (fread(header, 1, sizeof(header), font->file) != sizeof(header) ||
      memcmp(header, kCpfontMagic, sizeof(kCpfontMagic)) != 0 ||
      read_u16(header + 8) != kCpfontVersion || header[12] == 0U) {
    ink_cpfont_close(font);
    return ESP_ERR_INVALID_RESPONSE;
  }

  bool found_regular = false;
  uint32_t data_offset = 0U;
  uint16_t kern_left_entries = 0U;
  uint16_t kern_right_entries = 0U;
  uint8_t kern_left_classes = 0U;
  uint8_t kern_right_classes = 0U;
  uint8_t ligature_count = 0U;
  uint32_t toc_bytes;
  uint32_t toc_end;
  if (!checked_mul_u32(header[12], sizeof(toc), &toc_bytes) ||
      !checked_add_u32(sizeof(header), toc_bytes, &toc_end) ||
      toc_end > font->file_size) {
    ink_cpfont_close(font);
    return ESP_ERR_INVALID_SIZE;
  }
  for (uint8_t i = 0; i < header[12]; ++i) {
    if (fread(toc, 1, sizeof(toc), font->file) != sizeof(toc)) {
      ink_cpfont_close(font);
      return ESP_FAIL;
    }
    if (toc[0] != 0U && !found_regular) continue;
    found_regular = true;
    font->interval_count = read_u32(toc + 4);
    font->glyph_count = read_u32(toc + 8);
    font->advance_y = toc[12];
    font->ascender = read_i16(toc + 13);
    font->descender = read_i16(toc + 15);
    kern_left_entries = read_u16(toc + 17);
    kern_right_entries = read_u16(toc + 19);
    kern_left_classes = toc[21];
    kern_right_classes = toc[22];
    ligature_count = toc[23];
    data_offset = read_u32(toc + 24);
    break;
  }
  if (!found_regular || font->interval_count == 0U ||
      font->glyph_count == 0U) {
    ink_cpfont_close(font);
    return ESP_ERR_INVALID_RESPONSE;
  }

  uint32_t interval_bytes;
  uint32_t glyph_bytes;
  uint32_t glyphs_offset;
  uint32_t kern_left_offset;
  uint32_t kern_right_offset;
  uint32_t kern_matrix_offset;
  uint32_t kern_matrix_bytes;
  uint32_t ligature_offset;
  uint32_t ligature_bytes;
  uint32_t bitmap_offset;
  if (data_offset < toc_end ||
      !checked_mul_u32(font->interval_count, 12U, &interval_bytes) ||
      !checked_add_u32(data_offset, interval_bytes, &glyphs_offset) ||
      !checked_mul_u32(font->glyph_count, 16U, &glyph_bytes) ||
      !checked_add_u32(glyphs_offset, glyph_bytes, &kern_left_offset) ||
      !checked_add_u32(kern_left_offset, (uint32_t)kern_left_entries * 3U,
                       &kern_right_offset) ||
      !checked_add_u32(kern_right_offset, (uint32_t)kern_right_entries * 3U,
                       &kern_matrix_offset) ||
      !checked_mul_u32(kern_left_classes, kern_right_classes,
                       &kern_matrix_bytes) ||
      !checked_add_u32(kern_matrix_offset, kern_matrix_bytes,
                       &ligature_offset) ||
      !checked_mul_u32(ligature_count, 8U, &ligature_bytes) ||
      !checked_add_u32(ligature_offset, ligature_bytes, &bitmap_offset) ||
      bitmap_offset > font->file_size) {
    ink_cpfont_close(font);
    return ESP_ERR_INVALID_SIZE;
  }

  font->intervals = cpfont_calloc(font->interval_count,
                                  sizeof(*font->intervals));
  if (font->intervals == NULL) {
    ink_cpfont_close(font);
    return ESP_ERR_NO_MEM;
  }
  if (fseek(font->file, (long)data_offset, SEEK_SET) != 0) {
    ink_cpfont_close(font);
    return ESP_FAIL;
  }
  for (uint32_t i = 0; i < font->interval_count; ++i) {
    uint8_t interval[12];
    if (fread(interval, 1, sizeof(interval), font->file) != sizeof(interval)) {
      ink_cpfont_close(font);
      return ESP_FAIL;
    }
    font->intervals[i].first = read_u32(interval);
    font->intervals[i].last = read_u32(interval + 4);
    font->intervals[i].offset = read_u32(interval + 8);
    if (font->intervals[i].first > font->intervals[i].last ||
        font->intervals[i].offset >= font->glyph_count ||
        (i > 0U && font->intervals[i - 1U].last >=
                       font->intervals[i].first)) {
      ink_cpfont_close(font);
      return ESP_ERR_INVALID_RESPONSE;
    }
    const uint32_t span =
        font->intervals[i].last - font->intervals[i].first;
    if (span >= font->glyph_count - font->intervals[i].offset) {
      ink_cpfont_close(font);
      return ESP_ERR_INVALID_RESPONSE;
    }
  }
  font->glyphs_file_offset = glyphs_offset;
  font->bitmap_file_offset = bitmap_offset;
  for (uint32_t i = 0; i < font->glyph_count; ++i) {
    ink_cpfont_glyph_t glyph;
    if (!read_glyph(font, i, &glyph)) {
      ink_cpfont_close(font);
      return ESP_ERR_INVALID_RESPONSE;
    }
  }
  font->last_glyph_valid = false;
  font->bitmap_scratch_size = INK_CPFONT_MAX_BITMAP_SCRATCH;
  font->bitmap_scratch = cpfont_malloc(font->bitmap_scratch_size);
  font->glyph_cache_capacity = INK_CPFONT_GLYPH_CACHE_CAPACITY;
  font->glyph_cache_entries = cpfont_calloc(
      font->glyph_cache_capacity, sizeof(ink_cpfont_cache_entry_t));
  if (font->bitmap_scratch == NULL || font->glyph_cache_entries == NULL) {
    ink_cpfont_close(font);
    return ESP_ERR_NO_MEM;
  }
  font->loaded = true;
  font->is_2bit = true;
  return ESP_OK;
}

esp_err_t ink_cpfont_draw_text_bw_scaled(ink_cpfont_t *font,
                                         uint8_t *buffer, int x, int top_y,
                                         const char *text,
                                         uint8_t scale_divisor,
                                         int *out_width_px) {
  if (out_width_px != NULL) *out_width_px = 0;
  if (font == NULL || text == NULL || scale_divisor == 0U)
    return ESP_ERR_INVALID_ARG;
  if (!ink_cpfont_is_loaded(font)) return ESP_FAIL;

  int cursor_x = x;
  const int baseline_y = top_y + font->ascender / (int16_t)scale_divisor;
  const char *cursor = text;
  while (*cursor != '\0') {
    const uint32_t cp = utf8_next_codepoint(&cursor);
    if (cp == 0U || cp == '\r' || cp == '\n') break;
    uint32_t glyph_index;
    ink_cpfont_glyph_t glyph;
    const uint8_t *bitmap;
    if (!interval_contains(font->intervals, font->interval_count, cp,
                           &glyph_index) ||
        !load_glyph_cached(font, glyph_index, &glyph, &bitmap)) {
      cursor_x += (font->advance_y / (int)scale_divisor) / 2;
      continue;
    }
    if (buffer != NULL && bitmap != NULL)
      draw_glyph_scaled(buffer, cursor_x, baseline_y, &glyph, bitmap,
                        scale_divisor);
    cursor_x += (int)(((glyph.advance_x + 8U) >> 4) / scale_divisor);
  }
  if (out_width_px != NULL) *out_width_px = cursor_x - x;
  return ESP_OK;
}

bool ink_cpfont_self_test(void) {
  ink_cpfont_t font;
  ink_cpfont_init(&font);
  if (ink_cpfont_is_loaded(&font)) return false;

  const ink_cpfont_interval_t intervals[] = {
      {.first = 'A', .last = 'C', .offset = 2U},
      {.first = 0x4E00U, .last = 0x4E02U, .offset = 5U},
  };
  uint32_t glyph_index = 0U;
  if (!interval_contains(intervals, 2U, 'B', &glyph_index) ||
      glyph_index != 3U || interval_contains(intervals, 2U, 'Z', NULL))
    return false;
  uint32_t checked = 0U;
  if (!checked_add_u32(1U, 2U, &checked) || checked != 3U ||
      checked_add_u32(UINT32_MAX, 1U, &checked) ||
      checked_mul_u32(UINT32_MAX, 2U, &checked))
    return false;

  ink_cpfont_cache_entry_t cache_entry = {0};
  font.glyph_cache_entries = &cache_entry;
  font.glyph_cache_capacity = 1U;
  if (cache_select(&font) != &cache_entry || cache_entry.valid)
    return false;
  font.glyph_cache_entries = NULL;
  font.glyph_cache_capacity = 0U;

  const ink_cpfont_glyph_t glyph = {
      .width = 4U, .height = 4U, .advance_x = 64U, .left = 0,
      .top = 4, .data_length = 4U, .data_offset = 0U};
  const uint8_t source[4] = {0xCCU, 0x00U, 0xCCU, 0x00U};
  uint8_t decoded[2];
  const size_t render_size = (INK_CPFONT_FB_WIDTH / 8) * 12U;
  uint8_t *render_buffer = cpfont_malloc(render_size);
  if (render_buffer == NULL) return false;
  decode_glyph_2bit(&glyph, source, decoded);
  memset(render_buffer, 0xFF, render_size);
  draw_glyph_scaled(render_buffer, 10, 10, &glyph, decoded, 2U);
  int pixel_count = 0;
  for (size_t i = 0; i < render_size; ++i)
    for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1)
      if ((render_buffer[i] & mask) == 0U) ++pixel_count;
  if (pixel_count != 4) {
    free(render_buffer);
    return false;
  }

  const char invalid[] = {(char)0xED, (char)0xA0, (char)0x80, 'A', '\0'};
  const char *cursor = invalid;
  const bool utf8_ok =
      utf8_next_codepoint(&cursor) == 0xFFFDU && cursor == invalid + 1 &&
      utf8_next_codepoint(&cursor) == 0xFFFDU && cursor == invalid + 2 &&
      utf8_next_codepoint(&cursor) == 0xFFFDU && cursor == invalid + 3 &&
      utf8_next_codepoint(&cursor) == 'A';
  free(render_buffer);
  return utf8_ok;
}
