#include "apps/ink_launcher_app.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "apps/ink_gray_cal_app.h"
#include "apps/ink_photo_album_app.h"
#include "apps/ink_reader_app.h"
#include "apps/ink_usb_msc_app.h"
#include "apps/ink_voice_note_app.h"
#include "apps/ink_wifi_setup_app.h"
#include "ink_system_runtime.h"

enum {
    INK_LAUNCHER_APP_COUNT = 6,
};

static const char *const kLauncherTargetIds[INK_LAUNCHER_APP_COUNT] = {
    "reader",
    "voice_note",
    "wifi_setup",
    "photo_album",
    "gray_cal",
    "usb_msc",
};

static ink_launcher_app_state_t s_launcher_state;

static void launcher_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool launcher_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event);
static bool launcher_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model);
static bool launcher_selection_self_test(void);
static bool launcher_missing_wifi_target_self_test(void);

static const ink_app_descriptor_t kLauncherApp = {
    .id = "launcher",
    .name = "Launcher",
    .enter = launcher_enter,
    .input = launcher_input,
    .render = launcher_render,
    .state = &s_launcher_state,
};

const ink_app_descriptor_t *ink_launcher_app_descriptor(void)
{
    return &kLauncherApp;
}

static void launcher_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_launcher_app_state_t *state;

    (void)runtime;
    if (app == NULL || app->state == NULL) {
        return;
    }

    state = (ink_launcher_app_state_t *)app->state;
    state->selected_app_index = 0U;
}

static bool launcher_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    ink_launcher_app_state_t *state;
    const ink_app_descriptor_t *target_app;

    if (runtime == NULL || app == NULL || app->state == NULL || event == NULL) {
        return false;
    }

    state = (ink_launcher_app_state_t *)app->state;
    switch (event->kind) {
        case INK_APP_EVENT_NAV_PREVIOUS:
        case INK_APP_EVENT_TILT_PREVIOUS:
            state->selected_app_index = (state->selected_app_index + INK_LAUNCHER_APP_COUNT - 1U)
                % INK_LAUNCHER_APP_COUNT;
            return true;
        case INK_APP_EVENT_NAV_NEXT:
        case INK_APP_EVENT_TILT_NEXT:
            state->selected_app_index = (state->selected_app_index + 1U) % INK_LAUNCHER_APP_COUNT;
            return true;
        case INK_APP_EVENT_BUTTON_CONFIRM:
            target_app = ink_system_runtime_find_app_by_id(
                runtime,
                kLauncherTargetIds[state->selected_app_index]);
            return target_app != NULL
                && ink_system_runtime_request_switch(runtime, target_app);
        default:
            return false;
    }
}

static bool launcher_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model)
{
    if (runtime == NULL || app == NULL || out_model == NULL) {
        return false;
    }

    memset(out_model, 0, sizeof(*out_model));
    out_model->mode = INK_APP_RENDER_MODE_LAUNCHER;
    out_model->request_full_refresh = runtime->force_full_refresh_on_next_render;
    out_model->state = app->state;
    runtime->force_full_refresh_on_next_render = false;
    return true;
}

static bool launcher_selection_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_render_model_t model;
    ink_app_event_t event = {
        .kind = INK_APP_EVENT_NONE,
    };
    ink_launcher_app_state_t *state = &s_launcher_state;
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *reader = NULL;
    const ink_app_descriptor_t *voice_note = NULL;
    const ink_app_descriptor_t *usb_msc = NULL;

    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, ink_reader_app_descriptor())
        || !ink_system_runtime_register_app(&runtime, ink_voice_note_app_descriptor())
        || !ink_system_runtime_register_app(&runtime, ink_wifi_setup_app_descriptor())
        || !ink_system_runtime_register_app(&runtime, ink_photo_album_app_descriptor())
        || !ink_system_runtime_register_app(&runtime, ink_gray_cal_app_descriptor())
        || !ink_system_runtime_register_app(&runtime, ink_usb_msc_app_descriptor())) {
        return false;
    }

    reader = ink_system_runtime_find_app_by_id(&runtime, "reader");
    voice_note = ink_system_runtime_find_app_by_id(&runtime, "voice_note");
    usb_msc = ink_system_runtime_find_app_by_id(&runtime, "usb_msc");
    if (reader == NULL || voice_note == NULL || usb_msc == NULL) {
        return false;
    }

    if (!ink_system_runtime_set_active_app(&runtime, launcher)
        || runtime.active_app != launcher
        || state->selected_app_index != 0U) {
        return false;
    }

    if (!launcher->render(&runtime, launcher, &model)
        || model.mode != INK_APP_RENDER_MODE_LAUNCHER
        || model.state != state
        || model.request_full_refresh) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_NEXT;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 1U) {
        return false;
    }

    event.kind = INK_APP_EVENT_BUTTON_CONFIRM;
    if (!launcher->input(&runtime, launcher, &event)
        || runtime.pending_app != voice_note
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    runtime.pending_app = NULL;
    runtime.force_full_refresh_on_next_render = false;

    event.kind = INK_APP_EVENT_NAV_NEXT;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 2U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_NEXT;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 3U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_NEXT;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 4U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_NEXT;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 5U) {
        return false;
    }

    event.kind = INK_APP_EVENT_BUTTON_CONFIRM;
    if (!launcher->input(&runtime, launcher, &event)
        || runtime.pending_app != usb_msc
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    runtime.pending_app = NULL;
    runtime.force_full_refresh_on_next_render = false;

    event.kind = INK_APP_EVENT_NAV_NEXT;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 0U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_PREVIOUS;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 5U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_PREVIOUS;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 4U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_PREVIOUS;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 3U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_PREVIOUS;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 2U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_PREVIOUS;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 1U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_PREVIOUS;
    if (!launcher->input(&runtime, launcher, &event) || state->selected_app_index != 0U) {
        return false;
    }

    event.kind = INK_APP_EVENT_BUTTON_CONFIRM;
    if (!launcher->input(&runtime, launcher, &event)
        || runtime.pending_app != reader
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    return true;
}

static bool launcher_missing_wifi_target_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_event_t event = {
        .kind = INK_APP_EVENT_BUTTON_CONFIRM,
    };
    ink_launcher_app_state_t *state = &s_launcher_state;
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();

    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, ink_reader_app_descriptor())
        || !ink_system_runtime_register_app(&runtime, ink_voice_note_app_descriptor())
        || !ink_system_runtime_register_app(&runtime, ink_photo_album_app_descriptor())
        || !ink_system_runtime_register_app(&runtime, ink_gray_cal_app_descriptor())
        || !ink_system_runtime_register_app(&runtime, ink_usb_msc_app_descriptor())
        || !ink_system_runtime_set_active_app(&runtime, launcher)) {
        return false;
    }

    state->selected_app_index = 2U;
    if (launcher->input(&runtime, launcher, &event)) {
        return false;
    }

    return runtime.pending_app == NULL;
}

bool ink_launcher_app_self_test(void)
{
    return launcher_selection_self_test()
        && launcher_missing_wifi_target_self_test();
}
