#include <stdio.h>
#include <string.h>

#include "reader_app_model.h"

static void set_item(ink_reader_catalog_item_t *item, const char *name) {
  snprintf(item->path, sizeof(item->path), "/sdcard/books/%s.xtc", name);
  snprintf(item->name, sizeof(item->name), "%s.xtc", name);
}

static void set_shelf(ink_reader_bookshelf_entry_t *entry, const char *name,
                      bool opened, bool favorite, uint32_t order) {
  entry->used = true;
  entry->has_opened = opened;
  entry->is_favorite = favorite;
  entry->recent_order = order;
  snprintf(entry->book_path, sizeof(entry->book_path),
           "/sdcard/books/%s.xtc", name);
}

static int expect_defaults_and_filters(void) {
  ink_reader_catalog_item_t items[4];
  ink_reader_catalog_t catalog = {.items = items, .count = 4};
  ink_reader_state_t state = {0};
  reader_app_model_t model;
  set_item(&items[0], "alpha");
  set_item(&items[1], "beta");
  set_item(&items[2], "gamma");
  set_item(&items[3], "delta");
  set_shelf(&state.bookshelf[0], "alpha", true, false, 2);
  set_shelf(&state.bookshelf[1], "beta", true, true, 8);
  set_shelf(&state.bookshelf[2], "missing", true, true, 99);
  set_shelf(&state.bookshelf[3], "gamma", false, true, 0);

  reader_app_model_init(&model);
  reader_app_model_rebuild(&model, &catalog, &state);
  return model.page == READER_APP_PAGE_LIBRARY &&
         model.tab == READER_LIBRARY_TAB_RECENT &&
         model.focus == READER_LIBRARY_FOCUS_ITEMS &&
         reader_app_model_visible_count(&model, READER_LIBRARY_TAB_RECENT) ==
             2 &&
         reader_app_model_visible_catalog_index(
             &model, READER_LIBRARY_TAB_RECENT, 0) == 1 &&
         reader_app_model_visible_catalog_index(
             &model, READER_LIBRARY_TAB_RECENT, 1) == 0 &&
         reader_app_model_visible_count(&model, READER_LIBRARY_TAB_ALL) == 4 &&
         reader_app_model_visible_catalog_index(
             &model, READER_LIBRARY_TAB_ALL, 3) == 3 &&
         reader_app_model_visible_count(&model,
                                        READER_LIBRARY_TAB_FAVORITES) == 2 &&
         reader_app_model_visible_catalog_index(
             &model, READER_LIBRARY_TAB_FAVORITES, 0) == 1 &&
         reader_app_model_visible_catalog_index(
             &model, READER_LIBRARY_TAB_FAVORITES, 1) == 2;
}

static int expect_item_tab_and_popup_inputs(void) {
  ink_reader_catalog_item_t item;
  ink_reader_catalog_t catalog = {.items = &item, .count = 1};
  ink_reader_state_t state = {0};
  reader_app_model_t model;
  set_item(&item, "one");
  reader_app_model_init(&model);
  reader_app_model_rebuild(&model, &catalog, &state);
  model.tab = READER_LIBRARY_TAB_ALL;

  if (reader_app_model_reduce(&model, READER_APP_INPUT_LEFT) !=
          READER_APP_EFFECT_REDRAW ||
      model.selected[READER_LIBRARY_TAB_ALL] != 0 ||
      reader_app_model_reduce(&model, READER_APP_INPUT_BACK) !=
          READER_APP_EFFECT_REDRAW ||
      model.focus != READER_LIBRARY_FOCUS_TABS)
    return 0;
  model.tab = READER_LIBRARY_TAB_RECENT;
  if (reader_app_model_reduce(&model, READER_APP_INPUT_LEFT) !=
          READER_APP_EFFECT_REDRAW ||
      model.tab != READER_LIBRARY_TAB_FAVORITES ||
      reader_app_model_reduce(&model, READER_APP_INPUT_RIGHT) !=
          READER_APP_EFFECT_REDRAW ||
      model.tab != READER_LIBRARY_TAB_RECENT ||
      reader_app_model_reduce(&model, READER_APP_INPUT_CONFIRM) !=
          READER_APP_EFFECT_REDRAW ||
      model.focus != READER_LIBRARY_FOCUS_ITEMS)
    return 0;
  model.focus = READER_LIBRARY_FOCUS_TABS;
  if (reader_app_model_reduce(&model, READER_APP_INPUT_BACK) !=
      READER_APP_EFFECT_RETURN_LAUNCHER)
    return 0;

  model.tab = READER_LIBRARY_TAB_ALL;
  model.focus = READER_LIBRARY_FOCUS_ITEMS;
  if (reader_app_model_reduce(&model, READER_APP_INPUT_CONFIRM) !=
          READER_APP_EFFECT_REDRAW ||
      model.focus != READER_LIBRARY_FOCUS_POPUP || model.popup_action != 0 ||
      reader_app_model_reduce(&model, READER_APP_INPUT_RIGHT) !=
          READER_APP_EFFECT_REDRAW ||
      model.popup_action != 1 ||
      reader_app_model_reduce(&model, READER_APP_INPUT_CONFIRM) !=
          READER_APP_EFFECT_TOGGLE_FAVORITE ||
      reader_app_model_reduce(&model, READER_APP_INPUT_LEFT) !=
          READER_APP_EFFECT_REDRAW ||
      reader_app_model_reduce(&model, READER_APP_INPUT_CONFIRM) !=
          READER_APP_EFFECT_OPEN_SELECTED ||
      reader_app_model_reduce(&model, READER_APP_INPUT_BACK) !=
          READER_APP_EFFECT_REDRAW ||
      model.focus != READER_LIBRARY_FOCUS_ITEMS)
    return 0;
  return 1;
}

static int expect_independent_selection_and_empty_tab(void) {
  ink_reader_catalog_item_t items[3];
  ink_reader_catalog_t catalog = {.items = items, .count = 3};
  ink_reader_state_t state = {0};
  reader_app_model_t model;
  for (size_t i = 0; i < 3; ++i) {
    char name[8];
    snprintf(name, sizeof(name), "book%u", (unsigned)i);
    set_item(&items[i], name);
  }
  reader_app_model_init(&model);
  reader_app_model_rebuild(&model, &catalog, &state);
  model.tab = READER_LIBRARY_TAB_ALL;
  reader_app_model_reduce(&model, READER_APP_INPUT_RIGHT);
  reader_app_model_reduce(&model, READER_APP_INPUT_RIGHT);
  model.focus = READER_LIBRARY_FOCUS_TABS;
  reader_app_model_reduce(&model, READER_APP_INPUT_LEFT);
  if (model.tab != READER_LIBRARY_TAB_RECENT ||
      model.selected[READER_LIBRARY_TAB_ALL] != 2 ||
      reader_app_model_reduce(&model, READER_APP_INPUT_CONFIRM) !=
          READER_APP_EFFECT_REDRAW ||
      reader_app_model_reduce(&model, READER_APP_INPUT_CONFIRM) !=
          READER_APP_EFFECT_NONE)
    return 0;
  model.focus = READER_LIBRARY_FOCUS_TABS;
  reader_app_model_reduce(&model, READER_APP_INPUT_RIGHT);
  return model.tab == READER_LIBRARY_TAB_ALL &&
         model.selected[READER_LIBRARY_TAB_ALL] == 2 &&
         reader_app_model_window_start(&model) == 0;
}

static int expect_favorite_removal_closes_and_clamps(void) {
  ink_reader_catalog_item_t items[3];
  ink_reader_catalog_t catalog = {.items = items, .count = 3};
  ink_reader_state_t state = {0};
  reader_app_model_t model;
  for (size_t i = 0; i < 3; ++i) {
    char name[8];
    snprintf(name, sizeof(name), "fav%u", (unsigned)i);
    set_item(&items[i], name);
    set_shelf(&state.bookshelf[i], name, false, true, 0);
  }
  reader_app_model_init(&model);
  reader_app_model_rebuild(&model, &catalog, &state);
  model.tab = READER_LIBRARY_TAB_FAVORITES;
  model.selected[READER_LIBRARY_TAB_FAVORITES] = 2;
  model.focus = READER_LIBRARY_FOCUS_POPUP;
  model.popup_action = 1;
  state.bookshelf[2].is_favorite = false;
  reader_app_model_rebuild(&model, &catalog, &state);
  return model.focus == READER_LIBRARY_FOCUS_ITEMS &&
         model.popup_action == 0 &&
         model.selected[READER_LIBRARY_TAB_FAVORITES] == 1 &&
         reader_app_model_selected_catalog_index(&model) == 1;
}

static int expect_recent_rebuild_keeps_opened_catalog_identity(void) {
  ink_reader_catalog_item_t items[3];
  ink_reader_catalog_t catalog = {.items = items, .count = 3};
  ink_reader_state_t state = {0};
  reader_app_model_t model;
  for (size_t i = 0; i < 3; ++i) {
    char name[8];
    snprintf(name, sizeof(name), "recent%u", (unsigned)i);
    set_item(&items[i], name);
    set_shelf(&state.bookshelf[i], name, true, false,
              (uint32_t)(3U - i));
  }

  reader_app_model_init(&model);
  reader_app_model_rebuild(&model, &catalog, &state);
  model.selected[READER_LIBRARY_TAB_RECENT] = 1U;
  if (reader_app_model_selected_catalog_index(&model) != 1U) return 0;

  model.page = READER_APP_PAGE_READING;
  model.focus = READER_LIBRARY_FOCUS_ITEMS;
  state.bookshelf[1].recent_order = 4U;
  reader_app_model_rebuild(&model, &catalog, &state);

  return reader_app_model_selected_catalog_index(&model) == 1U &&
         model.selected[READER_LIBRARY_TAB_RECENT] == 0U;
}

static int expect_visible_window(void) {
  ink_reader_catalog_item_t items[10];
  ink_reader_catalog_t catalog = {.items = items, .count = 10};
  ink_reader_state_t state = {0};
  reader_app_model_t model;
  for (size_t i = 0; i < 10; ++i) {
    char name[8];
    snprintf(name, sizeof(name), "all%u", (unsigned)i);
    set_item(&items[i], name);
  }
  reader_app_model_init(&model);
  reader_app_model_rebuild(&model, &catalog, &state);
  model.tab = READER_LIBRARY_TAB_ALL;
  model.selected[READER_LIBRARY_TAB_ALL] = 9;
  return reader_app_model_window_start(&model) == 2;
}

static int expect_reader_menu_hierarchy_and_chapter_effect(void) {
  reader_app_model_t model;
  reader_app_model_init(&model);
  model.page = READER_APP_PAGE_READING;

  if (reader_app_model_reduce_reading(&model, READER_APP_INPUT_CONFIRM, 10,
                                      3) !=
          READER_APP_EFFECT_REDRAW_MENU ||
      !model.reader_menu_open || model.reader_menu_tab != READER_MENU_CHAPTERS ||
      model.reader_menu_level != READER_MENU_LEVEL_TABS ||
      model.chapter_item_index != 0 || model.bookmark_item_index != 0 ||
      model.bookmark_action_index != 0)
    return 0;
  if (reader_app_model_reduce_reading(&model, READER_APP_INPUT_RIGHT, 10, 3) !=
          READER_APP_EFFECT_REDRAW_MENU ||
      model.reader_menu_tab != READER_MENU_BOOKMARKS ||
      reader_app_model_reduce_reading(&model, READER_APP_INPUT_LEFT, 10, 3) !=
          READER_APP_EFFECT_REDRAW_MENU ||
      model.reader_menu_tab != READER_MENU_CHAPTERS ||
      reader_app_model_reduce_reading(&model, READER_APP_INPUT_CONFIRM, 10,
                                      3) !=
          READER_APP_EFFECT_REDRAW_MENU ||
      model.reader_menu_level != READER_MENU_LEVEL_ITEMS)
    return 0;

  for (size_t i = 0; i < 12; ++i)
    (void)reader_app_model_reduce_reading(&model, READER_APP_INPUT_RIGHT, 10,
                                          3);
  if (model.chapter_item_index != 9 ||
      reader_app_model_reduce_reading(&model, READER_APP_INPUT_CONFIRM, 10,
                                      3) !=
          READER_APP_EFFECT_JUMP_CHAPTER ||
      model.reader_menu_open == false)
    return 0;
  if (reader_app_model_reduce_reading(&model, READER_APP_INPUT_BACK, 10, 3) !=
          READER_APP_EFFECT_REDRAW_MENU ||
      model.reader_menu_level != READER_MENU_LEVEL_TABS ||
      reader_app_model_reduce_reading(&model, READER_APP_INPUT_BACK, 10, 3) !=
          READER_APP_EFFECT_CLOSE_READER_MENU ||
      model.reader_menu_open)
    return 0;
  return 1;
}

static int expect_bookmark_effects_boundaries_and_window(void) {
  reader_app_model_t model;
  reader_app_model_init(&model);
  model.page = READER_APP_PAGE_READING;
  (void)reader_app_model_reduce_reading(&model, READER_APP_INPUT_CONFIRM, 2,
                                        12);
  (void)reader_app_model_reduce_reading(&model, READER_APP_INPUT_RIGHT, 2,
                                        12);
  (void)reader_app_model_reduce_reading(&model, READER_APP_INPUT_CONFIRM, 2,
                                        12);
  if (model.reader_menu_tab != READER_MENU_BOOKMARKS ||
      model.reader_menu_level != READER_MENU_LEVEL_ITEMS ||
      reader_app_model_reduce_reading(&model, READER_APP_INPUT_LEFT, 2, 12) !=
          READER_APP_EFFECT_NONE ||
      model.bookmark_item_index != 0 ||
      reader_app_model_reduce_reading(&model, READER_APP_INPUT_CONFIRM, 2,
                                      12) !=
          READER_APP_EFFECT_ADD_BOOKMARK)
    return 0;

  for (size_t i = 0; i < 20; ++i)
    (void)reader_app_model_reduce_reading(&model, READER_APP_INPUT_RIGHT, 2,
                                          12);
  if (model.bookmark_item_index != 12 ||
      reader_app_model_menu_window_start(&model) != 7 ||
      reader_app_model_reduce_reading(&model, READER_APP_INPUT_CONFIRM, 2,
                                      12) !=
          READER_APP_EFFECT_REDRAW_MENU ||
      model.reader_menu_level != READER_MENU_LEVEL_BOOKMARK_ACTIONS)
    return 0;
  for (size_t i = 0; i < 5; ++i)
    (void)reader_app_model_reduce_reading(&model, READER_APP_INPUT_RIGHT, 2,
                                          12);
  if (model.bookmark_action_index != 2 ||
      reader_app_model_reduce_reading(&model, READER_APP_INPUT_CONFIRM, 2,
                                      12) !=
          READER_APP_EFFECT_DELETE_BOOKMARK)
    return 0;
  reader_app_model_bookmark_deleted(&model, 11);
  if (model.reader_menu_level != READER_MENU_LEVEL_ITEMS ||
      model.bookmark_item_index != 11 ||
      reader_app_model_menu_window_start(&model) != 6)
    return 0;

  model.reader_menu_level = READER_MENU_LEVEL_BOOKMARK_ACTIONS;
  model.bookmark_action_index = 0;
  if (reader_app_model_reduce_reading(&model, READER_APP_INPUT_CONFIRM, 2,
                                      11) !=
      READER_APP_EFFECT_JUMP_BOOKMARK)
    return 0;
  model.bookmark_action_index = 1;
  if (reader_app_model_reduce_reading(&model, READER_APP_INPUT_CONFIRM, 2,
                                      11) !=
      READER_APP_EFFECT_OVERWRITE_BOOKMARK)
    return 0;
  if (reader_app_model_reduce_reading(&model, READER_APP_INPUT_BACK, 2, 11) !=
          READER_APP_EFFECT_REDRAW_MENU ||
      model.reader_menu_level != READER_MENU_LEVEL_ITEMS)
    return 0;
  return 1;
}

static int expect_bookmark_slot_filters_current_book(void) {
  ink_reader_state_t state = {0};
  state.bookmarks[2].used = true;
  snprintf(state.bookmarks[2].book_path,
           sizeof(state.bookmarks[2].book_path), "%s", "/books/other.xtc");
  state.bookmarks[5].used = true;
  snprintf(state.bookmarks[5].book_path,
           sizeof(state.bookmarks[5].book_path), "%s", "/books/current.xtc");
  state.bookmarks[9].used = true;
  snprintf(state.bookmarks[9].book_path,
           sizeof(state.bookmarks[9].book_path), "%s", "/books/current.xtc");
  return reader_app_model_bookmark_slot(&state, "/books/current.xtc", 0) ==
             5 &&
         reader_app_model_bookmark_slot(&state, "/books/current.xtc", 1) ==
             9 &&
         reader_app_model_bookmark_slot(&state, "/books/current.xtc", 2) ==
             SIZE_MAX;
}

int main(void) {
  if (!expect_defaults_and_filters()) {
    fputs("default/filter/recent ordering failed\n", stderr);
    return 1;
  }
  if (!expect_item_tab_and_popup_inputs()) {
    fputs("input hierarchy failed\n", stderr);
    return 1;
  }
  if (!expect_independent_selection_and_empty_tab()) {
    fputs("independent selection or empty tab failed\n", stderr);
    return 1;
  }
  if (!expect_favorite_removal_closes_and_clamps()) {
    fputs("favorite removal contraction failed\n", stderr);
    return 1;
  }
  if (!expect_recent_rebuild_keeps_opened_catalog_identity()) {
    fputs("recent rebuild changed opened book selection\n", stderr);
    return 1;
  }
  if (!expect_visible_window() || !reader_app_model_self_test()) {
    fputs("window/model self test failed\n", stderr);
    return 1;
  }
  if (!expect_reader_menu_hierarchy_and_chapter_effect()) {
    fputs("reader menu hierarchy/chapter effect failed\n", stderr);
    return 1;
  }
  if (!expect_bookmark_effects_boundaries_and_window()) {
    fputs("bookmark effects/boundaries/window failed\n", stderr);
    return 1;
  }
  if (!expect_bookmark_slot_filters_current_book()) {
    fputs("bookmark slot filtering failed\n", stderr);
    return 1;
  }
  puts("PASS: reader app model host tests");
  return 0;
}
