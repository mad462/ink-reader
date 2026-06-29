#include "apps/ink_voice_note_app.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "apps/ink_launcher_app.h"
#include "ink_button_input.h"
#include "ink_system_runtime.h"
#include "ink_system_services.h"
#include "voice_note/voice_note_service.h"

enum {
    VOICE_NOTE_POPUP_ACTION_COUNT = 3,
};

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
static voice_note_note_t *voice_note_ensure_visible_note_cache(ink_voice_note_app_state_t *state);
static epd_test_pattern_reader_menu_overlay_t *voice_note_ensure_popup_overlay(
    ink_voice_note_app_state_t *state);
static void voice_note_sync_from_service(ink_voice_note_app_state_t *state);
static void refresh_visible_notes(ink_voice_note_app_state_t *state);
static bool current_selection_is_new_card(const ink_voice_note_app_state_t *state);
static size_t voice_note_card_count(const ink_voice_note_app_state_t *state);
static bool voice_note_tab_has_new_card(voice_note_tab_t tab);
static size_t visible_note_index_from_selection(
    const ink_voice_note_app_state_t *state,
    size_t selection_index);
static bool select_adjacent_note_from_current(
    ink_voice_note_app_state_t *state,
    int direction);
static void cycle_tab(ink_voice_note_app_state_t *state, int direction);
static bool move_selection(ink_voice_note_app_state_t *state, int direction);
static bool handle_popup_confirm(ink_voice_note_app_state_t *state, uint32_t now_ms);
static const ink_button_snapshot_t *voice_note_snapshot_from_event(const ink_app_event_t *event);
static void voice_note_refresh_active_note_title(ink_voice_note_app_state_t *state);

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

static voice_note_note_t *voice_note_ensure_visible_note_cache(ink_voice_note_app_state_t *state)
{
    if (state == NULL) {
        return NULL;
    }
    if (state->visible_notes != NULL) {
        return state->visible_notes;
    }

    state->visible_notes = heap_caps_malloc(
        sizeof(voice_note_note_t) * VOICE_NOTE_MAX_NOTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (state->visible_notes == NULL) {
        state->visible_notes = heap_caps_malloc(
            sizeof(voice_note_note_t) * VOICE_NOTE_MAX_NOTES,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (state->visible_notes != NULL) {
        memset(state->visible_notes, 0, sizeof(voice_note_note_t) * VOICE_NOTE_MAX_NOTES);
    }
    return state->visible_notes;
}

static epd_test_pattern_reader_menu_overlay_t *voice_note_ensure_popup_overlay(
    ink_voice_note_app_state_t *state)
{
    if (state == NULL) {
        return NULL;
    }
    if (state->popup_overlay != NULL) {
        return state->popup_overlay;
    }

    state->popup_overlay = heap_caps_malloc(
        sizeof(epd_test_pattern_reader_menu_overlay_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (state->popup_overlay == NULL) {
        state->popup_overlay = heap_caps_malloc(
            sizeof(epd_test_pattern_reader_menu_overlay_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (state->popup_overlay != NULL) {
        memset(state->popup_overlay, 0, sizeof(*state->popup_overlay));
    }
    return state->popup_overlay;
}

static bool voice_note_tab_has_new_card(voice_note_tab_t tab)
{
    return tab == VOICE_NOTE_TAB_ALL || tab == VOICE_NOTE_TAB_PENDING;
}

static size_t voice_note_card_count(const ink_voice_note_app_state_t *state)
{
    size_t count = 0U;

    if (state == NULL) {
        return 0U;
    }

    count = state->visible_note_count;
    if (voice_note_tab_has_new_card(state->active_tab)) {
        count += 1U;
    }
    return count;
}

static size_t visible_note_index_from_selection(
    const ink_voice_note_app_state_t *state,
    size_t selection_index)
{
    if (state == NULL) {
        return VOICE_NOTE_MAX_NOTES;
    }
    if (voice_note_tab_has_new_card(state->active_tab)) {
        if (selection_index == 0U) {
            return VOICE_NOTE_MAX_NOTES;
        }
        return selection_index - 1U;
    }
    return selection_index;
}

static bool select_adjacent_note_from_current(
    ink_voice_note_app_state_t *state,
    int direction)
{
    size_t current_note_index = 0U;
    size_t target_note_index = 0U;

    if (state == NULL || direction == 0 || state->visible_note_count == 0U) {
        return false;
    }

    current_note_index = visible_note_index_from_selection(state, state->selected_index);
    if (current_note_index >= state->visible_note_count) {
        if (voice_note_tab_has_new_card(state->active_tab)) {
            state->selected_index = 1U;
            state->full_text_open = true;
            return true;
        }
        state->selected_index = 0U;
        state->full_text_open = true;
        return true;
    }

    if (direction < 0) {
        if (current_note_index == 0U) {
            return false;
        }
        target_note_index = current_note_index - 1U;
    } else {
        if (current_note_index + 1U >= state->visible_note_count) {
            return false;
        }
        target_note_index = current_note_index + 1U;
    }

    state->selected_index = voice_note_tab_has_new_card(state->active_tab)
        ? (target_note_index + 1U)
        : target_note_index;
    state->full_text_open = true;
    return true;
}

static void refresh_visible_notes(ink_voice_note_app_state_t *state)
{
    size_t card_count = 0U;

    if (state == NULL) {
        return;
    }
    if (voice_note_ensure_visible_note_cache(state) == NULL) {
        state->visible_note_count = 0U;
        return;
    }

    (void)voice_note_service_copy_note_summaries(
        state->active_tab,
        state->visible_notes,
        VOICE_NOTE_MAX_NOTES,
        &state->visible_note_count);

    card_count = voice_note_card_count(state);
    if (card_count == 0U) {
        state->selected_index = 0U;
    } else if (state->selected_index >= card_count) {
        state->selected_index = card_count - 1U;
    }
}

static void voice_note_sync_from_service(ink_voice_note_app_state_t *state)
{
    if (state == NULL) {
        return;
    }

    (void)voice_note_service_get_snapshot(&state->snapshot);
    refresh_visible_notes(state);
    voice_note_refresh_active_note_title(state);
}

static void voice_note_refresh_active_note_title(ink_voice_note_app_state_t *state)
{
    voice_note_note_t note;

    if (state == NULL) {
        return;
    }

    state->active_note_title[0] = '\0';
    if (state->snapshot.active_note_id[0] == '\0') {
        return;
    }
    if (!voice_note_service_load_note(state->snapshot.active_note_id, &note)) {
        return;
    }

    snprintf(
        state->active_note_title,
        sizeof(state->active_note_title),
        "%s",
        note.title[0] != '\0' ? note.title : "这是一条语音标签");
}

static bool current_selection_is_new_card(const ink_voice_note_app_state_t *state)
{
    return state != NULL
        && voice_note_tab_has_new_card(state->active_tab)
        && state->selected_index == 0U;
}

static void cycle_tab(ink_voice_note_app_state_t *state, int direction)
{
    int next_tab = 0;

    if (state == NULL || direction == 0) {
        return;
    }

    next_tab = (int)state->active_tab + direction;
    if (next_tab < 0) {
        next_tab = VOICE_NOTE_TAB_COUNT - 1;
    } else if (next_tab >= VOICE_NOTE_TAB_COUNT) {
        next_tab = 0;
    }

    state->active_tab = (voice_note_tab_t)next_tab;
    state->popup_open = false;
    state->popup_action_index = 0U;
    state->full_text_open = false;
    state->selected_index = 0U;
    refresh_visible_notes(state);
}

static bool move_selection(ink_voice_note_app_state_t *state, int direction)
{
    const size_t card_count = voice_note_card_count(state);

    if (state == NULL || direction == 0) {
        return false;
    }

    if (card_count == 0U) {
        return false;
    }

    if (direction < 0) {
        if (state->selected_index > 0U) {
            state->selected_index--;
            return true;
        }
        return false;
    }
    if (state->selected_index + 1U < card_count) {
        state->selected_index++;
        return true;
    }

    return false;
}

static const ink_button_snapshot_t *voice_note_snapshot_from_event(const ink_app_event_t *event)
{
    if (event == NULL || event->kind != INK_APP_EVENT_BUTTON_SNAPSHOT || event->payload == NULL) {
        return NULL;
    }
    return (const ink_button_snapshot_t *)event->payload;
}

static bool handle_popup_confirm(ink_voice_note_app_state_t *state, uint32_t now_ms)
{
    size_t note_index = 0U;
    voice_note_note_t note;

    if (state == NULL) {
        return false;
    }
    if (state->visible_notes == NULL) {
        return false;
    }

    note_index = visible_note_index_from_selection(state, state->selected_index);
    if (note_index >= state->visible_note_count) {
        state->popup_open = false;
        state->popup_action_index = 0U;
        return false;
    }

    note = state->visible_notes[note_index];
    state->popup_open = false;

    switch (state->popup_action_index) {
        case 0U:
            state->full_text_open = true;
            return true;
        case 1U:
            if (voice_note_service_set_note_status(
                    note.id,
                    note.status == VOICE_NOTE_STATUS_PENDING
                        ? VOICE_NOTE_STATUS_DONE
                        : VOICE_NOTE_STATUS_PENDING)) {
                voice_note_sync_from_service(state);
                return true;
            }
            return false;
        case 2U:
            if (voice_note_service_delete_note(note.id)) {
                voice_note_sync_from_service(state);
                return true;
            }
            return false;
        default:
            return false;
    }
}

static void voice_note_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_voice_note_app_state_t *state = NULL;
    ink_system_services_t *services = NULL;

    if (app == NULL || app->state == NULL) {
        return;
    }

    state = (ink_voice_note_app_state_t *)app->state;
    {
        voice_note_note_t *visible_notes = state->visible_notes;
        epd_test_pattern_reader_menu_overlay_t *popup_overlay = state->popup_overlay;

        memset(state, 0, sizeof(*state));
        state->visible_notes = visible_notes;
        state->popup_overlay = popup_overlay;
    }
    state->active_tab = VOICE_NOTE_TAB_PENDING;
    state->tab_bar_focus = false;
    (void)voice_note_ensure_popup_overlay(state);
    services = runtime != NULL ? runtime->services : NULL;
    if (services == NULL || !services->tf_ready) {
        state->snapshot.state = VOICE_NOTE_JOB_FAILED;
        snprintf(state->snapshot.status_text, sizeof(state->snapshot.status_text), "%s", "存储失败");
        refresh_visible_notes(state);
        voice_note_bind_render_state(&s_voice_note_render_state, state, services);
        return;
    }
    (void)voice_note_service_prepare_storage();
    (void)voice_note_service_get_snapshot(&state->snapshot);
    refresh_visible_notes(state);
    voice_note_bind_render_state(&s_voice_note_render_state, state, services);
}

static bool voice_note_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    ink_voice_note_app_state_t *state = NULL;
    const ink_button_snapshot_t *snapshot = NULL;
    const ink_app_descriptor_t *launcher = NULL;
    size_t note_index = 0U;

    if (runtime == NULL || app == NULL || app->state == NULL || event == NULL) {
        return false;
    }

    state = (ink_voice_note_app_state_t *)app->state;
    snapshot = voice_note_snapshot_from_event(event);
    launcher = ink_system_runtime_find_app_by_id(runtime, "launcher");

    if (state->full_text_open) {
        note_index = visible_note_index_from_selection(state, state->selected_index);
        switch (event->kind) {
            case INK_APP_EVENT_BUTTON_CONFIRM:
                if (note_index >= state->visible_note_count) {
                    return false;
                }
                return voice_note_service_toggle_playback(
                    state->visible_notes[note_index].id,
                    event->event_ms);
            case INK_APP_EVENT_BUTTON_BACK:
                (void)voice_note_service_stop_playback();
                state->full_text_open = false;
                return true;
            case INK_APP_EVENT_NAV_PREVIOUS:
            case INK_APP_EVENT_TILT_PREVIOUS:
                (void)voice_note_service_stop_playback();
                return select_adjacent_note_from_current(state, -1);
            case INK_APP_EVENT_NAV_NEXT:
            case INK_APP_EVENT_TILT_NEXT:
                (void)voice_note_service_stop_playback();
                return select_adjacent_note_from_current(state, 1);
            default:
                return false;
        }
    }

    if (state->popup_open) {
        switch (event->kind) {
            case INK_APP_EVENT_NAV_PREVIOUS:
            case INK_APP_EVENT_TILT_PREVIOUS:
                state->popup_action_index = (uint8_t)(
                    (state->popup_action_index + VOICE_NOTE_POPUP_ACTION_COUNT - 1U)
                    % VOICE_NOTE_POPUP_ACTION_COUNT);
                return true;
            case INK_APP_EVENT_NAV_NEXT:
            case INK_APP_EVENT_TILT_NEXT:
                state->popup_action_index = (uint8_t)(
                    (state->popup_action_index + 1U) % VOICE_NOTE_POPUP_ACTION_COUNT);
                return true;
            case INK_APP_EVENT_BUTTON_BACK:
                state->popup_open = false;
                state->popup_action_index = 0U;
                return true;
            case INK_APP_EVENT_BUTTON_CONFIRM:
                return handle_popup_confirm(state, event->event_ms);
            default:
                return false;
        }
    }

    if (state->tab_bar_focus) {
        switch (event->kind) {
            case INK_APP_EVENT_BUTTON_BACK:
                return launcher != NULL && ink_system_runtime_request_switch(runtime, launcher);
            case INK_APP_EVENT_NAV_PREVIOUS:
            case INK_APP_EVENT_TILT_PREVIOUS:
                cycle_tab(state, -1);
                return true;
            case INK_APP_EVENT_NAV_NEXT:
            case INK_APP_EVENT_TILT_NEXT:
                cycle_tab(state, 1);
                return true;
            case INK_APP_EVENT_BUTTON_CONFIRM:
                state->tab_bar_focus = false;
                refresh_visible_notes(state);
                return true;
            default:
                return false;
        }
    }

    switch (event->kind) {
        case INK_APP_EVENT_BUTTON_BACK:
            state->tab_bar_focus = true;
            return true;
        case INK_APP_EVENT_NAV_PREVIOUS:
        case INK_APP_EVENT_TILT_PREVIOUS:
            return move_selection(state, -1);
        case INK_APP_EVENT_NAV_NEXT:
        case INK_APP_EVENT_TILT_NEXT:
            return move_selection(state, 1);
        case INK_APP_EVENT_BUTTON_CONFIRM:
            if (current_selection_is_new_card(state)) {
                (void)voice_note_service_stop_playback();
                if (voice_note_service_start_capture(event->event_ms)) {
                    voice_note_sync_from_service(state);
                    return true;
                }
                return false;
            }
            note_index = visible_note_index_from_selection(state, state->selected_index);
            if (note_index < state->visible_note_count) {
                state->popup_open = true;
                state->popup_action_index = 0U;
                return true;
            }
            return false;
        case INK_APP_EVENT_BUTTON_SNAPSHOT:
            if (snapshot == NULL || !current_selection_is_new_card(state)) {
                return false;
            }
            if ((snapshot->released_mask & ink_button_input_mask_for_raw(INK_RAW_BUTTON_CONFIRM)) != 0U) {
                if (voice_note_service_stop_capture(event->event_ms)) {
                    voice_note_sync_from_service(state);
                    return true;
                }
                return false;
            }
            return false;
        default:
            return false;
    }
}

static bool voice_note_tick(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    uint32_t now_ms)
{
    ink_voice_note_app_state_t *state = NULL;
    voice_note_service_snapshot_t snapshot;
    bool dirty = false;

    (void)runtime;
    if (app == NULL || app->state == NULL) {
        return false;
    }

    state = (ink_voice_note_app_state_t *)app->state;
    dirty = voice_note_service_tick(now_ms);
    if (voice_note_service_get_snapshot(&snapshot)
        && memcmp(&snapshot, &state->snapshot, sizeof(snapshot)) != 0) {
        state->snapshot = snapshot;
        refresh_visible_notes(state);
        dirty = true;
    }

    return dirty;
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
    ink_app_event_t event = {.kind = INK_APP_EVENT_NAV_NEXT, .event_ms = 100U};
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *app = ink_voice_note_app_descriptor();

    memset(&services, 0, sizeof(services));
    memset(&model, 0, sizeof(model));
    ink_system_runtime_init(&runtime);
    runtime.services = &services;
    runtime.force_full_refresh_on_next_render = true;

    if (app->enter == NULL || app->render == NULL || app->input == NULL) {
        return false;
    }
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, app)) {
        return false;
    }

    app->enter(&runtime, app);
    if (s_voice_note_state.active_tab != VOICE_NOTE_TAB_PENDING) {
        return false;
    }
    if (s_voice_note_state.visible_notes == NULL) {
        return false;
    }
    s_voice_note_state.visible_note_count = 2U;
    s_voice_note_state.selected_index = 2U;
    if (app->input(&runtime, app, &event)) {
        return false;
    }
    if (s_voice_note_state.active_tab != VOICE_NOTE_TAB_PENDING
        || s_voice_note_state.selected_index != 2U) {
        return false;
    }

    event.kind = INK_APP_EVENT_BUTTON_BACK;
    if (!app->input(&runtime, app, &event) || !s_voice_note_state.tab_bar_focus) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_NEXT;
    if (!app->input(&runtime, app, &event)) {
        return false;
    }
    if (s_voice_note_state.active_tab != VOICE_NOTE_TAB_DONE || s_voice_note_state.tab_bar_focus) {
        return false;
    }

    event.kind = INK_APP_EVENT_BUTTON_BACK;
    if (!app->input(&runtime, app, &event) || !s_voice_note_state.tab_bar_focus) {
        return false;
    }
    event.kind = INK_APP_EVENT_BUTTON_BACK;
    if (!app->input(&runtime, app, &event)
        || runtime.pending_app != launcher
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    if (!app->render(&runtime, app, &model)) {
        return false;
    }

    s_voice_note_state.tab_bar_focus = false;
    s_voice_note_state.full_text_open = true;
    s_voice_note_state.visible_note_count = 2U;
    s_voice_note_state.active_tab = VOICE_NOTE_TAB_PENDING;
    s_voice_note_state.selected_index = 1U;
    snprintf(s_voice_note_state.visible_notes[0].id, sizeof(s_voice_note_state.visible_notes[0].id), "%s", "note-a");
    snprintf(s_voice_note_state.visible_notes[1].id, sizeof(s_voice_note_state.visible_notes[1].id), "%s", "note-b");

    event.kind = INK_APP_EVENT_NAV_NEXT;
    if (!app->input(&runtime, app, &event)
        || !s_voice_note_state.full_text_open
        || s_voice_note_state.selected_index != 2U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_NEXT;
    if (app->input(&runtime, app, &event) || s_voice_note_state.selected_index != 2U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_PREVIOUS;
    if (!app->input(&runtime, app, &event)
        || !s_voice_note_state.full_text_open
        || s_voice_note_state.selected_index != 1U) {
        return false;
    }

    event.kind = INK_APP_EVENT_BUTTON_CONFIRM;
    if (!app->input(&runtime, app, &event) || !s_voice_note_state.full_text_open) {
        return false;
    }

    event.kind = INK_APP_EVENT_BUTTON_BACK;
    if (!app->input(&runtime, app, &event) || s_voice_note_state.full_text_open) {
        return false;
    }

    return model.mode == INK_APP_RENDER_MODE_VOICE_NOTE
        && model.request_full_refresh
        && model.state == &s_voice_note_render_state
        && s_voice_note_render_state.state == &s_voice_note_state;
}
