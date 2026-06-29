#include "ink_app_render.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/task.h"

#include "ink_app_ui.h"

static size_t display_library_visible_count(const ink_ui_model_t *model, ink_library_tab_t tab);
static bool display_library_book_index_at(
    const ink_ui_model_t *model,
    ink_library_tab_t tab,
    size_t visible_index,
    size_t *browser_index_out);
static bool display_library_resolve_selected_path(
    const ink_ui_model_t *model,
    char *path_out,
    size_t path_out_size,
    char *title_out,
    size_t title_out_size);
static void copy_overlay_title(
    char *dst,
    size_t dst_size,
    const char *src,
    size_t max_chars);
static void fill_reader_menu_overlay(
    const ink_ui_model_t *model,
    epd_test_pattern_reader_menu_overlay_t *overlay);
static void fill_reader_loading_overlay(
    epd_test_pattern_reader_menu_overlay_t *overlay,
    const char *title,
    const char *line1,
    const char *line2);

static void copy_overlay_title(
    char *dst,
    size_t dst_size,
    const char *src,
    size_t max_chars)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    epd_test_pattern_truncate_text_middle(
        src != NULL ? src : "",
        dst,
        dst_size,
        max_chars);
}

static void format_reader_footer_text(
    const ink_ui_model_t *model,
    size_t page_index,
    char *left,
    size_t left_size,
    char *right,
    size_t right_size)
{
    size_t display_chapter_index = 0U;
    size_t display_chapter_total = 0U;
    const char *display_chapter_title = NULL;
    unsigned percent = 0U;

    if (left != NULL && left_size > 0U) {
        left[0] = '\0';
    }
    if (right != NULL && right_size > 0U) {
        right[0] = '\0';
    }
    if (model == NULL || !ink_reader_session_is_xtc_active(&model->reader_session)) {
        return;
    }

    if (model->reader_session.total_pages > 0U) {
        percent = (unsigned)(((page_index + 1U) * 100U) / model->reader_session.total_pages);
        if (percent > 100U) {
            percent = 100U;
        }
    }
    if (left != NULL
        && left_size > 0U
        && ink_reader_session_resolve_display_chapter_for_page(
            &model->reader_session,
            page_index,
            &display_chapter_index,
            &display_chapter_total,
            &display_chapter_title)) {
        snprintf(
            left,
            left_size,
            "%s",
            display_chapter_title != NULL ? display_chapter_title : "");
    } else if (left != NULL
        && left_size > 0U
        && model->reader_session.current_chapter_name[0] != '\0') {
        snprintf(
            left,
            left_size,
            "%s",
            model->reader_session.current_chapter_name);
    }

    if (right != NULL && right_size > 0U) {
        snprintf(
            right,
            right_size,
            "%u%% %u/%u",
            percent,
            (unsigned)(page_index + 1U),
            (unsigned)model->reader_session.total_pages);
    }
}

static void format_library_footer_text(
    const ink_ui_model_t *model,
    char *left,
    size_t left_size,
    char *right,
    size_t right_size)
{
    if (left != NULL && left_size > 0U) {
        snprintf(
            left,
            left_size,
            "%s",
            "确认打开书籍");
    }

    if (right != NULL && right_size > 0U) {
        const unsigned book_count = model != NULL ? (unsigned)model->browser.entry_count : 0U;
        snprintf(right, right_size, "%u 本书", book_count);
    }
}

static void format_lab_footer_text(
    const ink_ui_model_t *model,
    char *left,
    size_t left_size,
    char *right,
    size_t right_size)
{
    const bool reader_active =
        model != NULL && ink_reader_session_is_xtc_active(&model->reader_session);

    if (left != NULL && left_size > 0U) {
        left[0] = '\0';
    }
    if (right != NULL && right_size > 0U) {
        right[0] = '\0';
    }
    if (model == NULL) {
        return;
    }

    if (reader_active) {
        const size_t footer_page = model->reader_hold_navigation_active
            ? model->reader_session.current_page
            : (model->fast_browse.overlay_mode && model->fast_browse.active)
            ? model->fast_browse.target_page
            : model->reader_session.current_page;
        format_reader_footer_text(model, footer_page, left, left_size, right, right_size);
    } else if (model->shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        format_library_footer_text(model, left, left_size, right, right_size);
    } else if (left != NULL && left_size > 0U) {
        snprintf(
            left,
            left_size,
            "P%u/%u %s",
            (unsigned)(model->lab.current_page + 1U),
            (unsigned)INK_TUNING_PAGE_COUNT,
            ink_tuning_lab_page_name(model->lab.current_page));
    }
    if (!reader_active
        && model->shell.page != INK_RUNTIME_SHELL_PAGE_LIBRARY
        && right != NULL
        && right_size > 0U) {
        if (model->lab.current_page == INK_TUNING_PAGE_GRID_COMPARE) {
            snprintf(
                right,
                right_size,
                "GRID %s %02u/%02u",
                ink_tuning_lab_grid_compare_sweep_tag(),
                (unsigned)model->lab.grid_compare_step,
                (unsigned)ink_tuning_lab_grid_compare_cell_count());
        } else {
            snprintf(
                right,
                right_size,
                "%s%s%s #%u",
                ink_tuning_lab_refresh_profile_name(model->lab.refresh_profile),
                model->lab.auto_flip_stress_enabled ? "+AUTO" : "",
                model->lab.reader_white_refresh_pending ? "+WHITE" : "",
                (unsigned)model->lab.render_counter);
        }
    }
}

static size_t display_library_visible_count(const ink_ui_model_t *model, ink_library_tab_t tab)
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

static bool display_library_book_index_at(
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

static bool display_library_resolve_selected_path(
    const ink_ui_model_t *model,
    char *path_out,
    size_t path_out_size,
    char *title_out,
    size_t title_out_size)
{
    size_t browser_index = 0U;
    const ink_file_browser_entry_t *entry = NULL;

    if (path_out != NULL && path_out_size > 0U) {
        path_out[0] = '\0';
    }
    if (title_out != NULL && title_out_size > 0U) {
        title_out[0] = '\0';
    }
    if (model == NULL
        || !display_library_book_index_at(
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

static void fill_library_overlay_tabs(
    const ink_ui_model_t *model,
    epd_test_pattern_reader_menu_overlay_t *overlay)
{
    static const char *kTabLabels[INK_LIBRARY_TAB_COUNT] = {"最近", "全部", "收藏"};

    if (model == NULL || overlay == NULL) {
        return;
    }

    overlay->tab_count = INK_LIBRARY_TAB_COUNT;
    overlay->tabs_focus = model->library.focus == INK_LIBRARY_FOCUS_TABS;
    for (size_t i = 0; i < INK_LIBRARY_TAB_COUNT; ++i) {
        snprintf(overlay->tabs[i].label, sizeof(overlay->tabs[i].label), "%s", kTabLabels[i]);
        overlay->tabs[i].active = model->library.active_tab == (ink_library_tab_t)i;
        overlay->tabs[i].focused = overlay->tabs_focus && overlay->tabs[i].active;
    }
}

static void fill_library_overlay_cards(
    const ink_ui_model_t *model,
    epd_test_pattern_reader_menu_overlay_t *overlay)
{
    const ink_library_tab_t tab = model->library.active_tab;
    const size_t visible_count = display_library_visible_count(model, tab);
    size_t start_visible = 0U;
    size_t card_index = 0U;

    if (model == NULL || overlay == NULL) {
        return;
    }

    if (visible_count == 0U) {
        overlay->card_count = 1U;
        snprintf(overlay->cards[0].title, sizeof(overlay->cards[0].title), "%s", "暂无书籍");
        snprintf(overlay->cards[0].line1, sizeof(overlay->cards[0].line1), "%s", "请导入 XTC 电子书");
        overlay->cards[0].selected = true;
        return;
    }

    if (model->library.selected_index[tab] >= EPD_TEST_PATTERN_MENU_CARD_CAPACITY) {
        start_visible = model->library.selected_index[tab] - (EPD_TEST_PATTERN_MENU_CARD_CAPACITY - 1U);
    }

    for (size_t visible = start_visible;
         visible < visible_count && card_index < EPD_TEST_PATTERN_MENU_CARD_CAPACITY;
         ++visible, ++card_index) {
        size_t browser_index = 0U;
        size_t shelf_index = 0U;
        const ink_file_browser_entry_t *entry = NULL;
        const ink_app_state_bookshelf_entry_t *shelf = NULL;

        if (!display_library_book_index_at(model, tab, visible, &browser_index)) {
            break;
        }
        entry = &model->browser.entries[browser_index];
        if (ink_app_state_find_xtc_bookshelf_entry(&model->app_state, entry->full_path, &shelf_index)) {
            shelf = ink_app_state_bookshelf_entry_at(&model->app_state, shelf_index);
        }

        snprintf(
            overlay->cards[card_index].title,
            sizeof(overlay->cards[card_index].title),
            "%s",
            entry->name);
        if (shelf != NULL && shelf->has_opened) {
            snprintf(
                overlay->cards[card_index].line1,
                sizeof(overlay->cards[card_index].line1),
                "%u/%u %.23s",
                (unsigned)(shelf->page_index + 1U),
                (unsigned)(shelf->total_pages_snapshot > 0U ? shelf->total_pages_snapshot : 0U),
                shelf->chapter_title[0] != '\0' ? shelf->chapter_title : "最近阅读");
        } else {
            snprintf(overlay->cards[card_index].line1, sizeof(overlay->cards[card_index].line1), "%s", "未读");
        }
        overlay->cards[card_index].line2[0] = '\0';
        overlay->cards[card_index].selected =
            visible == model->library.selected_index[tab] && model->library.focus == INK_LIBRARY_FOCUS_ITEMS;
        overlay->cards[card_index].trailing_favorite = shelf != NULL && shelf->is_favorite;
    }

    overlay->card_count = card_index;
}

static void fill_library_overlay_actions(
    const ink_ui_model_t *model,
    epd_test_pattern_reader_menu_overlay_t *overlay)
{
    char path[INK_FILE_BROWSER_PATH_LENGTH + 1];
    char title[INK_FILE_BROWSER_NAME_LENGTH + 1];
    size_t shelf_index = 0U;
    const ink_app_state_bookshelf_entry_t *shelf = NULL;
    bool is_favorite = false;

    if (model == NULL || overlay == NULL || !model->library.popup_open) {
        return;
    }

    path[0] = '\0';
    title[0] = '\0';
    if (!display_library_resolve_selected_path(model, path, sizeof(path), title, sizeof(title))) {
        snprintf(title, sizeof(title), "%s", "书籍");
    }

    if (ink_app_state_find_xtc_bookshelf_entry(&model->app_state, path, &shelf_index)) {
        shelf = ink_app_state_bookshelf_entry_at(&model->app_state, shelf_index);
        is_favorite = shelf != NULL && shelf->is_favorite;
    }

    overlay->action_popup_open = true;
    overlay->action_count = 2U;
    copy_overlay_title(
        overlay->action_popup_title,
        sizeof(overlay->action_popup_title),
        title,
        18U);
    snprintf(overlay->actions[0].label, sizeof(overlay->actions[0].label), "%s", "打开");
    snprintf(
        overlay->actions[1].label,
        sizeof(overlay->actions[1].label),
        "%s",
        is_favorite ? "取消收藏" : "加入收藏");
    overlay->actions[0].selected = model->library.popup_action_index == 0U;
    overlay->actions[1].selected = model->library.popup_action_index == 1U;
}

static void fill_library_overlay(
    const ink_ui_model_t *model,
    epd_test_pattern_reader_menu_overlay_t *overlay)
{
    if (model == NULL || overlay == NULL) {
        return;
    }

    memset(overlay, 0, sizeof(*overlay));
    overlay->frameless_panel = true;
    snprintf(overlay->header_title, sizeof(overlay->header_title), "%s", "书库");
    fill_library_overlay_tabs(model, overlay);
    fill_library_overlay_cards(model, overlay);
    fill_library_overlay_actions(model, overlay);
}

bool ink_app_build_display_request(
    const ink_ui_model_t *model,
    uint32_t input_ms,
    uint32_t command_latency_ms,
    ink_runtime_shell_command_t command,
    ink_display_request_t *request)
{
    const bool reader_active = model != NULL
        && ink_reader_session_is_xtc_active(&model->reader_session);
    const bool fast_browse_overlay = reader_active
        && model != NULL
        && model->fast_browse.active
        && model->fast_browse.overlay_mode
        && !model->reader_menu.open;
    const bool reader_hold_navigation = reader_active
        && model != NULL
        && model->reader_hold_navigation_active;
    const bool reader_loading_overlay = model != NULL
        && model->shell.page == INK_RUNTIME_SHELL_PAGE_READER
        && model->reader_opening;

    if (model == NULL || request == NULL) {
        return false;
    }

    memset(request, 0, sizeof(*request));
    request->input_ms = input_ms;
    request->command_latency_ms = command_latency_ms;
    request->submitted_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    request->command = command;
    request->page = model->shell.page;
    request->refresh_strategy = INK_REFRESH_STRATEGY_NONE;
    request->tuning_page = (uint8_t)model->lab.current_page;
    request->refresh_profile = (uint8_t)(
        reader_active
            ? INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY
            : model->lab.refresh_profile);
    request->grid_compare_step = model->lab.grid_compare_step;
    request->use_grid_compare_variant =
        !reader_active
        && model->lab.current_page == INK_TUNING_PAGE_GRID_COMPARE
        && model->lab.grid_compare_step > 0U;
    request->grid_compare_variant_index =
        request->use_grid_compare_variant
            ? (uint8_t)(model->lab.grid_compare_step - 1U)
            : 0U;
    request->force_white_page = model->lab.reader_white_refresh_pending;
    request->use_fast_browse_overlay = fast_browse_overlay;
    request->use_reader_hold_navigation = reader_hold_navigation;
    request->use_library_overlay = request->page == INK_RUNTIME_SHELL_PAGE_LIBRARY;
    request->use_reader_menu_overlay = request->page == INK_RUNTIME_SHELL_PAGE_READER
        && model->reader_menu.open
        && reader_active;
    request->force_fixed_footer_partial =
        fast_browse_overlay
        || (!ink_reader_session_is_xtc_active(&model->reader_session)
            && (model->lab.refresh_profile == INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER
                || (model->lab.refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_A
                    && model->lab.current_page == INK_TUNING_PAGE_FOOTER)));
    request->use_footer_overlay = !request->force_white_page
        && !request->use_grid_compare_variant;
    if (!reader_active && model->lab.current_page == INK_TUNING_PAGE_GRAY_CAL) {
        request->use_footer_overlay = false;
    }
    request->force_fast_full_commit =
        model != NULL
        && model->reader_fast_full_commit_pending
        && !fast_browse_overlay
        && !model->reader_menu.open;
    if (request->page == INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        request->use_footer_overlay = false;
    }
    request->full_refresh = model->lab.force_full_refresh
        || (request->page != INK_RUNTIME_SHELL_PAGE_READER
            && ink_runtime_shell_requires_full_refresh(&model->shell))
        || request->refresh_profile == INK_TUNING_REFRESH_FULL;
    if (!reader_active && model->lab.current_page == INK_TUNING_PAGE_GRAY_CAL) {
        request->full_refresh = true;
    }
    if (request->force_fast_full_commit) {
        request->refresh_profile = INK_TUNING_REFRESH_FAST_FULL;
    }
    request->use_font = request->page != INK_RUNTIME_SHELL_PAGE_READER && !request->use_library_overlay;
    request->use_reader_layout = request->page == INK_RUNTIME_SHELL_PAGE_READER
        && reader_active
        && !fast_browse_overlay
        && !request->use_reader_menu_overlay;
    if (!request->force_white_page
        && !fast_browse_overlay
        && ink_reader_session_has_prepared_page(&model->reader_session)) {
        request->use_bitmap_page = true;
        request->bitmap_page_buffer = ink_reader_session_prepared_page_buffer(&model->reader_session);
        request->bitmap_page_length = ink_reader_session_prepared_page_length(&model->reader_session);
    }
    if (!request->force_white_page
        && !request->use_footer_overlay
        && !fast_browse_overlay
        && ink_reader_session_has_native_page(&model->reader_session)) {
        request->use_native_page = true;
        request->native_page_buffer = ink_reader_session_native_page_buffer(&model->reader_session);
        request->native_page_length = ink_reader_session_native_page_length(&model->reader_session);
    }
    ink_runtime_shell_render(
        &model->shell,
        &model->browser,
        &model->reader_session,
        &request->shell_view);
    (void)ink_reader_session_get_text_view(&model->reader_session, &request->reader_view);
    if (request->use_library_overlay) {
        fill_library_overlay(model, &request->menu_overlay);
    } else if (request->use_reader_menu_overlay) {
        fill_reader_menu_overlay(model, &request->menu_overlay);
    } else if (reader_loading_overlay) {
        fill_reader_loading_overlay(
            &request->menu_overlay,
            model->reader_loading_title[0] != '\0' ? model->reader_loading_title : "正在加载",
            model->reader_loading_line[0] != '\0' ? model->reader_loading_line : "正在打开书籍",
            model->reader_loading_hint[0] != '\0' ? model->reader_loading_hint : "Back 取消");
        request->use_reader_menu_overlay = true;
    }
    format_lab_footer_text(
        model,
        request->overlay_left,
        sizeof(request->overlay_left),
        request->overlay_right,
        sizeof(request->overlay_right));
    if (request->page == INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        request->overlay_left[0] = '\0';
        request->overlay_right[0] = '\0';
    }

    if (request->full_refresh) {
        request->refresh_strategy = INK_REFRESH_STRATEGY_PAGE_TRANSITION_FULL;
    } else if (request->use_reader_menu_overlay || request->use_library_overlay) {
        request->refresh_strategy = INK_REFRESH_STRATEGY_OVERLAY_LOCAL_UPDATE;
    } else if (request->use_reader_hold_navigation) {
        request->refresh_strategy = INK_REFRESH_STRATEGY_READER_HOLD_PREVIEW;
    } else if (request->page == INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        request->refresh_strategy = INK_REFRESH_STRATEGY_BW_UI_LIST_LOCAL;
    } else if (reader_active && request->page == INK_RUNTIME_SHELL_PAGE_READER) {
        request->refresh_strategy = request->force_white_page || request->force_fast_full_commit
            ? INK_REFRESH_STRATEGY_READER_TEXT_CLEANUP
            : INK_REFRESH_STRATEGY_READER_TEXT_TURN;
    } else {
        request->refresh_strategy = INK_REFRESH_STRATEGY_LAB_EXPLICIT_MODE;
    }

    return true;
}

static void fill_reader_loading_overlay(
    epd_test_pattern_reader_menu_overlay_t *overlay,
    const char *title,
    const char *line1,
    const char *line2)
{
    if (overlay == NULL) {
        return;
    }

    memset(overlay, 0, sizeof(*overlay));
    overlay->frameless_panel = true;
    snprintf(overlay->header_title, sizeof(overlay->header_title), "%s", "书库");
    overlay->compact_cards = false;
    overlay->bookmark_cards_tall = false;
    overlay->card_count = 0U;
    overlay->action_popup_open = true;
    overlay->action_count = 0U;
    copy_overlay_title(
        overlay->action_popup_title,
        sizeof(overlay->action_popup_title),
        title != NULL ? title : "正在加载",
        18U);
    (void)line1;
    (void)line2;
}

static void fill_reader_menu_overlay(
    const ink_ui_model_t *model,
    epd_test_pattern_reader_menu_overlay_t *overlay)
{
    static const char *kTabLabels[INK_READER_MENU_TAB_COUNT] = {"章节", "书签"};

    if (model == NULL || overlay == NULL) {
        return;
    }

    memset(overlay, 0, sizeof(*overlay));
    overlay->compact_cards = model->reader_menu.active_tab != INK_READER_MENU_TAB_BOOKMARKS;
    overlay->bookmark_cards_tall = model->reader_menu.active_tab == INK_READER_MENU_TAB_BOOKMARKS;
    overlay->frameless_panel = false;
    overlay->tab_count = INK_READER_MENU_TAB_COUNT;
    overlay->tabs_focus = model->reader_menu.level == INK_READER_MENU_LEVEL_TABS;

    for (size_t i = 0; i < INK_READER_MENU_TAB_COUNT; ++i) {
        snprintf(overlay->tabs[i].label, sizeof(overlay->tabs[i].label), "%s", kTabLabels[i]);
        overlay->tabs[i].active = model->reader_menu.active_tab == (ink_reader_menu_tab_t)i;
        overlay->tabs[i].focused = overlay->tabs_focus && overlay->tabs[i].active;
    }

    if (model->reader_menu.active_tab == INK_READER_MENU_TAB_CHAPTERS) {
        const size_t total = model->reader_session.xtc_book.chapter_entry_count;
        const size_t selected = model->reader_menu.chapter_item_index;
        size_t start = 0U;
        size_t count = 0U;

        if (selected >= EPD_TEST_PATTERN_MENU_CARD_CAPACITY) {
            start = selected - (EPD_TEST_PATTERN_MENU_CARD_CAPACITY - 1U);
        }
        for (size_t i = start;
             i < total && count < EPD_TEST_PATTERN_MENU_CARD_CAPACITY;
             ++i, ++count) {
            const ink_xtc_chapter_entry_t *chapter = &model->reader_session.xtc_book.chapter_entries[i];
            copy_overlay_title(
                overlay->cards[count].title,
                sizeof(overlay->cards[count].title),
                chapter->name,
                20U);
            overlay->cards[count].selected =
                model->reader_menu.level == INK_READER_MENU_LEVEL_ITEMS
                && i == selected;
        }
        overlay->card_count = count;
        return;
    }

    copy_overlay_title(
        overlay->cards[0].title,
        sizeof(overlay->cards[0].title),
        "将当前页添加到书签",
        20U);
    snprintf(
        overlay->cards[0].line1,
        sizeof(overlay->cards[0].line1),
        "P%u  %.54s",
        (unsigned)(model->reader_session.current_page + 1U),
        model->reader_session.current_chapter_name[0] != '\0'
            ? model->reader_session.current_chapter_name
            : "");
    overlay->cards[0].selected =
        model->reader_menu.level == INK_READER_MENU_LEVEL_ITEMS
        && model->reader_menu.bookmark_item_index == 0U;
    overlay->card_count = 1U;

    for (size_t i = 0; i < INK_APP_STATE_BOOKMARK_CAPACITY
        && overlay->card_count < EPD_TEST_PATTERN_MENU_CARD_CAPACITY;
         ++i) {
        const ink_app_state_bookmark_t *bookmark = ink_app_state_bookmark_at(&model->app_state, i);
        if (bookmark == NULL || strcmp(bookmark->book_path, model->reader_session.source_path) != 0) {
            continue;
        }
        copy_overlay_title(
            overlay->cards[overlay->card_count].title,
            sizeof(overlay->cards[overlay->card_count].title),
            bookmark->chapter_title[0] != '\0' ? bookmark->chapter_title : "书签",
            20U);
        snprintf(
            overlay->cards[overlay->card_count].line1,
            sizeof(overlay->cards[overlay->card_count].line1),
            "P%u  %s",
            (unsigned)(bookmark->page_index + 1U),
            bookmark->timestamp_text);
        overlay->cards[overlay->card_count].selected =
            model->reader_menu.level == INK_READER_MENU_LEVEL_ITEMS
            && model->reader_menu.bookmark_item_index == overlay->card_count;
        overlay->card_count++;
    }

    if (model->reader_menu.level == INK_READER_MENU_LEVEL_BOOKMARK_ACTIONS) {
        const bool current_card = model->reader_menu.bookmark_item_index == 0U;
        overlay->action_popup_open = true;
        copy_overlay_title(
            overlay->action_popup_title,
            sizeof(overlay->action_popup_title),
            current_card ? "当前页书签" : "书签操作",
            18U);
        overlay->action_count = 3U;
        snprintf(overlay->actions[0].label, sizeof(overlay->actions[0].label), "%s", current_card ? "返回" : "跳转");
        snprintf(overlay->actions[1].label, sizeof(overlay->actions[1].label), "%s", current_card ? "添加" : "覆盖");
        snprintf(overlay->actions[2].label, sizeof(overlay->actions[2].label), "%s", "删除");
        overlay->actions[0].selected = model->reader_menu.bookmark_action_index == 0U;
        overlay->actions[1].selected = model->reader_menu.bookmark_action_index == 1U;
        overlay->actions[2].selected = model->reader_menu.bookmark_action_index == 2U;
    }
}
