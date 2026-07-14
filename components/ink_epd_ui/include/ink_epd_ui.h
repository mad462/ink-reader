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
#define INK_LAUNCHER_HEADER_GUTTER 24
#define INK_LAUNCHER_DIVIDER_Y 38
#define INK_LAUNCHER_LIST_X 24
#define INK_LAUNCHER_LIST_Y 50
#define INK_LAUNCHER_ROW_WIDTH 432
#define INK_LAUNCHER_ROW_HEIGHT 70
#define INK_LAUNCHER_ROW_GAP 6
#define INK_LAUNCHER_MARKER_X 26
#define INK_LAUNCHER_MARKER_WIDTH 12
#define INK_LAUNCHER_MARKER_HEIGHT 4
#define INK_LAUNCHER_MARKER_Y_OFFSET 33
#define INK_LAUNCHER_MARKER_PADDING 4
#define INK_EPD_UI_MENU_TAB_CAPACITY 3
#define INK_EPD_UI_MENU_CARD_CAPACITY 8
#define INK_EPD_UI_MENU_ACTION_CAPACITY 4
#define INK_EPD_UI_READER_MENU_TAB_CAPACITY 2
#define INK_EPD_UI_READER_MENU_ITEM_CAPACITY 8
#define INK_EPD_UI_READER_MENU_BOOKMARK_VISIBLE 6
#define INK_EPD_UI_READER_MENU_ACTION_CAPACITY 3

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

typedef struct {
  const char *label;
  bool active;
  bool focused;
} ink_epd_ui_library_tab_t;

typedef struct {
  const char *title;
  const char *line1;
  const char *line2;
  bool selected;
  bool trailing_favorite;
} ink_epd_ui_library_card_t;

typedef struct {
  const char *label;
  bool selected;
} ink_epd_ui_library_action_t;

typedef struct {
  const char *header_title;
  const char *header_meta;
  ink_epd_ui_library_tab_t tabs[INK_EPD_UI_MENU_TAB_CAPACITY];
  size_t tab_count;
  ink_epd_ui_library_card_t cards[INK_EPD_UI_MENU_CARD_CAPACITY];
  size_t card_count;
  bool popup_open;
  const char *popup_title;
  ink_epd_ui_library_action_t actions[INK_EPD_UI_MENU_ACTION_CAPACITY];
  size_t action_count;
} ink_epd_ui_library_view_t;

typedef struct {
  size_t active_tab;
  bool tabs_focused;
  size_t window_start;
  size_t selected_card;
  bool popup_open;
  size_t selected_action;
} ink_epd_ui_library_focus_t;

typedef struct {
  const char *title;
  const char *line1;
  bool selected;
} ink_epd_ui_reader_menu_item_t;

typedef struct {
  ink_epd_ui_library_tab_t tabs[INK_EPD_UI_READER_MENU_TAB_CAPACITY];
  size_t tab_count;
  bool bookmarks_tab;
  ink_epd_ui_reader_menu_item_t
      items[INK_EPD_UI_READER_MENU_ITEM_CAPACITY];
  size_t item_count;
  bool popup_open;
  const char *popup_title;
  ink_epd_ui_library_action_t
      actions[INK_EPD_UI_READER_MENU_ACTION_CAPACITY];
  size_t action_count;
} ink_epd_ui_reader_menu_view_t;

typedef struct {
  size_t active_tab;
  bool tabs_focused;
  bool bookmarks_tab;
  size_t window_start;
  size_t selected_item;
  bool popup_open;
  size_t selected_action;
} ink_epd_ui_reader_menu_focus_t;

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
void ink_epd_ui_draw_launcher_with_fonts(
    uint8_t *buffer, size_t length, int selected,
    const ink_epd_ui_fonts_t *fonts);
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
void ink_epd_ui_draw_library(uint8_t *buffer, size_t length,
                             const ink_epd_ui_library_view_t *view,
                             const ink_epd_ui_fonts_t *fonts);
ink_epd_region_t ink_epd_ui_library_selection_region(
    const ink_epd_ui_library_focus_t *previous,
    const ink_epd_ui_library_focus_t *current);
void ink_epd_ui_draw_reader_menu(
    uint8_t *buffer, size_t length,
    const ink_epd_ui_reader_menu_view_t *view,
    const ink_epd_ui_fonts_t *fonts);
ink_epd_region_t ink_epd_ui_reader_menu_selection_region(
    const ink_epd_ui_reader_menu_focus_t *previous,
    const ink_epd_ui_reader_menu_focus_t *current);
void ink_epd_ui_draw_reader_footer(uint8_t *buffer, size_t length,
                                   ink_cpfont_t *font,
                                   const char *left_text,
                                   const char *right_text);
bool ink_epd_ui_reader_self_test(void);
bool ink_epd_ui_self_test(void);
