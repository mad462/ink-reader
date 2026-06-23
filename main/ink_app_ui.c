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
static bool toggle_reader_auto_flip_stress(ink_ui_model_t *model, bool *dirty_out);
static bool advance_reader_auto_flip_stress_impl(ink_ui_model_t *model);
static bool app_open_selected_browser_book(ink_ui_model_t *model);
static bool app_open_resume_book(ink_ui_model_t *model);
static bool app_return_to_library(ink_ui_model_t *model);
static bool app_library_move_previous(ink_ui_model_t *model);
static bool app_library_move_next(ink_ui_model_t *model);
static bool app_handle_library_command(ink_ui_model_t *model, ink_runtime_shell_command_t command);

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

    if (!ink_reader_session_open_xtc(
            &model->reader_session,
            model->browser.selected_file_path,
            &model->app_state)) {
        return false;
    }

    model->shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model->reader_nav_pending = false;
    model->reader_nav_pending_start_ms = 0U;
    ink_app_clear_fast_browse(model);
    model->lab.auto_flip_stress_enabled = false;
    model->lab.force_full_refresh = false;
    model->lab.reader_white_refresh_pending = false;
    (void)ink_runtime_shell_set_resume_available(
        &model->shell,
        ink_app_should_auto_resume_reader(&model->app_state));
    ink_reader_session_prefetch_next(&model->reader_session, NULL, NULL);
    return true;
}

static bool app_open_resume_book(ink_ui_model_t *model)
{
    if (model == NULL || !ink_runtime_shell_is_resume_selected(&model->shell)) {
        return false;
    }

    if (!ink_app_load_book_from_state(model)) {
        return false;
    }

    model->reader_nav_pending = false;
    model->reader_nav_pending_start_ms = 0U;
    ink_app_clear_fast_browse(model);
    model->lab.auto_flip_stress_enabled = false;
    model->lab.force_full_refresh = false;
    model->lab.reader_white_refresh_pending = false;
    ink_reader_session_prefetch_next(&model->reader_session, NULL, NULL);
    return true;
}

static bool app_return_to_library(ink_ui_model_t *model)
{
    if (model == NULL) {
        return false;
    }

    model->reader_fast_full_commit_pending = false;
    ink_reader_session_close(&model->reader_session);
    ink_app_clear_fast_browse(model);
    model->reader_nav_pending = false;
    model->reader_nav_pending_start_ms = 0U;
    model->lab.auto_flip_stress_enabled = false;
    model->lab.force_full_refresh = false;
    model->lab.reader_white_refresh_pending = false;
    model->shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    (void)ink_runtime_shell_set_resume_available(
        &model->shell,
        ink_app_should_auto_resume_reader(&model->app_state));
    model->shell.resume_selected = model->shell.can_resume_book;
    model->shell.full_refresh_requested = true;
    return true;
}

static bool app_library_move_previous(ink_ui_model_t *model)
{
    if (model == NULL) {
        return false;
    }

    if (model->shell.can_resume_book && !model->shell.resume_selected && model->browser.selected_index == 0U) {
        model->shell.resume_selected = true;
        return true;
    }

    if (model->shell.resume_selected) {
        return false;
    }

    return ink_file_browser_move_previous(&model->browser);
}

static bool app_library_move_next(ink_ui_model_t *model)
{
    if (model == NULL) {
        return false;
    }

    if (model->shell.resume_selected) {
        if (model->browser.entry_count == 0U) {
            return false;
        }
        model->shell.resume_selected = false;
        return true;
    }

    return ink_file_browser_move_next(&model->browser);
}

static bool app_handle_library_command(ink_ui_model_t *model, ink_runtime_shell_command_t command)
{
    bool entered_directory = false;
    bool selected_file = false;

    if (model == NULL) {
        return false;
    }

    switch (command) {
        case INK_RUNTIME_SHELL_COMMAND_BACK:
            model->shell.resume_selected = false;
            return ink_file_browser_go_parent(&model->browser);
        case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
            if (app_open_resume_book(model)) {
                return true;
            }
            if (ink_file_browser_confirm(&model->browser, &entered_directory, &selected_file) != ESP_OK) {
                return false;
            }
            if (selected_file) {
                return app_open_selected_browser_book(model);
            }
            return entered_directory;
        case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
            return app_library_move_previous(model);
        case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
            return app_library_move_next(model);
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
        switch (command) {
            case INK_RUNTIME_SHELL_COMMAND_BACK:
                model->lab.auto_flip_stress_enabled = false;
                return app_return_to_library(model);
            case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
                return false;
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
    model.shell.can_resume_book = true;
    model.shell.resume_selected = true;
    model.browser.entry_count = 2U;
    model.browser.selected_index = 0U;
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT)) {
        return false;
    }
    if (model.shell.resume_selected) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS)) {
        return false;
    }
    return model.shell.resume_selected;
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

bool ink_app_ui_self_test(void)
{
    return app_input_event_self_test()
        && app_lab_command_self_test()
        && app_library_command_self_test()
        && app_grid_compare_self_test()
        && app_auto_repeat_probe_self_test()
        && app_auto_flip_stress_self_test()
        && app_reader_white_refresh_self_test()
        && app_reader_confirm_ignored_self_test()
        && app_reader_back_returns_library_fast_commit_self_test();
}
