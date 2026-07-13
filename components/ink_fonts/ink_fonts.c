#include "ink_fonts.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_log.h"

static const char *TAG = "ink_fonts";
static const uint8_t kMagic[8] = {'C', 'P', 'F', 'O', 'N', 'T', 0, 0};

static const char *const kReaderFontPaths[] = {
    "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_18.cpfont",
    "/sdcard/fonts/LXGWWenKai_18.cpfont",
    "/sdcard/FONTS/LXGWWENKAI_18.CPFONT",
    "/sdcard/fonts/NotoSansSC_18.cpfont",
    "/sdcard/.fonts/NotoSansSC/NotoSansSC_18.cpfont",
};

static const char *const kMenuFontPaths[] = {
    "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_24.cpfont",
    "/sdcard/fonts/LXGWWenKai_24.cpfont",
    "/sdcard/FONTS/LXGWWENKAI_24.CPFONT",
    "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_20.cpfont",
    "/sdcard/fonts/LXGWWenKai_20.cpfont",
    "/sdcard/FONTS/LXGWWENKAI_20.CPFONT",
    "/sdcard/fonts/SmallSimSunBitmap_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSunBitmap_16.cpfont",
    "/sdcard/fonts/SmallSimSunEmbedded_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSunEmbedded_16.cpfont",
};

static const char *const kFooterFontPaths[] = {
    "/sdcard/fonts/SmallSimSunEmbedded_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSunEmbedded_16.cpfont",
    "/sdcard/FONTS/SMALLSIMSUNEMBEDDED_16.CPFONT",
    "/sdcard/fonts/SmallSimSunBitmap_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSunBitmap_16.cpfont",
    "/sdcard/fonts/SmallSimSun_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSun_16.cpfont",
    "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_16.cpfont",
    "/sdcard/fonts/LXGWWenKai_16.cpfont",
    "/sdcard/FONTS/LXGWWENKAI_16.CPFONT",
    "/sdcard/fonts/NotoSansSC_16.cpfont",
    "/sdcard/.fonts/NotoSansSC/NotoSansSC_16.cpfont",
    "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_18.cpfont",
    "/sdcard/fonts/LXGWWenKai_18.cpfont",
    "/sdcard/FONTS/LXGWWENKAI_18.CPFONT",
    "/sdcard/fonts/NotoSansSC_18.cpfont",
    "/sdcard/.fonts/NotoSansSC/NotoSansSC_18.cpfont",
};

static const char *const kFontDirectories[] = {
    "/sdcard/fonts",
    "/sdcard/FONTS",
    "/sdcard/.fonts/LXGWWenKai",
    "/sdcard/.fonts/NotoSansSC",
};

typedef struct {
  char **items;
  size_t count;
  size_t capacity;
} string_list_t;

typedef struct {
  uint32_t codepoint;
  size_t byte_count;
  bool valid;
} utf8_unit_t;

static bool parse_header(const uint8_t *header, size_t length,
                         ink_font_info_t *font) {
  if (header == NULL || length < 32U || font == NULL ||
      memcmp(header, kMagic, sizeof(kMagic)) != 0)
    return false;
  const uint16_t version = (uint16_t)(header[8] | ((uint16_t)header[9] << 8));
  if (version != 4U || header[12] == 0U) return false;
  font->loaded = true;
  font->version = version;
  return true;
}

static utf8_unit_t utf8_decode(const char *text) {
  const uint8_t *p = (const uint8_t *)text;
  utf8_unit_t result = {.codepoint = 0xFFFDU,
                        .byte_count = 1U,
                        .valid = false};
  if (p == NULL || p[0] == 0U) {
    result.codepoint = 0U;
    result.byte_count = 0U;
    result.valid = true;
    return result;
  }
  if (p[0] < 0x80U) {
    result.codepoint = p[0];
    result.valid = true;
    return result;
  }

  size_t length;
  uint32_t codepoint;
  uint32_t minimum;
  if (p[0] >= 0xC2U && p[0] <= 0xDFU) {
    length = 2U;
    codepoint = p[0] & 0x1FU;
    minimum = 0x80U;
  } else if (p[0] >= 0xE0U && p[0] <= 0xEFU) {
    length = 3U;
    codepoint = p[0] & 0x0FU;
    minimum = 0x800U;
  } else if (p[0] >= 0xF0U && p[0] <= 0xF4U) {
    length = 4U;
    codepoint = p[0] & 0x07U;
    minimum = 0x10000U;
  } else {
    return result;
  }
  for (size_t i = 1U; i < length; ++i) {
    if (p[i] == 0U || (p[i] & 0xC0U) != 0x80U) return result;
    codepoint = (codepoint << 6) | (p[i] & 0x3FU);
  }
  if (codepoint < minimum ||
      (codepoint >= 0xD800U && codepoint <= 0xDFFFU) ||
      codepoint > 0x10FFFFU)
    return result;
  result.codepoint = codepoint;
  result.byte_count = length;
  result.valid = true;
  return result;
}

static bool append_bytes(char *dst, size_t dst_size, size_t *used,
                         const char *bytes, size_t count) {
  if (*used >= dst_size || count > dst_size - *used - 1U) return false;
  memcpy(dst + *used, bytes, count);
  *used += count;
  dst[*used] = '\0';
  return true;
}

static bool has_cpfont_extension(const char *name) {
  const char *dot = name == NULL ? NULL : strrchr(name, '.');
  return dot != NULL && strcasecmp(dot, ".cpfont") == 0;
}

static void string_list_free(string_list_t *list) {
  if (list == NULL) return;
  for (size_t i = 0; i < list->count; ++i) free(list->items[i]);
  free(list->items);
  memset(list, 0, sizeof(*list));
}

static bool string_list_add(string_list_t *list, const char *value) {
  if (list->count == list->capacity) {
    const size_t capacity = list->capacity == 0U ? 8U : list->capacity * 2U;
    char **resized = realloc(list->items, capacity * sizeof(*resized));
    if (resized == NULL) return false;
    list->items = resized;
    list->capacity = capacity;
  }
  const size_t length = strlen(value);
  list->items[list->count] = malloc(length + 1U);
  if (list->items[list->count] == NULL) return false;
  memcpy(list->items[list->count], value, length + 1U);
  ++list->count;
  return true;
}

static int compare_names(const void *left, const void *right) {
  const char *const *a = left;
  const char *const *b = right;
  const int folded = strcasecmp(*a, *b);
  return folded != 0 ? folded : strcmp(*a, *b);
}

static bool join_path(char *path, size_t path_size, const char *directory,
                      const char *name) {
  const int written = snprintf(path, path_size, "%s/%s", directory, name);
  return written >= 0 && (size_t)written < path_size;
}

static bool is_directory_path(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_regular_path(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool collect_directory_names(const char *directory, bool directories,
                                    string_list_t *names) {
  DIR *dir = opendir(directory);
  if (dir == NULL) return false;
  bool ok = true;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if (!directories && !has_cpfont_extension(entry->d_name)) continue;
    char path[INK_CPFONT_PATH_LENGTH + 1];
    if (!join_path(path, sizeof(path), directory, entry->d_name)) continue;
    if ((directories && !is_directory_path(path)) ||
        (!directories && !is_regular_path(path)))
      continue;
    if (!string_list_add(names, entry->d_name)) {
      ok = false;
      break;
    }
  }
  closedir(dir);
  if (ok && names->count > 1U)
    qsort(names->items, names->count, sizeof(names->items[0]), compare_names);
  return ok;
}

static bool try_load_path(ink_cpfont_t *font, const char *path) {
  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    ESP_LOGD(TAG, "font candidate missing path=%s", path);
    return false;
  }
  const esp_err_t result = ink_cpfont_load(font, path);
  if (result == ESP_OK) {
    ESP_LOGI(TAG, "font loaded path=%s", path);
    return true;
  }
  ESP_LOGW(TAG, "font load failed path=%s err=%s", path,
           esp_err_to_name(result));
  return false;
}

static bool load_from_directory(ink_cpfont_t *font, const char *directory) {
  string_list_t names = {0};
  if (!collect_directory_names(directory, false, &names)) {
    string_list_free(&names);
    return false;
  }
  bool loaded = false;
  for (size_t i = 0; i < names.count && !loaded; ++i) {
    char path[INK_CPFONT_PATH_LENGTH + 1];
    if (!join_path(path, sizeof(path), directory, names.items[i])) {
      ESP_LOGW(TAG, "font path too long dir=%s name=%s", directory,
               names.items[i]);
      continue;
    }
    loaded = try_load_path(font, path);
  }
  string_list_free(&names);
  return loaded;
}

static bool load_from_family_directories(ink_cpfont_t *font) {
  static const char *root = "/sdcard/.fonts";
  string_list_t families = {0};
  if (!collect_directory_names(root, true, &families)) {
    string_list_free(&families);
    return false;
  }
  bool loaded = false;
  for (size_t i = 0; i < families.count && !loaded; ++i) {
    char family_path[INK_CPFONT_PATH_LENGTH + 1];
    if (!join_path(family_path, sizeof(family_path), root, families.items[i]))
      continue;
    loaded = load_from_directory(font, family_path);
  }
  string_list_free(&families);
  return loaded;
}

bool ink_fonts_probe_file(const char *path, ink_font_info_t *font) {
  if (path == NULL || font == NULL) return false;
  memset(font, 0, sizeof(*font));
  FILE *file = fopen(path, "rb");
  if (file == NULL) return false;
  uint8_t header[32];
  const bool read_ok = fread(header, 1, sizeof(header), file) == sizeof(header);
  fclose(file);
  if (!read_ok || !parse_header(header, sizeof(header), font)) return false;
  if (snprintf(font->path, sizeof(font->path), "%s", path) >=
      (int)sizeof(font->path)) {
    memset(font, 0, sizeof(*font));
    return false;
  }
  return true;
}

bool ink_fonts_load_default(ink_font_info_t *font) {
  if (font == NULL) return false;
  for (size_t i = 0; i < sizeof(kReaderFontPaths) / sizeof(kReaderFontPaths[0]);
       ++i)
    if (ink_fonts_probe_file(kReaderFontPaths[i], font)) return true;
  memset(font, 0, sizeof(*font));
  return false;
}

bool ink_fonts_load(ink_cpfont_t *font, ink_font_role_t role) {
  const char *const *paths;
  size_t path_count;
  if (font == NULL) return false;
  switch (role) {
    case INK_FONT_READER:
      paths = kReaderFontPaths;
      path_count = sizeof(kReaderFontPaths) / sizeof(kReaderFontPaths[0]);
      break;
    case INK_FONT_MENU:
      paths = kMenuFontPaths;
      path_count = sizeof(kMenuFontPaths) / sizeof(kMenuFontPaths[0]);
      break;
    case INK_FONT_FOOTER:
      paths = kFooterFontPaths;
      path_count = sizeof(kFooterFontPaths) / sizeof(kFooterFontPaths[0]);
      break;
    default:
      return false;
  }

  ink_cpfont_init(font);
  for (size_t i = 0; i < path_count; ++i)
    if (try_load_path(font, paths[i])) return true;
  for (size_t i = 0;
       i < sizeof(kFontDirectories) / sizeof(kFontDirectories[0]); ++i)
    if (load_from_directory(font, kFontDirectories[i])) return true;
  if (load_from_family_directories(font)) return true;
  ESP_LOGW(TAG, "font not found role=%d", (int)role);
  return false;
}

size_t ink_fonts_utf8_codepoint_count(const char *text) {
  if (text == NULL) return 0U;
  size_t count = 0U;
  while (*text != '\0') {
    const utf8_unit_t unit = utf8_decode(text);
    text += unit.byte_count;
    ++count;
  }
  return count;
}

bool ink_fonts_utf8_truncate_tail(const char *src, char *dst,
                                  size_t dst_size, size_t max_codepoints) {
  if (src == NULL || dst == NULL || dst_size == 0U) return false;
  dst[0] = '\0';
  const size_t total = ink_fonts_utf8_codepoint_count(src);
  const bool truncated = total > max_codepoints;
  const size_t keep = !truncated
                          ? total
                          : (max_codepoints <= 3U ? max_codepoints
                                                  : max_codepoints - 3U);
  size_t used = 0U;
  const char *cursor = src;
  static const char replacement[] = "\xEF\xBF\xBD";
  for (size_t i = 0; i < keep; ++i) {
    const utf8_unit_t unit = utf8_decode(cursor);
    const char *bytes = unit.valid ? cursor : replacement;
    const size_t byte_count = unit.valid ? unit.byte_count : 3U;
    if (!append_bytes(dst, dst_size, &used, bytes, byte_count)) {
      dst[0] = '\0';
      return false;
    }
    cursor += unit.byte_count;
  }
  if (truncated && max_codepoints > 3U &&
      !append_bytes(dst, dst_size, &used, "...", 3U)) {
    dst[0] = '\0';
    return false;
  }
  return true;
}

bool ink_fonts_self_test(void) {
  uint8_t good[32] = {'C', 'P', 'F', 'O', 'N', 'T', 0, 0, 4, 0, 0, 0, 1};
  uint8_t bad[32] = {0};
  ink_font_info_t font = {0};
  char truncated[16] = {0};
  const char malformed[] = {(char)0xC0, (char)0xAF, (char)0xED,
                            (char)0xA0, (char)0x80, (char)0xF4,
                            (char)0x90, (char)0x80, (char)0x80, 0};
  char normalized[32] = {0};
  return parse_header(good, sizeof(good), &font) && font.loaded &&
         font.version == 4U && !parse_header(bad, sizeof(bad), &font) &&
         ink_fonts_utf8_codepoint_count("相册A") == 3U &&
         ink_fonts_utf8_codepoint_count("相册ABC") == 5U &&
         ink_fonts_utf8_truncate_tail("相册ABC", truncated,
                                      sizeof(truncated), 4U) &&
         strcmp(truncated, "相...") == 0 &&
         ink_fonts_utf8_codepoint_count(truncated) == 4U &&
         ink_fonts_utf8_codepoint_count(malformed) == 9U &&
         ink_fonts_utf8_truncate_tail(malformed, normalized,
                                      sizeof(normalized), 9U) &&
         ink_fonts_utf8_codepoint_count(normalized) == 9U &&
         ink_cpfont_self_test();
}
