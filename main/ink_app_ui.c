#include "ink_app_ui.h"

#include <string.h>

#include "ink_app_boot.h"

enum {
    INK_TUNING_AUTO_PROBE_REPEAT_LIMIT = 12,
    INK_READER_AUTO_FLIP_PAGE_WINDOW = 4,
};

static bool app_input_event_self_test(void);
static bool app_lab_command_self_test(void);
static bool app_library_command_self_test(void);
static bool app_grid_compare_self_test(void);
static bool app_auto_repeat_probe_self_test(void);
static bool app_auto_flip_stress_self_test(void);
static bool app_reader_confirm_ignored_self_test(void);
static bool app_reader_menu_navigation_self_test(void);
static bool app_library_tabs_self_test(void);
static bool toggle_reader_auto_flip_stress(ink_ui_model_t *model, bool *dirty_out);
static bool advance_reader_auto_flip_stress_impl(ink_ui_model_t *model);
static bool app_open_selected_browser_book(ink_ui_model_t *model);
static bool app_return_to_library(ink_ui_model_t *model);
static bool app_handle_library_command(ink_ui_model_t *model, ink_runtime_shell_command_t command);
static size_t app_library_visible_count(const ink_ui_model_t *model, ink_library_tab_t tab);
static bool app_library_book_index_at(
    const ink_ui_model_t *model,
    ink_library_tab_t tab,
    size_t visible_index,
    size_t *browser_index_out);
static bool app_library_resolve_selected_path(
    const ink_ui_model_t *model,
    char *path_out,
    size_t path_out_size,
    char *title_out,
    size_t title_out_size);
static bool app_library_toggle_selected_favorite(ink_ui_model_t *model);
static bool app_library_open_selected_book(ink_ui_model_t *model);
static size_t app_reader_menu_chapter_count(const ink_ui_model_t *model);
static size_t app_reader_menu_bookmark_count(const ink_ui_model_t *model);
static bool app_reader_menu_resolve_selected_bookmark(
    const ink_ui_model_t *model,
    const ink_app_state_bookmark_t **bookmark_out,
    size_t *slot_index_out,
    bool *is_current_page_card_out);
static bool app_reader_menu_store_bookmark(
    ink_ui_model_t *model,
    size_t page_index,
    const char *chapter_title,
    size_t chapter_index,
    size_t total_pages_snapshot);
static bool app_reader_menu_overwrite_bookmark_slot(
    ink_ui_model_t *model,
    size_t bookmark_slot,
    size_t page_index,
    const char *chapter_title,
    size_t chapter_index,
    size_t total_pages_snapshot);
static bool app_reader_menu_apply_bookmark_action(ink_ui_model_t *model);
static bool app_reader_menu_activate_current_item(ink_ui_model_t *model);
static bool app_reader_menu_move_selection(
    ink_ui_model_t *model,
    bool forward,
    size_t chapter_count,
    size_t bookmark_count);

void ink_app_button_state_from_snapshot(
    const ink_button_snapshot_t *snapshot,
    ink_runtime_shell_button_state_t *buttons)
{
    static const ink_raw_button_t kRawMap[INK_RUNTIME_SHELL_BUTTON_COUNT] = {
        [INK_RUNTIME_SHELL_BUTTON_BACK] = INK_RAW_BUTTON_BACK,
        [INK_RUNTIME_SHELL_BUTTON_CONFIRM] = INK_RAW_BUTTON_CONFIRM,
        [INK_RUNTIME_SHELL_BUTTON_LEFT] = INK_RAW_BUTTON_LEFT,
        [INK_RUNTIME_SHELL_BUTTON_RIGHT] = INK_RAW_BUTTON_RIGHT,
        [INK_RUNTIME_SHELL_BUTTON_POWER] = INK_RAW_BUTTON_POWER,
    };

    if (snapshot == NULL || buttons == NULL) {
        return;
    }

    memset(buttons, 0, sizeof(*buttons));
    for (int i = 0; i < INK_RUNTIME_SHELL_BUTTON_COUNT; ++i) {
        const uint32_t mask = ink_button_input_mask_for_raw(kRawMap[i]);
        buttons->is_down[i] = (snapshot->stable_mask & mask) != 0U;
        buttons->was_pressed[i] = (snapshot->pressed_mask & mask) != 0U;
        buttons->was_released[i] = (snapshot->released_mask & mask) != 0U;
        buttons->held_ms[i] = snapshot->held_duration_ms[kRawMap[i]];
    }
}

ink_runtime_shell_command_t ink_app_command_from_snapshot(
    const ink_ui_model_t *model,
    const ink_button_snapshot_t *snapshot)
{
    (void)model;
    if (snapshot == NULL) {
        return INK_RUNTIME_SHELL_COMMAND_NONE;
    }
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_BACK)) {
        return INK_RUNTIME_SHELL_COMMAND_BACK;
    }
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_CONFIRM)) {
        return INK_RUNTIME_SHELL_COMMAND_CONFIRM;
    }
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_NAV_PREVIOUS)) {
        return INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS;
    }
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_NAV_NEXT)) {
        return INK_RUNTIME_SHELL_COMMAND_NAV_NEXT;
    }
    return INK_RUNTIME_SHELL_COMMAND_NONE;
}

bool ink_app_should_dispatch_button_event(
    const ink_button_snapshot_t *snapshot,
    uint32_t now_ms,
    uint32_t *last_hold_event_ms)
{
    uint32_t max_held_ms = 0U;

    if (snapshot == NULL || last_hold_event_ms == NULL) {
        return false;
    }
    if (snapshot->pressed_mask != 0U || snapshot->released_mask != 0U) {
        return true;
    }
    if (snapshot->stable_mask == 0U) {
        return false;
    }

    for (int i = 0; i < INK_RAW_BUTTON_COUNT; ++i) {
        if (snapshot->held_duration_ms[i] > max_held_ms) {
            max_held_ms = snapshot->held_duration_ms[i];
        }
    }
    if (max_held_ms < INK_INPUT_HOLD_EVENT_MS) {
        return false;
    }
    if (*last_hold_event_ms != 0U && (uint32_t)(now_ms - *last_hold_event_ms) < INK_INPUT_HOLD_EVENT_MS) {
        return false;
    }
    *last_hold_event_ms = now_ms;
    return true;
}

static bool app_open_selected_browser_book(ink_ui_model_t *model)
{
    if (model == NULL
        || !model->browser.selected_file_ready
        || model->browser.selected_file_type != INK_FILE_BROWSER_ENTRY_XTC
        || model->browser.selected_file_path[0] == '\0') {
        return false;
    }

    if (!ink_app_open_book_from_path(model, model->browser.selected_file_path)) {
        return false;
    }

    model->reader_nav_pending = false;
    model->reader_nav_pending_start_ms = 0U;
    model->reader_confirm_pending = false;
    model->reader_confirm_pending_start_ms = 0U;
    ink_app_clear_fast_browse(model);
    memset(&model->reader_menu, 0, sizeof(model->reader_menu));
    model->lab.auto_flip_stress_enabled = false;
    model->lab.force_full_refresh = false;
    model->lab.reader_white_refresh_pending = false;
    ink_reader_session_prefetch_next(&model->reader_session, NULL, NULL);
    return true;
}

static size_t app_library_visible_count(const ink_ui_model_t *model, ink_library_tab_t tab)
{
    size_t count = 0U;

    if (model == NULL) {
        return 0U;
    }

    for (size_t i = 0; i < model->browser.entry_count; ++i) {
        const ink_file_browser_entry_t *entry = &model->browser.entries[i];
        size_t shelf_index = 0U;
        const ink_app_state_bookshelf_entry_t *shelf = NULL;
        const bool has_shelf = ink_app_state_find_xtc_bookshelf_entry(
            &model->app_state,
            entry->full_path,
            &shelf_index);

        if (entry->type != INK_FILE_BROWSER_ENTRY_XTC) {
            continue;
        }
        if (has_shelf) {
            shelf = ink_app_state_bookshelf_entry_at(&model->app_state, shelf_index);
        }
        switch (tab) {
            case INK_LIBRARY_TAB_RECENT:
                if (shelf != NULL && shelf->has_opened) {
                    ++count;
                }
                break;
            case INK_LIBRARY_TAB_FAVORITES:
                if (shelf != NULL && shelf->is_favorite) {
                    ++count;
                }
                break;
            case INK_LIBRARY_TAB_ALL:
            default:
                ++count;
                break;
        }
    }

    return count;
}

static bool app_library_book_index_at(
    const ink_ui_model_t *model,
    ink_library_tab_t tab,
    size_t visible_index,
    size_t *browser_index_out)
{
    size_t ranked[INK_FILE_BROWSER_MAX_ENTRIES];
    size_t ranked_count = 0U;

    if (browser_index_out != NULL) {
        *browser_index_out = 0U;
    }
    if (model == NULL) {
        return false;
    }

    for (size_t i = 0; i < model->browser.entry_count && ranked_count < INK_FILE_BROWSER_MAX_ENTRIES; ++i) {
        const ink_file_browser_entry_t *entry = &model->browser.entries[i];
        size_t shelf_index = 0U;
        const ink_app_state_bookshelf_entry_t *shelf = NULL;
        const bool has_shelf = ink_app_state_find_xtc_bookshelf_entry(
            &model->app_state,
            entry->full_path,
            &shelf_index);

        if (entry->type != INK_FILE_BROWSER_ENTRY_XTC) {
            continue;
        }
        if (has_shelf) {
            shelf = ink_app_state_bookshelf_entry_at(&model->app_state, shelf_index);
        }
        if (tab == INK_LIBRARY_TAB_RECENT) {
            if (shelf == NULL || !shelf->has_opened) {
                continue;
            }
        } else if (tab == INK_LIBRARY_TAB_FAVORITES) {
            if (shelf == NULL || !shelf->is_favorite) {
                continue;
            }
        }
        ranked[ranked_count++] = i;
    }

    if (tab == INK_LIBRARY_TAB_RECENT) {
        for (size_t i = 0; i < ranked_count; ++i) {
            for (size_t j = i + 1U; j < ranked_count; ++j) {
                size_t a_index = 0U;
                size_t b_index = 0U;
                const ink_app_state_bookshelf_entry_t *a = NULL;
                const ink_app_state_bookshelf_entry_t *b = NULL;
                (void)ink_app_state_find_xtc_bookshelf_entry(
                    &model->app_state,
                    model->browser.entries[ranked[i]].full_path,
                    &a_index);
                (void)ink_app_state_find_xtc_bookshelf_entry(
                    &model->app_state,
                    model->browser.entries[ranked[j]].full_path,
                    &b_index);
                a = ink_app_state_bookshelf_entry_at(&model->app_state, a_index);
                b = ink_app_state_bookshelf_entry_at(&model->app_state, b_index);
                if (a != NULL && b != NULL && b->recent_order > a->recent_order) {
                    const size_t tmp = ranked[i];
                    ranked[i] = ranked[j];
                    ranked[j] = tmp;
                }
            }
        }
    }

    if (visible_index >= ranked_count) {
        return false;
    }
    if (browser_index_out != NULL) {
        *browser_index_out = ranked[visible_index];
    }
    return true;
}

static bool app_library_resolve_selected_path(
    const ink_ui_model_t *model,
    char *path_out,
    size_t path_out_size,
    char *title_out,
    size_t title_out_size)
{
    size_t browser_index = 0U;
    const ink_file_browser_entry_t *entry;

    if (path_out != NULL && path_out_size > 0U) {
        path_out[0] = '\0';
    }
    if (title_out != NULL && title_out_size > 0U) {
        title_out[0] = '\0';
    }
    if (model == NULL
        || !app_library_book_index_at(
            model,
            model->library.active_tab,
            model->library.selected_index[model->library.active_tab],
            &browser_index)) {
        return false;
    }

    entry = &model->browser.entries[browser_index];
    if (path_out != NULL && path_out_size > 0U) {
        snprintf(path_out, path_out_size, "%s", entry->full_path);
    }
    if (title_out != NULL && title_out_size > 0U) {
        snprintf(title_out, title_out_size, "%s", entry->name);
    }
    return true;
}

static bool app_library_toggle_selected_favorite(ink_ui_model_t *model)
{
    char path[INK_FILE_BROWSER_PATH_LENGTH + 1];
    char title[INK_FILE_BROWSER_NAME_LENGTH + 1];
    size_t shelf_index = 0U;
    const ink_app_state_bookshelf_entry_t *shelf = NULL;
    bool next_favorite = true;

    if (model == NULL || !app_library_resolve_selected_path(model, path, sizeof(path), title, sizeof(title))) {
        return false;
    }

    if (ink_app_state_find_xtc_bookshelf_entry(&model->app_state, path, &shelf_index)) {
        shelf = ink_app_state_bookshelf_entry_at(&model->app_state, shelf_index);
        if (shelf != NULL) {
            next_favorite = !shelf->is_favorite;
        }
    }
    if (!ink_app_state_set_xtc_favorite(&model->app_state, path, title, next_favorite)) {
        return false;
    }
    if (ink_app_persist_state(&model->app_state) != ESP_OK) {
        return false;
    }
    if (model->library.active_tab == INK_LIBRARY_TAB_FAVORITES && !next_favorite) {
        const size_t count = app_library_visible_count(model, INK_LIBRARY_TAB_FAVORITES);
        if (count == 0U) {
            model->library.selected_index[INK_LIBRARY_TAB_FAVORITES] = 0U;
            model->library.popup_open = false;
            model->library.focus = INK_LIBRARY_FOCUS_ITEMS;
        } else if (model->library.selected_index[INK_LIBRARY_TAB_FAVORITES] >= count) {
            model->library.selected_index[INK_LIBRARY_TAB_FAVORITES] = count - 1U;
        }
    }
    return true;
}

static bool app_library_open_selected_book(ink_ui_model_t *model)
{
    size_t browser_index = 0U;

    if (model == NULL
        || !app_library_book_index_at(
            model,
            model->library.active_tab,
            model->library.selected_index[model->library.active_tab],
            &browser_index)) {
        return false;
    }

    model->browser.selected_index = browser_index;
    model->browser.selected_file_ready = true;
    model->browser.selected_file_type = model->browser.entries[browser_index].type;
    snprintf(
        model->browser.selected_file_path,
        sizeof(model->browser.selected_file_path),
        "%s",
        model->browser.entries[browser_index].full_path);
    model->library.popup_open = false;
    model->library.focus = INK_LIBRARY_FOCUS_ITEMS;
    model->library.popup_action_index = 0U;
    return app_open_selected_browser_book(model);
}

static bool app_return_to_library(ink_ui_model_t *model)
{
    if (model == NULL) {
        return false;
    }

    if (ink_reader_session_is_xtc_active(&model->reader_session)) {
        (void)ink_app_persist_state(&model->app_state);
    }
    model->reader_fast_full_commit_pending = false;
    ink_reader_session_close(&model->reader_session);
    ink_app_clear_fast_browse(model);
    memset(&model->reader_menu, 0, sizeof(model->reader_menu));
    model->reader_nav_pending = false;
    model->reader_nav_pending_start_ms = 0U;
    model->reader_confirm_pending = false;
    model->reader_confirm_pending_start_ms = 0U;
    model->lab.auto_flip_stress_enabled = false;
    model->lab.force_full_refresh = false;
    model->lab.reader_white_refresh_pending = false;
    model->library.popup_open = false;
    model->library.popup_action_index = 0U;
    model->library.focus = INK_LIBRARY_FOCUS_ITEMS;
    model->shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    model->shell.full_refresh_requested = true;
    return true;
}

static bool app_open_reader_menu(ink_ui_model_t *model)
{
    if (model == NULL || !ink_reader_session_is_xtc_active(&model->reader_session)) {
        return false;
    }

    ink_app_clear_fast_browse(model);
    model->reader_nav_pending = false;
    model->reader_nav_pending_start_ms = 0U;
    model->reader_confirm_pending = false;
    model->reader_confirm_pending_start_ms = 0U;
    model->reader_fast_full_commit_pending = false;
    model->reader_menu.open = true;
    model->reader_menu.active_tab = INK_READER_MENU_TAB_CHAPTERS;
    model->reader_menu.level = INK_READER_MENU_LEVEL_TABS;
    model->reader_menu.chapter_item_index = 0U;
    model->reader_menu.bookmark_item_index = 0U;
    model->reader_menu.bookmark_action_index = 0U;
    return true;
}

static bool app_close_reader_menu(ink_ui_model_t *model)
{
    if (model == NULL || !model->reader_menu.open) {
        return false;
    }

    memset(&model->reader_menu, 0, sizeof(model->reader_menu));
    return true;
}

static bool app_handle_reader_menu_command(ink_ui_model_t *model, ink_runtime_shell_command_t command)
{
    const size_t chapter_count = app_reader_menu_chapter_count(model);
    const size_t bookmark_count = app_reader_menu_bookmark_count(model);

    if (model == NULL || !model->reader_menu.open) {
        return false;
    }

    if (model->reader_menu.level == INK_READER_MENU_LEVEL_TABS) {
        switch (command) {
            case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
            case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
                model->reader_menu.active_tab =
                    model->reader_menu.active_tab == INK_READER_MENU_TAB_CHAPTERS
                        ? INK_READER_MENU_TAB_BOOKMARKS
                        : INK_READER_MENU_TAB_CHAPTERS;
                return true;
            case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
                model->reader_menu.level = INK_READER_MENU_LEVEL_ITEMS;
                return true;
            case INK_RUNTIME_SHELL_COMMAND_BACK:
                return app_close_reader_menu(model);
            default:
                return false;
        }
    }

    if (model->reader_menu.level == INK_READER_MENU_LEVEL_BOOKMARK_ACTIONS) {
        switch (command) {
            case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
                if (model->reader_menu.bookmark_action_index > 0U) {
                    model->reader_menu.bookmark_action_index--;
                    return true;
                }
                return false;
            case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
                if (model->reader_menu.bookmark_action_index < 2U) {
                    model->reader_menu.bookmark_action_index++;
                    return true;
                }
                return false;
            case INK_RUNTIME_SHELL_COMMAND_BACK:
                model->reader_menu.level = INK_READER_MENU_LEVEL_ITEMS;
                return true;
            case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
                return app_reader_menu_apply_bookmark_action(model);
            default:
                return false;
        }
    }

    switch (command) {
        case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
            return app_reader_menu_move_selection(model, false, chapter_count, bookmark_count);
        case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
            return app_reader_menu_move_selection(model, true, chapter_count, bookmark_count);
        case INK_RUNTIME_SHELL_COMMAND_BACK:
            model->reader_menu.level = INK_READER_MENU_LEVEL_TABS;
            return true;
        case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
            return app_reader_menu_activate_current_item(model);
        default:
            return false;
    }
}

static size_t app_reader_menu_chapter_count(const ink_ui_model_t *model)
{
    return model != NULL && ink_reader_session_is_xtc_active(&model->reader_session)
        ? model->reader_session.xtc_book.chapter_entry_count
        : 0U;
}

static size_t app_reader_menu_bookmark_count(const ink_ui_model_t *model)
{
    return model != NULL && model->reader_session.source_path[0] != '\0'
        ? ink_app_state_count_bookmarks_for_path(&model->app_state, model->reader_session.source_path) + 1U
        : 1U;
}

static bool app_reader_menu_move_selection(
    ink_ui_model_t *model,
    bool forward,
    size_t chapter_count,
    size_t bookmark_count)
{
    if (model == NULL) {
        return false;
    }

    if (model->reader_menu.active_tab == INK_READER_MENU_TAB_CHAPTERS) {
        if (chapter_count == 0U) {
            return false;
        }
        if (forward) {
            if (model->reader_menu.chapter_item_index + 1U < chapter_count) {
                model->reader_menu.chapter_item_index++;
                return true;
            }
            return false;
        }
        if (model->reader_menu.chapter_item_index > 0U) {
            model->reader_menu.chapter_item_index--;
            return true;
        }
        return false;
    }

    if (bookmark_count == 0U) {
        return false;
    }
    if (forward) {
        if (model->reader_menu.bookmark_item_index + 1U < bookmark_count) {
            model->reader_menu.bookmark_item_index++;
            return true;
        }
        return false;
    }
    if (model->reader_menu.bookmark_item_index > 0U) {
        model->reader_menu.bookmark_item_index--;
        return true;
    }
    return false;
}

static bool app_reader_menu_activate_current_item(ink_ui_model_t *model)
{
    if (model == NULL || !model->reader_menu.open || !ink_reader_session_is_xtc_active(&model->reader_session)) {
        return false;
    }

    if (model->reader_menu.active_tab == INK_READER_MENU_TAB_CHAPTERS) {
        const size_t chapter_count = app_reader_menu_chapter_count(model);
        if (chapter_count == 0U || model->reader_menu.chapter_item_index >= chapter_count) {
            return false;
        }

        if (!ink_reader_session_jump_to_page(
                &model->reader_session,
                model->reader_session.xtc_book.chapter_entries[model->reader_menu.chapter_item_index].start_page,
                &model->app_state)) {
            return false;
        }
        ink_reader_session_prefetch_next(&model->reader_session, NULL, NULL);
        return app_close_reader_menu(model);
    }

    if (model->reader_menu.bookmark_item_index == 0U) {
        const char *chapter_title = model->reader_session.current_chapter_name[0] != '\0'
            ? model->reader_session.current_chapter_name
            : "";
        return app_reader_menu_store_bookmark(
            model,
            model->reader_session.current_page,
            chapter_title,
            model->reader_session.current_chapter_index,
            model->reader_session.total_pages);
    }

    model->reader_menu.level = INK_READER_MENU_LEVEL_BOOKMARK_ACTIONS;
    model->reader_menu.bookmark_action_index = 0U;
    return true;
}

static bool app_reader_menu_resolve_selected_bookmark(
    const ink_ui_model_t *model,
    const ink_app_state_bookmark_t **bookmark_out,
    size_t *slot_index_out,
    bool *is_current_page_card_out)
{
    size_t seen = 0U;
    const size_t target = model != NULL ? model->reader_menu.bookmark_item_index : 0U;

    if (bookmark_out != NULL) {
        *bookmark_out = NULL;
    }
    if (slot_index_out != NULL) {
        *slot_index_out = 0U;
    }
    if (is_current_page_card_out != NULL) {
        *is_current_page_card_out = false;
    }
    if (model == NULL || model->reader_session.source_path[0] == '\0') {
        return false;
    }

    if (target == 0U) {
        if (is_current_page_card_out != NULL) {
            *is_current_page_card_out = true;
        }
        return true;
    }

    for (size_t i = 0; i < INK_APP_STATE_BOOKMARK_CAPACITY; ++i) {
        const ink_app_state_bookmark_t *candidate = ink_app_state_bookmark_at(&model->app_state, i);
        if (candidate == NULL || strcmp(candidate->book_path, model->reader_session.source_path) != 0) {
            continue;
        }
        ++seen;
        if (seen == target) {
            if (bookmark_out != NULL) {
                *bookmark_out = candidate;
            }
            if (slot_index_out != NULL) {
                *slot_index_out = i;
            }
            return true;
        }
    }

    return false;
}

static bool app_reader_menu_store_bookmark(
    ink_ui_model_t *model,
    size_t page_index,
    const char *chapter_title,
    size_t chapter_index,
    size_t total_pages_snapshot)
{
    char timestamp[INK_APP_STATE_BOOKMARK_TIME_LENGTH + 1];

    if (model == NULL || model->reader_session.source_path[0] == '\0') {
        return false;
    }

    snprintf(
        timestamp,
        sizeof(timestamp),
        "T+%u:%02u",
        (unsigned)(page_index / 60U),
        (unsigned)(page_index % 60U));
    if (!ink_app_state_add_or_replace_xtc_bookmark(
            &model->app_state,
            model->reader_session.source_path,
            page_index,
            chapter_index,
            total_pages_snapshot,
            chapter_title,
            timestamp)) {
        return false;
    }

    return ink_app_persist_state(&model->app_state) == ESP_OK;
}

static bool app_reader_menu_overwrite_bookmark_slot(
    ink_ui_model_t *model,
    size_t bookmark_slot,
    size_t page_index,
    const char *chapter_title,
    size_t chapter_index,
    size_t total_pages_snapshot)
{
    char timestamp[INK_APP_STATE_BOOKMARK_TIME_LENGTH + 1];

    if (model == NULL || model->reader_session.source_path[0] == '\0') {
        return false;
    }

    snprintf(
        timestamp,
        sizeof(timestamp),
        "T+%u:%02u",
        (unsigned)(page_index / 60U),
        (unsigned)(page_index % 60U));
    if (!ink_app_state_overwrite_xtc_bookmark_at(
            &model->app_state,
            bookmark_slot,
            model->reader_session.source_path,
            page_index,
            chapter_index,
            total_pages_snapshot,
            chapter_title,
            timestamp)) {
        return false;
    }

    return ink_app_persist_state(&model->app_state) == ESP_OK;
}

static bool app_reader_menu_apply_bookmark_action(ink_ui_model_t *model)
{
    const ink_app_state_bookmark_t *bookmark = NULL;
    size_t bookmark_slot = 0U;
    bool is_current_page_card = false;

    if (model == NULL || model->reader_menu.level != INK_READER_MENU_LEVEL_BOOKMARK_ACTIONS) {
        return false;
    }
    if (!app_reader_menu_resolve_selected_bookmark(
            model,
            &bookmark,
            &bookmark_slot,
            &is_current_page_card)) {
        return false;
    }

    switch (model->reader_menu.bookmark_action_index) {
        case 0U:
            if (is_current_page_card) {
                model->reader_menu.level = INK_READER_MENU_LEVEL_ITEMS;
                return true;
            }
            if (!ink_reader_session_jump_to_page(
                    &model->reader_session,
                    bookmark->page_index,
                    &model->app_state)) {
                return false;
            }
            ink_reader_session_prefetch_next(&model->reader_session, NULL, NULL);
            return app_close_reader_menu(model);
        case 1U:
            if (is_current_page_card) {
                const char *chapter_title = model->reader_session.current_chapter_name[0] != '\0'
                    ? model->reader_session.current_chapter_name
                    : "";
                if (!app_reader_menu_store_bookmark(
                        model,
                        model->reader_session.current_page,
                        chapter_title,
                        model->reader_session.current_chapter_index,
                        model->reader_session.total_pages)) {
                    return false;
                }
            } else {
                const char *chapter_title = model->reader_session.current_chapter_name[0] != '\0'
                    ? model->reader_session.current_chapter_name
                    : "";
                if (!app_reader_menu_overwrite_bookmark_slot(
                        model,
                        bookmark_slot,
                        model->reader_session.current_page,
                        chapter_title,
                        model->reader_session.current_chapter_index,
                        model->reader_session.total_pages)) {
                    return false;
                }
            }
            model->reader_menu.level = INK_READER_MENU_LEVEL_ITEMS;
            return true;
        case 2U:
            if (is_current_page_card) {
                if (!ink_app_state_remove_xtc_bookmark(
                        &model->app_state,
                        model->reader_session.source_path,
                        model->reader_session.current_page)) {
                    model->reader_menu.level = INK_READER_MENU_LEVEL_ITEMS;
                    return true;
                }
            } else {
                memset(&model->app_state.bookmarks[bookmark_slot], 0, sizeof(model->app_state.bookmarks[bookmark_slot]));
            }
            if (ink_app_persist_state(&model->app_state) != ESP_OK) {
                return false;
            }
            if (model->reader_menu.bookmark_item_index >= app_reader_menu_bookmark_count(model)) {
                model->reader_menu.bookmark_item_index = app_reader_menu_bookmark_count(model) > 0U
                    ? app_reader_menu_bookmark_count(model) - 1U
                    : 0U;
            }
            model->reader_menu.level = INK_READER_MENU_LEVEL_ITEMS;
            return true;
        default:
            return false;
    }
}

static bool app_handle_library_command(ink_ui_model_t *model, ink_runtime_shell_command_t command)
{
    const ink_library_tab_t tab = model != NULL ? model->library.active_tab : INK_LIBRARY_TAB_RECENT;
    const size_t visible_count = app_library_visible_count(model, tab);

    if (model == NULL) {
        return false;
    }

    if (model->library.focus == INK_LIBRARY_FOCUS_POPUP) {
        switch (command) {
            case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
            case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
                model->library.popup_action_index = model->library.popup_action_index == 0U ? 1U : 0U;
                return true;
            case INK_RUNTIME_SHELL_COMMAND_BACK:
                model->library.popup_open = false;
                model->library.focus = INK_LIBRARY_FOCUS_ITEMS;
                return true;
            case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
                if (model->library.popup_action_index == 0U) {
                    return app_library_open_selected_book(model);
                }
                return app_library_toggle_selected_favorite(model);
            default:
                return false;
        }
    }

    if (model->library.focus == INK_LIBRARY_FOCUS_TABS) {
        switch (command) {
            case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
                model->library.active_tab = model->library.active_tab == 0
                    ? (ink_library_tab_t)(INK_LIBRARY_TAB_COUNT - 1U)
                    : (ink_library_tab_t)(model->library.active_tab - 1U);
                return true;
            case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
                model->library.active_tab =
                    (ink_library_tab_t)((model->library.active_tab + 1U) % INK_LIBRARY_TAB_COUNT);
                return true;
            case INK_RUNTIME_SHELL_COMMAND_BACK:
                model->library.focus = INK_LIBRARY_FOCUS_ITEMS;
                return true;
            case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
                model->library.focus = INK_LIBRARY_FOCUS_ITEMS;
                return true;
            default:
                return false;
        }
    }

    switch (command) {
        case INK_RUNTIME_SHELL_COMMAND_BACK:
            if (model->library.popup_open) {
                model->library.popup_open = false;
                return true;
            }
            model->library.focus = INK_LIBRARY_FOCUS_TABS;
            return true;
        case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
            if (visible_count == 0U) {
                return false;
            }
            model->library.popup_open = true;
            model->library.popup_action_index = 0U;
            model->library.focus = INK_LIBRARY_FOCUS_POPUP;
            return true;
        case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
            if (visible_count == 0U) {
                return false;
            }
            if (model->library.selected_index[tab] == 0U) {
                model->library.selected_index[tab] = visible_count - 1U;
            } else {
                model->library.selected_index[tab]--;
            }
            return true;
        case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
            if (visible_count == 0U) {
                return false;
            }
            model->library.selected_index[tab] =
                (model->library.selected_index[tab] + 1U) % visible_count;
            return true;
        default:
            return false;
    }
}

bool ink_app_handle_ui_command(ink_ui_model_t *model, ink_runtime_shell_command_t command)
{
    bool dirty = false;

    if (model == NULL || command == INK_RUNTIME_SHELL_COMMAND_NONE) {
        return false;
    }

    if (model->shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        return app_handle_library_command(model, command);
    }

    if (model->shell.page == INK_RUNTIME_SHELL_PAGE_READER
        && ink_reader_session_is_xtc_active(&model->reader_session)) {
        if (model->reader_menu.open) {
            return app_handle_reader_menu_command(model, command);
        }
        switch (command) {
            case INK_RUNTIME_SHELL_COMMAND_BACK:
                model->lab.auto_flip_stress_enabled = false;
                return app_return_to_library(model);
            case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
                return app_open_reader_menu(model);
            case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
                model->lab.auto_flip_stress_enabled = false;
                dirty = ink_reader_session_previous_page(&model->reader_session, &model->app_state);
                break;
            case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
                model->lab.auto_flip_stress_enabled = false;
                dirty = ink_reader_session_next_page(&model->reader_session, &model->app_state);
                break;
            default:
                return false;
        }
        if (dirty) {
            ink_reader_session_prefetch_next(&model->reader_session, NULL, NULL);
        }
        return dirty;
    }

    switch (command) {
        case INK_RUNTIME_SHELL_COMMAND_BACK:
            if (model->lab.current_page == INK_TUNING_PAGE_GRID_COMPARE) {
                return ink_tuning_lab_restart_grid_compare(&model->lab);
            }
            return ink_tuning_lab_request_force_full_refresh(&model->lab);
        case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
            return ink_tuning_lab_cycle_refresh_profile(&model->lab);
        case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
            return ink_tuning_lab_previous_page(&model->lab);
        case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
            return ink_tuning_lab_next_page(&model->lab);
        default:
            return false;
    }
}

bool ink_app_should_auto_repeat_tuning_probe(const ink_ui_model_t *model, esp_err_t result)
{
    if (model == NULL || result != ESP_OK) {
        return false;
    }
    if (model->lab.force_full_refresh
        || model->lab.render_counter == 0U
        || model->lab.render_counter >= INK_TUNING_AUTO_PROBE_REPEAT_LIMIT) {
        return false;
    }
    if (model->lab.current_page != INK_TUNING_PAGE_FOOTER) {
        return false;
    }
    return model->lab.refresh_profile == INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER
        || model->lab.refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_A;
}

static bool toggle_reader_auto_flip_stress(ink_ui_model_t *model, bool *dirty_out)
{
    size_t page_count = 0U;
    size_t start_page = 0U;
    bool dirty = false;

    if (dirty_out != NULL) {
        *dirty_out = false;
    }
    if (model == NULL || !ink_reader_session_is_xtc_active(&model->reader_session)) {
        return false;
    }

    if (model->reader_session.total_pages < 2U) {
        return false;
    }

    if (model->lab.auto_flip_stress_enabled) {
        model->lab.auto_flip_stress_enabled = false;
        dirty = true;
    } else {
        page_count = model->reader_session.total_pages < INK_READER_AUTO_FLIP_PAGE_WINDOW
            ? model->reader_session.total_pages
            : INK_READER_AUTO_FLIP_PAGE_WINDOW;
        start_page = model->reader_session.current_page;
        if (start_page + page_count > model->reader_session.total_pages) {
            start_page = model->reader_session.total_pages - page_count;
        }
        model->lab.auto_flip_start_page = start_page;
        model->lab.auto_flip_page_count = page_count;
        model->lab.auto_flip_stress_enabled = true;
        dirty = advance_reader_auto_flip_stress_impl(model);
        if (!dirty) {
            model->lab.auto_flip_stress_enabled = false;
            return false;
        }
    }

    if (dirty_out != NULL) {
        *dirty_out = dirty;
    }
    return true;
}

static bool advance_reader_auto_flip_stress_impl(ink_ui_model_t *model)
{
    size_t end_page = 0U;
    size_t target_page = 0U;

    if (model == NULL
        || !model->lab.auto_flip_stress_enabled
        || !ink_reader_session_is_xtc_active(&model->reader_session)
        || model->lab.auto_flip_page_count < 2U) {
        return false;
    }

    end_page = model->lab.auto_flip_start_page + model->lab.auto_flip_page_count - 1U;
    if (model->reader_session.current_page < end_page) {
        return ink_reader_session_next_page(&model->reader_session, &model->app_state);
    }

    target_page = model->lab.auto_flip_start_page;
    if (target_page == model->reader_session.current_page) {
        return false;
    }
    return ink_reader_session_jump_to_page(&model->reader_session, target_page, &model->app_state);
}

bool ink_app_advance_reader_auto_flip_stress(ink_ui_model_t *model)
{
    bool dirty = advance_reader_auto_flip_stress_impl(model);

    if (dirty && model != NULL) {
        ink_reader_session_prefetch_next(&model->reader_session, NULL, NULL);
    }
    return dirty;
}

bool ink_app_should_auto_advance_grid_compare(const ink_ui_model_t *model, esp_err_t result)
{
    return model != NULL
        && result == ESP_OK
        && model->lab.current_page == INK_TUNING_PAGE_GRID_COMPARE
        && model->lab.grid_compare_active
        && model->lab.grid_compare_step < ink_tuning_lab_grid_compare_cell_count();
}

bool ink_app_advance_grid_compare(ink_ui_model_t *model)
{
    return model != NULL && ink_tuning_lab_advance_grid_compare(&model->lab);
}

static bool app_input_event_self_test(void)
{
    ink_button_snapshot_t snapshot = {0};
    uint32_t last_hold_event_ms = 0U;

    snapshot.stable_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    snapshot.held_duration_ms[INK_RAW_BUTTON_RIGHT] = 80U;
    if (ink_app_should_dispatch_button_event(&snapshot, 80U, &last_hold_event_ms)) {
        return false;
    }
    snapshot.held_duration_ms[INK_RAW_BUTTON_RIGHT] = 130U;
    if (!ink_app_should_dispatch_button_event(&snapshot, 130U, &last_hold_event_ms)) {
        return false;
    }
    if (ink_app_should_dispatch_button_event(&snapshot, 200U, &last_hold_event_ms)) {
        return false;
    }
    snapshot.released_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    snapshot.stable_mask = 0U;
    if (!ink_app_should_dispatch_button_event(&snapshot, 260U, &last_hold_event_ms)) {
        return false;
    }
    return true;
}

static bool app_lab_command_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.lab.current_page = INK_TUNING_PAGE_TEXT;
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT)) {
        return false;
    }
    if (model.lab.current_page != INK_TUNING_PAGE_FOOTER) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_CONFIRM)) {
        return false;
    }
    if (model.lab.refresh_profile != INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_BACK)) {
        return false;
    }
    return model.lab.force_full_refresh;
}

static bool app_library_command_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    model.library.active_tab = INK_LIBRARY_TAB_ALL;
    model.library.focus = INK_LIBRARY_FOCUS_ITEMS;
    model.browser.entry_count = 2U;
    model.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(model.browser.entries[0].name, sizeof(model.browser.entries[0].name), "%s", "A.XTC");
    snprintf(model.browser.entries[0].full_path, sizeof(model.browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");
    model.browser.entries[1].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(model.browser.entries[1].name, sizeof(model.browser.entries[1].name), "%s", "B.XTC");
    snprintf(model.browser.entries[1].full_path, sizeof(model.browser.entries[1].full_path), "%s", "/sdcard/books/B.XTC");
    model.library.selected_index[INK_LIBRARY_TAB_ALL] = 1U;
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT)) {
        return false;
    }
    if (model.library.selected_index[INK_LIBRARY_TAB_ALL] != 0U) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS)) {
        return false;
    }
    return model.library.selected_index[INK_LIBRARY_TAB_ALL] == 1U;
}

static bool app_library_tabs_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    model.library.active_tab = INK_LIBRARY_TAB_RECENT;
    model.library.focus = INK_LIBRARY_FOCUS_ITEMS;
    model.browser.entry_count = 2U;
    model.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(model.browser.entries[0].name, sizeof(model.browser.entries[0].name), "%s", "A.XTC");
    snprintf(model.browser.entries[0].full_path, sizeof(model.browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");
    model.browser.entries[1].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(model.browser.entries[1].name, sizeof(model.browser.entries[1].name), "%s", "B.XTC");
    snprintf(model.browser.entries[1].full_path, sizeof(model.browser.entries[1].full_path), "%s", "/sdcard/books/B.XTC");
    if (!ink_app_state_note_xtc_opened(&model.app_state, "/sdcard/books/A.XTC", "A.XTC", 3U, 0U, 100U, "第一章")) {
        return false;
    }
    if (!ink_app_state_set_xtc_favorite(&model.app_state, "/sdcard/books/B.XTC", "B.XTC", true)) {
        return false;
    }
    if (app_library_visible_count(&model, INK_LIBRARY_TAB_RECENT) != 1U) {
        return false;
    }
    if (app_library_visible_count(&model, INK_LIBRARY_TAB_FAVORITES) != 1U) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_CONFIRM)) {
        return false;
    }
    if (!model.library.popup_open || model.library.focus != INK_LIBRARY_FOCUS_POPUP) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_BACK)) {
        return false;
    }
    if (model.library.popup_open || model.library.focus != INK_LIBRARY_FOCUS_ITEMS) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_BACK)) {
        return false;
    }
    if (model.library.focus != INK_LIBRARY_FOCUS_TABS) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT)) {
        return false;
    }
    if (model.library.active_tab != INK_LIBRARY_TAB_ALL) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS)) {
        return false;
    }
    return model.library.active_tab == INK_LIBRARY_TAB_RECENT;
}

static bool app_grid_compare_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.lab.current_page = INK_TUNING_PAGE_GRID_COMPARE;
    model.lab.grid_compare_active = true;
    model.lab.force_full_refresh = false;
    model.lab.reader_white_refresh_pending = false;
    if (!ink_app_should_auto_advance_grid_compare(&model, ESP_OK)) {
        return false;
    }
    if (!ink_app_advance_grid_compare(&model)) {
        return false;
    }
    if (model.lab.grid_compare_step != 1U) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_BACK)) {
        return false;
    }
    return model.lab.grid_compare_active
        && model.lab.grid_compare_step == 0U
        && model.lab.force_full_refresh;
}

static bool app_auto_repeat_probe_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.lab.current_page = INK_TUNING_PAGE_FOOTER;
    model.lab.render_counter = 1U;
    model.lab.refresh_profile = INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER;
    if (!ink_app_should_auto_repeat_tuning_probe(&model, ESP_OK)) {
        return false;
    }
    model.lab.refresh_profile = INK_TUNING_REFRESH_CUSTOM_LUT_A;
    if (!ink_app_should_auto_repeat_tuning_probe(&model, ESP_OK)) {
        return false;
    }
    model.lab.render_counter = INK_TUNING_AUTO_PROBE_REPEAT_LIMIT;
    if (ink_app_should_auto_repeat_tuning_probe(&model, ESP_OK)) {
        return false;
    }
    model.lab.render_counter = 1U;
    model.lab.current_page = INK_TUNING_PAGE_TEXT;
    if (ink_app_should_auto_repeat_tuning_probe(&model, ESP_OK)) {
        return false;
    }
    model.lab.current_page = INK_TUNING_PAGE_FOOTER;
    if (ink_app_should_auto_repeat_tuning_probe(&model, ESP_FAIL)) {
        return false;
    }
    model.lab.force_full_refresh = true;
    return !ink_app_should_auto_repeat_tuning_probe(&model, ESP_OK);
}

static bool app_auto_flip_stress_self_test(void)
{
    ink_ui_model_t model;
    bool dirty = false;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.reader_session.total_pages = 1U;
    model.reader_session.current_page = 0U;

    if (toggle_reader_auto_flip_stress(&model, &dirty)) {
        return false;
    }

    model.reader_session.total_pages = 10U;
    model.reader_session.current_page = 7U;
    model.lab.auto_flip_stress_enabled = true;
    if (!toggle_reader_auto_flip_stress(&model, &dirty) || !dirty) {
        return false;
    }
    if (model.lab.auto_flip_stress_enabled) {
        return false;
    }

    return !ink_app_advance_reader_auto_flip_stress(&model);
}

static bool app_reader_white_refresh_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.lab.auto_flip_stress_enabled = true;
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_BACK)) {
        return false;
    }
    return model.shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && !ink_reader_session_is_xtc_active(&model.reader_session)
        && !model.lab.auto_flip_stress_enabled
        && !model.lab.reader_white_refresh_pending
        && !model.lab.force_full_refresh;
}

static bool app_reader_confirm_ignored_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 4U;
    model.reader_session.total_pages = 10U;
    return !ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_CONFIRM)
        && !model.lab.auto_flip_stress_enabled
        && model.reader_session.current_page == 4U;
}

static bool app_reader_back_returns_library_fast_commit_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 7U;
    model.reader_session.total_pages = 20U;

    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_BACK)) {
        return false;
    }

    return model.shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && model.shell.full_refresh_requested
        && !model.reader_fast_full_commit_pending
        && !ink_reader_session_is_xtc_active(&model.reader_session);
}

static bool app_reader_menu_navigation_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 20U;
    model.reader_session.total_pages = 100U;
    model.reader_session.xtc_book.chapter_entry_count = 3U;

    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_CONFIRM)) {
        return false;
    }
    if (!model.reader_menu.open
        || model.reader_menu.active_tab != INK_READER_MENU_TAB_CHAPTERS
        || model.reader_menu.level != INK_READER_MENU_LEVEL_TABS) {
        return false;
    }
    model.fast_browse.active = true;
    model.reader_nav_pending = true;
    model.reader_fast_full_commit_pending = true;
    if (!app_open_reader_menu(&model)) {
        return false;
    }
    if (model.fast_browse.active || model.reader_nav_pending || model.reader_fast_full_commit_pending) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT)) {
        return false;
    }
    if (model.reader_menu.active_tab != INK_READER_MENU_TAB_BOOKMARKS) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_CONFIRM)) {
        return false;
    }
    if (model.reader_menu.level != INK_READER_MENU_LEVEL_ITEMS) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_BACK)) {
        return false;
    }
    if (!model.reader_menu.open || model.reader_menu.level != INK_READER_MENU_LEVEL_TABS) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_BACK)) {
        return false;
    }
    return !model.reader_menu.open;
}

bool ink_app_ui_self_test(void)
{
    return app_input_event_self_test()
        && app_lab_command_self_test()
        && app_library_command_self_test()
        && app_library_tabs_self_test()
        && app_grid_compare_self_test()
        && app_auto_repeat_probe_self_test()
        && app_auto_flip_stress_self_test()
        && app_reader_white_refresh_self_test()
        && app_reader_confirm_ignored_self_test()
        && app_reader_menu_navigation_self_test()
        && app_reader_back_returns_library_fast_commit_self_test();
}
