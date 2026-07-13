#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ink_cpfont.h"

typedef enum {
  INK_FONT_READER,
  INK_FONT_MENU,
  INK_FONT_FOOTER,
} ink_font_role_t;

typedef struct {
  bool loaded;
  uint16_t version;
  char path[256];
} ink_font_info_t;

bool ink_fonts_load_default(ink_font_info_t *font);
bool ink_fonts_probe_file(const char *path, ink_font_info_t *font);
bool ink_fonts_load(ink_cpfont_t *font, ink_font_role_t role);
size_t ink_fonts_utf8_codepoint_count(const char *text);
bool ink_fonts_utf8_truncate_tail(const char *src, char *dst,
                                  size_t dst_size, size_t max_codepoints);
bool ink_fonts_self_test(void);
