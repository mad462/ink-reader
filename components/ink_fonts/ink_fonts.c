#include "ink_fonts.h"

#include <stdio.h>
#include <string.h>

static const uint8_t kMagic[8] = {'C', 'P', 'F', 'O', 'N', 'T', 0, 0};

static bool parse_header(const uint8_t *header, size_t length,
                         ink_font_info_t *font) {
  if (!header || length < 32 || !font ||
      memcmp(header, kMagic, sizeof(kMagic)) != 0)
    return false;
  const uint16_t version = (uint16_t)(header[8] | ((uint16_t)header[9] << 8));
  if (version != 4 || header[12] == 0) return false;
  font->loaded = true;
  font->version = version;
  return true;
}

bool ink_fonts_probe_file(const char *path, ink_font_info_t *font) {
  if (!path || !font) return false;
  memset(font, 0, sizeof(*font));
  FILE *file = fopen(path, "rb");
  if (!file) return false;
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
  static const char *paths[] = {
      "/sdcard/fonts/LXGWWenKai_18.cpfont",
      "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_18.cpfont",
      "/sdcard/fonts/NotoSansSC_18.cpfont",
  };
  if (!font) return false;
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i)
    if (ink_fonts_probe_file(paths[i], font)) return true;
  memset(font, 0, sizeof(*font));
  return false;
}

bool ink_fonts_self_test(void) {
  uint8_t good[32] = {'C', 'P', 'F', 'O', 'N', 'T', 0, 0, 4, 0, 0, 0, 1};
  uint8_t bad[32] = {0};
  ink_font_info_t font = {0};
  return parse_header(good, sizeof(good), &font) && font.loaded &&
         font.version == 4 && !parse_header(bad, sizeof(bad), &font);
}
