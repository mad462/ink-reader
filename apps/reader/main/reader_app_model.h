#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ink_reader_core.h"
#include "ink_reader_state.h"

typedef enum {
  READER_APP_PAGE_LIBRARY = 0,
  READER_APP_PAGE_READING,
} reader_app_page_t;

typedef enum {
  READER_LIBRARY_TAB_RECENT = 0,
  READER_LIBRARY_TAB_ALL,
  READER_LIBRARY_TAB_FAVORITES,
  READER_LIBRARY_TAB_COUNT,
} reader_library_tab_t;

typedef enum {
  READER_LIBRARY_FOCUS_ITEMS = 0,
  READER_LIBRARY_FOCUS_TABS,
  READER_LIBRARY_FOCUS_POPUP,
} reader_library_focus_t;

typedef enum {
  READER_APP_INPUT_LEFT = 0,
  READER_APP_INPUT_RIGHT,
  READER_APP_INPUT_CONFIRM,
  READER_APP_INPUT_BACK,
} reader_app_input_t;

typedef enum {
  READER_APP_EFFECT_NONE = 0,
  READER_APP_EFFECT_REDRAW,
  READER_APP_EFFECT_OPEN_SELECTED,
  READER_APP_EFFECT_TOGGLE_FAVORITE,
  READER_APP_EFFECT_RETURN_LAUNCHER,
  READER_APP_EFFECT_REDRAW_MENU,
  READER_APP_EFFECT_CLOSE_READER_MENU,
  READER_APP_EFFECT_JUMP_CHAPTER,
  READER_APP_EFFECT_ADD_BOOKMARK,
  READER_APP_EFFECT_JUMP_BOOKMARK,
  READER_APP_EFFECT_OVERWRITE_BOOKMARK,
  READER_APP_EFFECT_DELETE_BOOKMARK,
} reader_app_effect_t;

typedef enum {
  READER_MENU_CHAPTERS = 0,
  READER_MENU_BOOKMARKS,
} reader_menu_tab_t;

typedef enum {
  READER_MENU_LEVEL_TABS = 0,
  READER_MENU_LEVEL_ITEMS,
  READER_MENU_LEVEL_BOOKMARK_ACTIONS,
} reader_menu_level_t;

typedef struct {
  reader_app_page_t page;
  reader_library_tab_t tab;
  reader_library_focus_t focus;
  size_t selected[READER_LIBRARY_TAB_COUNT];
  size_t popup_action;
  size_t visible_catalog[READER_LIBRARY_TAB_COUNT]
                        [INK_READER_CATALOG_CAPACITY];
  size_t visible_count[READER_LIBRARY_TAB_COUNT];
  bool reader_menu_open;
  reader_menu_tab_t reader_menu_tab;
  reader_menu_level_t reader_menu_level;
  size_t chapter_item_index;
  size_t bookmark_item_index;
  size_t bookmark_action_index;
} reader_app_model_t;

void reader_app_model_init(reader_app_model_t *model);
void reader_app_model_rebuild(reader_app_model_t *model,
                              const ink_reader_catalog_t *catalog,
                              const ink_reader_state_t *state);
reader_app_effect_t reader_app_model_reduce(reader_app_model_t *model,
                                            reader_app_input_t input);
reader_app_effect_t reader_app_model_reduce_reading(
    reader_app_model_t *model, reader_app_input_t input, size_t chapter_count,
    size_t bookmark_count);
void reader_app_model_close_reader_menu(reader_app_model_t *model);
void reader_app_model_bookmark_deleted(reader_app_model_t *model,
                                       size_t bookmark_count);
size_t reader_app_model_menu_window_start(const reader_app_model_t *model);
size_t reader_app_model_bookmark_slot(const ink_reader_state_t *state,
                                      const char *path,
                                      size_t bookmark_index);
size_t reader_app_model_visible_count(const reader_app_model_t *model,
                                      reader_library_tab_t tab);
size_t reader_app_model_visible_catalog_index(const reader_app_model_t *model,
                                              reader_library_tab_t tab,
                                              size_t visible_index);
size_t reader_app_model_selected_catalog_index(
    const reader_app_model_t *model);
size_t reader_app_model_window_start(const reader_app_model_t *model);
bool reader_app_model_self_test(void);
