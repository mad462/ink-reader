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
