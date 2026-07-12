#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { bool loaded; uint16_t version; char path[256]; } ink_font_info_t;

bool ink_fonts_load_default(ink_font_info_t *font);
bool ink_fonts_probe_file(const char *path, ink_font_info_t *font);
bool ink_fonts_self_test(void);
