#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INK_EPD_WIDTH 480
#define INK_EPD_HEIGHT 800
#define INK_EPD_BUFFER_SIZE (INK_EPD_WIDTH * INK_EPD_HEIGHT / 8)

void ink_epd_ui_clear(uint8_t *buffer, size_t length, bool white);
void ink_epd_ui_set_pixel(uint8_t *buffer, size_t length, int x, int y,
                          bool black);
void ink_epd_ui_fill_rect(uint8_t *buffer, size_t length, int x, int y,
                          int width, int height, bool black);
void ink_epd_ui_draw_text(uint8_t *buffer, size_t length, int x, int y,
                          int scale, const char *text, bool black);
void ink_epd_ui_draw_launcher(uint8_t *buffer, size_t length, int selected);
void ink_epd_ui_draw_status(uint8_t *buffer, size_t length, const char *title,
                            const char *message);
bool ink_epd_ui_self_test(void);
