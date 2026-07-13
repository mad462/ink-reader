#include "ink_epd_ui.h"

#include <stdio.h>
#include <stdlib.h>
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

static const int kLauncherRows[2] = {300, 440};
static const int kLauncherLineX = 96;
static const int kLauncherLineWidth = 20;
static const int kLauncherLineHeight = 4;
static const int kLauncherLineYOffset = 12;
static const int kLauncherSelectionPadding = 4;
static const int kPhotoListX = 24;
static const int kPhotoListY = 50;
static const int kPhotoListWidth = 432;
static const int kPhotoListHeight = 614;
static const int kPhotoListContentInset = 16;
static const int kPhotoListMarkerXInset = 2;
static const int kPhotoListMarkerWidth = 4;
static const int kPhotoListMarkerInset = 8;
static const int kPhotoListTitleYOffset = 8;
static const int kPhotoListSelectionPadding = 4;

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
  const char *labels[2] = {"READER", "PHOTO"};
  for (int i = 0; i < 2; ++i) {
    if (selected == i)
      ink_epd_ui_fill_rect(buffer, length, kLauncherLineX,
                           kLauncherRows[i] + kLauncherLineYOffset,
                           kLauncherLineWidth, kLauncherLineHeight, true);
    ink_epd_ui_draw_text(buffer, length, 135, kLauncherRows[i], 4, labels[i],
                         true);
  }
  ink_epd_ui_draw_text(buffer, length, 120, 690, 2, "LEFT RIGHT  CONFIRM",
                       true);
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

void ink_epd_ui_draw_photo_list(uint8_t *buffer, size_t length,
                                const ink_epd_photo_row_t *rows, size_t count,
                                size_t selected) {
  if (!buffer || length < INK_EPD_BUFFER_SIZE) return;
  ink_epd_ui_clear(buffer, length, true);
  ink_epd_ui_draw_text(buffer, length, kPhotoListX, 8, 3, "PHOTO ALBUM",
                       true);

  if (!rows || count == 0) {
    ink_epd_ui_draw_text(buffer, length, kPhotoListX + kPhotoListContentInset,
                         88, 3, "NO PHOTOS FOUND", true);
    return;
  }
  if (selected >= count) selected = count - 1;

  char counter[32];
  snprintf(counter, sizeof(counter), "%u/%u", (unsigned)(selected + 1),
           (unsigned)count);
  const int counter_width = (int)strlen(counter) * 12;
  ink_epd_ui_draw_text(buffer, length,
                       kPhotoListX + kPhotoListWidth - counter_width, 10, 2,
                       counter, true);

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

    char title[35];
    snprintf(title, sizeof(title), "%02u. %s", (unsigned)(index + 1),
             rows[index].name ? rows[index].name : "");
    ink_epd_ui_draw_text(buffer, length,
                         kPhotoListX + kPhotoListContentInset,
                         y + kPhotoListTitleYOffset, 2, title, true);
  }
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
  int top = kPhotoListY +
            (int)(first - previous_start) *
                (INK_PHOTO_LIST_ROW_HEIGHT + INK_PHOTO_LIST_ROW_GAP) -
            kPhotoListSelectionPadding;
  int bottom = kPhotoListY +
               (int)(last - previous_start) *
                   (INK_PHOTO_LIST_ROW_HEIGHT + INK_PHOTO_LIST_ROW_GAP) +
               INK_PHOTO_LIST_ROW_HEIGHT + kPhotoListSelectionPadding;
  if (top < 0) top = 0;
  if (bottom > INK_EPD_HEIGHT) bottom = INK_EPD_HEIGHT;
  int left = kPhotoListX - kPhotoListSelectionPadding;
  int right = kPhotoListX + kPhotoListWidth + kPhotoListSelectionPadding;
  if (left < 0) left = 0;
  if (right > INK_EPD_WIDTH) right = INK_EPD_WIDTH;
  return (ink_epd_region_t){.x = left,
                            .y = top,
                            .width = right - left,
                            .height = bottom - top};
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
  uint8_t *buffer = malloc(INK_EPD_BUFFER_SIZE);
  if (!buffer) return false;

  const ink_epd_region_t same_window =
      ink_epd_ui_photo_list_selection_region(0, 1, 14);
  const ink_epd_region_t scrolled =
      ink_epd_ui_photo_list_selection_region(7, 8, 20);
  const ink_epd_region_t empty =
      ink_epd_ui_photo_list_selection_region(1, 2, 0);
  const ink_epd_region_t clamped_photo =
      ink_epd_ui_photo_list_selection_region(99, 99, 3);
  if (same_window.x != 20 || same_window.y != 46 ||
      same_window.width != 440 || same_window.height != 92 ||
      scrolled.x != 24 || scrolled.y != 50 || scrolled.width != 432 ||
      scrolled.height != 614 || empty.width != 0 || empty.height != 0 ||
      clamped_photo.x < 0 || clamped_photo.y < 0 ||
      clamped_photo.x + clamped_photo.width > INK_EPD_WIDTH ||
      clamped_photo.y + clamped_photo.height > INK_EPD_HEIGHT) {
    free(buffer);
    return false;
  }

  ink_epd_ui_draw_photo_list(buffer, INK_EPD_BUFFER_SIZE, NULL, 0, 0);
  const size_t prompt_index = (size_t)88 * (INK_EPD_WIDTH / 8) + 40 / 8;
  const uint8_t prompt_mask = (uint8_t)(0x80u >> (40 & 7));
  const bool empty_prompt_has_black = !(buffer[prompt_index] & prompt_mask);
  ink_epd_photo_row_t rows[2] = {{.name = "ONE"}, {.name = "TWO"}};
  ink_epd_ui_draw_photo_list(buffer, INK_EPD_BUFFER_SIZE, rows, 2, 0);
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
      (buffer[marker_index] & marker_mask) ||
      (buffer[marker_end_index] & marker_end_mask) ||
      !(buffer[marker_boundary_index] & marker_boundary_mask) ||
      !(buffer[row_white_index] & row_white_mask) ||
      (buffer[text_index] & text_mask)) {
    free(buffer);
    return false;
  }

  const ink_epd_region_t moved = ink_epd_ui_launcher_selection_region(0, 1);
  const ink_epd_region_t unchanged =
      ink_epd_ui_launcher_selection_region(0, 0);
  const ink_epd_region_t clamped =
      ink_epd_ui_launcher_selection_region(-100, 100);
  if (moved.x != 92 || moved.y != 308 || moved.width != 28 ||
      moved.height != 152 || unchanged.x != 92 || unchanged.y != 308 ||
      unchanged.width != 28 || unchanged.height != 12 || clamped.x < 0 ||
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
  const size_t reader_index = (size_t)300 * (INK_EPD_WIDTH / 8) + 135 / 8;
  const uint8_t reader_mask = (uint8_t)(0x80u >> (135 & 7));
  const size_t photo_index = (size_t)440 * (INK_EPD_WIDTH / 8) + 135 / 8;
  const uint8_t photo_mask = (uint8_t)(0x80u >> (135 & 7));
  bool launcher_style_valid = (buffer[white_index] & white_mask) &&
                              !(buffer[reader_index] & reader_mask) &&
                              !(buffer[photo_index] & photo_mask);
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
