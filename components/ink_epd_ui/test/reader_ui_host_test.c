#include "ink_epd_ui.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  ink_cpfont_t *font;
  int x;
  int y;
  uint8_t scale_divisor;
  char text[192];
} draw_call_t;

static draw_call_t s_draw_calls[32];
static size_t s_draw_call_count;

bool ink_cpfont_is_loaded(const ink_cpfont_t *font) {
  return font && font->loaded;
}

esp_err_t ink_cpfont_draw_text_bw_scaled(ink_cpfont_t *font,
                                         uint8_t *buffer, int x, int top_y,
                                         const char *text,
                                         uint8_t scale_divisor,
                                         int *out_width_px) {
  (void)buffer;
  if (!font || !font->loaded || !text || scale_divisor == 0U)
    return ESP_FAIL;
  if (out_width_px)
    *out_width_px =
        (int)strlen(text) * (int)font->advance_y * 2 /
        (3 * (int)scale_divisor);
  if (buffer &&
      s_draw_call_count < sizeof(s_draw_calls) / sizeof(s_draw_calls[0])) {
    draw_call_t *call = &s_draw_calls[s_draw_call_count++];
    call->font = font;
    call->x = x;
    call->y = top_y;
    call->scale_divisor = scale_divisor;
    snprintf(call->text, sizeof(call->text), "%s", text);
  }
  return ESP_OK;
}

static bool pixel_is_black(const uint8_t *buffer, int x, int y) {
  const size_t index = (size_t)y * (INK_EPD_WIDTH / 8) + (size_t)x / 8;
  return (buffer[index] & (uint8_t)(0x80U >> (x & 7))) == 0U;
}

static const draw_call_t *find_draw_call(const char *text) {
  for (size_t i = 0; i < s_draw_call_count; ++i)
    if (strcmp(s_draw_calls[i].text, text) == 0) return &s_draw_calls[i];
  return NULL;
}

static bool library_style_test(void) {
  uint8_t buffer[INK_EPD_BUFFER_SIZE];
  ink_cpfont_t title_font = {.loaded = true, .advance_y = 24U};
  ink_cpfont_t body_font = {.loaded = true, .advance_y = 24U};
  ink_cpfont_t footer_font = {.loaded = true, .advance_y = 16U};
  const ink_epd_ui_fonts_t fonts = {
      .title = &title_font, .body = &body_font, .footer = &footer_font};
  ink_epd_ui_library_view_t view = {
      .header_title = "LIBRARY",
      .header_meta = "8 BOOKS",
      .tab_count = 1,
      .card_count = 1,
  };
  view.tabs[0] = (ink_epd_ui_library_tab_t){
      .label = "RECENT", .active = true, .focused = true};
  view.cards[0].title =
      "THIS TITLE IS DELIBERATELY LONGER THAN THE CARD WIDTH LIMIT";
  view.cards[0].line1 = "12/128";

  s_draw_call_count = 0U;
  ink_epd_ui_draw_library(buffer, sizeof(buffer), &view, &fonts);

  const draw_call_t *header = find_draw_call("LIBRARY");
  const draw_call_t *progress = find_draw_call("12/128");
  bool ellipsis_found = false;
  for (size_t i = 0; i < s_draw_call_count; ++i) {
    const size_t length = strlen(s_draw_calls[i].text);
    if (length >= 3U &&
        strcmp(s_draw_calls[i].text + length - 3U, "...") == 0)
      ellipsis_found = true;
  }

  bool focused_marker_absent = true;
  for (int y = 64; y < 94; ++y)
    focused_marker_absent = focused_marker_absent &&
                            !pixel_is_black(buffer, 30, y);
  const bool active_underline_present = pixel_is_black(buffer, 30, 97);
  const bool outer_frame_absent = !pixel_is_black(buffer, 8, 8);
  const bool launcher_divider_present = pixel_is_black(buffer, 24, 38) &&
                                        pixel_is_black(buffer, 455, 38) &&
                                        !pixel_is_black(buffer, 23, 38) &&
                                        !pixel_is_black(buffer, 456, 38);
  const draw_call_t *tab = find_draw_call("RECENT");

  return header && header->font == &body_font &&
         header->scale_divisor == 2U && header->x == 24 && header->y == 13 &&
         !find_draw_call("8 BOOKS") && tab && tab->x == 69 && tab->y == 73 &&
         progress && progress->font == &footer_font &&
         progress->scale_divisor == 1U && progress->y == 142 &&
         ellipsis_found && focused_marker_absent && active_underline_present &&
         outer_frame_absent && launcher_divider_present;
}

static bool library_popup_style_test(void) {
  uint8_t buffer[INK_EPD_BUFFER_SIZE];
  ink_cpfont_t title_font = {.loaded = true, .advance_y = 24U};
  ink_cpfont_t body_font = {.loaded = true, .advance_y = 24U};
  ink_cpfont_t footer_font = {.loaded = true, .advance_y = 16U};
  const ink_epd_ui_fonts_t fonts = {
      .title = &title_font, .body = &body_font, .footer = &footer_font};
  ink_epd_ui_library_view_t view = {
      .header_title = "LIBRARY",
      .popup_open = true,
      .popup_title = "BOOK",
      .action_count = 2,
  };
  view.actions[0] =
      (ink_epd_ui_library_action_t){.label = "OPEN", .selected = true};
  view.actions[1].label = "FAVORITE";

  s_draw_call_count = 0U;
  ink_epd_ui_draw_library(buffer, sizeof(buffer), &view, &fonts);
  const draw_call_t *title = find_draw_call("BOOK");
  const draw_call_t *open = find_draw_call("OPEN");
  const draw_call_t *favorite = find_draw_call("FAVORITE");
  return title && title->font == &body_font && title->scale_divisor == 2U &&
         title->x == 224 && title->y == 334 && open && open->x == 133 &&
         open->y == 397 && favorite && favorite->x == 298 &&
         favorite->y == 397;
}

static bool reader_menu_style_test(void) {
  uint8_t buffer[INK_EPD_BUFFER_SIZE];
  ink_cpfont_t title_font = {.loaded = true, .advance_y = 24U};
  ink_cpfont_t body_font = {.loaded = true, .advance_y = 24U};
  const ink_epd_ui_fonts_t fonts = {
      .title = &title_font, .body = &body_font};
  ink_epd_ui_reader_menu_view_t view = {
      .tab_count = 2,
      .item_count = 1,
  };
  view.tabs[0] = (ink_epd_ui_library_tab_t){
      .label = "CHAPTERS", .active = true, .focused = true};
  view.tabs[1].label = "BOOKMARKS";
  view.items[0].title = "CHAPTER 1";

  s_draw_call_count = 0U;
  ink_epd_ui_draw_reader_menu(buffer, sizeof(buffer), &view, &fonts);
  const draw_call_t *chapters = find_draw_call("CHAPTERS");
  const draw_call_t *bookmarks = find_draw_call("BOOKMARKS");
  const draw_call_t *chapter_item = find_draw_call("CHAPTER 1");
  const bool focused_marker_absent = !pixel_is_black(buffer, 40, 148);
  const bool active_underline_present = pixel_is_black(buffer, 40, 175);
  if (!chapters || chapters->x != 102 || chapters->y != 151 ||
      !bookmarks || bookmarks->x != 309 || bookmarks->y != 151 ||
      !chapter_item || chapter_item->y != 218 || !focused_marker_absent ||
      !active_underline_present)
    return false;

  view.bookmarks_tab = true;
  view.items[0].title = "MARK";
  view.items[0].line1 = "PAGE 12";
  view.popup_open = true;
  view.popup_title = "BOOKMARK";
  view.action_count = 1;
  view.actions[0].label = "DELETE";
  s_draw_call_count = 0U;
  ink_epd_ui_draw_reader_menu(buffer, sizeof(buffer), &view, &fonts);
  const draw_call_t *mark = find_draw_call("MARK");
  const draw_call_t *details = find_draw_call("PAGE 12");
  const draw_call_t *popup_title = find_draw_call("BOOKMARK");
  const draw_call_t *action = find_draw_call("DELETE");
  return mark && mark->y == 213 && details && details->y == 233 &&
         popup_title && popup_title->x == 208 && popup_title->y == 301 &&
         action && action->x == 216 && action->y == 348;
}

int main(void) {
  if (!ink_epd_ui_reader_self_test()) {
    fprintf(stderr, "reader UI self test failed\n");
    return 1;
  }
  if (!library_style_test()) {
    fprintf(stderr, "reader library style test failed\n");
    return 1;
  }
  if (!library_popup_style_test()) {
    fprintf(stderr, "reader library popup style test failed\n");
    return 1;
  }
  if (!reader_menu_style_test()) {
    fprintf(stderr, "reader menu style test failed\n");
    return 1;
  }
  puts("PASS: reader UI host self test");
  return 0;
}
