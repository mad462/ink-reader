#include "reader_app_model.h"

#include <stdint.h>
#include <string.h>

#define READER_APP_NO_INDEX SIZE_MAX

static const ink_reader_bookshelf_entry_t *find_shelf_entry(
    const ink_reader_state_t *state, const char *path) {
  if (!state || !path) return NULL;
  for (size_t i = 0; i < INK_READER_BOOKSHELF_CAPACITY; ++i) {
    const ink_reader_bookshelf_entry_t *entry = &state->bookshelf[i];
    if (entry->used && strcmp(entry->book_path, path) == 0) return entry;
  }
  return NULL;
}

static bool tab_accepts(reader_library_tab_t tab,
                        const ink_reader_bookshelf_entry_t *entry) {
  if (tab == READER_LIBRARY_TAB_ALL) return true;
  if (!entry) return false;
  return tab == READER_LIBRARY_TAB_RECENT ? entry->has_opened
                                          : entry->is_favorite;
}

static uint32_t recent_order(const ink_reader_catalog_t *catalog,
                             const ink_reader_state_t *state,
                             size_t catalog_index) {
  const ink_reader_catalog_item_t *item = &catalog->items[catalog_index];
  const ink_reader_bookshelf_entry_t *entry =
      find_shelf_entry(state, item->path);
  return entry ? entry->recent_order : 0U;
}

static void sort_recent(reader_app_model_t *model,
                        const ink_reader_catalog_t *catalog,
                        const ink_reader_state_t *state) {
  const size_t count = model->visible_count[READER_LIBRARY_TAB_RECENT];
  for (size_t i = 1; i < count; ++i) {
    const size_t value =
        model->visible_catalog[READER_LIBRARY_TAB_RECENT][i];
    const uint32_t value_order = recent_order(catalog, state, value);
    size_t j = i;
    while (j > 0U) {
      const size_t previous =
          model->visible_catalog[READER_LIBRARY_TAB_RECENT][j - 1U];
      if (recent_order(catalog, state, previous) >= value_order) break;
      model->visible_catalog[READER_LIBRARY_TAB_RECENT][j] = previous;
      --j;
    }
    model->visible_catalog[READER_LIBRARY_TAB_RECENT][j] = value;
  }
}

void reader_app_model_init(reader_app_model_t *model) {
  if (!model) return;
  memset(model, 0, sizeof(*model));
  model->page = READER_APP_PAGE_LIBRARY;
  model->tab = READER_LIBRARY_TAB_RECENT;
  model->focus = READER_LIBRARY_FOCUS_ITEMS;
}

size_t reader_app_model_visible_count(const reader_app_model_t *model,
                                      reader_library_tab_t tab) {
  return model && tab < READER_LIBRARY_TAB_COUNT ? model->visible_count[tab]
                                                 : 0U;
}

size_t reader_app_model_visible_catalog_index(const reader_app_model_t *model,
                                              reader_library_tab_t tab,
                                              size_t visible_index) {
  return model && tab < READER_LIBRARY_TAB_COUNT &&
                 visible_index < model->visible_count[tab]
             ? model->visible_catalog[tab][visible_index]
             : READER_APP_NO_INDEX;
}

size_t reader_app_model_selected_catalog_index(
    const reader_app_model_t *model) {
  if (!model || model->tab >= READER_LIBRARY_TAB_COUNT)
    return READER_APP_NO_INDEX;
  return reader_app_model_visible_catalog_index(
      model, model->tab, model->selected[model->tab]);
}

void reader_app_model_rebuild(reader_app_model_t *model,
                              const ink_reader_catalog_t *catalog,
                              const ink_reader_state_t *state) {
  if (!model) return;
  const size_t selected_catalog_index =
      reader_app_model_selected_catalog_index(model);
  memset(model->visible_count, 0, sizeof(model->visible_count));
  const size_t catalog_count =
      catalog && catalog->items
          ? (catalog->count < INK_READER_CATALOG_CAPACITY
                 ? catalog->count
                 : INK_READER_CATALOG_CAPACITY)
          : 0U;
  for (size_t catalog_index = 0; catalog_index < catalog_count;
       ++catalog_index) {
    const ink_reader_bookshelf_entry_t *entry =
        find_shelf_entry(state, catalog->items[catalog_index].path);
    for (size_t tab = 0; tab < READER_LIBRARY_TAB_COUNT; ++tab) {
      if (!tab_accepts((reader_library_tab_t)tab, entry)) continue;
      size_t *count = &model->visible_count[tab];
      model->visible_catalog[tab][*count] = catalog_index;
      ++*count;
    }
  }
  sort_recent(model, catalog, state);

  for (size_t tab = 0; tab < READER_LIBRARY_TAB_COUNT; ++tab) {
    const size_t count = model->visible_count[tab];
    if (count == 0U)
      model->selected[tab] = 0U;
    else if (model->selected[tab] >= count)
      model->selected[tab] = count - 1U;
  }

  if (selected_catalog_index != READER_APP_NO_INDEX) {
    bool still_visible = false;
    const size_t count = model->visible_count[model->tab];
    for (size_t i = 0; i < count; ++i) {
      if (model->visible_catalog[model->tab][i] == selected_catalog_index) {
        model->selected[model->tab] = i;
        still_visible = true;
        break;
      }
    }
    if (!still_visible) {
      model->focus = READER_LIBRARY_FOCUS_ITEMS;
      model->popup_action = 0U;
    }
  }
}

static reader_app_effect_t reduce_items(reader_app_model_t *model,
                                        reader_app_input_t input) {
  const size_t count = model->visible_count[model->tab];
  if (input == READER_APP_INPUT_BACK) {
    model->focus = READER_LIBRARY_FOCUS_TABS;
    return READER_APP_EFFECT_REDRAW;
  }
  if (input == READER_APP_INPUT_CONFIRM) {
    if (count == 0U) return READER_APP_EFFECT_NONE;
    model->focus = READER_LIBRARY_FOCUS_POPUP;
    model->popup_action = 0U;
    return READER_APP_EFFECT_REDRAW;
  }
  if (count == 0U) return READER_APP_EFFECT_NONE;
  if (input == READER_APP_INPUT_LEFT) {
    size_t *selected = &model->selected[model->tab];
    *selected = *selected == 0U ? count - 1U : *selected - 1U;
    return READER_APP_EFFECT_REDRAW;
  }
  if (input == READER_APP_INPUT_RIGHT) {
    size_t *selected = &model->selected[model->tab];
    *selected = (*selected + 1U) % count;
    return READER_APP_EFFECT_REDRAW;
  }
  return READER_APP_EFFECT_NONE;
}

static reader_app_effect_t reduce_tabs(reader_app_model_t *model,
                                       reader_app_input_t input) {
  if (input == READER_APP_INPUT_BACK)
    return READER_APP_EFFECT_RETURN_LAUNCHER;
  if (input == READER_APP_INPUT_CONFIRM) {
    model->focus = READER_LIBRARY_FOCUS_ITEMS;
    return READER_APP_EFFECT_REDRAW;
  }
  if (input == READER_APP_INPUT_LEFT) {
    model->tab = model->tab == READER_LIBRARY_TAB_RECENT
                     ? READER_LIBRARY_TAB_FAVORITES
                     : (reader_library_tab_t)(model->tab - 1);
    return READER_APP_EFFECT_REDRAW;
  }
  if (input == READER_APP_INPUT_RIGHT) {
    model->tab = (reader_library_tab_t)((model->tab + 1) %
                                       READER_LIBRARY_TAB_COUNT);
    return READER_APP_EFFECT_REDRAW;
  }
  return READER_APP_EFFECT_NONE;
}

static reader_app_effect_t reduce_popup(reader_app_model_t *model,
                                        reader_app_input_t input) {
  if (input == READER_APP_INPUT_BACK) {
    model->focus = READER_LIBRARY_FOCUS_ITEMS;
    model->popup_action = 0U;
    return READER_APP_EFFECT_REDRAW;
  }
  if (input == READER_APP_INPUT_LEFT || input == READER_APP_INPUT_RIGHT) {
    model->popup_action = model->popup_action == 0U ? 1U : 0U;
    return READER_APP_EFFECT_REDRAW;
  }
  if (input == READER_APP_INPUT_CONFIRM)
    return model->popup_action == 0U
               ? READER_APP_EFFECT_OPEN_SELECTED
               : READER_APP_EFFECT_TOGGLE_FAVORITE;
  return READER_APP_EFFECT_NONE;
}

reader_app_effect_t reader_app_model_reduce(reader_app_model_t *model,
                                            reader_app_input_t input) {
  if (!model) return READER_APP_EFFECT_NONE;
  if (model->page == READER_APP_PAGE_READING) {
    if (input == READER_APP_INPUT_BACK) {
      model->page = READER_APP_PAGE_LIBRARY;
      return READER_APP_EFFECT_REDRAW;
    }
    return READER_APP_EFFECT_NONE;
  }
  if (model->focus == READER_LIBRARY_FOCUS_ITEMS)
    return reduce_items(model, input);
  if (model->focus == READER_LIBRARY_FOCUS_TABS)
    return reduce_tabs(model, input);
  return reduce_popup(model, input);
}

void reader_app_model_close_reader_menu(reader_app_model_t *model) {
  if (!model) return;
  model->reader_menu_open = false;
  model->reader_menu_tab = READER_MENU_CHAPTERS;
  model->reader_menu_level = READER_MENU_LEVEL_TABS;
  model->chapter_item_index = 0U;
  model->bookmark_item_index = 0U;
  model->bookmark_action_index = 0U;
}

static reader_app_effect_t reduce_reader_menu_tabs(
    reader_app_model_t *model, reader_app_input_t input) {
  if (input == READER_APP_INPUT_LEFT || input == READER_APP_INPUT_RIGHT) {
    model->reader_menu_tab = model->reader_menu_tab == READER_MENU_CHAPTERS
                                 ? READER_MENU_BOOKMARKS
                                 : READER_MENU_CHAPTERS;
    return READER_APP_EFFECT_REDRAW_MENU;
  }
  if (input == READER_APP_INPUT_CONFIRM) {
    model->reader_menu_level = READER_MENU_LEVEL_ITEMS;
    return READER_APP_EFFECT_REDRAW_MENU;
  }
  if (input == READER_APP_INPUT_BACK) {
    reader_app_model_close_reader_menu(model);
    return READER_APP_EFFECT_CLOSE_READER_MENU;
  }
  return READER_APP_EFFECT_NONE;
}

static reader_app_effect_t reduce_reader_menu_items(
    reader_app_model_t *model, reader_app_input_t input, size_t chapter_count,
    size_t bookmark_count) {
  size_t *selected = model->reader_menu_tab == READER_MENU_CHAPTERS
                         ? &model->chapter_item_index
                         : &model->bookmark_item_index;
  const size_t item_count = model->reader_menu_tab == READER_MENU_CHAPTERS
                                ? chapter_count
                                : bookmark_count + 1U;
  if (input == READER_APP_INPUT_BACK) {
    model->reader_menu_level = READER_MENU_LEVEL_TABS;
    return READER_APP_EFFECT_REDRAW_MENU;
  }
  if (input == READER_APP_INPUT_LEFT) {
    if (*selected == 0U) return READER_APP_EFFECT_NONE;
    --*selected;
    return READER_APP_EFFECT_REDRAW_MENU;
  }
  if (input == READER_APP_INPUT_RIGHT) {
    if (*selected + 1U >= item_count) return READER_APP_EFFECT_NONE;
    ++*selected;
    return READER_APP_EFFECT_REDRAW_MENU;
  }
  if (input != READER_APP_INPUT_CONFIRM || item_count == 0U)
    return READER_APP_EFFECT_NONE;
  if (model->reader_menu_tab == READER_MENU_CHAPTERS)
    return READER_APP_EFFECT_JUMP_CHAPTER;
  if (model->bookmark_item_index == 0U)
    return READER_APP_EFFECT_ADD_BOOKMARK;
  model->reader_menu_level = READER_MENU_LEVEL_BOOKMARK_ACTIONS;
  model->bookmark_action_index = 0U;
  return READER_APP_EFFECT_REDRAW_MENU;
}

static reader_app_effect_t reduce_reader_bookmark_actions(
    reader_app_model_t *model, reader_app_input_t input) {
  if (input == READER_APP_INPUT_BACK) {
    model->reader_menu_level = READER_MENU_LEVEL_ITEMS;
    return READER_APP_EFFECT_REDRAW_MENU;
  }
  if (input == READER_APP_INPUT_LEFT) {
    if (model->bookmark_action_index == 0U) return READER_APP_EFFECT_NONE;
    --model->bookmark_action_index;
    return READER_APP_EFFECT_REDRAW_MENU;
  }
  if (input == READER_APP_INPUT_RIGHT) {
    if (model->bookmark_action_index >= 2U) return READER_APP_EFFECT_NONE;
    ++model->bookmark_action_index;
    return READER_APP_EFFECT_REDRAW_MENU;
  }
  if (input != READER_APP_INPUT_CONFIRM) return READER_APP_EFFECT_NONE;
  static const reader_app_effect_t effects[] = {
      READER_APP_EFFECT_JUMP_BOOKMARK,
      READER_APP_EFFECT_OVERWRITE_BOOKMARK,
      READER_APP_EFFECT_DELETE_BOOKMARK,
  };
  return effects[model->bookmark_action_index];
}

reader_app_effect_t reader_app_model_reduce_reading(
    reader_app_model_t *model, reader_app_input_t input, size_t chapter_count,
    size_t bookmark_count) {
  if (!model || model->page != READER_APP_PAGE_READING)
    return READER_APP_EFFECT_NONE;
  if (!model->reader_menu_open) {
    if (input != READER_APP_INPUT_CONFIRM) return READER_APP_EFFECT_NONE;
    reader_app_model_close_reader_menu(model);
    model->reader_menu_open = true;
    return READER_APP_EFFECT_REDRAW_MENU;
  }
  if (model->reader_menu_level == READER_MENU_LEVEL_TABS)
    return reduce_reader_menu_tabs(model, input);
  if (model->reader_menu_level == READER_MENU_LEVEL_ITEMS)
    return reduce_reader_menu_items(model, input, chapter_count,
                                    bookmark_count);
  return reduce_reader_bookmark_actions(model, input);
}

void reader_app_model_bookmark_deleted(reader_app_model_t *model,
                                       size_t bookmark_count) {
  if (!model) return;
  if (model->bookmark_item_index > bookmark_count)
    model->bookmark_item_index = bookmark_count;
  model->reader_menu_level = READER_MENU_LEVEL_ITEMS;
  model->bookmark_action_index = 0U;
}

size_t reader_app_model_menu_window_start(const reader_app_model_t *model) {
  if (!model) return 0U;
  if (model->reader_menu_tab == READER_MENU_CHAPTERS)
    return model->chapter_item_index < 8U ? 0U
                                          : model->chapter_item_index - 7U;
  return model->bookmark_item_index < 6U ? 0U
                                         : model->bookmark_item_index - 5U;
}

size_t reader_app_model_bookmark_slot(const ink_reader_state_t *state,
                                      const char *path,
                                      size_t bookmark_index) {
  if (!state || !path) return READER_APP_NO_INDEX;
  size_t seen = 0U;
  for (size_t slot = 0; slot < INK_READER_BOOKMARK_CAPACITY; ++slot) {
    const ink_reader_bookmark_t *bookmark = &state->bookmarks[slot];
    if (!bookmark->used || strcmp(bookmark->book_path, path) != 0) continue;
    if (seen == bookmark_index) return slot;
    ++seen;
  }
  return READER_APP_NO_INDEX;
}

size_t reader_app_model_window_start(const reader_app_model_t *model) {
  if (!model || model->tab >= READER_LIBRARY_TAB_COUNT) return 0U;
  const size_t selected = model->selected[model->tab];
  return selected < 8U ? 0U : selected - 7U;
}

bool reader_app_model_self_test(void) {
  reader_app_model_t model;
  reader_app_model_init(&model);
  if (model.page != READER_APP_PAGE_LIBRARY ||
      model.tab != READER_LIBRARY_TAB_RECENT ||
      model.focus != READER_LIBRARY_FOCUS_ITEMS)
    return false;
  if (reader_app_model_reduce(&model, READER_APP_INPUT_BACK) !=
          READER_APP_EFFECT_REDRAW ||
      model.focus != READER_LIBRARY_FOCUS_TABS)
    return false;
  return reader_app_model_reduce(&model, READER_APP_INPUT_BACK) ==
         READER_APP_EFFECT_RETURN_LAUNCHER;
}
