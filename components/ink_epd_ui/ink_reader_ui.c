#include "ink_epd_ui.h"

#include <stdlib.h>
#include <string.h>

enum {
  LIBRARY_PAGE_X = 8,
  LIBRARY_PAGE_Y = 8,
  LIBRARY_PAGE_WIDTH = 464,
  LIBRARY_PAGE_HEIGHT = 776,
  LIBRARY_HEADER_HEIGHT = 38,
  LIBRARY_TAB_X = 24,
  LIBRARY_TAB_Y = 58,
  LIBRARY_TAB_WIDTH = 138,
  LIBRARY_TAB_GAP = 8,
  LIBRARY_TAB_HEIGHT = 42,
  LIBRARY_CARD_X = 24,
  LIBRARY_CARD_Y = 108,
  LIBRARY_CARD_WIDTH = 432,
  LIBRARY_CARD_HEIGHT = 58,
  LIBRARY_CARD_GAP = 6,
  LIBRARY_POPUP_X = 54,
  LIBRARY_POPUP_Y = 314,
  LIBRARY_POPUP_WIDTH = 372,
  LIBRARY_POPUP_HEIGHT = 164,
  LIBRARY_ACTION_HEIGHT = 34,
  LIBRARY_ACTION_GAP = 10,
  READER_MENU_PANEL_X = 24,
  READER_MENU_PANEL_Y = 118,
  READER_MENU_PANEL_WIDTH = 432,
  READER_MENU_PANEL_HEIGHT = 534,
  READER_MENU_TAB_Y = 136,
  READER_MENU_TAB_HEIGHT = 42,
  READER_MENU_TAB_GAP = 10,
  READER_MENU_CHAPTER_Y = 200,
  READER_MENU_CHAPTER_HEIGHT = 48,
  READER_MENU_CHAPTER_GAP = 8,
  READER_MENU_BOOKMARK_Y = 200,
  READER_MENU_BOOKMARK_HEIGHT = 58,
  READER_MENU_BOOKMARK_GAP = 8,
  READER_MENU_POPUP_X = 70,
  READER_MENU_POPUP_Y = 281,
  READER_MENU_POPUP_WIDTH = 340,
  READER_MENU_POPUP_HEIGHT = 208,
  READER_FOOTER_BAND_Y = 780,
  READER_FOOTER_BAND_HEIGHT = 20,
  READER_FOOTER_SEPARATOR_Y = 779,
  READER_FOOTER_TEXT_Y = 782,
  READER_FOOTER_LEFT_X = 8,
  READER_FOOTER_RIGHT_X = 472,
  READER_FOOTER_TEXT_GAP = 8,
};

static void draw_outline(uint8_t *buffer, size_t length, int x, int y,
                         int width, int height) {
  if (width <= 0 || height <= 0) return;
  ink_epd_ui_fill_rect(buffer, length, x, y, width, 1, true);
  ink_epd_ui_fill_rect(buffer, length, x, y + height - 1, width, 1, true);
  ink_epd_ui_fill_rect(buffer, length, x, y, 1, height, true);
  ink_epd_ui_fill_rect(buffer, length, x + width - 1, y, 1, height, true);
}

static size_t utf8_character_length(unsigned char first) {
  if (first < 0x80U) return 1U;
  if ((first & 0xE0U) == 0xC0U) return 2U;
  if ((first & 0xF0U) == 0xE0U) return 3U;
  if ((first & 0xF8U) == 0xF0U) return 4U;
  return 1U;
}

static void draw_clipped_text(uint8_t *buffer, size_t length,
                              ink_cpfont_t *font, int x, int y, int max_width,
                              int ascii_scale, uint8_t font_scale_divisor,
                              const char *text) {
  if (!text || max_width <= 0) return;

  char clipped[192];
  size_t source_offset = 0;
  size_t clipped_length = 0;
  while (text[source_offset] != '\0' &&
         clipped_length + 4U < sizeof(clipped)) {
    const size_t character_length =
        utf8_character_length((unsigned char)text[source_offset]);
    size_t available = strlen(text + source_offset);
    if (character_length > available) break;
    memcpy(clipped + clipped_length, text + source_offset, character_length);
    clipped_length += character_length;
    clipped[clipped_length] = '\0';

    int measured_width = 0;
    if (!ink_epd_ui_measure_text(font, clipped, ascii_scale,
                                 font_scale_divisor, &measured_width) ||
        measured_width > max_width) {
      clipped_length -= character_length;
      clipped[clipped_length] = '\0';
      break;
    }
    source_offset += character_length;
  }

  if (clipped_length > 0U)
    (void)ink_epd_ui_draw_text_font(buffer, length, font, x, y, ascii_scale,
                                    font_scale_divisor, clipped, NULL);
}

static void draw_heart_icon(uint8_t *buffer, size_t length, int x, int y) {
  ink_epd_ui_fill_rect(buffer, length, x + 2, y, 4, 2, true);
  ink_epd_ui_fill_rect(buffer, length, x + 10, y, 4, 2, true);
  ink_epd_ui_fill_rect(buffer, length, x, y + 2, 16, 5, true);
  ink_epd_ui_fill_rect(buffer, length, x + 2, y + 7, 12, 3, true);
  ink_epd_ui_fill_rect(buffer, length, x + 5, y + 10, 6, 3, true);
  ink_epd_ui_fill_rect(buffer, length, x + 7, y + 13, 2, 2, true);
}

static size_t bounded_count(size_t count, size_t capacity) {
  return count < capacity ? count : capacity;
}

void ink_epd_ui_draw_library(uint8_t *buffer, size_t length,
                             const ink_epd_ui_library_view_t *view,
                             const ink_epd_ui_fonts_t *fonts) {
  if (!buffer || length < INK_EPD_BUFFER_SIZE) return;

  ink_epd_ui_clear(buffer, length, true);
  if (!view) return;

  ink_cpfont_t *title_font = fonts ? fonts->title : NULL;
  ink_cpfont_t *body_font = fonts ? fonts->body : NULL;
  ink_cpfont_t *footer_font = fonts ? fonts->footer : NULL;
  draw_outline(buffer, length, LIBRARY_PAGE_X, LIBRARY_PAGE_Y,
               LIBRARY_PAGE_WIDTH, LIBRARY_PAGE_HEIGHT);
  draw_clipped_text(buffer, length, title_font, LIBRARY_TAB_X,
                    LIBRARY_PAGE_Y + 8, 260, 2, 1U, view->header_title);
  draw_clipped_text(buffer, length, footer_font, 340, LIBRARY_PAGE_Y + 12,
                    112, 1, 2U, view->header_meta);
  ink_epd_ui_fill_rect(buffer, length, LIBRARY_PAGE_X,
                       LIBRARY_PAGE_Y + LIBRARY_HEADER_HEIGHT,
                       LIBRARY_PAGE_WIDTH, 1, true);

  const size_t tab_count =
      bounded_count(view->tab_count, INK_EPD_UI_MENU_TAB_CAPACITY);
  for (size_t i = 0; i < tab_count; ++i) {
    const int x = LIBRARY_TAB_X +
                  (int)i * (LIBRARY_TAB_WIDTH + LIBRARY_TAB_GAP);
    draw_outline(buffer, length, x, LIBRARY_TAB_Y, LIBRARY_TAB_WIDTH,
                 LIBRARY_TAB_HEIGHT);
    if (view->tabs[i].active)
      ink_epd_ui_fill_rect(buffer, length, x + 1,
                           LIBRARY_TAB_Y + LIBRARY_TAB_HEIGHT - 4,
                           LIBRARY_TAB_WIDTH - 2, 3, true);
    if (view->tabs[i].focused)
      ink_epd_ui_fill_rect(buffer, length, x + 6, LIBRARY_TAB_Y + 6, 4,
                           LIBRARY_TAB_HEIGHT - 12, true);
    draw_clipped_text(buffer, length, body_font, x + 16, LIBRARY_TAB_Y + 13,
                      LIBRARY_TAB_WIDTH - 24, 1, 2U, view->tabs[i].label);
  }

  const size_t card_count =
      bounded_count(view->card_count, INK_EPD_UI_MENU_CARD_CAPACITY);
  for (size_t i = 0; i < card_count; ++i) {
    const ink_epd_ui_library_card_t *card = &view->cards[i];
    const int y = LIBRARY_CARD_Y +
                  (int)i * (LIBRARY_CARD_HEIGHT + LIBRARY_CARD_GAP);
    draw_outline(buffer, length, LIBRARY_CARD_X, y, LIBRARY_CARD_WIDTH,
                 LIBRARY_CARD_HEIGHT);
    if (card->selected)
      ink_epd_ui_fill_rect(buffer, length, LIBRARY_CARD_X + 1, y + 6, 4,
                           LIBRARY_CARD_HEIGHT - 12, true);
    const int text_x = LIBRARY_CARD_X + 14;
    const int text_width = card->trailing_favorite ? 374 : 406;
    draw_clipped_text(buffer, length, title_font, text_x, y + 7, text_width,
                      1, 2U, card->title);
    draw_clipped_text(buffer, length, body_font, text_x, y + 27, text_width,
                      1, 2U, card->line1);
    draw_clipped_text(buffer, length, footer_font, text_x, y + 43, text_width,
                      1, 2U, card->line2);
    if (card->trailing_favorite)
      draw_heart_icon(buffer, length, LIBRARY_CARD_X + 397, y + 35);
  }

  if (!view->popup_open) return;
  ink_epd_ui_fill_rect(buffer, length, LIBRARY_POPUP_X, LIBRARY_POPUP_Y,
                       LIBRARY_POPUP_WIDTH, LIBRARY_POPUP_HEIGHT, false);
  draw_outline(buffer, length, LIBRARY_POPUP_X, LIBRARY_POPUP_Y,
               LIBRARY_POPUP_WIDTH, LIBRARY_POPUP_HEIGHT);
  draw_clipped_text(buffer, length, title_font, LIBRARY_POPUP_X + 16,
                    LIBRARY_POPUP_Y + 14, LIBRARY_POPUP_WIDTH - 32, 2, 1U,
                    view->popup_title);

  const size_t action_count =
      bounded_count(view->action_count, INK_EPD_UI_MENU_ACTION_CAPACITY);
  const int action_width =
      (LIBRARY_POPUP_WIDTH - 3 * LIBRARY_ACTION_GAP) / 2;
  const int action_start_y = LIBRARY_POPUP_Y + 72;
  for (size_t i = 0; i < action_count; ++i) {
    const int column = (int)(i % 2U);
    const int row = (int)(i / 2U);
    const int x = LIBRARY_POPUP_X + LIBRARY_ACTION_GAP +
                  column * (action_width + LIBRARY_ACTION_GAP);
    const int y = action_start_y + row * (LIBRARY_ACTION_HEIGHT + 6);
    draw_outline(buffer, length, x, y, action_width, LIBRARY_ACTION_HEIGHT);
    if (view->actions[i].selected)
      ink_epd_ui_fill_rect(buffer, length, x + 1, y + 1, 4,
                           LIBRARY_ACTION_HEIGHT - 2, true);
    draw_clipped_text(buffer, length, body_font, x + 12, y + 11,
                      action_width - 20, 1, 2U, view->actions[i].label);
  }
}

ink_epd_region_t ink_epd_ui_library_selection_region(
    const ink_epd_ui_library_focus_t *previous,
    const ink_epd_ui_library_focus_t *current) {
  const ink_epd_region_t full = {.x = LIBRARY_PAGE_X,
                                 .y = LIBRARY_PAGE_Y,
                                 .width = LIBRARY_PAGE_WIDTH,
                                 .height = LIBRARY_PAGE_HEIGHT};
  if (!previous || !current || previous->active_tab != current->active_tab ||
      previous->tabs_focused != current->tabs_focused ||
      previous->window_start != current->window_start ||
      previous->popup_open != current->popup_open || current->popup_open)
    return full;

  size_t first = previous->selected_card;
  size_t last = current->selected_card;
  if (first >= INK_EPD_UI_MENU_CARD_CAPACITY ||
      last >= INK_EPD_UI_MENU_CARD_CAPACITY)
    return full;
  if (first > last) {
    const size_t temporary = first;
    first = last;
    last = temporary;
  }
  const int top = LIBRARY_CARD_Y +
                  (int)first * (LIBRARY_CARD_HEIGHT + LIBRARY_CARD_GAP);
  const int bottom = LIBRARY_CARD_Y +
                     (int)last * (LIBRARY_CARD_HEIGHT + LIBRARY_CARD_GAP) +
                     LIBRARY_CARD_HEIGHT;
  return (ink_epd_region_t){.x = LIBRARY_CARD_X,
                            .y = top,
                            .width = LIBRARY_CARD_WIDTH,
                            .height = bottom - top};
}

void ink_epd_ui_draw_reader_menu(
    uint8_t *buffer, size_t length,
    const ink_epd_ui_reader_menu_view_t *view,
    const ink_epd_ui_fonts_t *fonts) {
  if (!buffer || length < INK_EPD_BUFFER_SIZE || !view) return;

  ink_cpfont_t *title_font = fonts ? fonts->title : NULL;
  ink_cpfont_t *body_font = fonts ? fonts->body : NULL;
  ink_epd_ui_fill_rect(buffer, length, READER_MENU_PANEL_X,
                       READER_MENU_PANEL_Y, READER_MENU_PANEL_WIDTH,
                       READER_MENU_PANEL_HEIGHT, false);
  draw_outline(buffer, length, READER_MENU_PANEL_X, READER_MENU_PANEL_Y,
               READER_MENU_PANEL_WIDTH, READER_MENU_PANEL_HEIGHT);

  const int tab_width =
      (READER_MENU_PANEL_WIDTH - 3 * READER_MENU_TAB_GAP) / 2;
  const size_t tab_count = bounded_count(
      view->tab_count, INK_EPD_UI_READER_MENU_TAB_CAPACITY);
  for (size_t i = 0; i < tab_count; ++i) {
    const int x = READER_MENU_PANEL_X + READER_MENU_TAB_GAP +
                  (int)i * (tab_width + READER_MENU_TAB_GAP);
    draw_outline(buffer, length, x, READER_MENU_TAB_Y, tab_width,
                 READER_MENU_TAB_HEIGHT);
    if (view->tabs[i].active)
      ink_epd_ui_fill_rect(buffer, length, x + 1,
                           READER_MENU_TAB_Y + READER_MENU_TAB_HEIGHT - 4,
                           tab_width - 2, 3, true);
    if (view->tabs[i].focused)
      ink_epd_ui_fill_rect(buffer, length, x + 6, READER_MENU_TAB_Y + 6, 4,
                           READER_MENU_TAB_HEIGHT - 12, true);
    draw_clipped_text(buffer, length, body_font, x + 16,
                      READER_MENU_TAB_Y + 13, tab_width - 24, 1, 2U,
                      view->tabs[i].label);
  }

  const size_t visible_capacity =
      view->bookmarks_tab ? INK_EPD_UI_READER_MENU_BOOKMARK_VISIBLE
                          : INK_EPD_UI_READER_MENU_ITEM_CAPACITY;
  const size_t item_count = bounded_count(view->item_count, visible_capacity);
  const int item_y = view->bookmarks_tab ? READER_MENU_BOOKMARK_Y
                                         : READER_MENU_CHAPTER_Y;
  const int item_height = view->bookmarks_tab
                              ? READER_MENU_BOOKMARK_HEIGHT
                              : READER_MENU_CHAPTER_HEIGHT;
  const int item_gap = view->bookmarks_tab ? READER_MENU_BOOKMARK_GAP
                                           : READER_MENU_CHAPTER_GAP;
  const int item_x = READER_MENU_PANEL_X + READER_MENU_TAB_GAP;
  const int item_width = READER_MENU_PANEL_WIDTH - 2 * READER_MENU_TAB_GAP;
  for (size_t i = 0; i < item_count; ++i) {
    const ink_epd_ui_reader_menu_item_t *item = &view->items[i];
    const int y = item_y + (int)i * (item_height + item_gap);
    draw_outline(buffer, length, item_x, y, item_width, item_height);
    if (item->selected)
      ink_epd_ui_fill_rect(buffer, length, item_x + 1, y + 6, 4,
                           item_height - 12, true);
    draw_clipped_text(buffer, length, title_font, item_x + 14, y + 8,
                      item_width - 28, 1, 2U, item->title);
    if (view->bookmarks_tab)
      draw_clipped_text(buffer, length, body_font, item_x + 14, y + 31,
                        item_width - 28, 1, 2U, item->line1);
  }

  if (!view->popup_open) return;
  ink_epd_ui_fill_rect(buffer, length, READER_MENU_POPUP_X,
                       READER_MENU_POPUP_Y, READER_MENU_POPUP_WIDTH,
                       READER_MENU_POPUP_HEIGHT, false);
  draw_outline(buffer, length, READER_MENU_POPUP_X, READER_MENU_POPUP_Y,
               READER_MENU_POPUP_WIDTH, READER_MENU_POPUP_HEIGHT);
  draw_clipped_text(buffer, length, title_font, READER_MENU_POPUP_X + 16,
                    READER_MENU_POPUP_Y + 16,
                    READER_MENU_POPUP_WIDTH - 32, 1, 2U,
                    view->popup_title);
  const size_t action_count = bounded_count(
      view->action_count, INK_EPD_UI_READER_MENU_ACTION_CAPACITY);
  for (size_t i = 0; i < action_count; ++i) {
    const int x = READER_MENU_POPUP_X + 20;
    const int y = READER_MENU_POPUP_Y + 56 + (int)i * 44;
    const int width = READER_MENU_POPUP_WIDTH - 40;
    draw_outline(buffer, length, x, y, width, 34);
    if (view->actions[i].selected)
      ink_epd_ui_fill_rect(buffer, length, x + 1, y + 5, 4, 24, true);
    draw_clipped_text(buffer, length, body_font, x + 14, y + 10, width - 28,
                      1, 2U, view->actions[i].label);
  }
}

ink_epd_region_t ink_epd_ui_reader_menu_selection_region(
    const ink_epd_ui_reader_menu_focus_t *previous,
    const ink_epd_ui_reader_menu_focus_t *current) {
  (void)previous;
  (void)current;
  return (ink_epd_region_t){.x = READER_MENU_PANEL_X,
                            .y = READER_MENU_PANEL_Y,
                            .width = READER_MENU_PANEL_WIDTH,
                            .height = READER_MENU_PANEL_HEIGHT};
}

static bool reader_pixel_is_black(const uint8_t *buffer, int x, int y) {
  const size_t index = (size_t)y * (INK_EPD_WIDTH / 8) + (size_t)x / 8;
  return (buffer[index] & (uint8_t)(0x80U >> (x & 7))) == 0U;
}

void ink_epd_ui_draw_reader_footer(uint8_t *buffer, size_t length,
                                   ink_cpfont_t *font,
                                   const char *left_text,
                                   const char *right_text) {
  if (!buffer || length < INK_EPD_BUFFER_SIZE) return;
  const uint8_t scale_divisor =
      ink_cpfont_is_loaded(font) && font->advance_y > 24U ? 2U : 1U;
  ink_epd_ui_fill_rect(buffer, length, 0, READER_FOOTER_BAND_Y,
                       INK_EPD_WIDTH, READER_FOOTER_BAND_HEIGHT, false);
  ink_epd_ui_fill_rect(buffer, length, 0, READER_FOOTER_SEPARATOR_Y,
                       INK_EPD_WIDTH, 1, true);

  int right_width = 0;
  if (!ink_epd_ui_measure_text(font, right_text ? right_text : "", 2,
                               scale_divisor, &right_width))
    right_width = 0;
  if (right_width < 0) right_width = 0;
  const int right_max_width = READER_FOOTER_RIGHT_X - READER_FOOTER_LEFT_X;
  if (right_width > right_max_width) right_width = right_max_width;
  const int right_x = READER_FOOTER_RIGHT_X - right_width;
  int left_width = right_x - READER_FOOTER_TEXT_GAP - READER_FOOTER_LEFT_X;
  if (left_width < 0) left_width = 0;

  draw_clipped_text(buffer, length, font, READER_FOOTER_LEFT_X,
                    READER_FOOTER_TEXT_Y, left_width, 2, scale_divisor,
                    left_text ? left_text : "");
  draw_clipped_text(buffer, length, font, right_x, READER_FOOTER_TEXT_Y,
                    right_width, 2, scale_divisor,
                    right_text ? right_text : "");
}

bool ink_epd_ui_reader_self_test(void) {
  uint8_t *buffer = malloc(INK_EPD_BUFFER_SIZE);
  if (!buffer) return false;

  ink_epd_ui_library_view_t view = {
      .header_title = "LIBRARY",
      .header_meta = "8 BOOKS",
      .tab_count = 3,
      .card_count = 8,
  };
  const char *tab_labels[3] = {"RECENT", "ALL", "FAVORITE"};
  for (size_t i = 0; i < 3; ++i) {
    view.tabs[i].label = tab_labels[i];
    view.tabs[i].active = i == 0;
    view.tabs[i].focused = i == 0;
  }
  for (size_t i = 0; i < 8; ++i) {
    view.cards[i].title =
        i == 0 ? "A VERY LONG ASCII BOOK TITLE THAT MUST BE CLIPPED" : "BOOK";
    view.cards[i].line1 =
        i == 0 ? "LONG UTF8 \xe9\x98\x85\xe8\xaf\xbb\xe6\x96\x87\xe6\x9c\xac \xe9\x98\x85\xe8\xaf\xbb\xe6\x96\x87\xe6\x9c\xac \xe9\x98\x85\xe8\xaf\xbb\xe6\x96\x87\xe6\x9c\xac" : "1/8";
    view.cards[i].line2 = "";
    view.cards[i].selected = i == 0;
  }
  view.cards[0].trailing_favorite = true;

  ink_epd_ui_draw_library(buffer, INK_EPD_BUFFER_SIZE, &view, NULL);
  bool ok = reader_pixel_is_black(buffer, 24, 58) &&
            reader_pixel_is_black(buffer, 170, 58) &&
            reader_pixel_is_black(buffer, 316, 58) &&
            reader_pixel_is_black(buffer, 24, 108) &&
            reader_pixel_is_black(buffer, 24, 556) &&
            reader_pixel_is_black(buffer, 429, 150) &&
            !reader_pixel_is_black(buffer, 457, 130);

  ink_epd_ui_library_view_t empty = {
      .header_title = "LIBRARY",
      .tab_count = 3,
  };
  ink_epd_ui_draw_library(buffer, INK_EPD_BUFFER_SIZE, &empty, NULL);
  ok = ok && !reader_pixel_is_black(buffer, 24, 108);

  ink_epd_ui_library_focus_t previous = {
      .active_tab = 0, .window_start = 0, .selected_card = 0};
  ink_epd_ui_library_focus_t current = {
      .active_tab = 0, .window_start = 0, .selected_card = 1};
  ink_epd_region_t region =
      ink_epd_ui_library_selection_region(&previous, &current);
  ok = ok && region.x == 24 && region.y == 108 && region.width == 432 &&
       region.height == 122;
  current.tabs_focused = true;
  region = ink_epd_ui_library_selection_region(&previous, &current);
  ok = ok && region.x == 8 && region.y == 8 && region.width == 464 &&
       region.height == 776;
  current.tabs_focused = false;
  previous.tabs_focused = true;
  region = ink_epd_ui_library_selection_region(&previous, &current);
  ok = ok && region.x == 8 && region.y == 8 && region.width == 464 &&
       region.height == 776;
  previous.tabs_focused = false;
  region = ink_epd_ui_library_selection_region(&previous, &current);
  ok = ok && region.x == 24 && region.y == 108 && region.width == 432 &&
       region.height == 122;
  current.active_tab = 1;
  region = ink_epd_ui_library_selection_region(&previous, &current);
  ok = ok && region.x == 8 && region.y == 8 && region.width == 464 &&
       region.height == 776;
  current.active_tab = 0;
  current.window_start = 1;
  region = ink_epd_ui_library_selection_region(&previous, &current);
  ok = ok && region.width == 464 && region.height == 776;
  current.window_start = 0;
  current.popup_open = true;
  region = ink_epd_ui_library_selection_region(&previous, &current);
  ok = ok && region.width == 464 && region.height == 776;

  view.popup_open = true;
  view.popup_title = "BOOK ACTION";
  view.action_count = 2;
  view.actions[0].label = "OPEN";
  view.actions[0].selected = true;
  view.actions[1].label = "FAVORITE";
  ink_epd_ui_draw_library(buffer, INK_EPD_BUFFER_SIZE, &view, NULL);
  ok = ok && reader_pixel_is_black(buffer, 54, 314) &&
       reader_pixel_is_black(buffer, 425, 477) &&
       !reader_pixel_is_black(buffer, 53, 314) &&
       !reader_pixel_is_black(buffer, 426, 477);

  ink_epd_ui_clear(buffer, INK_EPD_BUFFER_SIZE, true);
  ink_epd_ui_reader_menu_view_t reader_menu = {
      .tab_count = 2,
      .item_count = 8,
  };
  reader_menu.tabs[0] = (ink_epd_ui_library_tab_t){
      .label = "CHAPTERS", .active = true, .focused = true};
  reader_menu.tabs[1].label = "BOOKMARKS";
  for (size_t i = 0; i < 8; ++i) {
    reader_menu.items[i].title = "CHAPTER";
    reader_menu.items[i].selected = i == 7;
  }
  ink_epd_ui_draw_reader_menu(buffer, INK_EPD_BUFFER_SIZE, &reader_menu,
                              NULL);
  ok = ok && reader_pixel_is_black(buffer, 24, 118) &&
       reader_pixel_is_black(buffer, 455, 651) &&
       reader_pixel_is_black(buffer, 34, 592) &&
       !reader_pixel_is_black(buffer, 23, 118) &&
       !reader_pixel_is_black(buffer, 456, 651);

  reader_menu.bookmarks_tab = true;
  reader_menu.item_count = 8;
  reader_menu.popup_open = true;
  reader_menu.popup_title = "BOOKMARK";
  reader_menu.action_count = 3;
  for (size_t i = 0; i < 3; ++i) {
    reader_menu.actions[i].label = "ACTION";
    reader_menu.actions[i].selected = i == 2;
  }
  ink_epd_ui_draw_reader_menu(buffer, INK_EPD_BUFFER_SIZE, &reader_menu,
                              NULL);
  ok = ok && reader_pixel_is_black(buffer, 34, 530) &&
       !reader_pixel_is_black(buffer, 34, 596) &&
       reader_pixel_is_black(buffer, 70, 281) &&
       reader_pixel_is_black(buffer, 409, 488) &&
       !reader_pixel_is_black(buffer, 69, 281) &&
       !reader_pixel_is_black(buffer, 410, 488);

  ink_epd_ui_reader_menu_focus_t menu_previous = {0};
  ink_epd_ui_reader_menu_focus_t menu_current = {.selected_item = 7};
  region = ink_epd_ui_reader_menu_selection_region(&menu_previous,
                                                    &menu_current);
  ok = ok && region.x == 24 && region.y == 118 && region.width == 432 &&
       region.height == 534;

  ink_epd_ui_clear(buffer, INK_EPD_BUFFER_SIZE, true);
  ink_epd_ui_set_pixel(buffer, INK_EPD_BUFFER_SIZE, 0,
                       READER_FOOTER_SEPARATOR_Y - 1, true);
  ink_epd_ui_draw_reader_footer(buffer, INK_EPD_BUFFER_SIZE, NULL, "CHAPTER",
                                "100% 1071/1071");
  bool separator_ok = true;
  for (int x = 0; x < INK_EPD_WIDTH; ++x)
    separator_ok = separator_ok &&
                   reader_pixel_is_black(buffer, x,
                                         READER_FOOTER_SEPARATOR_Y);
  bool left_text_ok = false;
  bool right_text_ok = false;
  uint8_t final_glyph[12 * (INK_EPD_HEIGHT - READER_FOOTER_TEXT_Y)];
  for (int y = READER_FOOTER_TEXT_Y; y < INK_EPD_HEIGHT; ++y) {
    for (int x = READER_FOOTER_LEFT_X; x < 150; ++x)
      left_text_ok =
          left_text_ok || reader_pixel_is_black(buffer, x, y);
    for (int x = 316; x < READER_FOOTER_RIGHT_X; ++x)
      right_text_ok =
          right_text_ok || reader_pixel_is_black(buffer, x, y);
    for (int x = READER_FOOTER_RIGHT_X - 12;
         x < READER_FOOTER_RIGHT_X; ++x)
      final_glyph[(y - READER_FOOTER_TEXT_Y) * 12 +
                  x - (READER_FOOTER_RIGHT_X - 12)] =
          reader_pixel_is_black(buffer, x, y) ? 1U : 0U;
  }
  const bool footer_bounds_ok =
      separator_ok && left_text_ok && right_text_ok &&
      reader_pixel_is_black(buffer, 0, READER_FOOTER_SEPARATOR_Y - 1) &&
      !reader_pixel_is_black(buffer, INK_EPD_WIDTH - 1,
                             READER_FOOTER_SEPARATOR_Y - 1) &&
      !reader_pixel_is_black(buffer, INK_EPD_WIDTH / 2,
                             INK_EPD_HEIGHT - 1);
  ink_epd_ui_clear(buffer, INK_EPD_BUFFER_SIZE, true);
  (void)ink_epd_ui_draw_text_font(
      buffer, INK_EPD_BUFFER_SIZE, NULL, READER_FOOTER_RIGHT_X - 12,
      READER_FOOTER_TEXT_Y, 2, 1U, "1", NULL);
  bool final_glyph_ok = true;
  for (int y = READER_FOOTER_TEXT_Y; y < INK_EPD_HEIGHT; ++y)
    for (int x = READER_FOOTER_RIGHT_X - 12;
         x < READER_FOOTER_RIGHT_X; ++x)
      final_glyph_ok =
          final_glyph_ok &&
          final_glyph[(y - READER_FOOTER_TEXT_Y) * 12 +
                      x - (READER_FOOTER_RIGHT_X - 12)] ==
              (reader_pixel_is_black(buffer, x, y) ? 1U : 0U);
  ok = ok && footer_bounds_ok && final_glyph_ok;

  free(buffer);
  return ok;
}
