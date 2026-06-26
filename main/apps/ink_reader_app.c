#include "apps/ink_reader_app.h"

#include <string.h>

#include "apps/ink_launcher_app.h"
#include "ink_system_runtime.h"

static void reader_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool reader_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event);
static bool reader_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model);
static bool reader_navigation_self_test(void);

static const ink_app_descriptor_t kReaderApp = {
    .id = "reader",
    .name = "Reader",
    .enter = reader_enter,
    .input = reader_input,
    .render = reader_render,
};

const ink_app_descriptor_t *ink_reader_app_descriptor(void)
{
    return &kReaderApp;
}

static void reader_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    if (runtime == NULL || app == NULL) {
        return;
    }

    runtime->force_full_refresh_on_next_render = true;
}

static bool reader_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    const ink_app_descriptor_t *launcher;

    (void)app;
    if (runtime == NULL || event == NULL) {
        return false;
    }

    if (event->kind != INK_APP_EVENT_BUTTON_BACK) {
        return false;
    }

    launcher = ink_system_runtime_find_app_by_id(runtime, "launcher");
    return launcher != NULL && ink_system_runtime_request_switch(runtime, launcher);
}

static bool reader_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model)
{
    if (runtime == NULL || app == NULL || out_model == NULL) {
        return false;
    }

    memset(out_model, 0, sizeof(*out_model));
    out_model->mode = INK_APP_RENDER_MODE_READER_PLACEHOLDER;
    out_model->request_full_refresh = runtime->force_full_refresh_on_next_render;
    out_model->state = app->state;
    runtime->force_full_refresh_on_next_render = false;
    return true;
}

static bool reader_navigation_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_render_model_t model;
    ink_app_event_t event = {
        .kind = INK_APP_EVENT_BUTTON_BACK,
    };
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();

    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, reader)) {
        return false;
    }

    reader->enter(&runtime, reader);
    if (!runtime.force_full_refresh_on_next_render) {
        return false;
    }

    if (!reader->render(&runtime, reader, &model)
        || model.mode != INK_APP_RENDER_MODE_READER_PLACEHOLDER
        || model.state != reader->state
        || !model.request_full_refresh
        || runtime.force_full_refresh_on_next_render) {
        return false;
    }

    if (!reader->input(&runtime, reader, &event)
        || runtime.pending_app != launcher
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    return true;
}

bool ink_reader_app_self_test(void)
{
    return reader_navigation_self_test();
}
