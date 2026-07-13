#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ink_cpfont.h"

#define INK_EPD_WIDTH 480
#define INK_EPD_HEIGHT 800
#define INK_EPD_BUFFER_SIZE (INK_EPD_WIDTH * INK_EPD_HEIGHT / 8)
#define INK_PHOTO_LIST_ROW_HEIGHT 42
#define INK_PHOTO_LIST_ROW_GAP 2
#define INK_PHOTO_LIST_VISIBLE_ROWS 14

typedef struct {
  int x;
  int y;
  int width;
  int height;
} ink_epd_region_t;

typedef struct {
  const char *name;
} ink_epd_photo_row_t;

typedef struct {
  ink_cpfont_t *title;
  ink_cpfont_t *body;
  ink_cpfont_t *footer;
} ink_epd_ui_fonts_t;

void ink_epd_ui_clear(uint8_t *buffer, size_t length, bool white);
void ink_epd_ui_set_pixel(uint8_t *buffer, size_t length, int x, int y,
                          bool black);
void ink_epd_ui_fill_rect(uint8_t *buffer, size_t length, int x, int y,
                          int width, int height, bool black);
void ink_epd_ui_draw_text(uint8_t *buffer, size_t length, int x, int y,
                          int scale, const char *text, bool black);
bool ink_epd_ui_measure_text(ink_cpfont_t *font, const char *text,
                             int ascii_scale, uint8_t font_scale_divisor,
                             int *out_width);
bool ink_epd_ui_draw_text_font(uint8_t *buffer, size_t length,
                               ink_cpfont_t *font, int x, int y,
                               int ascii_scale, uint8_t font_scale_divisor,
                               const char *text, int *out_width);
void ink_epd_ui_draw_launcher(uint8_t *buffer, size_t length, int selected);
ink_epd_region_t ink_epd_ui_launcher_selection_region(int previous,
                                                       int selected);
void ink_epd_ui_draw_photo_list(uint8_t *buffer, size_t length,
                                const ink_epd_photo_row_t *rows, size_t count,
                                size_t selected);
void ink_epd_ui_draw_photo_list_with_fonts(
    uint8_t *buffer, size_t length, const ink_epd_photo_row_t *rows,
    size_t count, size_t selected, const ink_epd_ui_fonts_t *fonts);
ink_epd_region_t ink_epd_ui_photo_list_selection_region(size_t previous,
                                                         size_t selected,
                                                         size_t count);
void ink_epd_ui_draw_status(uint8_t *buffer, size_t length, const char *title,
                            const char *message);
void ink_epd_ui_draw_status_with_fonts(uint8_t *buffer, size_t length,
                                       const char *title,
                                       const char *message,
                                       const ink_epd_ui_fonts_t *fonts);
bool ink_epd_ui_self_test(void);
