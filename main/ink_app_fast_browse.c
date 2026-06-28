#include "ink_app_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "ink_app_boot.h"

static const char *TAG = "ink_reader";

static uint32_t fast_browse_step_size_from_hold_ms(uint32_t held_ms);
static uint32_t fast_browse_step_interval_ms(uint32_t held_ms);
static bool fast_browse_begin(ink_ui_model_t *model, ink_fast_browse_dir_t direction, uint32_t now_ms);
static bool fast_browse_apply_step_with_hold_ms(ink_ui_model_t *model, uint32_t held_ms, uint32_t now_ms);
static bool fast_browse_cancel(ink_ui_model_t *model);
static bool button_state_is_down(const ink_runtime_shell_button_state_t *buttons, ink_logical_button_t button);
static bool app_fast_browse_state_self_test(void);
static bool app_fast_browse_step_uses_single_jump_self_test(void);

size_t ink_app_reader_total_pages(const ink_ui_model_t *model)
{
    if (model == NULL) {
        return 0U;
    }
    return ink_reader_session_is_xtc_active(&model->reader_session)
        ? model->reader_session.total_pages
        : 0U;
}

size_t ink_app_reader_current_page(const ink_ui_model_t *model)
{
    if (model == NULL) {
        return 0U;
    }
    return ink_reader_session_is_xtc_active(&model->reader_session)
        ? model->reader_session.current_page
        : 0U;
}

void ink_app_clear_fast_browse(ink_ui_model_t *model)
{
    if (model == NULL) {
        return;
    }
    model->reader_hold_navigation_active = false;
    memset(&model->fast_browse, 0, sizeof(model->fast_browse));
}

static uint32_t fast_browse_step_size_from_hold_ms(uint32_t held_ms)
{
    (void)held_ms;
    return 5U;
}

static uint32_t fast_browse_step_interval_ms(uint32_t held_ms)
{
    if (held_ms < 5000U) {
        return 360U;
    }
    if (held_ms < 10000U) {
        return 300U;
    }
    if (held_ms < 20000U) {
        return 240U;
    }
    return 200U;
}

static bool fast_browse_begin(ink_ui_model_t *model, ink_fast_browse_dir_t direction, uint32_t now_ms)
{
    const size_t total_pages = ink_app_reader_total_pages(model);
    if (model == NULL || total_pages == 0U) {
        return false;
    }

    model->fast_browse.active = true;
    model->fast_browse.overlay_mode = true;
    model->fast_browse.dirty = true;
    model->fast_browse.origin_page = ink_app_reader_current_page(model);
    model->fast_browse.target_page = model->fast_browse.origin_page;
    model->fast_browse.visible_page = model->fast_browse.origin_page;
    model->fast_browse.has_visible_page = true;
    model->fast_browse.commit_fast_full_pending = false;
    model->fast_browse.release_armed = false;
    model->fast_browse.total_pages = total_pages;
    model->fast_browse.direction = direction;
    model->fast_browse.hold_start_ms = now_ms;
    model->fast_browse.last_step_ms = now_ms;
    model->reader_hold_navigation_active = false;
    ESP_LOGI(
        TAG,
        "fast browse begin page=%u total=%u dir=%s mode=overlay",
        (unsigned)(model->fast_browse.origin_page + 1U),
        (unsigned)total_pages,
        direction == INK_FAST_BROWSE_DIR_FORWARD ? "forward" : "backward");
    return true;
}

static bool fast_browse_apply_step_with_hold_ms(ink_ui_model_t *model, uint32_t held_ms, uint32_t now_ms)
{
    const bool first_step = model != NULL
        && model->fast_browse.active
        && model->fast_browse.last_step_ms == model->fast_browse.hold_start_ms;
    const uint32_t step_size = fast_browse_step_size_from_hold_ms(held_ms);
    uint32_t step_interval_ms;
    bool dirty = false;

    if (model == NULL || !model->fast_browse.active || model->fast_browse.total_pages == 0U) {
        return false;
    }

    step_interval_ms = fast_browse_step_interval_ms(held_ms);
    if (!first_step && (uint32_t)(now_ms - model->fast_browse.last_step_ms) < step_interval_ms) {
        return false;
    }

    model->fast_browse.last_step_ms = now_ms;
    model->lab.auto_flip_stress_enabled = false;
    dirty = ink_reader_session_skip_pages(
        &model->reader_session,
        model->fast_browse.direction == INK_FAST_BROWSE_DIR_FORWARD
            ? (int32_t)step_size
            : -(int32_t)step_size,
        &model->app_state);

    if (!dirty) {
        return false;
    }

    model->fast_browse.target_page = model->reader_session.current_page;
    model->fast_browse.dirty = true;
    (void)ink_app_persist_state(&model->app_state);
    ink_reader_session_prefetch_next(&model->reader_session, NULL, NULL);
    ESP_LOGI(
        TAG,
        "reader hold turn page=%u held=%ums step=%u interval=%ums",
        (unsigned)(model->reader_session.current_page + 1U),
        (unsigned)held_ms,
        (unsigned)step_size,
        (unsigned)step_interval_ms);
    return true;
}

static bool fast_browse_cancel(ink_ui_model_t *model)
{
    if (model == NULL || !model->fast_browse.active) {
        return false;
    }

    ESP_LOGI(
        TAG,
        "reader hold stop page=%u origin=%u",
        (unsigned)(model->reader_session.current_page + 1U),
        (unsigned)(model->fast_browse.origin_page + 1U));
    model->reader_nav_pending = false;
    model->reader_nav_pending_start_ms = 0U;
    model->reader_fast_full_commit_pending = true;
    ink_app_clear_fast_browse(model);
    return true;
}

void ink_app_fast_browse_note_preview_landed(ink_ui_model_t *model)
{
    if (model == NULL || !model->fast_browse.active || !model->fast_browse.overlay_mode) {
        return;
    }

    model->fast_browse.visible_page = model->fast_browse.target_page;
    model->fast_browse.has_visible_page = true;
}

static bool button_state_is_down(const ink_runtime_shell_button_state_t *buttons, ink_logical_button_t button)
{
    int shell_button = -1;

    if (buttons == NULL) {
        return false;
    }

    switch (button) {
        case INK_LOGICAL_BUTTON_BACK:
            shell_button = INK_RUNTIME_SHELL_BUTTON_BACK;
            break;
        case INK_LOGICAL_BUTTON_CONFIRM:
            shell_button = INK_RUNTIME_SHELL_BUTTON_CONFIRM;
            break;
        case INK_LOGICAL_BUTTON_NAV_PREVIOUS:
            shell_button = INK_RUNTIME_SHELL_BUTTON_LEFT;
            break;
        case INK_LOGICAL_BUTTON_NAV_NEXT:
            shell_button = INK_RUNTIME_SHELL_BUTTON_RIGHT;
            break;
        case INK_LOGICAL_BUTTON_POWER:
            shell_button = INK_RUNTIME_SHELL_BUTTON_POWER;
            break;
        default:
            return false;
    }

    return shell_button >= 0
        && shell_button < INK_RUNTIME_SHELL_BUTTON_COUNT
        && buttons->is_down[shell_button];
}

bool ink_app_process_reader_xtc_buttons(
    ink_app_context_t *app,
    const ink_button_snapshot_t *snapshot,
    uint32_t event_ms,
    ink_runtime_shell_command_t *command_out)
{
    if (app == NULL) {
        return false;
    }

    return ink_app_process_reader_xtc_buttons_for_model(
        &app->model,
        snapshot,
        event_ms,
        command_out);
}

bool ink_app_process_reader_xtc_buttons_for_model(
    ink_ui_model_t *model,
    const ink_button_snapshot_t *snapshot,
    uint32_t event_ms,
    ink_runtime_shell_command_t *command_out)
{
    if (command_out != NULL) {
        *command_out = INK_RUNTIME_SHELL_COMMAND_NONE;
    }
    if (model == NULL) {
        return false;
    }

    if (model->fast_browse.active) {
        const ink_logical_button_t dir_button = model->fast_browse.direction == INK_FAST_BROWSE_DIR_FORWARD
            ? INK_LOGICAL_BUTTON_NAV_NEXT
            : INK_LOGICAL_BUTTON_NAV_PREVIOUS;
        if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_BACK)) {
            return fast_browse_cancel(model);
        } else if (ink_button_snapshot_was_released(snapshot, dir_button)) {
            return fast_browse_cancel(model);
        }
        return false;
    }

    if (model->reader_nav_pending) {
        const ink_logical_button_t pending_button = model->reader_nav_pending_dir == INK_FAST_BROWSE_DIR_FORWARD
            ? INK_LOGICAL_BUTTON_NAV_NEXT
            : INK_LOGICAL_BUTTON_NAV_PREVIOUS;
        if (snapshot != NULL && ink_button_snapshot_was_released(snapshot, pending_button)) {
            model->reader_nav_pending = false;
            model->reader_nav_pending_start_ms = 0U;
            if (command_out != NULL) {
                *command_out = model->reader_nav_pending_dir == INK_FAST_BROWSE_DIR_FORWARD
                    ? INK_RUNTIME_SHELL_COMMAND_NAV_NEXT
                    : INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS;
            }
            return true;
        }
        return false;
    }

    if (model->reader_confirm_pending) {
        const uint32_t held_ms = snapshot != NULL
            ? ink_button_snapshot_get_held_ms(snapshot, INK_LOGICAL_BUTTON_CONFIRM)
            : 0U;
        if (snapshot != NULL
            && ink_button_snapshot_is_pressed(snapshot, INK_LOGICAL_BUTTON_CONFIRM)
            && held_ms >= INK_READER_CONFIRM_LONG_PRESS_MS) {
            model->reader_confirm_pending = false;
            model->reader_confirm_pending_start_ms = 0U;
            return ink_tuning_lab_request_reader_white_refresh(&model->lab);
        }
        if (snapshot != NULL
            && ink_button_snapshot_was_released(snapshot, INK_LOGICAL_BUTTON_CONFIRM)) {
            model->reader_confirm_pending = false;
            model->reader_confirm_pending_start_ms = 0U;
            if (command_out != NULL) {
                *command_out = INK_RUNTIME_SHELL_COMMAND_CONFIRM;
            }
            return true;
        }
        return false;
    }

    if (snapshot != NULL && ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_NAV_PREVIOUS)) {
        model->reader_nav_pending = true;
        model->reader_nav_pending_dir = INK_FAST_BROWSE_DIR_BACKWARD;
        model->reader_nav_pending_start_ms = event_ms;
        return false;
    }
    if (snapshot != NULL && ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_NAV_NEXT)) {
        model->reader_nav_pending = true;
        model->reader_nav_pending_dir = INK_FAST_BROWSE_DIR_FORWARD;
        model->reader_nav_pending_start_ms = event_ms;
        return false;
    }
    if (snapshot != NULL && ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_BACK)) {
        if (command_out != NULL) {
            *command_out = INK_RUNTIME_SHELL_COMMAND_BACK;
        }
        return true;
    }
    if (snapshot != NULL && ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_CONFIRM)) {
        model->reader_confirm_pending = true;
        model->reader_confirm_pending_start_ms = event_ms;
        return false;
    }
    return false;
}

bool ink_app_fast_browse_handle_idle(
    ink_app_context_t *app,
    uint32_t now_ms,
    ink_runtime_shell_command_t *command_out)
{
    if (app == NULL) {
        return false;
    }

    return ink_app_fast_browse_handle_idle_for_model(
        &app->model,
        &app->model.buttons,
        now_ms,
        command_out);
}

bool ink_app_fast_browse_handle_idle_for_model(
    ink_ui_model_t *model,
    const ink_runtime_shell_button_state_t *buttons,
    uint32_t now_ms,
    ink_runtime_shell_command_t *command_out)
{
    if (command_out != NULL) {
        *command_out = INK_RUNTIME_SHELL_COMMAND_NONE;
    }
    if (model == NULL) {
        return false;
    }

    if (model->shell.page != INK_RUNTIME_SHELL_PAGE_READER
        || !ink_reader_session_is_xtc_active(&model->reader_session)) {
        return false;
    }

    if (model->reader_confirm_pending
        && model->reader_confirm_pending_start_ms != 0U
        && button_state_is_down(buttons, INK_LOGICAL_BUTTON_CONFIRM)
        && (uint32_t)(now_ms - model->reader_confirm_pending_start_ms) >= INK_READER_CONFIRM_LONG_PRESS_MS) {
        model->reader_confirm_pending = false;
        model->reader_confirm_pending_start_ms = 0U;
        return ink_tuning_lab_request_reader_white_refresh(&model->lab);
    }

    return false;
}

bool ink_app_drive_reader_nav_hold_for_model(
    ink_ui_model_t *model,
    const ink_runtime_shell_button_state_t *buttons,
    bool display_idle,
    uint32_t now_ms)
{
    ink_logical_button_t dir_button;

    if (model == NULL
        || buttons == NULL
        || model->shell.page != INK_RUNTIME_SHELL_PAGE_READER
        || !ink_reader_session_is_xtc_active(&model->reader_session)) {
        return false;
    }

    if (model->fast_browse.active) {
        dir_button = model->fast_browse.direction == INK_FAST_BROWSE_DIR_FORWARD
            ? INK_LOGICAL_BUTTON_NAV_NEXT
            : INK_LOGICAL_BUTTON_NAV_PREVIOUS;
        if (!button_state_is_down(buttons, dir_button)) {
            return fast_browse_cancel(model);
        }
        return fast_browse_apply_step_with_hold_ms(
            model,
            (uint32_t)(now_ms - model->fast_browse.hold_start_ms),
            now_ms);
    }

    (void)display_idle;
    return false;
}

bool ink_app_maybe_start_reader_nav_hold_from_snapshot_for_model(
    ink_ui_model_t *model,
    const ink_button_snapshot_t *snapshot,
    bool display_idle,
    uint32_t now_ms)
{
    ink_logical_button_t dir_button;
    uint32_t held_ms;

    if (model == NULL
        || snapshot == NULL
        || model->shell.page != INK_RUNTIME_SHELL_PAGE_READER
        || !ink_reader_session_is_xtc_active(&model->reader_session)
        || model->fast_browse.active
        || !model->reader_nav_pending
        || model->reader_nav_pending_start_ms == 0U
        || !display_idle) {
        return false;
    }

    dir_button = model->reader_nav_pending_dir == INK_FAST_BROWSE_DIR_FORWARD
        ? INK_LOGICAL_BUTTON_NAV_NEXT
        : INK_LOGICAL_BUTTON_NAV_PREVIOUS;
    if (!ink_button_snapshot_is_pressed(snapshot, dir_button)) {
        return false;
    }
    held_ms = ink_button_snapshot_get_held_ms(snapshot, dir_button);
    if (held_ms < INK_FAST_BROWSE_ENTER_MS) {
        return false;
    }

    model->reader_nav_pending = false;
    model->reader_nav_pending_start_ms = 0U;
    if (!fast_browse_begin(model, model->reader_nav_pending_dir, now_ms)) {
        return false;
    }
    return fast_browse_apply_step_with_hold_ms(model, held_ms, now_ms);
}

bool ink_app_fast_browse_self_test(void)
{
    return app_fast_browse_state_self_test()
        && app_fast_browse_step_uses_single_jump_self_test();
}

static bool app_fast_browse_state_self_test(void)
{
    ink_ui_model_t model;
    memset(&model, 0, sizeof(model));
    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 99U;
    model.reader_session.total_pages = 1000U;

    if (!fast_browse_begin(&model, INK_FAST_BROWSE_DIR_FORWARD, 1200U)) {
        return false;
    }
    if (!model.fast_browse.active || model.fast_browse.origin_page != 99U || model.fast_browse.target_page != 99U) {
        return false;
    }
    if (fast_browse_step_size_from_hold_ms(200U) != 5U
        || fast_browse_step_size_from_hold_ms(7000U) != 5U
        || fast_browse_step_size_from_hold_ms(15000U) != 5U
        || fast_browse_step_size_from_hold_ms(25000U) != 5U) {
        return false;
    }
    if (fast_browse_step_interval_ms(200U) != 360U
        || fast_browse_step_interval_ms(7000U) != 300U
        || fast_browse_step_interval_ms(15000U) != 240U
        || fast_browse_step_interval_ms(25000U) != 200U) {
        return false;
    }
    if (!fast_browse_apply_step_with_hold_ms(&model, 0U, 1200U)
        || model.fast_browse.target_page != 104U
        || model.reader_session.current_page != 104U) {
        return false;
    }
    if (!model.fast_browse.overlay_mode
        || !model.fast_browse.dirty
        || !model.fast_browse.has_visible_page
        || model.fast_browse.visible_page != 99U
        || model.reader_hold_navigation_active) {
        return false;
    }
    if (fast_browse_apply_step_with_hold_ms(&model, 100U, 1260U)) {
        return false;
    }
    if (!fast_browse_apply_step_with_hold_ms(&model, 6000U, 1400U)
        || model.fast_browse.target_page != 109U
        || model.reader_session.current_page != 109U) {
        return false;
    }
    if (!model.fast_browse.dirty
        || model.fast_browse.visible_page != 99U
        || !model.fast_browse.has_visible_page) {
        return false;
    }
    if (!fast_browse_cancel(&model) || model.fast_browse.active) {
        return false;
    }
    if (!model.reader_fast_full_commit_pending) {
        return false;
    }
    if (!fast_browse_begin(&model, INK_FAST_BROWSE_DIR_FORWARD, 2000U)) {
        return false;
    }
    model.buttons.is_down[INK_RUNTIME_SHELL_BUTTON_RIGHT] = true;
    if (!ink_app_drive_reader_nav_hold_for_model(&model, &model.buttons, false, 2225U)
        || model.reader_session.current_page != 114U) {
        return false;
    }
    ink_app_clear_fast_browse(&model);
    return !model.fast_browse.active;
}

static bool app_fast_browse_step_uses_single_jump_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 10U;
    model.reader_session.total_pages = 1000U;
    model.reader_session.xtc_book.opened = true;
    model.reader_session.xtc_book.current_page = 10U;
    model.reader_session.xtc_book.page_entry_count = 1000U;

    if (!fast_browse_begin(&model, INK_FAST_BROWSE_DIR_FORWARD, 1200U)) {
        return false;
    }
    if (!fast_browse_apply_step_with_hold_ms(&model, 0U, 1200U)) {
        return false;
    }

    return model.reader_session.current_page == 15U
        && model.fast_browse.target_page == 15U;
}
