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
static bool fast_browse_apply_step(ink_ui_model_t *model, const ink_button_snapshot_t *snapshot, uint32_t now_ms);
static bool fast_browse_finish_pending(ink_ui_model_t *model);
static bool button_state_is_down(const ink_runtime_shell_button_state_t *buttons, ink_logical_button_t button);
static bool app_fast_browse_state_self_test(void);

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
    memset(&model->fast_browse, 0, sizeof(model->fast_browse));
}

static uint32_t fast_browse_step_size_from_hold_ms(uint32_t held_ms)
{
    if (held_ms < 5000U) {
        return 1U;
    }
    if (held_ms < 10000U) {
        return 5U;
    }
    if (held_ms < 20000U) {
        return 10U;
    }
    return 20U;
}

static uint32_t fast_browse_step_interval_ms(uint32_t held_ms)
{
    if (held_ms < 5000U) {
        return 220U;
    }
    if (held_ms < 10000U) {
        return 180U;
    }
    if (held_ms < 20000U) {
        return 140U;
    }
    return 110U;
}

static bool fast_browse_begin(ink_ui_model_t *model, ink_fast_browse_dir_t direction, uint32_t now_ms)
{
    const size_t total_pages = ink_app_reader_total_pages(model);
    if (model == NULL || total_pages == 0U) {
        return false;
    }

    model->fast_browse.active = true;
    model->fast_browse.dirty = true;
    model->fast_browse.origin_page = ink_app_reader_current_page(model);
    model->fast_browse.target_page = model->fast_browse.origin_page;
    model->fast_browse.visible_page = model->fast_browse.origin_page;
    model->fast_browse.has_visible_page = false;
    model->fast_browse.commit_fast_full_pending = false;
    model->fast_browse.release_armed = false;
    model->fast_browse.total_pages = total_pages;
    model->fast_browse.direction = direction;
    model->fast_browse.hold_start_ms = now_ms;
    model->fast_browse.last_step_ms = now_ms;
    ESP_LOGI(
        TAG,
        "fast browse begin page=%u total=%u dir=%s",
        (unsigned)(model->fast_browse.origin_page + 1U),
        (unsigned)total_pages,
        direction == INK_FAST_BROWSE_DIR_FORWARD ? "forward" : "backward");
    return true;
}

static bool fast_browse_apply_step_with_hold_ms(ink_ui_model_t *model, uint32_t held_ms, uint32_t now_ms)
{
    const bool first_step = model != NULL
        && model->fast_browse.active
        && model->fast_browse.target_page == model->fast_browse.origin_page
        && model->fast_browse.last_step_ms == model->fast_browse.hold_start_ms;
    uint32_t step;
    uint32_t step_interval_ms;
    size_t next_page;

    if (model == NULL || !model->fast_browse.active || model->fast_browse.total_pages == 0U) {
        return false;
    }

    step_interval_ms = fast_browse_step_interval_ms(held_ms);
    if (!first_step && (uint32_t)(now_ms - model->fast_browse.last_step_ms) < step_interval_ms) {
        return false;
    }

    step = fast_browse_step_size_from_hold_ms(held_ms);
    next_page = model->fast_browse.target_page;
    if (model->fast_browse.direction == INK_FAST_BROWSE_DIR_FORWARD) {
        next_page = next_page + step >= model->fast_browse.total_pages
            ? model->fast_browse.total_pages - 1U
            : next_page + step;
    } else {
        next_page = step > next_page ? 0U : next_page - step;
    }

    model->fast_browse.last_step_ms = now_ms;
    if (next_page == model->fast_browse.target_page) {
        return false;
    }

    model->fast_browse.target_page = next_page;
    model->fast_browse.dirty = true;
    ESP_LOGI(
        TAG,
        "fast browse target page=%u held=%ums step=%u interval=%ums",
        (unsigned)(next_page + 1U),
        (unsigned)held_ms,
        (unsigned)step,
        (unsigned)step_interval_ms);
    return true;
}

static bool fast_browse_apply_step(ink_ui_model_t *model, const ink_button_snapshot_t *snapshot, uint32_t now_ms)
{
    uint32_t held_ms;

    if (model == NULL || snapshot == NULL || !model->fast_browse.active || model->fast_browse.total_pages == 0U) {
        return false;
    }

    held_ms = model->fast_browse.direction == INK_FAST_BROWSE_DIR_FORWARD
        ? ink_button_snapshot_get_held_ms(snapshot, INK_LOGICAL_BUTTON_NAV_NEXT)
        : ink_button_snapshot_get_held_ms(snapshot, INK_LOGICAL_BUTTON_NAV_PREVIOUS);
    return fast_browse_apply_step_with_hold_ms(model, held_ms, now_ms);
}

static bool fast_browse_finish_pending(ink_ui_model_t *model)
{
    bool dirty = false;

    if (model == NULL || !model->fast_browse.active) {
        return false;
    }

    if (model->fast_browse.cancel_pending) {
        ESP_LOGI(TAG, "fast browse cancel origin=%u", (unsigned)(model->fast_browse.origin_page + 1U));
        ink_app_clear_fast_browse(model);
        return true;
    }

    if (!model->fast_browse.commit_pending) {
        return false;
    }

    if (model->fast_browse.target_page != model->fast_browse.origin_page) {
        const size_t commit_page = model->fast_browse.target_page;
        model->reader_fast_full_commit_pending = true;
        if (ink_reader_session_is_xtc_active(&model->reader_session)) {
            dirty = ink_reader_session_jump_to_page(
                &model->reader_session,
                commit_page,
                &model->app_state);
            if (dirty) {
                (void)ink_app_persist_state(&model->app_state);
            }
        } else {
            dirty = false;
        }
        model->fast_browse.target_page = commit_page;
    } else {
        dirty = true;
    }

    ESP_LOGI(
        TAG,
        "fast browse commit target=%u visible=%u has_visible=%d ret=%d",
        (unsigned)(model->fast_browse.target_page + 1U),
        (unsigned)(model->fast_browse.visible_page + 1U),
        model->fast_browse.has_visible_page ? 1 : 0,
        dirty ? 1 : 0);
    ink_app_clear_fast_browse(model);
    return dirty;
}

void ink_app_fast_browse_note_preview_landed(ink_ui_model_t *model)
{
    if (model == NULL || !model->fast_browse.active) {
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
    ink_ui_model_t *model = &app->model;
    bool dirty = false;

    if (command_out != NULL) {
        *command_out = INK_RUNTIME_SHELL_COMMAND_NONE;
    }

    if (model->fast_browse.active) {
        const ink_logical_button_t dir_button = model->fast_browse.direction == INK_FAST_BROWSE_DIR_FORWARD
            ? INK_LOGICAL_BUTTON_NAV_NEXT
            : INK_LOGICAL_BUTTON_NAV_PREVIOUS;
        const bool dir_down = ink_button_snapshot_is_pressed(snapshot, dir_button);
        if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_BACK)) {
            model->fast_browse.cancel_pending = true;
        } else if (dir_down) {
            model->fast_browse.release_armed = true;
            dirty |= fast_browse_apply_step(model, snapshot, event_ms);
        } else if (model->fast_browse.release_armed
            && ink_button_snapshot_was_released(snapshot, dir_button)) {
            model->fast_browse.commit_pending = true;
            model->fast_browse.release_armed = false;
        }
        dirty |= fast_browse_finish_pending(model);
        return dirty;
    }

    if (model->reader_nav_pending) {
        const ink_logical_button_t pending_button = model->reader_nav_pending_dir == INK_FAST_BROWSE_DIR_FORWARD
            ? INK_LOGICAL_BUTTON_NAV_NEXT
            : INK_LOGICAL_BUTTON_NAV_PREVIOUS;
        const uint32_t held_ms = ink_button_snapshot_get_held_ms(snapshot, pending_button);
        if (ink_button_snapshot_is_pressed(snapshot, pending_button) && held_ms >= INK_FAST_BROWSE_ENTER_MS) {
            model->reader_nav_pending = false;
            model->reader_nav_pending_start_ms = 0U;
            dirty |= fast_browse_begin(model, model->reader_nav_pending_dir, event_ms);
            dirty |= fast_browse_apply_step(model, snapshot, event_ms);
            return dirty;
        }
        if (ink_button_snapshot_was_released(snapshot, pending_button)) {
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

    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_NAV_PREVIOUS)) {
        model->reader_nav_pending = true;
        model->reader_nav_pending_dir = INK_FAST_BROWSE_DIR_BACKWARD;
        model->reader_nav_pending_start_ms = event_ms;
        return false;
    }
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_NAV_NEXT)) {
        model->reader_nav_pending = true;
        model->reader_nav_pending_dir = INK_FAST_BROWSE_DIR_FORWARD;
        model->reader_nav_pending_start_ms = event_ms;
        return false;
    }
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_BACK)) {
        if (command_out != NULL) {
            *command_out = INK_RUNTIME_SHELL_COMMAND_BACK;
        }
        return true;
    }
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_CONFIRM)) {
        if (command_out != NULL) {
            *command_out = INK_RUNTIME_SHELL_COMMAND_CONFIRM;
        }
        return true;
    }
    return false;
}

bool ink_app_fast_browse_handle_idle(
    ink_app_context_t *app,
    uint32_t now_ms,
    ink_runtime_shell_command_t *command_out)
{
    ink_ui_model_t *model;

    if (command_out != NULL) {
        *command_out = INK_RUNTIME_SHELL_COMMAND_NONE;
    }
    if (app == NULL) {
        return false;
    }

    model = &app->model;
    if (model->shell.page != INK_RUNTIME_SHELL_PAGE_READER
        || !ink_reader_session_is_xtc_active(&model->reader_session)) {
        return false;
    }

    if (model->fast_browse.active) {
        const ink_logical_button_t dir_button = model->fast_browse.direction == INK_FAST_BROWSE_DIR_FORWARD
            ? INK_LOGICAL_BUTTON_NAV_NEXT
            : INK_LOGICAL_BUTTON_NAV_PREVIOUS;
        const bool dir_down = button_state_is_down(&model->buttons, dir_button);
        bool dirty = false;

        if (dir_down) {
            const uint32_t held_ms = (uint32_t)(now_ms - model->fast_browse.hold_start_ms);
            dirty |= fast_browse_apply_step_with_hold_ms(model, held_ms, now_ms);
        } else if (model->fast_browse.release_armed) {
            model->fast_browse.commit_pending = true;
            model->fast_browse.release_armed = false;
        }
        dirty |= fast_browse_finish_pending(model);
        return dirty;
    }

    if (model->reader_nav_pending
        && model->reader_nav_pending_start_ms != 0U
        && button_state_is_down(
            &model->buttons,
            model->reader_nav_pending_dir == INK_FAST_BROWSE_DIR_FORWARD
                ? INK_LOGICAL_BUTTON_NAV_NEXT
                : INK_LOGICAL_BUTTON_NAV_PREVIOUS)
        && (uint32_t)(now_ms - model->reader_nav_pending_start_ms) >= INK_FAST_BROWSE_ENTER_MS) {
        bool dirty;
        model->reader_nav_pending = false;
        model->reader_nav_pending_start_ms = 0U;
        dirty = fast_browse_begin(model, model->reader_nav_pending_dir, now_ms);
        dirty |= fast_browse_apply_step_with_hold_ms(model, 0U, now_ms);
        return dirty;
    }

    return false;
}

bool ink_app_fast_browse_self_test(void)
{
    return app_fast_browse_state_self_test();
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
    if (fast_browse_step_size_from_hold_ms(200U) != 1U
        || fast_browse_step_size_from_hold_ms(7000U) != 5U
        || fast_browse_step_size_from_hold_ms(15000U) != 10U
        || fast_browse_step_size_from_hold_ms(25000U) != 20U) {
        return false;
    }
    if (fast_browse_step_interval_ms(200U) != 220U
        || fast_browse_step_interval_ms(7000U) != 180U
        || fast_browse_step_interval_ms(15000U) != 140U
        || fast_browse_step_interval_ms(25000U) != 110U) {
        return false;
    }
    if (!fast_browse_apply_step_with_hold_ms(&model, 0U, 1200U) || model.fast_browse.target_page != 100U) {
        return false;
    }
    if (fast_browse_apply_step_with_hold_ms(&model, 100U, 1260U)) {
        return false;
    }
    if (!fast_browse_apply_step_with_hold_ms(&model, 6000U, 1400U) || model.fast_browse.target_page != 105U) {
        return false;
    }
    model.fast_browse.release_armed = true;
    ink_app_fast_browse_note_preview_landed(&model);
    model.fast_browse.commit_pending = true;
    if (!fast_browse_finish_pending(&model)
        || model.reader_session.current_page != 105U
        || !model.reader_fast_full_commit_pending) {
        return false;
    }
    if (!fast_browse_begin(&model, INK_FAST_BROWSE_DIR_FORWARD, 2000U)) {
        return false;
    }
    model.fast_browse.target_page = 132U;
    model.fast_browse.visible_page = 130U;
    model.fast_browse.has_visible_page = true;
    model.fast_browse.commit_pending = true;
    if (!fast_browse_finish_pending(&model)
        || model.reader_session.current_page != 132U) {
        return false;
    }
    ink_app_clear_fast_browse(&model);
    return !model.fast_browse.active;
}
