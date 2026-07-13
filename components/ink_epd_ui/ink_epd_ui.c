#include "ink_epd_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ink_cpfont.h"
#include "ink_ui_text_assets.h"

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

static const int kLauncherRows[2] = {
    INK_LAUNCHER_LIST_Y,
    INK_LAUNCHER_LIST_Y + INK_LAUNCHER_ROW_HEIGHT + INK_LAUNCHER_ROW_GAP,
};
static const int kLauncherLineX = INK_LAUNCHER_MARKER_X;
static const int kLauncherLineWidth = INK_LAUNCHER_MARKER_WIDTH;
static const int kLauncherLineHeight = INK_LAUNCHER_MARKER_HEIGHT;
static const int kLauncherLineYOffset = INK_LAUNCHER_MARKER_Y_OFFSET;
static const int kLauncherSelectionPadding = INK_LAUNCHER_MARKER_PADDING;
static const int kPhotoListX = 24;
static const int kPhotoListY = 50;
static const int kPhotoListWidth = 432;
static const int kPhotoListHeight = 614;
static const int kPhotoListContentInset = 16;
static const int kPhotoListMarkerXInset = 2;
static const int kPhotoListMarkerWidth = 4;
static const int kPhotoListMarkerInset = 8;
static const int kPhotoListTitleYOffset = 8;

static bool draw_builtin_text(uint8_t *buffer, size_t length, int x, int y,
                              const char *text, uint8_t pixel_size);
static uint8_t photo_body_font_scale_divisor(void);

_Static_assert(INK_LAUNCHER_HEADER_GUTTER == 24, "launcher gutter");
_Static_assert(INK_LAUNCHER_DIVIDER_Y == 38, "launcher divider");
_Static_assert(INK_LAUNCHER_LIST_X == 24 && INK_LAUNCHER_ROW_WIDTH == 432,
               "launcher list geometry");
_Static_assert(INK_LAUNCHER_ROW_HEIGHT == 70 && INK_LAUNCHER_ROW_GAP == 6,
               "launcher row geometry");
_Static_assert(INK_LAUNCHER_MARKER_Y_OFFSET ==
                   (INK_LAUNCHER_ROW_HEIGHT - INK_LAUNCHER_MARKER_HEIGHT) / 2,
               "launcher marker vertical center");

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

static bool draw_builtin_text(uint8_t *buffer, size_t length, int x, int y,
                              const char *text, uint8_t pixel_size) {
  const ink_ui_text_asset_t *asset =
      ink_ui_text_asset_find(text, pixel_size);
  if (!buffer || length < INK_EPD_BUFFER_SIZE || !asset) return false;

  const size_t stride = ((size_t)asset->width + 7U) / 8U;
  for (uint16_t py = 0; py < asset->height; ++py) {
    for (uint16_t px = 0; px < asset->width; ++px) {
      const uint8_t mask = (uint8_t)(0x80U >> (px & 7U));
      if (asset->bitmap[(size_t)py * stride + px / 8U] & mask)
        ink_epd_ui_set_pixel(buffer, length, x + px, y + py, true);
    }
  }
  return true;
}

static uint8_t photo_body_font_scale_divisor(void) { return 2U; }

static bool pixel_is_black(const uint8_t *buffer, int x, int y) {
  const size_t index = (size_t)y * (INK_EPD_WIDTH / 8) + (size_t)x / 8;
  const uint8_t mask = (uint8_t)(0x80U >> (x & 7));
  return !(buffer[index] & mask);
}

static bool region_has_black(const uint8_t *buffer, int x, int y, int width,
                             int height) {
  for (int py = y; py < y + height; ++py)
    for (int px = x; px < x + width; ++px)
      if (pixel_is_black(buffer, px, py)) return true;
  return false;
}

static bool text_is_ascii(const char *text) {
  if (!text) return false;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
    if (*p >= 0x80U) return false;
  return true;
}

bool ink_epd_ui_measure_text(ink_cpfont_t *font, const char *text,
                             int ascii_scale, uint8_t font_scale_divisor,
                             int *out_width) {
  if (out_width) *out_width = 0;
  if (!text || ascii_scale < 1 || font_scale_divisor == 0U) return false;
  if (ink_cpfont_is_loaded(font))
    return ink_cpfont_draw_text_bw_scaled(font, NULL, 0, 0, text,
                                          font_scale_divisor,
                                          out_width) == ESP_OK;
  if (!text_is_ascii(text)) return false;
  if (out_width) *out_width = (int)strlen(text) * 6 * ascii_scale;
  return true;
}

bool ink_epd_ui_draw_text_font(uint8_t *buffer, size_t length,
                               ink_cpfont_t *font, int x, int y,
                               int ascii_scale, uint8_t font_scale_divisor,
                               const char *text, int *out_width) {
  if (out_width) *out_width = 0;
  if (!buffer || length < INK_EPD_BUFFER_SIZE || !text || ascii_scale < 1 ||
      font_scale_divisor == 0U)
    return false;
  if (ink_cpfont_is_loaded(font))
    return ink_cpfont_draw_text_bw_scaled(font, buffer, x, y, text,
                                          font_scale_divisor,
                                          out_width) == ESP_OK;
  if (!text_is_ascii(text)) return false;
  ink_epd_ui_draw_text(buffer, length, x, y, ascii_scale, text, true);
  if (out_width) *out_width = (int)strlen(text) * 6 * ascii_scale;
  return true;
}

static void draw_hline(uint8_t *buffer, size_t length, int x, int y,
                       int width) {
  ink_epd_ui_fill_rect(buffer, length, x, y, width, 1, true);
}

static void draw_vline(uint8_t *buffer, size_t length, int x, int y,
                       int height) {
  ink_epd_ui_fill_rect(buffer, length, x, y, 1, height, true);
}

static void draw_rect_outline(uint8_t *buffer, size_t length, int x, int y,
                              int width, int height) {
  draw_hline(buffer, length, x, y, width);
  draw_hline(buffer, length, x, y + height - 1, width);
  draw_vline(buffer, length, x, y, height);
  draw_vline(buffer, length, x + width - 1, y, height);
}

static void draw_book_icon(uint8_t *buffer, size_t length, int x, int y) {
  draw_rect_outline(buffer, length, x, y, 18, 20);
  draw_vline(buffer, length, x + 4, y + 2, 16);
  draw_hline(buffer, length, x + 7, y + 5, 8);
  draw_hline(buffer, length, x + 7, y + 9, 8);
  draw_hline(buffer, length, x + 7, y + 13, 6);
}

static void draw_photo_icon(uint8_t *buffer, size_t length, int x, int y) {
  draw_rect_outline(buffer, length, x, y, 20, 18);
  ink_epd_ui_fill_rect(buffer, length, x + 4, y + 11, 5, 3, true);
  ink_epd_ui_fill_rect(buffer, length, x + 9, y + 9, 6, 5, true);
  ink_epd_ui_fill_rect(buffer, length, x + 14, y + 7, 3, 7, true);
  ink_epd_ui_fill_rect(buffer, length, x + 13, y + 3, 3, 3, true);
}

static void draw_chevron(uint8_t *buffer, size_t length, int x, int y) {
  for (int i = 0; i < 4; ++i)
    ink_epd_ui_set_pixel(buffer, length, x + i, y + i, true);
  for (int i = 1; i < 4; ++i)
    ink_epd_ui_set_pixel(buffer, length, x + 3 - i, y + 3 + i, true);
}

void ink_epd_ui_draw_launcher_with_fonts(
    uint8_t *buffer, size_t length, int selected,
    const ink_epd_ui_fonts_t *fonts) {
  if (!buffer || length < INK_EPD_BUFFER_SIZE) return;
  if (selected < 0) selected = 0;
  if (selected > 1) selected = 1;
  (void)fonts;
  ink_epd_ui_clear(buffer, length, true);
  if (!draw_builtin_text(buffer, length, INK_LAUNCHER_HEADER_GUTTER, 8,
                         "启动器", 24U))
    ink_epd_ui_draw_text(buffer, length, INK_LAUNCHER_HEADER_GUTTER, 8, 2,
                         "LAUNCHER", true);
  const ink_ui_text_asset_t *meta =
      ink_ui_text_asset_find("阅读 / 相册", 16U);
  const int meta_x = meta ? INK_EPD_WIDTH - INK_LAUNCHER_HEADER_GUTTER -
                                (int)meta->width
                          : 330;
  if (!draw_builtin_text(buffer, length, meta_x, 10, "阅读 / 相册", 16U))
    ink_epd_ui_draw_text(buffer, length, meta_x, 10, 1, "READER / PHOTO",
                         true);
  ink_epd_ui_fill_rect(
      buffer, length, INK_LAUNCHER_HEADER_GUTTER, INK_LAUNCHER_DIVIDER_Y,
      INK_EPD_WIDTH - 2 * INK_LAUNCHER_HEADER_GUTTER, 1, true);

  const char *titles[2] = {"书库", "相册"};
  const char *fallback_titles[2] = {"READER", "PHOTO"};
  const char *descriptions[2] = {"打开图书与最近阅读",
                                 "浏览 TF 卡灰阶图片"};
  const char *fallback_descriptions[2] = {"OPEN BOOKS FROM TF CARD",
                                          "BROWSE GRAYSCALE BMP"};
  for (int i = 0; i < 2; ++i) {
    const int row_y = kLauncherRows[i];
    if (selected == i)
      ink_epd_ui_fill_rect(buffer, length, kLauncherLineX,
                           row_y + kLauncherLineYOffset,
                           kLauncherLineWidth, kLauncherLineHeight, true);
    if (i == 0)
      draw_book_icon(buffer, length, INK_LAUNCHER_LIST_X + 16,
                     row_y + (INK_LAUNCHER_ROW_HEIGHT - 20) / 2);
    else
      draw_photo_icon(buffer, length, INK_LAUNCHER_LIST_X + 16,
                      row_y + (INK_LAUNCHER_ROW_HEIGHT - 18) / 2);
    if (!draw_builtin_text(buffer, length, 88, row_y + 10, titles[i], 24U))
      ink_epd_ui_draw_text(buffer, length, 88, row_y + 10, 1,
                           fallback_titles[i], true);
    if (!draw_builtin_text(buffer, length, 88, row_y + 40,
                           descriptions[i], 16U))
      ink_epd_ui_draw_text(buffer, length, 88, row_y + 40, 1,
                           fallback_descriptions[i], true);
    draw_chevron(buffer, length,
                 INK_LAUNCHER_LIST_X + INK_LAUNCHER_ROW_WIDTH - 26,
                 row_y + 22);
    draw_hline(buffer, length, INK_LAUNCHER_LIST_X,
               row_y + INK_LAUNCHER_ROW_HEIGHT - 1,
               INK_LAUNCHER_ROW_WIDTH);
  }
}

void ink_epd_ui_draw_launcher(uint8_t *buffer, size_t length, int selected) {
  ink_epd_ui_draw_launcher_with_fonts(buffer, length, selected, NULL);
}

ink_epd_region_t ink_epd_ui_launcher_selection_region(int previous,
                                                       int selected) {
  if (previous < 0) previous = 0;
  if (previous > 1) previous = 1;
  if (selected < 0) selected = 0;
  if (selected > 1) selected = 1;

  const int first = previous < selected ? previous : selected;
  const int last = previous > selected ? previous : selected;
  int left = kLauncherLineX - kLauncherSelectionPadding;
  int top = kLauncherRows[first] + kLauncherLineYOffset -
            kLauncherSelectionPadding;
  int right = kLauncherLineX + kLauncherLineWidth +
              kLauncherSelectionPadding;
  int bottom = kLauncherRows[last] + kLauncherLineYOffset +
               kLauncherLineHeight + kLauncherSelectionPadding;
  if (left < 0) left = 0;
  if (top < 0) top = 0;
  if (right > INK_EPD_WIDTH) right = INK_EPD_WIDTH;
  if (bottom > INK_EPD_HEIGHT) bottom = INK_EPD_HEIGHT;
  return (ink_epd_region_t){
      .x = left,
      .y = top,
      .width = right - left,
      .height = bottom - top,
  };
}

static size_t photo_list_window_start(size_t selected, size_t count) {
  if (count == 0) return 0;
  if (selected >= count) selected = count - 1;
  if (count <= INK_PHOTO_LIST_VISIBLE_ROWS) return 0;

  size_t start = selected > INK_PHOTO_LIST_VISIBLE_ROWS / 2
                     ? selected - INK_PHOTO_LIST_VISIBLE_ROWS / 2
                     : 0;
  const size_t last_start = count - INK_PHOTO_LIST_VISIBLE_ROWS;
  return start < last_start ? start : last_start;
}

static size_t utf8_unit_size(const unsigned char *text) {
  if (!text || text[0] == 0) return 0;
  if (text[0] < 0x80U) return 1;
  size_t size = 1;
  if (text[0] >= 0xC2U && text[0] <= 0xDFU)
    size = 2;
  else if (text[0] >= 0xE0U && text[0] <= 0xEFU)
    size = 3;
  else if (text[0] >= 0xF0U && text[0] <= 0xF4U)
    size = 4;
  for (size_t i = 1; i < size; ++i)
    if (text[i] == 0 || (text[i] & 0xC0U) != 0x80U) return 1;
  return size;
}

static bool utf8_truncate_tail(const char *src, char *dst, size_t dst_size,
                               size_t max_codepoints) {
  if (!src || !dst || dst_size == 0 || max_codepoints == 0) return false;

  size_t total = 0;
  for (const unsigned char *p = (const unsigned char *)src; *p;
       p += utf8_unit_size(p))
    ++total;

  const bool truncated = total > max_codepoints;
  const size_t keep = truncated && max_codepoints > 3
                          ? max_codepoints - 3
                          : max_codepoints;
  size_t used = 0;
  const unsigned char *p = (const unsigned char *)src;
  for (size_t index = 0; *p && index < keep; ++index) {
    const size_t unit_size = utf8_unit_size(p);
    if (unit_size > dst_size - used - 1U) return false;
    memcpy(dst + used, p, unit_size);
    used += unit_size;
    p += unit_size;
  }
  if (truncated && max_codepoints > 3) {
    if (3U > dst_size - used - 1U) return false;
    memcpy(dst + used, "...", 3U);
    used += 3U;
  }
  dst[used] = '\0';
  return true;
}

static void format_photo_title(char *title, size_t title_size,
                               size_t index, const char *name,
                               int max_width, ink_cpfont_t *font) {
  title[0] = 0;
  for (size_t max_codepoints = 24U; max_codepoints > 0U;
       --max_codepoints) {
    char display_name[128];
    if (!utf8_truncate_tail(name ? name : "", display_name,
                            sizeof(display_name), max_codepoints))
      continue;
    if (snprintf(title, title_size, "%02u. %s", (unsigned)(index + 1),
                 display_name) >= (int)title_size)
      continue;
    int width = 0;
    if (ink_epd_ui_measure_text(font, title, 2,
                                photo_body_font_scale_divisor(), &width) &&
        width <= max_width)
      return;
  }
  snprintf(title, title_size, "%02u.", (unsigned)(index + 1));
}

void ink_epd_ui_draw_photo_list_with_fonts(
    uint8_t *buffer, size_t length, const ink_epd_photo_row_t *rows,
    size_t count, size_t selected, const ink_epd_ui_fonts_t *fonts) {
  if (!buffer || length < INK_EPD_BUFFER_SIZE) return;
  ink_cpfont_t *title_font = fonts ? fonts->title : NULL;
  ink_cpfont_t *body_font = fonts ? fonts->body : NULL;
  ink_epd_ui_clear(buffer, length, true);
  if (ink_cpfont_is_loaded(title_font)) {
    (void)ink_epd_ui_draw_text_font(buffer, length, title_font, kPhotoListX,
                                    8, 2, 1U, "相册", NULL);
  } else if (!draw_builtin_text(buffer, length, kPhotoListX, 8, "相册",
                                12U)) {
    ink_epd_ui_draw_text(buffer, length, kPhotoListX, 8, 2, "PHOTO ALBUM",
                         true);
  }

  if (!rows || count == 0) {
    if (!draw_builtin_text(buffer, length,
                           kPhotoListX + kPhotoListContentInset, 88,
                           "未找到图片", 16U))
      (void)ink_epd_ui_draw_text_font(
          buffer, length, body_font,
          kPhotoListX + kPhotoListContentInset, 88, 2,
          photo_body_font_scale_divisor(),
          ink_cpfont_is_loaded(body_font) ? "未找到图片" : "NO PHOTOS FOUND",
          NULL);
    return;
  }
  if (selected >= count) selected = count - 1;

  const size_t start = photo_list_window_start(selected, count);
  size_t visible = count - start;
  if (visible > INK_PHOTO_LIST_VISIBLE_ROWS)
    visible = INK_PHOTO_LIST_VISIBLE_ROWS;
  for (size_t row = 0; row < visible; ++row) {
    const size_t index = start + row;
    const int y = kPhotoListY +
                  (int)row * (INK_PHOTO_LIST_ROW_HEIGHT +
                              INK_PHOTO_LIST_ROW_GAP);
    ink_epd_ui_fill_rect(buffer, length, kPhotoListX, y, kPhotoListWidth,
                         INK_PHOTO_LIST_ROW_HEIGHT, false);
    if (index == selected)
      ink_epd_ui_fill_rect(
          buffer, length, kPhotoListX + kPhotoListMarkerXInset,
          y + kPhotoListMarkerInset, kPhotoListMarkerWidth,
          INK_PHOTO_LIST_ROW_HEIGHT - 2 * kPhotoListMarkerInset, true);

    char title[160];
    const int text_x = kPhotoListX + kPhotoListContentInset;
    const int text_max_width = kPhotoListWidth - 2 * kPhotoListContentInset;
    format_photo_title(title, sizeof(title), index, rows[index].name,
                       text_max_width, body_font);
    (void)ink_epd_ui_draw_text_font(buffer, length, body_font, text_x,
                                    y + kPhotoListTitleYOffset, 2,
                                    photo_body_font_scale_divisor(), title,
                                    NULL);
  }
}

void ink_epd_ui_draw_photo_list(uint8_t *buffer, size_t length,
                                const ink_epd_photo_row_t *rows, size_t count,
                                size_t selected) {
  ink_epd_ui_draw_photo_list_with_fonts(buffer, length, rows, count, selected,
                                        NULL);
}

ink_epd_region_t ink_epd_ui_photo_list_selection_region(size_t previous,
                                                         size_t selected,
                                                         size_t count) {
  if (count == 0)
    return (ink_epd_region_t){.x = kPhotoListX,
                              .y = kPhotoListY,
                              .width = 0,
                              .height = 0};
  if (previous >= count) previous = count - 1;
  if (selected >= count) selected = count - 1;

  const size_t previous_start = photo_list_window_start(previous, count);
  const size_t selected_start = photo_list_window_start(selected, count);
  if (previous_start != selected_start)
    return (ink_epd_region_t){.x = kPhotoListX,
                              .y = kPhotoListY,
                              .width = kPhotoListWidth,
                              .height = kPhotoListHeight};

  const size_t first = previous < selected ? previous : selected;
  const size_t last = previous > selected ? previous : selected;
  const int top = kPhotoListY +
                  (int)(first - previous_start) *
                      (INK_PHOTO_LIST_ROW_HEIGHT + INK_PHOTO_LIST_ROW_GAP);
  const int bottom =
      kPhotoListY +
      (int)(last - previous_start) *
          (INK_PHOTO_LIST_ROW_HEIGHT + INK_PHOTO_LIST_ROW_GAP) +
      INK_PHOTO_LIST_ROW_HEIGHT;
  return (ink_epd_region_t){.x = kPhotoListX,
                            .y = top,
                            .width = kPhotoListWidth,
                            .height = bottom - top};
}

void ink_epd_ui_draw_status_with_fonts(uint8_t *buffer, size_t length,
                                       const char *title,
                                       const char *message,
                                       const ink_epd_ui_fonts_t *fonts) {
  ink_cpfont_t *title_font = fonts ? fonts->title : NULL;
  ink_cpfont_t *body_font = fonts ? fonts->body : NULL;
  ink_epd_ui_clear(buffer, length, true);
  const char *resolved_title = title ? title : "INK";
  const char *resolved_message = message ? message : "ERROR";
  if (!draw_builtin_text(buffer, length, INK_LAUNCHER_HEADER_GUTTER, 8,
                         resolved_title, 24U))
    (void)ink_epd_ui_draw_text_font(
        buffer, length, title_font, INK_LAUNCHER_HEADER_GUTTER, 8, 2, 1U,
        resolved_title, NULL);
  ink_epd_ui_fill_rect(
      buffer, length, INK_LAUNCHER_HEADER_GUTTER, INK_LAUNCHER_DIVIDER_Y,
      INK_EPD_WIDTH - 2 * INK_LAUNCHER_HEADER_GUTTER, 1, true);
  if (!draw_builtin_text(buffer, length, 40, 88, resolved_message, 16U))
    (void)ink_epd_ui_draw_text_font(buffer, length, body_font, 40, 88, 2, 2U,
                                    resolved_message, NULL);
}

void ink_epd_ui_draw_status(uint8_t *buffer, size_t length, const char *title,
                            const char *message) {
  ink_epd_ui_draw_status_with_fonts(buffer, length, title, message, NULL);
}

bool ink_epd_ui_self_test(void) {
  int measured_width = 0;
  if (!ink_epd_ui_measure_text(NULL, "ABC", 2, 1U, &measured_width) ||
      measured_width != 36)
    return false;
  uint8_t *buffer = malloc(INK_EPD_BUFFER_SIZE);
  if (!buffer) return false;

  ink_epd_ui_clear(buffer, INK_EPD_BUFFER_SIZE, true);
  if (!draw_builtin_text(buffer, INK_EPD_BUFFER_SIZE, 10, 10, "相册", 12U)) {
    free(buffer);
    return false;
  }
  const bool builtin_has_black = region_has_black(buffer, 10, 10, 40, 20);
  if (!builtin_has_black || photo_body_font_scale_divisor() != 2U) {
    free(buffer);
    return false;
  }

  ink_epd_ui_clear(buffer, INK_EPD_BUFFER_SIZE, true);
  if (ink_epd_ui_draw_text_font(buffer, INK_EPD_BUFFER_SIZE, NULL, 0, 0, 2,
                                1U, "相册", NULL)) {
    free(buffer);
    return false;
  }
  for (size_t i = 0; i < INK_EPD_BUFFER_SIZE; ++i) {
    if (buffer[i] != 0xFFU) {
      free(buffer);
      return false;
    }
  }

  const ink_epd_region_t same_window =
      ink_epd_ui_photo_list_selection_region(0, 1, 14);
  const ink_epd_region_t scrolled =
      ink_epd_ui_photo_list_selection_region(7, 8, 20);
  const ink_epd_region_t empty =
      ink_epd_ui_photo_list_selection_region(1, 2, 0);
  const ink_epd_region_t clamped_photo =
      ink_epd_ui_photo_list_selection_region(99, 99, 3);
  if (same_window.x != 24 || same_window.y != 50 ||
      same_window.width != 432 || same_window.height != 86 ||
      scrolled.x != 24 || scrolled.y != 50 || scrolled.width != 432 ||
      scrolled.height != 614 || empty.width != 0 || empty.height != 0 ||
      clamped_photo.x < 0 || clamped_photo.y < 0 ||
      clamped_photo.x + clamped_photo.width > INK_EPD_WIDTH ||
      clamped_photo.y + clamped_photo.height > INK_EPD_HEIGHT) {
    free(buffer);
    return false;
  }

  ink_epd_ui_draw_photo_list(buffer, INK_EPD_BUFFER_SIZE, NULL, 0, 0);
  const bool empty_prompt_has_black =
      region_has_black(buffer, 40, 88, 160, 24);
  ink_epd_photo_row_t rows[2] = {{.name = "ONE"}, {.name = "TWO"}};
  ink_epd_ui_draw_photo_list(buffer, INK_EPD_BUFFER_SIZE, rows, 2, 0);
  const size_t header_clear_index =
      (size_t)12 * (INK_EPD_WIDTH / 8) + 422 / 8;
  const uint8_t header_clear_mask = (uint8_t)(0x80u >> (422 & 7));
  const size_t selected_right_index =
      (size_t)60 * (INK_EPD_WIDTH / 8) + 406 / 8;
  const uint8_t selected_right_mask = (uint8_t)(0x80u >> (406 & 7));
  const size_t marker_index = (size_t)58 * (INK_EPD_WIDTH / 8) + 26 / 8;
  const uint8_t marker_mask = (uint8_t)(0x80u >> (26 & 7));
  const size_t marker_end_index =
      (size_t)83 * (INK_EPD_WIDTH / 8) + 29 / 8;
  const uint8_t marker_end_mask = (uint8_t)(0x80u >> (29 & 7));
  const size_t marker_boundary_index =
      (size_t)57 * (INK_EPD_WIDTH / 8) + 26 / 8;
  const uint8_t marker_boundary_mask = (uint8_t)(0x80u >> (26 & 7));
  const size_t row_white_index = (size_t)51 * (INK_EPD_WIDTH / 8) + 30 / 8;
  const uint8_t row_white_mask = (uint8_t)(0x80u >> (30 & 7));
  const size_t text_index = (size_t)60 * (INK_EPD_WIDTH / 8) + 40 / 8;
  const uint8_t text_mask = (uint8_t)(0x80u >> (40 & 7));
  if (!empty_prompt_has_black ||
      !(buffer[header_clear_index] & header_clear_mask) ||
      !(buffer[selected_right_index] & selected_right_mask) ||
      (buffer[marker_index] & marker_mask) ||
      (buffer[marker_end_index] & marker_end_mask) ||
      !(buffer[marker_boundary_index] & marker_boundary_mask) ||
      !(buffer[row_white_index] & row_white_mask) ||
      (buffer[text_index] & text_mask)) {
    free(buffer);
    return false;
  }

  ink_epd_ui_draw_photo_list(buffer, INK_EPD_BUFFER_SIZE, rows, 2, 1);
  const size_t second_marker_index =
      (size_t)102 * (INK_EPD_WIDTH / 8) + 26 / 8;
  const uint8_t second_marker_mask = (uint8_t)(0x80u >> (26 & 7));
  if (!(buffer[marker_index] & marker_mask) ||
      (buffer[second_marker_index] & second_marker_mask)) {
    free(buffer);
    return false;
  }

  const ink_epd_region_t moved = ink_epd_ui_launcher_selection_region(0, 1);
  const ink_epd_region_t unchanged =
      ink_epd_ui_launcher_selection_region(0, 0);
  const ink_epd_region_t clamped =
      ink_epd_ui_launcher_selection_region(-100, 100);
  if (moved.x != 22 || moved.y != 79 || moved.width != 20 ||
      moved.height != 88 || unchanged.x != 22 || unchanged.y != 79 ||
      unchanged.width != 20 || unchanged.height != 12 || clamped.x < 0 ||
      clamped.y < 0 || clamped.x + clamped.width > INK_EPD_WIDTH ||
      clamped.y + clamped.height > INK_EPD_HEIGHT) {
    free(buffer);
    return false;
  }

  ink_epd_ui_clear(buffer, INK_EPD_BUFFER_SIZE, true);
  ink_epd_ui_set_pixel(buffer, INK_EPD_BUFFER_SIZE, 0, 0, true);
  if (buffer[0] != 0x7f) {
    free(buffer);
    return false;
  }
  ink_epd_ui_set_pixel(buffer, INK_EPD_BUFFER_SIZE, -1, 0, true);
  ink_epd_ui_draw_launcher(buffer, INK_EPD_BUFFER_SIZE, 0);
  const size_t white_index = (size_t)270 * (INK_EPD_WIDTH / 8) + 70 / 8;
  const uint8_t white_mask = (uint8_t)(0x80u >> (70 & 7));
  const size_t book_icon_index = (size_t)75 * (INK_EPD_WIDTH / 8) + 40 / 8;
  const uint8_t book_icon_mask = (uint8_t)(0x80u >> (40 & 7));
  const size_t photo_icon_index = (size_t)152 * (INK_EPD_WIDTH / 8) + 40 / 8;
  const uint8_t photo_icon_mask = (uint8_t)(0x80u >> (40 & 7));
  const size_t chevron_index = (size_t)72 * (INK_EPD_WIDTH / 8) + 430 / 8;
  const uint8_t chevron_mask = (uint8_t)(0x80u >> (430 & 7));
  bool launcher_style_valid = (buffer[white_index] & white_mask) &&
                              region_has_black(buffer, 88, 60, 32, 16) &&
                              region_has_black(buffer, 88, 136, 32, 16) &&
                              region_has_black(buffer, 88, 90, 180, 20) &&
                              region_has_black(buffer, 88, 166, 180, 20) &&
                              !(buffer[book_icon_index] & book_icon_mask) &&
                              !(buffer[photo_icon_index] & photo_icon_mask) &&
                              !(buffer[chevron_index] & chevron_mask) &&
                              pixel_is_black(buffer, 100, 119) &&
                              pixel_is_black(buffer, 100, 195);
  for (int y = 0; y < kLauncherLineHeight; ++y) {
    for (int x = 0; x < kLauncherLineWidth; ++x) {
      const int px = kLauncherLineX + x;
      const size_t selected_index =
          (size_t)(kLauncherRows[0] + kLauncherLineYOffset + y) *
              (INK_EPD_WIDTH / 8) +
          (size_t)px / 8;
      const size_t unselected_index =
          (size_t)(kLauncherRows[1] + kLauncherLineYOffset + y) *
              (INK_EPD_WIDTH / 8) +
          (size_t)px / 8;
      const uint8_t mask = (uint8_t)(0x80u >> (px & 7));
      launcher_style_valid = launcher_style_valid &&
                             !(buffer[selected_index] & mask) &&
                             (buffer[unselected_index] & mask);
    }
  }
  const int boundary_x[4] = {kLauncherLineX - 1,
                             kLauncherLineX + kLauncherLineWidth,
                             kLauncherLineX, kLauncherLineX};
  const int boundary_y[4] = {
      kLauncherRows[0] + kLauncherLineYOffset,
      kLauncherRows[0] + kLauncherLineYOffset,
      kLauncherRows[0] + kLauncherLineYOffset - 1,
      kLauncherRows[0] + kLauncherLineYOffset + kLauncherLineHeight};
  for (int i = 0; i < 4; ++i) {
    const size_t index =
        (size_t)boundary_y[i] * (INK_EPD_WIDTH / 8) + boundary_x[i] / 8;
    const uint8_t mask = (uint8_t)(0x80u >> (boundary_x[i] & 7));
    launcher_style_valid = launcher_style_valid && (buffer[index] & mask);
  }
  if (!launcher_style_valid) {
    free(buffer);
    return false;
  }
  bool changed = false;
  for (size_t i = 0; i < INK_EPD_BUFFER_SIZE; ++i) {
    if (buffer[i] != 0xff) {
      changed = true;
      break;
    }
  }
  free(buffer);
  return changed;
}
