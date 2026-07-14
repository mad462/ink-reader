#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_fonts.h"
#include "ink_hw.h"
#include "ink_input.h"
#include "ink_reader_core.h"
#include "ink_reader_state.h"
#include "ink_sd.h"
#include "reader_app_model.h"
#include "reader_refresh_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "reader";

enum { READER_PARTIAL_REFRESH_LIMIT = 50 };

static ink_reader_catalog_t s_catalog;
static ink_reader_state_t s_state;
static reader_app_model_t s_model;
static reader_app_model_t s_candidate_model;
static ink_cpfont_t s_menu_font;
static ink_cpfont_t s_footer_font;
static ink_epd_ui_fonts_t s_library_fonts;

static bool reader_should_cleanup(unsigned successful_page_turns) {
  return successful_page_turns >= READER_PARTIAL_REFRESH_LIMIT - 1U;
}

static bool find_changed_region(const uint8_t *previous,
                                const uint8_t *current, uint16_t width,
                                uint16_t height, ink_epd_region_t *region) {
  if (!previous || !current || !region || width == 0 || height == 0 ||
      (width & 7U) != 0)
    return false;
  int min_x = width;
  int min_y = height;
  int max_x = -1;
  int max_y = -1;
  const size_t stride = width / 8U;
  for (uint16_t y = 0; y < height; ++y) {
    for (uint16_t byte_x = 0; byte_x < stride; ++byte_x) {
      const uint8_t changed =
          previous[(size_t)y * stride + byte_x] ^
          current[(size_t)y * stride + byte_x];
      if (changed == 0U) continue;
      for (uint8_t bit = 0; bit < 8U; ++bit) {
        if ((changed & (uint8_t)(0x80U >> bit)) == 0U) continue;
        const int x = (int)byte_x * 8 + bit;
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if ((int)y < min_y) min_y = y;
        if ((int)y > max_y) max_y = y;
      }
    }
  }
  if (max_x < min_x || max_y < min_y) return false;
  *region = (ink_epd_region_t){.x = min_x,
                              .y = min_y,
                              .width = max_x - min_x + 1,
                              .height = max_y - min_y + 1};
  return true;
}

static bool reader_refresh_policy_self_test(void) {
  const uint8_t previous[4] = {0xff, 0xff, 0xff, 0xff};
  uint8_t current[4];
  memcpy(current, previous, sizeof(current));
  ink_epd_region_t region = {0};
  if (find_changed_region(previous, current, 16, 2, &region)) return false;
  current[0] &= (uint8_t)~(0x80U >> 3);
  current[3] &= (uint8_t)~(0x80U >> 7);
  if (!find_changed_region(previous, current, 16, 2, &region) ||
      region.x != 3 || region.y != 0 || region.width != 13 ||
      region.height != 2)
    return false;
  const uint8_t boundary_previous[2] = {0xff, 0xff};
  const uint8_t boundary_current[2] = {0x7f, 0xfe};
  return find_changed_region(boundary_previous, boundary_current, 8, 2,
                             &region) &&
         region.x == 0 && region.y == 0 && region.width == 8 &&
         region.height == 2 && !reader_should_cleanup(48) &&
         reader_should_cleanup(49);
}

static void log_stage(const char *stage, int64_t started_us) {
  ESP_LOGI(TAG, "APP_STAGE name=reader stage=%s elapsed_ms=%lld", stage,
           (long long)((esp_timer_get_time() - started_us) / 1000));
}

static void wait_for_launcher(esp_err_t input_ret) {
  while (true) {
    ink_input_snapshot_t input = {0};
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (input_ret == ESP_OK && ink_input_poll(now_ms, &input) == ESP_OK &&
        ink_input_was_pressed(&input, INK_BUTTON_BACK)) {
      ESP_LOGI(TAG, "BOOT_SWITCH from=reader to=launcher");
      const esp_err_t ret = ink_boot_switch_to_launcher();
      if (ret != ESP_OK)
        ESP_LOGE(TAG, "return to launcher failed err=%s",
                 esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static const ink_reader_bookshelf_entry_t *find_shelf(const char *path) {
  size_t index = 0;
  return ink_reader_state_find_bookshelf(&s_state, path, &index)
             ? ink_reader_state_bookshelf_at(&s_state, index)
             : NULL;
}

static const char *catalog_title(const ink_reader_catalog_item_t *item,
                                 const ink_reader_bookshelf_entry_t *entry) {
  return entry && entry->title[0] != '\0' ? entry->title : item->name;
}

static bool is_printable_ascii(const char *text) {
  if (!text || text[0] == '\0') return false;
  for (const unsigned char *cursor = (const unsigned char *)text; *cursor;
       ++cursor) {
    if (*cursor < 0x20U || *cursor > 0x7eU) return false;
  }
  return true;
}

static const char *visible_title(const ink_reader_catalog_item_t *item,
                                 const ink_reader_bookshelf_entry_t *entry,
                                 bool localized) {
  const char *title = catalog_title(item, entry);
  return localized || is_printable_ascii(title) ? title : "BOOK";
}

static void build_library_view(const reader_app_model_t *model,
                               ink_epd_ui_library_view_t *view,
                               char lines[INK_EPD_UI_MENU_CARD_CAPACITY][96],
                               char meta[24], bool localized) {
  static const char *const localized_tabs[READER_LIBRARY_TAB_COUNT] = {
      "最近", "全部", "收藏"};
  static const char *const ascii_tabs[READER_LIBRARY_TAB_COUNT] = {
      "RECENT", "ALL", "FAVORITES"};
  const char *const *tab_labels = localized ? localized_tabs : ascii_tabs;
  memset(view, 0, sizeof(*view));
  snprintf(meta, 24, "%u BOOKS", (unsigned)s_catalog.count);
  view->header_title = localized ? "书库" : "LIBRARY";
  view->header_meta = meta;
  view->tab_count = READER_LIBRARY_TAB_COUNT;
  for (size_t i = 0; i < READER_LIBRARY_TAB_COUNT; ++i) {
    view->tabs[i].label = tab_labels[i];
    view->tabs[i].active = model->tab == (reader_library_tab_t)i;
    view->tabs[i].focused =
        view->tabs[i].active && model->focus == READER_LIBRARY_FOCUS_TABS;
  }

  const size_t visible_count =
      reader_app_model_visible_count(model, model->tab);
  const size_t window_start = reader_app_model_window_start(model);
  const size_t card_count =
      visible_count - window_start < INK_EPD_UI_MENU_CARD_CAPACITY
          ? visible_count - window_start
          : INK_EPD_UI_MENU_CARD_CAPACITY;
  view->card_count = card_count;
  for (size_t card = 0; card < card_count; ++card) {
    const size_t visible_index = window_start + card;
    const size_t catalog_index = reader_app_model_visible_catalog_index(
        model, model->tab, visible_index);
    const ink_reader_catalog_item_t *item =
        ink_reader_catalog_at(&s_catalog, catalog_index);
    if (!item) continue;
    const ink_reader_bookshelf_entry_t *entry = find_shelf(item->path);
    view->cards[card].title = visible_title(item, entry, localized);
    if (entry && entry->has_opened) {
      const unsigned page = (unsigned)entry->page_index + 1U;
      const unsigned total = (unsigned)entry->total_pages_snapshot;
      if (entry->chapter_title[0] != '\0' &&
          (localized || is_printable_ascii(entry->chapter_title)))
        snprintf(lines[card], sizeof(lines[card]), "%u/%u %s", page, total,
                 entry->chapter_title);
      else
        snprintf(lines[card], sizeof(lines[card]), "%u/%u", page, total);
    } else {
      snprintf(lines[card], sizeof(lines[card]), "%s",
               localized ? "未读" : "UNREAD");
    }
    view->cards[card].line1 = lines[card];
    view->cards[card].line2 = "";
    view->cards[card].selected =
        model->focus == READER_LIBRARY_FOCUS_ITEMS &&
        model->selected[model->tab] == visible_index;
    view->cards[card].trailing_favorite = entry && entry->is_favorite;
  }

  if (visible_count == 0U) {
    view->card_count = 1U;
    if (s_catalog.count == 0U)
      view->cards[0].title = localized ? "未找到书籍" : "NO BOOKS";
    else if (model->tab == READER_LIBRARY_TAB_RECENT)
      view->cards[0].title = localized ? "暂无最近阅读" : "NO RECENT";
    else
      view->cards[0].title = localized ? "暂无收藏" : "NO FAVORITES";
    view->cards[0].line1 = "";
    view->cards[0].line2 = "";
  }

  if (model->focus != READER_LIBRARY_FOCUS_POPUP) return;
  const size_t catalog_index = reader_app_model_selected_catalog_index(model);
  const ink_reader_catalog_item_t *item =
      ink_reader_catalog_at(&s_catalog, catalog_index);
  if (!item) return;
  const ink_reader_bookshelf_entry_t *entry = find_shelf(item->path);
  view->popup_open = true;
  view->popup_title = visible_title(item, entry, localized);
  view->action_count = 2U;
  view->actions[0].label = localized ? "打开" : "OPEN";
  view->actions[0].selected = model->popup_action == 0U;
  view->actions[1].label = entry && entry->is_favorite
                               ? (localized ? "取消收藏" : "REMOVE FAVORITE")
                               : (localized ? "加入收藏" : "ADD FAVORITE");
  view->actions[1].selected = model->popup_action == 1U;
}

static ink_epd_ui_library_focus_t library_focus(
    const reader_app_model_t *model) {
  const size_t window_start = reader_app_model_window_start(model);
  return (ink_epd_ui_library_focus_t){
      .active_tab = model->tab,
      .tabs_focused = model->focus == READER_LIBRARY_FOCUS_TABS,
      .window_start = window_start,
      .selected_card = model->selected[model->tab] >= window_start
                           ? model->selected[model->tab] - window_start
                           : 0U,
      .popup_open = model->focus == READER_LIBRARY_FOCUS_POPUP,
      .selected_action = model->popup_action,
  };
}

static void draw_library(const reader_app_model_t *model, uint8_t *buffer) {
  ink_epd_ui_library_view_t view;
  char lines[INK_EPD_UI_MENU_CARD_CAPACITY][96] = {{0}};
  char meta[24];
  const bool localized = ink_cpfont_is_loaded(s_library_fonts.body);
  build_library_view(model, &view, lines, meta, localized);
  ink_epd_ui_draw_library(buffer, INK_EPD_BUFFER_SIZE, &view,
                          &s_library_fonts);
}

static bool refresh_library_candidate(const reader_app_model_t *previous,
                                      const reader_app_model_t *candidate,
                                      uint8_t *framebuffer,
                                      uint8_t *candidate_framebuffer,
                                      reader_refresh_state_t *refresh_state) {
  draw_library(candidate, candidate_framebuffer);
  ink_epd_region_t changed = {0};
  const bool has_changes = find_changed_region(
      framebuffer, candidate_framebuffer, INK_HW_WIDTH, INK_HW_HEIGHT,
      &changed);
  const reader_refresh_mode_t mode = reader_refresh_state_choose(
      refresh_state, READER_REFRESH_PARTIAL);
  if (!has_changes && mode == READER_REFRESH_PARTIAL)
    return true;
  esp_err_t ret;
  if (mode == READER_REFRESH_FULL) {
    ESP_LOGI(TAG, "LIBRARY_REFRESH mode=recovery_full");
    ret = ink_hw_full_refresh(candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  } else {
    const ink_epd_ui_library_focus_t previous_focus = library_focus(previous);
    const ink_epd_ui_library_focus_t candidate_focus = library_focus(candidate);
    const ink_epd_region_t policy = ink_epd_ui_library_selection_region(
        &previous_focus, &candidate_focus);
    ESP_LOGI(
        TAG,
        "LIBRARY_REFRESH mode=partial x=%d y=%d w=%d h=%d policy=%d,%d,%d,%d",
        changed.x, changed.y, changed.width, changed.height, policy.x, policy.y,
        policy.width, policy.height);
    ret = ink_hw_partial_refresh_area(
        candidate_framebuffer, INK_EPD_BUFFER_SIZE, (uint16_t)changed.x,
        (uint16_t)changed.y, (uint16_t)changed.width,
        (uint16_t)changed.height);
  }
  reader_refresh_state_record(refresh_state, ret == ESP_OK);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "library refresh failed err=%s",
             esp_err_to_name(ret));
    return false;
  }
  memcpy(framebuffer, candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  return true;
}

static bool save_state(const char *context) {
  if (ink_reader_state_save(INK_READER_STATE_PATH, &s_state)) return true;
  ESP_LOGE(TAG, "state save failed context=%s path=%s", context,
           INK_READER_STATE_PATH);
  return false;
}

static size_t chapter_index_for_page(const ink_reader_book_t *book,
                                     size_t page_index) {
  for (size_t i = 0; i < book->chapter_count; ++i) {
    const ink_reader_chapter_t *chapter = ink_reader_book_chapter_at(book, i);
    if (chapter && page_index >= chapter->start_page &&
        page_index <= chapter->end_page)
      return i;
  }
  return 0U;
}

static const char *chapter_title_for_page(const ink_reader_book_t *book,
                                          size_t page_index) {
  const ink_reader_chapter_t *chapter =
      ink_reader_book_chapter_for_page(book, page_index);
  return chapter ? chapter->title : "";
}

static void update_progress(const ink_reader_book_t *book) {
  if (!book || !book->file) return;
  const size_t chapter_index =
      chapter_index_for_page(book, book->current_page);
  ink_reader_state_remember_open(&s_state, book->path, book->current_page,
                                 chapter_index, book->page_count);
  size_t shelf_index = 0U;
  if (!ink_reader_state_find_bookshelf(&s_state, book->path, &shelf_index))
    return;
  ink_reader_bookshelf_entry_t *entry = &s_state.bookshelf[shelf_index];
  entry->page_index = book->current_page;
  entry->chapter_index = chapter_index;
  entry->total_pages_snapshot = book->page_count;
  snprintf(entry->chapter_title, sizeof(entry->chapter_title), "%s",
           chapter_title_for_page(book, book->current_page));
}

typedef struct {
  char titles[INK_EPD_UI_READER_MENU_ITEM_CAPACITY][96];
  char details[INK_EPD_UI_READER_MENU_ITEM_CAPACITY][48];
} reader_menu_text_t;

static void build_reader_menu_view(
    const reader_app_model_t *model, const ink_reader_book_t *book,
    const ink_reader_state_t *state, ink_epd_ui_reader_menu_view_t *view,
    reader_menu_text_t *text, bool localized) {
  memset(view, 0, sizeof(*view));
  memset(text, 0, sizeof(*text));
  view->tab_count = 2U;
  view->bookmarks_tab = model->reader_menu_tab == READER_MENU_BOOKMARKS;
  view->tabs[0].label = localized ? "章节" : "CHAPTERS";
  view->tabs[1].label = localized ? "书签" : "BOOKMARKS";
  for (size_t i = 0; i < view->tab_count; ++i) {
    view->tabs[i].active = model->reader_menu_tab == (reader_menu_tab_t)i;
    view->tabs[i].focused = view->tabs[i].active &&
                            model->reader_menu_level == READER_MENU_LEVEL_TABS;
  }

  const size_t window_start = reader_app_model_menu_window_start(model);
  if (!view->bookmarks_tab) {
    const size_t remaining = book->chapter_count > window_start
                                 ? book->chapter_count - window_start
                                 : 0U;
    view->item_count =
        remaining < INK_EPD_UI_READER_MENU_ITEM_CAPACITY
            ? remaining
            : INK_EPD_UI_READER_MENU_ITEM_CAPACITY;
    for (size_t i = 0; i < view->item_count; ++i) {
      const size_t chapter_index = window_start + i;
      const ink_reader_chapter_t *chapter =
          ink_reader_book_chapter_at(book, chapter_index);
      const char *title = chapter ? chapter->title : "";
      if (title[0] != '\0' && (localized || is_printable_ascii(title)))
        snprintf(text->titles[i], sizeof(text->titles[i]), "%s", title);
      else
        snprintf(text->titles[i], sizeof(text->titles[i]), "CHAPTER %u",
                 (unsigned)chapter_index + 1U);
      view->items[i].title = text->titles[i];
      view->items[i].line1 = "";
      view->items[i].selected =
          model->reader_menu_level == READER_MENU_LEVEL_ITEMS &&
          model->chapter_item_index == chapter_index;
    }
  } else {
    const size_t bookmark_count =
        ink_reader_state_bookmark_count(state, book->path);
    const size_t total_items = bookmark_count + 1U;
    const size_t remaining = total_items > window_start
                                 ? total_items - window_start
                                 : 0U;
    view->item_count =
        remaining < INK_EPD_UI_READER_MENU_BOOKMARK_VISIBLE
            ? remaining
            : INK_EPD_UI_READER_MENU_BOOKMARK_VISIBLE;
    for (size_t i = 0; i < view->item_count; ++i) {
      const size_t item_index = window_start + i;
      if (item_index == 0U) {
        snprintf(text->titles[i], sizeof(text->titles[i]), "%s",
                 localized ? "将当前页添加到书签" : "ADD CURRENT PAGE");
        snprintf(text->details[i], sizeof(text->details[i]), "T+%u:%02u",
                 (unsigned)(book->current_page / 60U),
                 (unsigned)(book->current_page % 60U));
      } else {
        const size_t slot = reader_app_model_bookmark_slot(
            state, book->path, item_index - 1U);
        const ink_reader_bookmark_t *bookmark =
            ink_reader_state_bookmark_at(state, slot);
        if (bookmark) {
          const char *title = bookmark->chapter_title;
          if (title[0] != '\0' && (localized || is_printable_ascii(title)))
            snprintf(text->titles[i], sizeof(text->titles[i]), "%s", title);
          else
            snprintf(text->titles[i], sizeof(text->titles[i]), "PAGE %u",
                     (unsigned)bookmark->page_index + 1U);
          snprintf(text->details[i], sizeof(text->details[i]), "%s",
                   bookmark->timestamp_text);
        }
      }
      view->items[i].title = text->titles[i];
      view->items[i].line1 = text->details[i];
      view->items[i].selected =
          model->reader_menu_level == READER_MENU_LEVEL_ITEMS &&
          model->bookmark_item_index == item_index;
    }
  }

  if (model->reader_menu_level != READER_MENU_LEVEL_BOOKMARK_ACTIONS)
    return;
  view->popup_open = true;
  view->popup_title = localized ? "书签操作" : "BOOKMARK ACTION";
  view->action_count = 3U;
  static const char *const localized_actions[] = {"跳转", "覆盖", "删除"};
  static const char *const ascii_actions[] = {"JUMP", "OVERWRITE", "DELETE"};
  const char *const *actions = localized ? localized_actions : ascii_actions;
  for (size_t i = 0; i < view->action_count; ++i) {
    view->actions[i].label = actions[i];
    view->actions[i].selected = model->bookmark_action_index == i;
  }
}

static void draw_reader_menu(const reader_app_model_t *model,
                             const ink_reader_book_t *book,
                             const ink_reader_state_t *state,
                             uint8_t *buffer) {
  ink_epd_ui_reader_menu_view_t view;
  reader_menu_text_t text;
  build_reader_menu_view(model, book, state, &view, &text,
                         ink_cpfont_is_loaded(s_library_fonts.body));
  ink_epd_ui_draw_reader_menu(buffer, INK_EPD_BUFFER_SIZE, &view,
                              &s_library_fonts);
}

static bool refresh_reader_menu_candidate(
    const reader_app_model_t *previous,
    const reader_app_model_t *candidate_model, const ink_reader_book_t *book,
    const ink_reader_state_t *candidate_state, uint8_t *framebuffer,
    uint8_t *candidate_framebuffer,
    reader_refresh_state_t *refresh_state) {
  (void)previous;
  memcpy(candidate_framebuffer, framebuffer, INK_EPD_BUFFER_SIZE);
  draw_reader_menu(candidate_model, book, candidate_state,
                   candidate_framebuffer);
  ink_epd_region_t changed = {0};
  const bool has_changes = find_changed_region(
      framebuffer, candidate_framebuffer, INK_HW_WIDTH, INK_HW_HEIGHT,
      &changed);
  const reader_refresh_mode_t mode = reader_refresh_state_choose(
      refresh_state, READER_REFRESH_PARTIAL);
  esp_err_t ret = ESP_OK;
  if (mode == READER_REFRESH_FULL) {
    ESP_LOGI(TAG, "READER_MENU_REFRESH mode=recovery_full");
    ret = ink_hw_full_refresh(candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  } else if (has_changes) {
    const ink_epd_region_t menu_region =
        ink_epd_ui_reader_menu_selection_region(NULL, NULL);
    ESP_LOGI(TAG,
             "READER_MENU_REFRESH mode=partial x=%d y=%d w=%d h=%d",
             changed.x, changed.y, changed.width, changed.height);
    ret = ink_hw_partial_refresh_area(
        candidate_framebuffer, INK_EPD_BUFFER_SIZE,
        (uint16_t)menu_region.x, (uint16_t)menu_region.y,
        (uint16_t)menu_region.width, (uint16_t)menu_region.height);
  }
  reader_refresh_state_record(refresh_state, ret == ESP_OK);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "reader menu refresh failed err=%s", esp_err_to_name(ret));
    return false;
  }
  memcpy(framebuffer, candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  return true;
}

static bool refresh_reader_page_candidate(
    reader_app_model_t *candidate_model, ink_reader_book_t *book,
    size_t target_page, size_t rollback_page, uint8_t *framebuffer,
    uint8_t *candidate_framebuffer,
    reader_refresh_state_t *refresh_state) {
  if (target_page >= book->page_count ||
      !ink_reader_book_load_page(book, target_page, candidate_framebuffer,
                                 INK_EPD_BUFFER_SIZE)) {
    book->current_page = rollback_page;
    return false;
  }
  ink_epd_region_t changed = {0};
  const bool has_changes = find_changed_region(
      framebuffer, candidate_framebuffer, INK_HW_WIDTH, INK_HW_HEIGHT,
      &changed);
  const bool recovery = reader_refresh_state_choose(
                            refresh_state, READER_REFRESH_PARTIAL) ==
                        READER_REFRESH_FULL;
  esp_err_t ret = ESP_OK;
  if (recovery) {
    ret = ink_hw_full_refresh(candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  } else if (has_changes) {
    ret = ink_hw_partial_refresh_area(
        candidate_framebuffer, INK_EPD_BUFFER_SIZE, (uint16_t)changed.x,
        (uint16_t)changed.y, (uint16_t)changed.width,
        (uint16_t)changed.height);
  }
  reader_refresh_state_record(refresh_state, ret == ESP_OK);
  if (ret != ESP_OK) {
    book->current_page = rollback_page;
    return false;
  }
  reader_app_model_close_reader_menu(candidate_model);
  memcpy(framebuffer, candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  return true;
}

static bool open_selected_book(reader_app_model_t *candidate_model,
                               ink_reader_book_t *book, uint8_t *framebuffer,
                               uint8_t *candidate_framebuffer,
                               reader_refresh_state_t *refresh_state,
                               unsigned *successful_page_turns) {
  const size_t catalog_index =
      reader_app_model_selected_catalog_index(candidate_model);
  const ink_reader_catalog_item_t *item =
      ink_reader_catalog_at(&s_catalog, catalog_index);
  if (!item) return false;
  ink_reader_book_t candidate_book;
  ink_reader_book_init(&candidate_book);
  if (!ink_reader_book_open(&candidate_book, item->path)) {
    ESP_LOGE(TAG, "book open failed path=%s", item->path);
    return false;
  }
  size_t page_index = 0U;
  (void)ink_reader_state_find_progress(&s_state, item->path, &page_index, NULL,
                                       NULL);
  if (page_index >= candidate_book.page_count)
    page_index = candidate_book.page_count - 1U;
  if (!ink_reader_book_load_page(&candidate_book, page_index,
                                 candidate_framebuffer,
                                 INK_EPD_BUFFER_SIZE)) {
    ESP_LOGE(TAG, "book page load failed path=%s page=%u", item->path,
             (unsigned)page_index);
    ink_reader_book_close(&candidate_book);
    return false;
  }
  const esp_err_t refresh_ret =
      ink_hw_full_refresh(candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  reader_refresh_state_record(refresh_state, refresh_ret == ESP_OK);
  if (refresh_ret != ESP_OK) {
    ESP_LOGE(TAG, "book full refresh failed path=%s err=%s", item->path,
             esp_err_to_name(refresh_ret));
    ink_reader_book_close(&candidate_book);
    return false;
  }

  ink_reader_book_close(book);
  *book = candidate_book;
  memcpy(framebuffer, candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  candidate_model->page = READER_APP_PAGE_READING;
  candidate_model->focus = READER_LIBRARY_FOCUS_ITEMS;
  candidate_model->popup_action = 0U;
  *successful_page_turns = 0U;
  const char *title = book->has_metadata && book->metadata.title[0] != '\0'
                          ? book->metadata.title
                          : item->name;
  if (!ink_reader_state_note_open(
          &s_state, book->path, title, book->current_page,
          chapter_index_for_page(book, book->current_page), book->page_count,
          chapter_title_for_page(book, book->current_page)))
    ESP_LOGE(TAG, "state note open failed path=%s", book->path);
  update_progress(book);
  reader_app_model_rebuild(candidate_model, &s_catalog, &s_state);
  (void)save_state("open");
  return true;
}

static bool toggle_selected_favorite(
    reader_app_model_t *candidate_model, uint8_t *framebuffer,
    uint8_t *candidate_framebuffer,
    reader_refresh_state_t *refresh_state) {
  const size_t catalog_index =
      reader_app_model_selected_catalog_index(candidate_model);
  const ink_reader_catalog_item_t *item =
      ink_reader_catalog_at(&s_catalog, catalog_index);
  if (!item) return false;
  ink_reader_state_t *candidate_state = heap_caps_malloc(
      sizeof(*candidate_state), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!candidate_state) {
    ESP_LOGE(TAG, "favorite candidate allocation failed");
    return false;
  }
  memcpy(candidate_state, &s_state, sizeof(*candidate_state));
  const ink_reader_bookshelf_entry_t *entry = find_shelf(item->path);
  const bool favorite = entry && entry->is_favorite;
  const char *title = catalog_title(item, entry);
  if (!ink_reader_state_set_favorite(candidate_state, item->path, title,
                                     !favorite)) {
    ESP_LOGE(TAG, "favorite state mutation failed path=%s", item->path);
    free(candidate_state);
    return false;
  }
  reader_app_model_rebuild(candidate_model, &s_catalog, candidate_state);
  const bool refreshed = refresh_library_candidate(
      &s_model, candidate_model, framebuffer, candidate_framebuffer,
      refresh_state);
  if (refreshed) {
    memcpy(&s_state, candidate_state, sizeof(s_state));
    s_model = *candidate_model;
    (void)save_state("favorite");
  }
  free(candidate_state);
  return refreshed;
}

static bool return_to_library(reader_app_model_t *candidate_model,
                              const ink_reader_book_t *book,
                              uint8_t *framebuffer,
                              uint8_t *candidate_framebuffer,
                              reader_refresh_state_t *refresh_state) {
  update_progress(book);
  reader_app_model_rebuild(candidate_model, &s_catalog, &s_state);
  draw_library(candidate_model, candidate_framebuffer);
  const esp_err_t ret =
      ink_hw_full_refresh(candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  reader_refresh_state_record(refresh_state, ret == ESP_OK);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "library full refresh failed err=%s", esp_err_to_name(ret));
    return false;
  }
  memcpy(framebuffer, candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  s_model = *candidate_model;
  (void)save_state("return_library");
  return true;
}

static void handle_library_input(reader_app_input_t input,
                                 ink_reader_book_t *book,
                                 uint8_t *framebuffer,
                                 uint8_t *candidate_framebuffer,
                                 reader_refresh_state_t *refresh_state,
                                 unsigned *successful_page_turns) {
  s_candidate_model = s_model;
  const reader_app_effect_t effect =
      reader_app_model_reduce(&s_candidate_model, input);
  if (effect == READER_APP_EFFECT_NONE) return;
  if (effect == READER_APP_EFFECT_RETURN_LAUNCHER) {
    ESP_LOGI(TAG, "BOOT_SWITCH from=reader to=launcher");
    const esp_err_t ret = ink_boot_switch_to_launcher();
    if (ret != ESP_OK)
      ESP_LOGE(TAG, "return to launcher failed err=%s", esp_err_to_name(ret));
    return;
  }
  if (effect == READER_APP_EFFECT_OPEN_SELECTED) {
    if (open_selected_book(&s_candidate_model, book, framebuffer,
                           candidate_framebuffer, refresh_state,
                           successful_page_turns))
      s_model = s_candidate_model;
    return;
  }
  if (effect == READER_APP_EFFECT_TOGGLE_FAVORITE) {
    (void)toggle_selected_favorite(&s_candidate_model, framebuffer,
                                   candidate_framebuffer, refresh_state);
    return;
  }
  if (refresh_library_candidate(&s_model, &s_candidate_model, framebuffer,
                                candidate_framebuffer, refresh_state))
    s_model = s_candidate_model;
}

static bool apply_bookmark_mutation(
    reader_app_effect_t effect, reader_app_model_t *candidate_model,
    const ink_reader_book_t *book, uint8_t *framebuffer,
    uint8_t *candidate_framebuffer,
    reader_refresh_state_t *refresh_state) {
  ink_reader_state_t *candidate_state = heap_caps_malloc(
      sizeof(*candidate_state), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!candidate_state) return false;
  memcpy(candidate_state, &s_state, sizeof(*candidate_state));

  char timestamp[INK_READER_BOOKMARK_TIME_LENGTH + 1];
  snprintf(timestamp, sizeof(timestamp), "T+%u:%02u",
           (unsigned)(book->current_page / 60U),
           (unsigned)(book->current_page % 60U));
  const size_t chapter_index =
      chapter_index_for_page(book, book->current_page);
  const char *chapter_title =
      chapter_title_for_page(book, book->current_page);
  bool mutated = false;
  if (effect == READER_APP_EFFECT_ADD_BOOKMARK) {
    mutated = ink_reader_state_bookmark_add_or_replace(
        candidate_state, book->path, book->current_page, chapter_index,
        book->page_count, chapter_title, timestamp);
  } else {
    if (candidate_model->bookmark_item_index == 0U) {
      free(candidate_state);
      return false;
    }
    const size_t slot = reader_app_model_bookmark_slot(
        candidate_state, book->path,
        candidate_model->bookmark_item_index - 1U);
    const ink_reader_bookmark_t *bookmark =
        ink_reader_state_bookmark_at(candidate_state, slot);
    if (!bookmark) {
      free(candidate_state);
      return false;
    }
    if (effect == READER_APP_EFFECT_OVERWRITE_BOOKMARK) {
      mutated = ink_reader_state_bookmark_overwrite(
          candidate_state, slot, book->path, book->current_page,
          chapter_index, book->page_count, chapter_title, timestamp);
      candidate_model->reader_menu_level = READER_MENU_LEVEL_ITEMS;
      candidate_model->bookmark_action_index = 0U;
    } else if (effect == READER_APP_EFFECT_DELETE_BOOKMARK) {
      mutated = ink_reader_state_bookmark_remove_at(candidate_state, slot);
      reader_app_model_bookmark_deleted(
          candidate_model,
          ink_reader_state_bookmark_count(candidate_state, book->path));
    }
  }
  if (!mutated) {
    ESP_LOGE(TAG, "bookmark state mutation failed effect=%d", (int)effect);
    free(candidate_state);
    return false;
  }

  const bool refreshed = refresh_reader_menu_candidate(
      &s_model, candidate_model, book, candidate_state, framebuffer,
      candidate_framebuffer, refresh_state);
  if (!refreshed) {
    free(candidate_state);
    return false;
  }
  if (!ink_reader_state_save(INK_READER_STATE_PATH, candidate_state)) {
    ESP_LOGE(TAG, "bookmark state save failed effect=%d", (int)effect);
    if (!refresh_reader_menu_candidate(
            candidate_model, &s_model, book, &s_state, framebuffer,
            candidate_framebuffer, refresh_state))
      ESP_LOGE(TAG, "bookmark menu rollback refresh failed");
    free(candidate_state);
    return false;
  }
  memcpy(&s_state, candidate_state, sizeof(s_state));
  s_model = *candidate_model;
  free(candidate_state);
  return true;
}

static bool jump_from_reader_menu(
    reader_app_effect_t effect, reader_app_model_t *candidate_model,
    ink_reader_book_t *book, uint8_t *framebuffer,
    uint8_t *candidate_framebuffer, reader_refresh_state_t *refresh_state,
    unsigned *successful_page_turns) {
  const size_t previous_page = book->current_page;
  size_t target_page = previous_page;
  if (effect == READER_APP_EFFECT_JUMP_CHAPTER) {
    if (!ink_reader_book_jump_to_chapter(book,
                                         candidate_model->chapter_item_index))
      return false;
    target_page = book->current_page;
    book->current_page = previous_page;
  } else {
    if (candidate_model->bookmark_item_index == 0U) return false;
    const size_t slot = reader_app_model_bookmark_slot(
        &s_state, book->path, candidate_model->bookmark_item_index - 1U);
    const ink_reader_bookmark_t *bookmark =
        ink_reader_state_bookmark_at(&s_state, slot);
    if (!bookmark || bookmark->page_index >= book->page_count) return false;
    target_page = bookmark->page_index;
  }
  if (!refresh_reader_page_candidate(
          candidate_model, book, target_page, previous_page, framebuffer,
          candidate_framebuffer, refresh_state))
    return false;
  s_model = *candidate_model;
  *successful_page_turns = 0U;
  update_progress(book);
  (void)save_state("reader_menu_jump");
  return true;
}

static void handle_reading_input(reader_app_input_t input,
                                 ink_reader_book_t *book,
                                 uint8_t *framebuffer,
                                 uint8_t *candidate_framebuffer,
                                 reader_refresh_state_t *refresh_state,
                                 unsigned *successful_page_turns) {
  if (s_model.reader_menu_open || input == READER_APP_INPUT_CONFIRM) {
    s_candidate_model = s_model;
    const size_t bookmark_count =
        ink_reader_state_bookmark_count(&s_state, book->path);
    const reader_app_effect_t effect = reader_app_model_reduce_reading(
        &s_candidate_model, input, book->chapter_count, bookmark_count);
    if (effect == READER_APP_EFFECT_NONE) return;
    if (effect == READER_APP_EFFECT_REDRAW_MENU) {
      if (refresh_reader_menu_candidate(
              &s_model, &s_candidate_model, book, &s_state, framebuffer,
              candidate_framebuffer, refresh_state))
        s_model = s_candidate_model;
      return;
    }
    if (effect == READER_APP_EFFECT_CLOSE_READER_MENU) {
      const size_t current_page = book->current_page;
      if (refresh_reader_page_candidate(
              &s_candidate_model, book, current_page, current_page,
              framebuffer, candidate_framebuffer, refresh_state))
        s_model = s_candidate_model;
      return;
    }
    if (effect == READER_APP_EFFECT_JUMP_CHAPTER ||
        effect == READER_APP_EFFECT_JUMP_BOOKMARK) {
      (void)jump_from_reader_menu(
          effect, &s_candidate_model, book, framebuffer,
          candidate_framebuffer, refresh_state, successful_page_turns);
      return;
    }
    if (effect == READER_APP_EFFECT_ADD_BOOKMARK ||
        effect == READER_APP_EFFECT_OVERWRITE_BOOKMARK ||
        effect == READER_APP_EFFECT_DELETE_BOOKMARK) {
      (void)apply_bookmark_mutation(
          effect, &s_candidate_model, book, framebuffer,
          candidate_framebuffer, refresh_state);
      return;
    }
  }
  if (input == READER_APP_INPUT_BACK) {
    s_candidate_model = s_model;
    if (reader_app_model_reduce(&s_candidate_model, input) ==
        READER_APP_EFFECT_REDRAW)
      (void)return_to_library(&s_candidate_model, book, framebuffer,
                              candidate_framebuffer, refresh_state);
    return;
  }
  if (input != READER_APP_INPUT_LEFT && input != READER_APP_INPUT_RIGHT)
    return;
  size_t target_page = book->current_page;
  if (input == READER_APP_INPUT_LEFT && target_page > 0U)
    --target_page;
  if (input == READER_APP_INPUT_RIGHT && target_page + 1U < book->page_count)
    ++target_page;
  if (target_page == book->current_page) return;

  const size_t previous_page = book->current_page;
  memcpy(candidate_framebuffer, framebuffer, INK_EPD_BUFFER_SIZE);
  if (!ink_reader_book_load_page(book, target_page, candidate_framebuffer,
                                 INK_EPD_BUFFER_SIZE)) {
    ESP_LOGE(TAG, "page load failed page=%u", (unsigned)target_page);
    return;
  }
  ink_epd_region_t region = {0};
  const bool changed = find_changed_region(
      framebuffer, candidate_framebuffer, INK_HW_WIDTH, INK_HW_HEIGHT,
      &region);
  const bool recovery = reader_refresh_state_choose(
                            refresh_state, READER_REFRESH_PARTIAL) ==
                        READER_REFRESH_FULL;
  const bool cleanup =
      !recovery && reader_should_cleanup(*successful_page_turns);
  esp_err_t ret = ESP_OK;
  if (recovery) {
    ESP_LOGI(TAG, "PAGE_REFRESH mode=recovery_full page=%u",
             (unsigned)target_page);
    ret = ink_hw_full_refresh(candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  } else if (cleanup) {
    ESP_LOGI(TAG, "PAGE_REFRESH mode=cleanup_full page=%u",
             (unsigned)target_page);
    ret = ink_hw_full_refresh(candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  } else if (changed) {
    ESP_LOGI(TAG, "PAGE_REFRESH mode=partial page=%u x=%d y=%d w=%d h=%d",
             (unsigned)target_page, region.x, region.y, region.width,
             region.height);
    ret = ink_hw_partial_refresh_area(
        candidate_framebuffer, INK_EPD_BUFFER_SIZE, (uint16_t)region.x,
        (uint16_t)region.y, (uint16_t)region.width, (uint16_t)region.height);
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "page refresh failed err=%s", esp_err_to_name(ret));
    reader_refresh_state_record(refresh_state, false);
    book->current_page = previous_page;
    return;
  }
  memcpy(framebuffer, candidate_framebuffer, INK_EPD_BUFFER_SIZE);
  reader_refresh_state_record(refresh_state, true);
  *successful_page_turns =
      (cleanup || recovery) ? 0U : *successful_page_turns + 1U;
  update_progress(book);
}

static bool input_event(const ink_input_snapshot_t *input,
                        reader_app_input_t *event) {
  if (ink_input_was_pressed(input, INK_BUTTON_BACK))
    *event = READER_APP_INPUT_BACK;
  else if (ink_input_was_pressed(input, INK_BUTTON_CONFIRM))
    *event = READER_APP_INPUT_CONFIRM;
  else if (ink_input_was_pressed(input, INK_BUTTON_LEFT))
    *event = READER_APP_INPUT_LEFT;
  else if (ink_input_was_pressed(input, INK_BUTTON_RIGHT))
    *event = READER_APP_INPUT_RIGHT;
  else
    return false;
  return true;
}

void app_main(void) {
  const int64_t started_us = esp_timer_get_time();
  ESP_LOGI(TAG, "APP_START name=reader");
  if (!ink_reader_core_self_test() || !reader_app_model_self_test() ||
      !reader_refresh_policy_self_test()) {
    ESP_LOGE(TAG, "reader component self test failed");
    return;
  }
  uint8_t *framebuffer = heap_caps_malloc(INK_EPD_BUFFER_SIZE,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint8_t *candidate_framebuffer = heap_caps_malloc(
      INK_EPD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!framebuffer || !candidate_framebuffer) {
    ESP_LOGE(TAG, "framebuffer allocation failed current=%p candidate=%p",
             framebuffer, candidate_framebuffer);
    uint8_t *error_framebuffer =
        framebuffer ? framebuffer : candidate_framebuffer;
    const esp_err_t memory_display_ret = ink_hw_init();
    const esp_err_t memory_input_ret = ink_input_init();
    if (memory_display_ret == ESP_OK && error_framebuffer) {
      ink_epd_ui_draw_status(error_framebuffer, INK_EPD_BUFFER_SIZE, "READER",
                             "MEMORY ERROR");
      (void)ink_hw_full_refresh(error_framebuffer, INK_EPD_BUFFER_SIZE);
    }
    wait_for_launcher(memory_input_ret);
    return;
  }

  const esp_err_t display_ret = ink_hw_init();
  if (display_ret != ESP_OK)
    ESP_LOGE(TAG, "display init failed err=%s", esp_err_to_name(display_ret));
  const esp_err_t input_ret = ink_input_init();
  if (input_ret != ESP_OK)
    ESP_LOGE(TAG, "input init failed err=%s", esp_err_to_name(input_ret));
  const esp_err_t sd_ret = ink_sd_mount();
  log_stage("sd_mount_done", started_us);

  ink_reader_catalog_init(&s_catalog);
  ink_reader_state_default(&s_state);
  if (sd_ret == ESP_OK) {
    if (!ink_reader_catalog_load(&s_catalog))
      ESP_LOGW(TAG, "catalog unavailable path=/sdcard/books");
    const ink_reader_state_result_t state_ret =
        ink_reader_state_load(INK_READER_STATE_PATH, &s_state);
    if (state_ret != INK_READER_STATE_OK &&
        state_ret != INK_READER_STATE_NOT_FOUND) {
      ESP_LOGW(TAG, "state load ignored result=%d", (int)state_ret);
      ink_reader_state_default(&s_state);
    }
    const bool menu_ok = ink_fonts_load(&s_menu_font, INK_FONT_MENU);
    const bool footer_ok = ink_fonts_load(&s_footer_font, INK_FONT_FOOTER);
    if (menu_ok) {
      s_library_fonts.title = &s_menu_font;
      s_library_fonts.body = &s_menu_font;
    } else {
      ESP_LOGW(TAG, "menu font unavailable; using ASCII fallback");
    }
    if (footer_ok)
      s_library_fonts.footer = &s_footer_font;
    else
      ESP_LOGW(TAG, "footer font unavailable; using ASCII fallback");
  } else {
    ESP_LOGE(TAG, "SD mount failed err=%s", esp_err_to_name(sd_ret));
  }
  log_stage("library_data_loaded", started_us);

  reader_app_model_init(&s_model);
  reader_app_model_rebuild(&s_model, &s_catalog, &s_state);
  draw_library(&s_model, framebuffer);
  reader_refresh_state_t refresh_state;
  reader_refresh_state_init(&refresh_state);
  if (display_ret == ESP_OK) {
    ESP_LOGI(TAG, "FIRST_REFRESH mode=full page=library");
    const esp_err_t ret =
        ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
    reader_refresh_state_record(&refresh_state, ret == ESP_OK);
    if (ret == ESP_OK) {
      log_stage("first_refresh_done", started_us);
    } else {
      ESP_LOGE(TAG, "library refresh failed err=%s", esp_err_to_name(ret));
    }
  }

  ink_reader_book_t book;
  ink_reader_book_init(&book);
  unsigned successful_page_turns = 0U;
  while (true) {
    ink_input_snapshot_t input = {0};
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    reader_app_input_t event;
    if (input_ret == ESP_OK && ink_input_poll(now_ms, &input) == ESP_OK &&
        input_event(&input, &event)) {
      if (s_model.page == READER_APP_PAGE_LIBRARY)
        handle_library_input(event, &book, framebuffer, candidate_framebuffer,
                             &refresh_state, &successful_page_turns);
      else
        handle_reading_input(event, &book, framebuffer, candidate_framebuffer,
                             &refresh_state, &successful_page_turns);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
