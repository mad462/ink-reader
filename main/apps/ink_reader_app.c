#include "apps/ink_reader_app.h"

#include <string.h>

#include "apps/ink_launcher_app.h"
#include "ink_app_boot.h"
#include "ink_app_render.h"
#include "ink_app_ui.h"
#include "ink_system_runtime.h"

static ink_reader_app_state_t s_reader_state;

static void reader_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static void reader_exit(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool reader_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event);
static bool reader_tick(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    uint32_t now_ms);
static bool reader_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model);
static bool request_switch_to_launcher(ink_system_runtime_t *runtime);
static bool reader_transition_requires_full_refresh(
    ink_runtime_shell_page_t previous_page,
    ink_runtime_shell_page_t current_page,
    const ink_runtime_shell_t *shell);
static ink_runtime_shell_command_t app_event_to_shell_command(const ink_app_event_t *event);
static void reader_refresh_storage_views(ink_reader_app_state_t *state, ink_system_services_t *services);
static bool reader_begin_open_selected_book(ink_reader_app_state_t *state);
static bool reader_navigation_self_test(void);
static bool reader_library_input_does_not_force_full_refresh_self_test(void);
static bool reader_in_page_input_does_not_force_full_refresh_self_test(void);
static bool reader_reenter_preserves_state_self_test(void);
static bool reader_confirm_long_press_requests_white_refresh_self_test(void);
static bool reader_nav_long_press_repeats_real_page_turns_self_test(void);
static bool reader_nav_press_event_starts_hold_pending_self_test(void);
static bool reader_snapshot_updates_button_state_for_hold_tick_self_test(void);
static bool reader_enter_initial_full_refresh_is_one_shot_self_test(void);
static bool reader_internal_open_does_not_force_full_refresh_self_test(void);
static bool reader_opening_blocks_non_back_input_self_test(void);
static bool reader_hold_release_invalidates_queued_render_self_test(void);
static bool reader_tick_uses_latest_button_state_to_stop_hold_self_test(void);
static bool reader_latest_button_state_uses_processing_time_self_test(void);
static void reader_begin_opening(
    ink_reader_app_state_t *state,
    const char *path,
    const char *title);
static bool reader_finish_opening(
    ink_system_runtime_t *runtime,
    ink_reader_app_state_t *state);
static const ink_button_snapshot_t *reader_snapshot_from_event(const ink_app_event_t *event);

static const ink_app_descriptor_t kReaderApp = {
    .id = "reader",
    .name = "Reader",
    .enter = reader_enter,
    .exit = reader_exit,
    .input = reader_input,
    .tick = reader_tick,
    .render = reader_render,
    .state = &s_reader_state,
};

const ink_app_descriptor_t *ink_reader_app_descriptor(void)
{
    return &kReaderApp;
}

static bool request_switch_to_launcher(ink_system_runtime_t *runtime)
{
    const ink_app_descriptor_t *launcher = ink_system_runtime_find_app_by_id(runtime, "launcher");

    return launcher != NULL
        && ink_system_runtime_request_switch(runtime, launcher);
}

static const ink_button_snapshot_t *reader_snapshot_from_event(const ink_app_event_t *event)
{
    if (event == NULL) {
        return NULL;
    }
    switch (event->kind) {
        case INK_APP_EVENT_BUTTON_SNAPSHOT:
        case INK_APP_EVENT_NAV_PREVIOUS:
        case INK_APP_EVENT_NAV_NEXT:
        case INK_APP_EVENT_BUTTON_BACK:
        case INK_APP_EVENT_BUTTON_CONFIRM:
            break;
        default:
            return NULL;
    }
    if (event->payload == NULL) {
        return NULL;
    }
    return (const ink_button_snapshot_t *)event->payload;
}

static bool reader_begin_open_selected_book(ink_reader_app_state_t *state)
{
    size_t ranked[INK_FILE_BROWSER_MAX_ENTRIES];
    size_t ranked_count = 0U;
    size_t browser_index = 0U;
    const ink_library_tab_t tab =
        state != NULL ? state->ui.library.active_tab : INK_LIBRARY_TAB_ALL;

    if (state == NULL) {
        return false;
    }

    for (size_t i = 0; i < state->ui.browser.entry_count && ranked_count < INK_FILE_BROWSER_MAX_ENTRIES; ++i) {
        const ink_file_browser_entry_t *entry = &state->ui.browser.entries[i];
        size_t shelf_index = 0U;
        const ink_app_state_bookshelf_entry_t *shelf = NULL;
        const bool has_shelf = ink_app_state_find_xtc_bookshelf_entry(
            &state->ui.app_state,
            entry->full_path,
            &shelf_index);

        if (entry->type != INK_FILE_BROWSER_ENTRY_XTC) {
            continue;
        }
        if (has_shelf) {
            shelf = ink_app_state_bookshelf_entry_at(&state->ui.app_state, shelf_index);
        }
        if (tab == INK_LIBRARY_TAB_RECENT && (shelf == NULL || !shelf->has_opened)) {
            continue;
        }
        if (tab == INK_LIBRARY_TAB_FAVORITES && (shelf == NULL || !shelf->is_favorite)) {
            continue;
        }
        ranked[ranked_count++] = i;
    }

    if (state->ui.library.selected_index[tab] >= ranked_count) {
        return false;
    }

    browser_index = ranked[state->ui.library.selected_index[tab]];
    state->ui.browser.selected_index = browser_index;
    state->ui.browser.selected_file_ready = true;
    state->ui.browser.selected_file_type = state->ui.browser.entries[browser_index].type;
    snprintf(
        state->ui.browser.selected_file_path,
        sizeof(state->ui.browser.selected_file_path),
        "%s",
        state->ui.browser.entries[browser_index].full_path);
    state->ui.library.popup_open = false;
    state->ui.library.focus = INK_LIBRARY_FOCUS_ITEMS;
    state->ui.library.popup_action_index = 0U;
    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.reader_menu.open = false;
    state->ui.reader_opening = false;
    reader_begin_opening(
        state,
        state->ui.browser.entries[browser_index].full_path,
        state->ui.browser.entries[browser_index].name);
    return true;
}

static void reader_begin_opening(
    ink_reader_app_state_t *state,
    const char *path,
    const char *title)
{
    if (state == NULL) {
        return;
    }

    state->ui.reader_opening = true;
    snprintf(
        state->ui.reader_loading_title,
        sizeof(state->ui.reader_loading_title),
        "%s",
        "正在加载");
    snprintf(
        state->ui.reader_loading_line,
        sizeof(state->ui.reader_loading_line),
        "%s",
        title != NULL && title[0] != '\0' ? title : "正在打开书籍");
    snprintf(
        state->ui.reader_loading_hint,
        sizeof(state->ui.reader_loading_hint),
        "%s",
        "Back 取消");
    if (path != NULL) {
        snprintf(
            state->pending_open_path,
            sizeof(state->pending_open_path),
            "%s",
            path);
    } else {
        state->pending_open_path[0] = '\0';
    }
}

static bool reader_finish_opening(
    ink_system_runtime_t *runtime,
    ink_reader_app_state_t *state)
{
    if (runtime == NULL || state == NULL || !state->ui.reader_opening) {
        return false;
    }

    if (state->pending_open_path[0] == '\0') {
        state->ui.reader_opening = false;
        return true;
    }

    if (!ink_app_open_book_from_path(&state->ui, state->pending_open_path)) {
        state->ui.reader_opening = false;
        state->pending_open_path[0] = '\0';
        state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
        runtime->force_full_refresh_on_next_render = true;
        return true;
    }

    state->ui.reader_opening = false;
    state->pending_open_path[0] = '\0';
    return true;
}

static bool reader_transition_requires_full_refresh(
    ink_runtime_shell_page_t previous_page,
    ink_runtime_shell_page_t current_page,
    const ink_runtime_shell_t *shell)
{
    (void)previous_page;
    (void)current_page;
    return ink_runtime_shell_requires_full_refresh(shell);
}

static ink_runtime_shell_command_t app_event_to_shell_command(const ink_app_event_t *event)
{
    if (event == NULL) {
        return INK_RUNTIME_SHELL_COMMAND_NONE;
    }

    switch (event->kind) {
        case INK_APP_EVENT_BUTTON_BACK:
            return INK_RUNTIME_SHELL_COMMAND_BACK;
        case INK_APP_EVENT_BUTTON_CONFIRM:
            return INK_RUNTIME_SHELL_COMMAND_CONFIRM;
        case INK_APP_EVENT_NAV_PREVIOUS:
        case INK_APP_EVENT_TILT_PREVIOUS:
            return INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS;
        case INK_APP_EVENT_NAV_NEXT:
        case INK_APP_EVENT_TILT_NEXT:
            return INK_RUNTIME_SHELL_COMMAND_NAV_NEXT;
        default:
            return INK_RUNTIME_SHELL_COMMAND_NONE;
    }
}

static bool reader_should_route_nav_event_to_hold_logic(
    const ink_reader_app_state_t *state,
    const ink_app_event_t *event)
{
    if (state == NULL || event == NULL) {
        return false;
    }

    if (state->ui.shell.page != INK_RUNTIME_SHELL_PAGE_READER
        || !ink_reader_session_is_xtc_active(&state->ui.reader_session)) {
        return false;
    }

    return event->kind == INK_APP_EVENT_NAV_PREVIOUS
        || event->kind == INK_APP_EVENT_NAV_NEXT;
}

static void reader_refresh_storage_views(ink_reader_app_state_t *state, ink_system_services_t *services)
{
    if (state == NULL || services == NULL) {
        return;
    }

    if (services->tf_ready) {
        if (ink_file_browser_init(&state->ui.browser, INK_APP_BOOKS_PATH, INK_APP_BOOKS_PATH) != ESP_OK) {
            ink_app_prepare_browser_fallback(&state->ui.browser);
        }
    } else {
        ink_app_prepare_browser_fallback(&state->ui.browser);
    }
    state->storage_epoch_seen = services->storage_epoch;
}

static void reader_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_reader_app_state_t *state = NULL;
    ink_system_services_t *services = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL) {
        return;
    }

    services = runtime->services;
    state = (ink_reader_app_state_t *)app->state;
    if (!state->initialized) {
        ink_tuning_lab_init(&state->ui.lab);
        ink_runtime_shell_init(&state->ui.shell);
        state->ui.shell.full_refresh_requested = false;
        ink_reader_session_init(&state->ui.reader_session);
        ink_app_state_prepare_default(&state->ui.app_state);
        state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
        state->ui.library.active_tab = INK_LIBRARY_TAB_RECENT;
        state->ui.library.focus = INK_LIBRARY_FOCUS_ITEMS;

        if (services != NULL && services->tf_ready) {
            if (ink_file_browser_init(&state->ui.browser, INK_APP_BOOKS_PATH, INK_APP_BOOKS_PATH) != ESP_OK) {
                ink_app_prepare_browser_fallback(&state->ui.browser);
            }
            if (ink_app_state_load_file(INK_APP_STATE_FILE_PATH, &state->ui.app_state) != ESP_OK) {
                ink_app_state_prepare_default(&state->ui.app_state);
            }
        } else {
            ink_app_prepare_browser_fallback(&state->ui.browser);
        }

        state->storage_epoch_seen = services != NULL ? services->storage_epoch : 0U;
        state->initialized = true;
    }

    if (services != NULL && state->storage_epoch_seen != services->storage_epoch) {
        reader_refresh_storage_views(state, services);
        if (services->reader_resume_pending
            && services->reader_resume_path[0] != '\0'
            && ink_app_open_book_from_path(&state->ui, services->reader_resume_path)) {
            (void)ink_reader_session_jump_to_page(
                &state->ui.reader_session,
                services->reader_resume_page,
                &state->ui.app_state);
            ink_reader_session_prefetch_next(&state->ui.reader_session, NULL, NULL);
        }
        services->reader_resume_pending = false;
        services->reader_resume_path[0] = '\0';
        services->reader_resume_page = 0U;
    }

    runtime->force_full_refresh_on_next_render = true;
}

static void reader_exit(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_reader_app_state_t *state = NULL;

    (void)runtime;
    if (app == NULL || app->state == NULL) {
        return;
    }

    state = (ink_reader_app_state_t *)app->state;
    if (state->ui.reader_session.source_path[0] != '\0') {
        if (runtime != NULL
            && runtime->services != NULL
            && ink_reader_session_is_xtc_active(&state->ui.reader_session)) {
            runtime->services->reader_resume_pending = true;
            snprintf(
                runtime->services->reader_resume_path,
                sizeof(runtime->services->reader_resume_path),
                "%s",
                state->ui.reader_session.source_path);
            runtime->services->reader_resume_page = state->ui.reader_session.current_page;
        }
        (void)ink_app_persist_state(&state->ui.app_state);
    }
    ink_reader_session_close(&state->ui.reader_session);
}

static bool reader_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    ink_reader_app_state_t *state = NULL;
    ink_runtime_shell_command_t command = INK_RUNTIME_SHELL_COMMAND_NONE;
    ink_runtime_shell_page_t previous_page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    const ink_button_snapshot_t *snapshot = NULL;
    bool dirty = false;

    if (runtime == NULL || app == NULL || app->state == NULL || event == NULL) {
        return false;
    }

    state = (ink_reader_app_state_t *)app->state;
    if (state->ui.reader_opening) {
        if (event->kind == INK_APP_EVENT_DISPLAY_DONE) {
            return false;
        }
        if (event->kind == INK_APP_EVENT_BUTTON_BACK
            || (event->kind == INK_APP_EVENT_BUTTON_SNAPSHOT
                && event->payload != NULL
                && ink_button_snapshot_was_pressed(
                    (const ink_button_snapshot_t *)event->payload,
                    INK_LOGICAL_BUTTON_BACK))) {
            state->ui.reader_opening = false;
            state->pending_open_path[0] = '\0';
            state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
            state->ui.reader_loading_title[0] = '\0';
            state->ui.reader_loading_line[0] = '\0';
            state->ui.reader_loading_hint[0] = '\0';
            if (runtime->services != NULL) {
                ink_display_mailbox_invalidate_pending(&runtime->services->mailbox);
            }
            runtime->force_full_refresh_on_next_render = true;
            return true;
        }
        return event->kind == INK_APP_EVENT_BUTTON_SNAPSHOT;
    }
    if (event->kind == INK_APP_EVENT_DISPLAY_DONE) {
        if (state->ui.fast_browse.active) {
            ink_app_fast_browse_note_preview_landed(&state->ui);
            return false;
        }
        return false;
    }

    snapshot = reader_snapshot_from_event(event);
    if (event->kind == INK_APP_EVENT_BUTTON_SNAPSHOT && snapshot != NULL) {
        ink_app_button_state_from_snapshot(snapshot, &state->ui.buttons);
    }
    if (((event->kind == INK_APP_EVENT_BUTTON_SNAPSHOT && snapshot != NULL)
            || reader_should_route_nav_event_to_hold_logic(state, event))
        && state->ui.shell.page == INK_RUNTIME_SHELL_PAGE_READER
        && ink_reader_session_is_xtc_active(&state->ui.reader_session)) {
        dirty = ink_app_process_reader_xtc_buttons_for_model(
            &state->ui,
            snapshot,
            event->event_ms,
            &command);
        if (runtime->services != NULL
            && event->kind == INK_APP_EVENT_BUTTON_SNAPSHOT
            && ink_app_drive_reader_nav_hold_for_model(
                &state->ui,
                &runtime->services->latest_buttons,
                ink_display_mailbox_is_idle(&runtime->services->mailbox),
                (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()))) {
            dirty = true;
        }
        if (!state->ui.fast_browse.active
            && !state->ui.reader_nav_pending
            && runtime->services != NULL
            && event->kind == INK_APP_EVENT_BUTTON_SNAPSHOT
            && snapshot != NULL
            && (ink_button_snapshot_was_released(snapshot, INK_LOGICAL_BUTTON_NAV_NEXT)
                || ink_button_snapshot_was_released(snapshot, INK_LOGICAL_BUTTON_NAV_PREVIOUS))) {
            ink_display_mailbox_invalidate_pending(&runtime->services->mailbox);
        }
        if (command != INK_RUNTIME_SHELL_COMMAND_NONE) {
            previous_page = state->ui.shell.page;
            dirty |= ink_app_handle_ui_command(&state->ui, command);
            if (reader_transition_requires_full_refresh(
                    previous_page,
                    state->ui.shell.page,
                    &state->ui.shell)) {
                runtime->force_full_refresh_on_next_render = true;
            }
        }
        return dirty;
    }

    command = app_event_to_shell_command(event);
    if (command == INK_RUNTIME_SHELL_COMMAND_NONE) {
        return false;
    }

    if (command == INK_RUNTIME_SHELL_COMMAND_CONFIRM
        && state->ui.shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && state->ui.library.popup_open
        && state->ui.library.focus == INK_LIBRARY_FOCUS_POPUP
        && state->ui.library.popup_action_index == 0U) {
        return reader_begin_open_selected_book(state);
    }

    if (command == INK_RUNTIME_SHELL_COMMAND_BACK
        && state->ui.shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && state->ui.library.focus == INK_LIBRARY_FOCUS_TABS
        && !state->ui.library.popup_open) {
        return request_switch_to_launcher(runtime);
    }

    previous_page = state->ui.shell.page;
    if (!ink_app_handle_ui_command(&state->ui, command)) {
        return false;
    }

    if (reader_transition_requires_full_refresh(
            previous_page,
            state->ui.shell.page,
            &state->ui.shell)) {
        runtime->force_full_refresh_on_next_render = true;
    }
    return true;
}

static bool reader_tick(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    uint32_t now_ms)
{
    ink_reader_app_state_t *state = NULL;
    ink_runtime_shell_command_t idle_command = INK_RUNTIME_SHELL_COMMAND_NONE;
    bool dirty = false;

    (void)runtime;
    if (app == NULL || app->state == NULL) {
        return false;
    }

    state = (ink_reader_app_state_t *)app->state;
    if (state->ui.reader_opening) {
        return reader_finish_opening(runtime, state);
    }
    if (runtime != NULL
        && runtime->services != NULL
        && ink_app_drive_reader_nav_hold_for_model(
            &state->ui,
            &runtime->services->latest_buttons,
            ink_display_mailbox_is_idle(&runtime->services->mailbox),
            now_ms)) {
        return true;
    }
    if (state->ui.fast_browse.active
        && runtime != NULL
        && runtime->services != NULL
        && !runtime->services->latest_buttons.is_down[
            state->ui.fast_browse.direction == INK_FAST_BROWSE_DIR_FORWARD
                ? INK_RUNTIME_SHELL_BUTTON_RIGHT
                : INK_RUNTIME_SHELL_BUTTON_LEFT]) {
        ink_display_mailbox_invalidate_pending(&runtime->services->mailbox);
        return true;
    }
    if (state->ui.reader_nav_pending
        && runtime != NULL
        && runtime->services != NULL
        && !runtime->services->latest_buttons.is_down[
            state->ui.reader_nav_pending_dir == INK_FAST_BROWSE_DIR_FORWARD
                ? INK_RUNTIME_SHELL_BUTTON_RIGHT
                : INK_RUNTIME_SHELL_BUTTON_LEFT]) {
        state->ui.reader_nav_pending = false;
        state->ui.reader_nav_pending_start_ms = 0U;
    }
    dirty = ink_app_fast_browse_handle_idle_for_model(
        &state->ui,
        &state->ui.buttons,
        now_ms,
        &idle_command);
    if (idle_command != INK_RUNTIME_SHELL_COMMAND_NONE) {
        dirty |= ink_app_handle_ui_command(&state->ui, idle_command);
    }
    return dirty;
}

static bool reader_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model)
{
    ink_reader_app_state_t *state = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL || out_model == NULL) {
        return false;
    }

    state = (ink_reader_app_state_t *)app->state;
    memset(out_model, 0, sizeof(*out_model));
    out_model->mode = INK_APP_RENDER_MODE_READER_SUBSYSTEM;
    out_model->request_full_refresh = runtime->force_full_refresh_on_next_render
        || ink_runtime_shell_requires_full_refresh(&state->ui.shell);
    out_model->state = &state->ui;
    runtime->force_full_refresh_on_next_render = false;
    if (out_model->request_full_refresh
        && state->ui.shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        ink_runtime_shell_mark_rendered(&state->ui.shell);
    }
    return true;
}

static bool reader_navigation_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_render_model_t model;
    ink_app_event_t next_event = {
        .kind = INK_APP_EVENT_NAV_NEXT,
    };
    ink_app_event_t confirm_event = {
        .kind = INK_APP_EVENT_BUTTON_CONFIRM,
    };
    ink_app_event_t back_event = {
        .kind = INK_APP_EVENT_BUTTON_BACK,
    };
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, reader)) {
        return false;
    }

    if (!ink_system_runtime_set_active_app(&runtime, reader)
        || runtime.active_app != reader
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    if (!reader->render(&runtime, reader, &model)
        || model.mode != INK_APP_RENDER_MODE_READER_SUBSYSTEM
        || model.state != &state->ui
        || !model.request_full_refresh
        || runtime.force_full_refresh_on_next_render) {
        return false;
    }

    if (state->ui.shell.page != INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        return false;
    }

    state->ui.browser.entry_count = 2U;
    state->ui.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    state->ui.browser.entries[1].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(state->ui.browser.entries[0].name, sizeof(state->ui.browser.entries[0].name), "%s", "A.XTC");
    snprintf(state->ui.browser.entries[0].full_path, sizeof(state->ui.browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");
    snprintf(state->ui.browser.entries[1].name, sizeof(state->ui.browser.entries[1].name), "%s", "B.XTC");
    snprintf(state->ui.browser.entries[1].full_path, sizeof(state->ui.browser.entries[1].full_path), "%s", "/sdcard/books/B.XTC");
    state->ui.library.active_tab = INK_LIBRARY_TAB_ALL;
    state->ui.library.focus = INK_LIBRARY_FOCUS_ITEMS;
    state->ui.library.selected_index[INK_LIBRARY_TAB_ALL] = 0U;

    if (!reader->input(&runtime, reader, &next_event)
        || state->ui.library.selected_index[INK_LIBRARY_TAB_ALL] != 1U
        || runtime.force_full_refresh_on_next_render) {
        return false;
    }

    if (!reader->input(&runtime, reader, &confirm_event)
        || !state->ui.library.popup_open
        || state->ui.library.focus != INK_LIBRARY_FOCUS_POPUP
        || runtime.force_full_refresh_on_next_render) {
        return false;
    }

    runtime.force_full_refresh_on_next_render = false;
    state->ui.browser.selected_index = 1U;
    state->ui.browser.selected_file_ready = true;
    state->ui.browser.selected_file_type = INK_FILE_BROWSER_ENTRY_XTC;
    memcpy(
        state->ui.browser.selected_file_path,
        state->ui.browser.entries[1].full_path,
        sizeof(state->ui.browser.selected_file_path));
    state->ui.browser.selected_file_path[sizeof(state->ui.browser.selected_file_path) - 1U] = '\0';
    state->ui.reader_session.active = true;
    state->ui.reader_session.xtc_active = true;
    state->ui.reader_session.current_page = 7U;
    state->ui.reader_session.total_pages = 20U;
    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    if (!reader->input(&runtime, reader, &back_event)
        || state->ui.shell.page != INK_RUNTIME_SHELL_PAGE_LIBRARY
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    runtime.force_full_refresh_on_next_render = false;
    state->ui.library.popup_open = false;
    state->ui.library.focus = INK_LIBRARY_FOCUS_TABS;
    if (!reader->input(&runtime, reader, &back_event)
        || runtime.pending_app != launcher
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    return true;
}

bool ink_reader_app_self_test(void)
{
    return reader_navigation_self_test()
        && reader_library_input_does_not_force_full_refresh_self_test()
        && reader_in_page_input_does_not_force_full_refresh_self_test()
        && reader_reenter_preserves_state_self_test()
        && reader_confirm_long_press_requests_white_refresh_self_test()
        && reader_nav_press_event_starts_hold_pending_self_test()
        && reader_nav_long_press_repeats_real_page_turns_self_test()
        && reader_snapshot_updates_button_state_for_hold_tick_self_test()
        && reader_enter_initial_full_refresh_is_one_shot_self_test()
        && reader_internal_open_does_not_force_full_refresh_self_test()
        && reader_opening_blocks_non_back_input_self_test()
        && reader_hold_release_invalidates_queued_render_self_test()
        && reader_tick_uses_latest_button_state_to_stop_hold_self_test()
        && reader_latest_button_state_uses_processing_time_self_test();
}

static bool reader_library_input_does_not_force_full_refresh_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_event_t next_event = {
        .kind = INK_APP_EVENT_NAV_NEXT,
    };
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    runtime.force_full_refresh_on_next_render = false;
    state->ui.library.active_tab = INK_LIBRARY_TAB_ALL;
    state->ui.library.focus = INK_LIBRARY_FOCUS_ITEMS;
    state->ui.browser.entry_count = 2U;
    state->ui.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    state->ui.browser.entries[1].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(state->ui.browser.entries[0].name, sizeof(state->ui.browser.entries[0].name), "%s", "A.XTC");
    snprintf(state->ui.browser.entries[1].name, sizeof(state->ui.browser.entries[1].name), "%s", "B.XTC");

    if (!reader->input(&runtime, reader, &next_event)) {
        return false;
    }

    return state->ui.library.selected_index[INK_LIBRARY_TAB_ALL] == 1U
        && !runtime.force_full_refresh_on_next_render;
}

static bool reader_reenter_preserves_state_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    state->ui.library.active_tab = INK_LIBRARY_TAB_FAVORITES;
    state->ui.library.focus = INK_LIBRARY_FOCUS_POPUP;
    state->ui.library.popup_open = true;
    state->ui.library.popup_action_index = 1U;
    state->ui.library.selected_index[INK_LIBRARY_TAB_FAVORITES] = 2U;

    if (!ink_system_runtime_switch_now(&runtime, launcher)
        || !ink_system_runtime_switch_now(&runtime, reader)) {
        return false;
    }

    return state->initialized
        && state->ui.shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && state->ui.library.active_tab == INK_LIBRARY_TAB_FAVORITES
        && state->ui.library.focus == INK_LIBRARY_FOCUS_POPUP
        && state->ui.library.popup_open
        && state->ui.library.popup_action_index == 1U
        && state->ui.library.selected_index[INK_LIBRARY_TAB_FAVORITES] == 2U;
}

static bool reader_in_page_input_does_not_force_full_refresh_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_event_t confirm_event = {
        .kind = INK_APP_EVENT_BUTTON_CONFIRM,
    };
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    runtime.force_full_refresh_on_next_render = false;
    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.shell.full_refresh_requested = false;
    state->ui.reader_session.active = true;
    state->ui.reader_session.xtc_active = true;
    state->ui.reader_session.current_page = 3U;
    state->ui.reader_session.total_pages = 10U;

    if (!reader->input(&runtime, reader, &confirm_event)) {
        return false;
    }

    return state->ui.reader_menu.open
        && !runtime.force_full_refresh_on_next_render;
}

static bool reader_confirm_long_press_requests_white_refresh_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_event_t snapshot_event = {
        .kind = INK_APP_EVENT_BUTTON_SNAPSHOT,
        .event_ms = 600U,
    };
    ink_button_snapshot_t snapshot = {0};
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    runtime.force_full_refresh_on_next_render = false;
    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.shell.full_refresh_requested = false;
    state->ui.reader_session.active = true;
    state->ui.reader_session.xtc_active = true;
    state->ui.reader_session.current_page = 3U;
    state->ui.reader_session.total_pages = 10U;

    snapshot.stable_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_CONFIRM);
    snapshot.held_duration_ms[INK_RAW_BUTTON_CONFIRM] = INK_READER_CONFIRM_LONG_PRESS_MS + 20U;
    snapshot_event.payload = &snapshot;

    if (!reader->input(&runtime, reader, &snapshot_event)) {
        return false;
    }

    return state->ui.lab.reader_white_refresh_pending
        && !state->ui.reader_menu.open
        && !runtime.force_full_refresh_on_next_render;
}

static bool reader_nav_press_event_starts_hold_pending_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_event_t press_event = {
        .kind = INK_APP_EVENT_NAV_NEXT,
        .event_ms = 100U,
    };
    ink_button_snapshot_t press_snapshot = {0};
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.shell.full_refresh_requested = false;
    state->ui.reader_session.active = true;
    state->ui.reader_session.xtc_active = true;
    state->ui.reader_session.current_page = 3U;
    state->ui.reader_session.total_pages = 10U;

    press_snapshot.pressed_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    press_snapshot.stable_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    press_event.payload = &press_snapshot;

    if (reader->input(&runtime, reader, &press_event)) {
        return false;
    }

    return state->ui.reader_nav_pending
        && state->ui.reader_nav_pending_dir == INK_FAST_BROWSE_DIR_FORWARD
        && state->ui.reader_session.current_page == 3U
        && !runtime.force_full_refresh_on_next_render;
}

static bool reader_nav_long_press_repeats_real_page_turns_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_event_t press_event = {
        .kind = INK_APP_EVENT_NAV_NEXT,
        .event_ms = 100U,
    };
    ink_app_event_t hold_event = {
        .kind = INK_APP_EVENT_BUTTON_SNAPSHOT,
        .event_ms = INK_FAST_BROWSE_ENTER_MS + 140U,
    };
    ink_app_event_t release_event = {
        .kind = INK_APP_EVENT_BUTTON_SNAPSHOT,
        .event_ms = INK_FAST_BROWSE_ENTER_MS + 220U,
    };
    ink_button_snapshot_t press_snapshot = {0};
    ink_button_snapshot_t hold_snapshot = {0};
    ink_button_snapshot_t release_snapshot = {0};
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.shell.full_refresh_requested = false;
    state->ui.reader_session.active = true;
    state->ui.reader_session.xtc_active = true;
    state->ui.reader_session.current_page = 3U;
    state->ui.reader_session.total_pages = 10U;

    press_snapshot.pressed_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    press_snapshot.stable_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    press_snapshot.held_duration_ms[INK_RAW_BUTTON_RIGHT] = 0U;
    press_event.payload = &press_snapshot;

    if (reader->input(&runtime, reader, &press_event)) {
        return false;
    }
    if (!state->ui.reader_nav_pending
        || state->ui.reader_session.current_page != 3U) {
        return false;
    }

    hold_snapshot.stable_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    hold_snapshot.held_duration_ms[INK_RAW_BUTTON_RIGHT] = INK_FAST_BROWSE_ENTER_MS + 20U;
    hold_event.payload = &hold_snapshot;

    if (!reader->input(&runtime, reader, &hold_event)) {
        return false;
    }
    if (state->ui.reader_session.current_page != 4U
        || !state->ui.fast_browse.active
        || state->ui.reader_fast_full_commit_pending) {
        return false;
    }

    state->ui.buttons.is_down[INK_RUNTIME_SHELL_BUTTON_RIGHT] = true;
    state->ui.buttons.held_ms[INK_RUNTIME_SHELL_BUTTON_RIGHT] = INK_FAST_BROWSE_ENTER_MS + 260U;
    if (!reader->tick(&runtime, reader, INK_FAST_BROWSE_ENTER_MS + 420U)) {
        return false;
    }
    if (state->ui.reader_session.current_page != 5U) {
        return false;
    }

    release_snapshot.released_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    release_event.payload = &release_snapshot;
    if (!reader->input(&runtime, reader, &release_event)) {
        return false;
    }
    if (state->ui.fast_browse.active || state->ui.reader_nav_pending) {
        return false;
    }

    state->ui.buttons.is_down[INK_RUNTIME_SHELL_BUTTON_RIGHT] = false;
    state->ui.buttons.held_ms[INK_RUNTIME_SHELL_BUTTON_RIGHT] = 0U;
    if (reader->tick(&runtime, reader, INK_FAST_BROWSE_ENTER_MS + 700U)) {
        return false;
    }

    return state->ui.reader_session.current_page == 5U
        && !runtime.force_full_refresh_on_next_render;
}

static bool reader_snapshot_updates_button_state_for_hold_tick_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_event_t press_event = {
        .kind = INK_APP_EVENT_BUTTON_SNAPSHOT,
        .event_ms = 100U,
    };
    ink_button_snapshot_t press_snapshot = {0};
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.shell.full_refresh_requested = false;
    state->ui.reader_session.active = true;
    state->ui.reader_session.xtc_active = true;
    state->ui.reader_session.current_page = 3U;
    state->ui.reader_session.total_pages = 10U;

    press_snapshot.pressed_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    press_snapshot.stable_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    press_snapshot.held_duration_ms[INK_RAW_BUTTON_RIGHT] = 0U;
    press_event.payload = &press_snapshot;

    if (reader->input(&runtime, reader, &press_event)) {
        return false;
    }

    return state->ui.buttons.is_down[INK_RUNTIME_SHELL_BUTTON_RIGHT]
        && state->ui.buttons.was_pressed[INK_RUNTIME_SHELL_BUTTON_RIGHT]
        && state->ui.buttons.held_ms[INK_RUNTIME_SHELL_BUTTON_RIGHT] == 0U;
}

static bool reader_enter_initial_full_refresh_is_one_shot_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_render_model_t model;
    ink_app_event_t next_event = {
        .kind = INK_APP_EVENT_NAV_NEXT,
    };
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    if (!reader->render(&runtime, reader, &model) || !model.request_full_refresh) {
        return false;
    }
    if (!reader->render(&runtime, reader, &model) || model.request_full_refresh) {
        return false;
    }

    state->ui.shell.full_refresh_requested = false;
    state->ui.library.active_tab = INK_LIBRARY_TAB_ALL;
    state->ui.library.focus = INK_LIBRARY_FOCUS_ITEMS;
    state->ui.browser.entry_count = 2U;
    state->ui.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    state->ui.browser.entries[1].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(state->ui.browser.entries[0].name, sizeof(state->ui.browser.entries[0].name), "%s", "A.XTC");
    snprintf(state->ui.browser.entries[1].name, sizeof(state->ui.browser.entries[1].name), "%s", "B.XTC");

    if (!reader->input(&runtime, reader, &next_event)) {
        return false;
    }

    return state->ui.library.selected_index[INK_LIBRARY_TAB_ALL] == 1U
        && !runtime.force_full_refresh_on_next_render;
}

static bool reader_internal_open_does_not_force_full_refresh_self_test(void)
{
    ink_runtime_shell_t shell;

    ink_runtime_shell_init(&shell);
    shell.full_refresh_requested = false;
    if (reader_transition_requires_full_refresh(
            INK_RUNTIME_SHELL_PAGE_LIBRARY,
            INK_RUNTIME_SHELL_PAGE_READER,
            &shell)) {
        return false;
    }

    shell.full_refresh_requested = true;
    if (!reader_transition_requires_full_refresh(
            INK_RUNTIME_SHELL_PAGE_READER,
            INK_RUNTIME_SHELL_PAGE_LIBRARY,
            &shell)) {
        return false;
    }

    return true;
}

static bool reader_opening_blocks_non_back_input_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_event_t next_event = {
        .kind = INK_APP_EVENT_NAV_NEXT,
        .event_ms = 50U,
    };
    ink_app_event_t back_event = {
        .kind = INK_APP_EVENT_BUTTON_BACK,
        .event_ms = 60U,
    };
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.reader_opening = true;
    snprintf(state->pending_open_path, sizeof(state->pending_open_path), "%s", "/sdcard/books/A.XTC");

    if (!reader->input(&runtime, reader, &next_event)) {
        return false;
    }
    if (!state->ui.reader_opening || state->ui.shell.page != INK_RUNTIME_SHELL_PAGE_READER) {
        return false;
    }

    if (!reader->input(&runtime, reader, &back_event)) {
        return false;
    }

    return !state->ui.reader_opening
        && state->pending_open_path[0] == '\0'
        && state->ui.shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && runtime.force_full_refresh_on_next_render;
}

static bool reader_hold_release_invalidates_queued_render_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_system_services_t services;
    ink_app_event_t release_event = {
        .kind = INK_APP_EVENT_BUTTON_SNAPSHOT,
        .event_ms = 400U,
    };
    ink_button_snapshot_t release_snapshot = {0};
    ink_display_request_t request = {0};
    ink_display_request_t claimed = {0};
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();
    bool ok = false;

    memset(state, 0, sizeof(*state));
    memset(&services, 0, sizeof(services));
    ink_system_runtime_init(&runtime);
    ink_system_runtime_bind_services(&runtime, &services);
    ink_display_mailbox_init(&services.mailbox, NULL, NULL, NULL, NULL, NULL);
    if (!ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.reader_session.active = true;
    state->ui.reader_session.xtc_active = true;
    state->ui.reader_session.current_page = 5U;
    state->ui.reader_session.total_pages = 100U;
    state->ui.fast_browse.active = true;
    state->ui.reader_hold_navigation_active = true;
    state->ui.fast_browse.direction = INK_FAST_BROWSE_DIR_FORWARD;

    request.page = INK_RUNTIME_SHELL_PAGE_READER;
    (void)ink_display_mailbox_submit(&services.mailbox, &request);

    release_snapshot.released_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    release_event.payload = &release_snapshot;
    if (!reader->input(&runtime, reader, &release_event)) {
        return false;
    }

    ok = !state->ui.fast_browse.active
        && !state->ui.reader_hold_navigation_active
        && ink_display_mailbox_is_idle(&services.mailbox)
        && !ink_display_mailbox_try_claim_latest(&services.mailbox, &claimed);
    return ok;
}

static bool reader_tick_uses_latest_button_state_to_stop_hold_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_system_services_t services;
    ink_reader_app_state_t *state = &s_reader_state;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    memset(state, 0, sizeof(*state));
    memset(&services, 0, sizeof(services));
    ink_system_runtime_init(&runtime);
    ink_system_runtime_bind_services(&runtime, &services);
    ink_display_mailbox_init(&services.mailbox, NULL, NULL, NULL, NULL, NULL);
    if (!ink_system_runtime_register_app(&runtime, reader)
        || !ink_system_runtime_set_active_app(&runtime, reader)) {
        return false;
    }

    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.reader_session.active = true;
    state->ui.reader_session.xtc_active = true;
    state->ui.reader_session.current_page = 8U;
    state->ui.reader_session.total_pages = 100U;
    state->ui.fast_browse.active = true;
    state->ui.fast_browse.direction = INK_FAST_BROWSE_DIR_FORWARD;
    state->ui.reader_hold_navigation_active = true;
    state->ui.buttons.is_down[INK_RUNTIME_SHELL_BUTTON_RIGHT] = true;
    services.latest_buttons.is_down[INK_RUNTIME_SHELL_BUTTON_RIGHT] = false;
    services.latest_buttons_ms = 500U;

    if (!reader->tick(&runtime, reader, 560U)) {
        return false;
    }

    return !state->ui.fast_browse.active
        && !state->ui.reader_hold_navigation_active
        && state->ui.reader_session.current_page == 8U;
}

static bool reader_latest_button_state_uses_processing_time_self_test(void)
{
    ink_ui_model_t ui;
    ink_runtime_shell_button_state_t buttons;

    memset(&ui, 0, sizeof(ui));
    memset(&buttons, 0, sizeof(buttons));
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    ui.reader_session.active = true;
    ui.reader_session.xtc_active = true;
    ui.reader_session.total_pages = 100U;
    ui.reader_nav_pending = true;
    ui.reader_nav_pending_dir = INK_FAST_BROWSE_DIR_FORWARD;
    ui.reader_nav_pending_start_ms = 1000U;
    buttons.is_down[INK_RUNTIME_SHELL_BUTTON_RIGHT] = true;

    return !ink_app_drive_reader_nav_hold_for_model(&ui, &buttons, false, 1300U)
        && ink_app_drive_reader_nav_hold_for_model(&ui, &buttons, true, 1300U)
        && ui.fast_browse.active;
}
