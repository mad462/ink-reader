#include "apps/ink_gray_cal_app.h"

#include <string.h>

#include "apps/ink_launcher_app.h"
#include "ink_app_render.h"
#include "ink_system_runtime.h"

static ink_gray_cal_app_state_t s_gray_cal_state;

static void gray_cal_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool gray_cal_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event);
static bool gray_cal_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model);
static bool request_switch_to_launcher(ink_system_runtime_t *runtime);
static bool gray_cal_navigation_self_test(void);
static bool gray_cal_back_returns_launcher_self_test(void);

static const ink_app_descriptor_t kGrayCalApp = {
    .id = "gray_cal",
    .name = "Gray Cal",
    .enter = gray_cal_enter,
    .input = gray_cal_input,
    .render = gray_cal_render,
    .state = &s_gray_cal_state,
};

const ink_app_descriptor_t *ink_gray_cal_app_descriptor(void)
{
    return &kGrayCalApp;
}

static bool request_switch_to_launcher(ink_system_runtime_t *runtime)
{
    const ink_app_descriptor_t *launcher = ink_system_runtime_find_app_by_id(runtime, "launcher");

    return launcher != NULL
        && ink_system_runtime_request_switch(runtime, launcher);
}

static void gray_cal_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_gray_cal_app_state_t *state = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL) {
        return;
    }

    state = (ink_gray_cal_app_state_t *)app->state;
    if (!state->initialized) {
        memset(state, 0, sizeof(*state));
        ink_tuning_lab_init(&state->ui.lab);
        ink_runtime_shell_init(&state->ui.shell);
        state->initialized = true;
    }

    ink_reader_session_close(&state->ui.reader_session);
    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.shell.full_refresh_requested = true;
    state->ui.lab.current_page = INK_TUNING_PAGE_GRAY_CAL;
    state->ui.lab.refresh_profile = INK_TUNING_REFRESH_FAST_FULL;
    state->ui.lab.force_full_refresh = true;
    state->ui.lab.reader_white_refresh_pending = false;
    state->ui.lab.grid_compare_active = false;
    state->ui.lab.grid_compare_step = 0U;
    runtime->force_full_refresh_on_next_render = true;
}

static bool gray_cal_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    ink_gray_cal_app_state_t *state = NULL;
    bool dirty = false;

    (void)app;
    if (runtime == NULL || event == NULL || app == NULL || app->state == NULL) {
        return false;
    }

    state = (ink_gray_cal_app_state_t *)app->state;
    switch (event->kind) {
        case INK_APP_EVENT_BUTTON_BACK:
            return request_switch_to_launcher(runtime);
        case INK_APP_EVENT_NAV_PREVIOUS:
        case INK_APP_EVENT_TILT_PREVIOUS:
            dirty = ink_tuning_lab_previous_page(&state->ui.lab);
            break;
        case INK_APP_EVENT_NAV_NEXT:
        case INK_APP_EVENT_TILT_NEXT:
            dirty = ink_tuning_lab_next_page(&state->ui.lab);
            break;
        case INK_APP_EVENT_BUTTON_CONFIRM:
            dirty = ink_tuning_lab_cycle_refresh_profile(&state->ui.lab);
            break;
        default:
            return false;
    }

    if (dirty) {
        state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
        state->ui.shell.full_refresh_requested = true;
        runtime->force_full_refresh_on_next_render = true;
    }
    return dirty;
}

static bool gray_cal_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model)
{
    ink_gray_cal_app_state_t *state = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL || out_model == NULL) {
        return false;
    }

    state = (ink_gray_cal_app_state_t *)app->state;
    memset(out_model, 0, sizeof(*out_model));
    out_model->mode = INK_APP_RENDER_MODE_READER_SUBSYSTEM;
    out_model->request_full_refresh = runtime->force_full_refresh_on_next_render
        || ink_runtime_shell_requires_full_refresh(&state->ui.shell)
        || state->ui.lab.force_full_refresh;
    out_model->state = &state->ui;
    runtime->force_full_refresh_on_next_render = false;
    return true;
}

static bool gray_cal_navigation_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_render_model_t model;
    ink_app_event_t event = {.kind = INK_APP_EVENT_NAV_PREVIOUS};
    ink_gray_cal_app_state_t *state = &s_gray_cal_state;
    const ink_app_descriptor_t *app = ink_gray_cal_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, app)
        || !ink_system_runtime_set_active_app(&runtime, app)) {
        return false;
    }

    if (state->ui.lab.current_page != INK_TUNING_PAGE_GRAY_CAL) {
        return false;
    }

    if (!gray_cal_render(&runtime, app, &model)
        || model.mode != INK_APP_RENDER_MODE_READER_SUBSYSTEM
        || model.state != &state->ui
        || !model.request_full_refresh) {
        return false;
    }

    runtime.force_full_refresh_on_next_render = false;
    state->ui.lab.force_full_refresh = false;
    if (!gray_cal_input(&runtime, app, &event)
        || state->ui.lab.current_page != INK_TUNING_PAGE_GRID_COMPARE
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_NEXT;
    return gray_cal_input(&runtime, app, &event)
        && state->ui.lab.current_page == INK_TUNING_PAGE_GRAY_CAL
        && runtime.force_full_refresh_on_next_render;
}

static bool gray_cal_back_returns_launcher_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_event_t event = {.kind = INK_APP_EVENT_BUTTON_BACK};
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *gray_cal = ink_gray_cal_app_descriptor();

    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, gray_cal)
        || !ink_system_runtime_set_active_app(&runtime, gray_cal)) {
        return false;
    }

    return gray_cal->input(&runtime, gray_cal, &event)
        && runtime.pending_app == launcher
        && runtime.force_full_refresh_on_next_render;
}

bool ink_gray_cal_app_self_test(void)
{
    return gray_cal_navigation_self_test()
        && gray_cal_back_returns_launcher_self_test();
}
