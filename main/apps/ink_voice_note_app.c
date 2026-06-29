#include "apps/ink_voice_note_app.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "ink_system_runtime.h"
#include "ink_system_services.h"
#include "voice_note/voice_note_service.h"

static ink_voice_note_app_state_t s_voice_note_state;
static ink_voice_note_app_render_state_t s_voice_note_render_state;

static void voice_note_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool voice_note_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event);
static bool voice_note_tick(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    uint32_t now_ms);
static bool voice_note_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model);
static void voice_note_bind_render_state(
    ink_voice_note_app_render_state_t *render_state,
    const ink_voice_note_app_state_t *state,
    const ink_system_services_t *services);

static const ink_app_descriptor_t kVoiceNoteApp = {
    .id = "voice_note",
    .name = "Voice Note",
    .enter = voice_note_enter,
    .input = voice_note_input,
    .tick = voice_note_tick,
    .render = voice_note_render,
    .state = &s_voice_note_state,
};

const ink_app_descriptor_t *ink_voice_note_app_descriptor(void)
{
    return &kVoiceNoteApp;
}

static void voice_note_bind_render_state(
    ink_voice_note_app_render_state_t *render_state,
    const ink_voice_note_app_state_t *state,
    const ink_system_services_t *services)
{
    if (render_state == NULL) {
        return;
    }

    render_state->state = state;
    render_state->menu_font = services != NULL ? &services->menu_font : NULL;
    render_state->footer_font = services != NULL ? &services->footer_font : NULL;
    render_state->reader_font = services != NULL ? &services->reader_font : NULL;
    ink_system_services_get_time_badge(
        services,
        render_state->header_meta,
        sizeof(render_state->header_meta));
}

static void voice_note_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_voice_note_app_state_t *state = NULL;
    ink_system_services_t *services = NULL;

    if (app == NULL || app->state == NULL) {
        return;
    }

    state = (ink_voice_note_app_state_t *)app->state;
    memset(state, 0, sizeof(*state));
    state->active_tab = VOICE_NOTE_TAB_PENDING;
    services = runtime != NULL ? runtime->services : NULL;
    (void)voice_note_service_init();
    (void)voice_note_service_get_snapshot(&state->snapshot);
    voice_note_bind_render_state(&s_voice_note_render_state, state, services);
}

static bool voice_note_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    (void)runtime;
    (void)app;
    (void)event;
    return false;
}

static bool voice_note_tick(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    uint32_t now_ms)
{
    ink_voice_note_app_state_t *state = NULL;

    (void)runtime;
    if (app == NULL || app->state == NULL) {
        return false;
    }

    state = (ink_voice_note_app_state_t *)app->state;
    if (!voice_note_service_tick(now_ms)) {
        return false;
    }

    return voice_note_service_get_snapshot(&state->snapshot);
}

static bool voice_note_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model)
{
    ink_system_services_t *services = NULL;
    ink_voice_note_app_state_t *state = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL || out_model == NULL) {
        return false;
    }

    services = runtime->services;
    state = (ink_voice_note_app_state_t *)app->state;
    voice_note_bind_render_state(&s_voice_note_render_state, state, services);

    memset(out_model, 0, sizeof(*out_model));
    out_model->mode = INK_APP_RENDER_MODE_VOICE_NOTE;
    out_model->request_full_refresh = runtime->force_full_refresh_on_next_render;
    out_model->refresh_strategy = out_model->request_full_refresh
        ? INK_REFRESH_STRATEGY_PAGE_TRANSITION_FULL
        : INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST;
    out_model->state = &s_voice_note_render_state;
    runtime->force_full_refresh_on_next_render = false;
    return true;
}

bool ink_voice_note_app_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_system_services_t services;
    ink_app_render_model_t model;
    const ink_app_descriptor_t *app = ink_voice_note_app_descriptor();

    memset(&services, 0, sizeof(services));
    memset(&model, 0, sizeof(model));
    ink_system_runtime_init(&runtime);
    runtime.services = &services;
    runtime.force_full_refresh_on_next_render = true;

    if (app->enter == NULL || app->render == NULL) {
        return false;
    }

    app->enter(&runtime, app);
    if (s_voice_note_state.active_tab != VOICE_NOTE_TAB_PENDING) {
        return false;
    }

    if (!app->render(&runtime, app, &model)) {
        return false;
    }

    return model.mode == INK_APP_RENDER_MODE_VOICE_NOTE
        && model.request_full_refresh
        && model.state == &s_voice_note_render_state
        && s_voice_note_render_state.state == &s_voice_note_state;
}
