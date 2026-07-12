#include "ink_epd_ui.h"

#include <string.h>

static const uint8_t kFont5x7[59][5] = {{0, 0, 0, 0, 0},
                                        {0, 0, 0x5f, 0, 0},
                                        {0, 7, 0, 7, 0},
                                        {0x14, 0x7f, 0x14, 0x7f, 0x14},
                                        {0x24, 0x2a, 0x7f, 0x2a, 0x12},
                                        {0x23, 0x13, 8, 0x64, 0x62},
                                        {0x36, 0x49, 0x55, 0x22, 0x50},
                                        {0, 5, 3, 0, 0},
                                        {0, 0x1c, 0x22, 0x41, 0},
                                        {0, 0x41, 0x22, 0x1c, 0},
                                        {0x14, 8, 0x3e, 8, 0x14},
                                        {8, 8, 0x3e, 8, 8},
                                        {0, 0x50, 0x30, 0, 0},
                                        {8, 8, 8, 8, 8},
                                        {0, 0x60, 0x60, 0, 0},
                                        {0x20, 0x10, 8, 4, 2},
                                        {0x3e, 0x51, 0x49, 0x45, 0x3e},
                                        {0, 0x42, 0x7f, 0x40, 0},
                                        {0x42, 0x61, 0x51, 0x49, 0x46},
                                        {0x21, 0x41, 0x45, 0x4b, 0x31},
                                        {0x18, 0x14, 0x12, 0x7f, 0x10},
                                        {0x27, 0x45, 0x45, 0x45, 0x39},
                                        {0x3c, 0x4a, 0x49, 0x49, 0x30},
                                        {1, 0x71, 9, 5, 3},
                                        {0x36, 0x49, 0x49, 0x49, 0x36},
                                        {6, 0x49, 0x49, 0x29, 0x1e},
                                        {0, 0x36, 0x36, 0, 0},
                                        {0, 0x56, 0x36, 0, 0},
                                        {8, 0x14, 0x22, 0x41, 0},
                                        {0x14, 0x14, 0x14, 0x14, 0x14},
                                        {0, 0x41, 0x22, 0x14, 8},
                                        {2, 1, 0x51, 9, 6},
                                        {0x32, 0x49, 0x79, 0x41, 0x3e},
                                        {0x7e, 0x11, 0x11, 0x11, 0x7e},
                                        {0x7f, 0x49, 0x49, 0x49, 0x36},
                                        {0x3e, 0x41, 0x41, 0x41, 0x22},
                                        {0x7f, 0x41, 0x41, 0x22, 0x1c},
                                        {0x7f, 0x49, 0x49, 0x49, 0x41},
                                        {0x7f, 9, 9, 9, 1},
                                        {0x3e, 0x41, 0x49, 0x49, 0x7a},
                                        {0x7f, 8, 8, 8, 0x7f},
                                        {0, 0x41, 0x7f, 0x41, 0},
                                        {0x20, 0x40, 0x41, 0x3f, 1},
                                        {0x7f, 8, 0x14, 0x22, 0x41},
                                        {0x7f, 0x40, 0x40, 0x40, 0x40},
                                        {0x7f, 2, 0x0c, 2, 0x7f},
                                        {0x7f, 4, 8, 0x10, 0x7f},
                                        {0x3e, 0x41, 0x41, 0x41, 0x3e},
                                        {0x7f, 9, 9, 9, 6},
                                        {0x3e, 0x41, 0x51, 0x21, 0x5e},
                                        {0x7f, 9, 0x19, 0x29, 0x46},
                                        {0x46, 0x49, 0x49, 0x49, 0x31},
                                        {1, 1, 0x7f, 1, 1},
                                        {0x3f, 0x40, 0x40, 0x40, 0x3f},
                                        {0x1f, 0x20, 0x40, 0x20, 0x1f},
                                        {0x3f, 0x40, 0x38, 0x40, 0x3f},
                                        {0x63, 0x14, 8, 0x14, 0x63},
                                        {7, 8, 0x70, 8, 7},
                                        {0x61, 0x51, 0x49, 0x45, 0x43}};

static const uint8_t *glyph_for(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
  if (ch < ' ' || ch > 'Z') ch = '?';
  return kFont5x7[(unsigned char)ch - ' '];
}

void ink_epd_ui_clear(uint8_t *buffer, size_t length, bool white) {
  if (buffer && length >= INK_EPD_BUFFER_SIZE)
    memset(buffer, white ? 0xff : 0x00, INK_EPD_BUFFER_SIZE);
}

void ink_epd_ui_set_pixel(uint8_t *buffer, size_t length, int x, int y,
                          bool black) {
  if (!buffer || length < INK_EPD_BUFFER_SIZE || x < 0 || y < 0 ||
      x >= INK_EPD_WIDTH || y >= INK_EPD_HEIGHT)
    return;
  const size_t index = (size_t)y * (INK_EPD_WIDTH / 8) + (size_t)x / 8;
  const uint8_t mask = (uint8_t)(0x80u >> (x & 7));
  if (black)
    buffer[index] &= (uint8_t)~mask;
  else
    buffer[index] |= mask;
}

void ink_epd_ui_fill_rect(uint8_t *buffer, size_t length, int x, int y,
                          int width, int height, bool black) {
  for (int py = y; py < y + height; ++py)
    for (int px = x; px < x + width; ++px)
      ink_epd_ui_set_pixel(buffer, length, px, py, black);
}

void ink_epd_ui_draw_text(uint8_t *buffer, size_t length, int x, int y,
                          int scale, const char *text, bool black) {
  if (!text || scale < 1) return;
  for (; *text; ++text, x += 6 * scale) {
    const uint8_t *glyph = glyph_for(*text);
    for (int col = 0; col < 5; ++col)
      for (int row = 0; row < 7; ++row) {
        if (glyph[col] & (1u << row))
          ink_epd_ui_fill_rect(buffer, length, x + col * scale, y + row * scale,
                               scale, scale, black);
      }
  }
}

void ink_epd_ui_draw_launcher(uint8_t *buffer, size_t length, int selected) {
  if (!buffer || length < INK_EPD_BUFFER_SIZE) return;
  ink_epd_ui_clear(buffer, length, true);
  ink_epd_ui_draw_text(buffer, length, 78, 100, 5, "INK READER", true);
  const int rows[2] = {300, 440};
  const char *labels[2] = {"READER", "PHOTO"};
  for (int i = 0; i < 2; ++i) {
    const bool active = selected == i;
    ink_epd_ui_fill_rect(buffer, length, 65, rows[i] - 35, 350, 100, active);
    ink_epd_ui_draw_text(buffer, length, 135, rows[i], 4, labels[i], !active);
  }
  ink_epd_ui_draw_text(buffer, length, 120, 690, 2, "LEFT RIGHT  CONFIRM",
                       true);
}

void ink_epd_ui_draw_status(uint8_t *buffer, size_t length, const char *title,
                            const char *message) {
  ink_epd_ui_clear(buffer, length, true);
  ink_epd_ui_draw_text(buffer, length, 40, 100, 4, title ? title : "INK", true);
  ink_epd_ui_fill_rect(buffer, length, 40, 180, 400, 4, true);
  ink_epd_ui_draw_text(buffer, length, 40, 260, 3, message ? message : "ERROR",
                       true);
}

bool ink_epd_ui_self_test(void) {
  uint8_t buffer[INK_EPD_BUFFER_SIZE];
  ink_epd_ui_clear(buffer, sizeof(buffer), true);
  ink_epd_ui_set_pixel(buffer, sizeof(buffer), 0, 0, true);
  if (buffer[0] != 0x7f) return false;
  ink_epd_ui_set_pixel(buffer, sizeof(buffer), -1, 0, true);
  ink_epd_ui_draw_launcher(buffer, sizeof(buffer), 0);
  for (size_t i = 0; i < sizeof(buffer); ++i)
    if (buffer[i] != 0xff) return true;
  return false;
}
