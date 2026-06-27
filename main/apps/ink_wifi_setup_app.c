#include "apps/ink_wifi_setup_app.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "apps/ink_launcher_app.h"
#include "ink_system_runtime.h"
#include "ink_system_services.h"
#include "ink_wifi_manager.h"

static const char *TAG = "wifi_setup_app";

enum {
    INK_WIFI_SETUP_APP_QUEUE_LEN = 4,
    INK_WIFI_SETUP_APP_WORKER_STACK = 6144,
};

typedef struct {
    uint32_t token;
    ink_wifi_setup_request_t request;
} ink_wifi_setup_app_work_item_t;

typedef struct {
    uint32_t token;
    ink_wifi_setup_request_type_t type;
    esp_err_t result;
    ink_wifi_scan_list_t scan;
    ink_wifi_status_t status;
    char ssid[INK_WIFI_SSID_MAX_LEN + 1];
} ink_wifi_setup_app_work_result_t;

typedef struct {
    ink_wifi_setup_app_view_t view;
    ink_wifi_setup_app_render_state_t render_state;
    QueueHandle_t request_queue;
    QueueHandle_t result_queue;
    TaskHandle_t worker_task;
    uint32_t active_token;
    bool worker_ready;
    bool partial_refresh_pending;
    int partial_x;
    int partial_y;
    int partial_w;
    int partial_h;
} ink_wifi_setup_app_state_t;

static ink_wifi_setup_app_state_t s_wifi_setup_state;

static void wifi_setup_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static void wifi_setup_exit(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool wifi_setup_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event);
static bool wifi_setup_tick(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    uint32_t now_ms);
static bool wifi_setup_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model);
static void wifi_setup_worker_task(void *arg);
static bool ensure_worker_ready(ink_wifi_setup_app_state_t *state);
static void reset_view_for_enter(ink_wifi_setup_app_state_t *state);
static void clear_partial_refresh(ink_wifi_setup_app_state_t *state);
static void set_partial_refresh(
    ink_wifi_setup_app_state_t *state,
    int x,
    int y,
    int w,
    int h);
static void set_partial_refresh_region(
    ink_wifi_setup_app_state_t *state,
    const ink_wifi_setup_ui_region_t *region,
    int pad);
static bool request_switch_to_launcher(ink_system_runtime_t *runtime);
static bool post_work_request(
    ink_wifi_setup_app_state_t *state,
    uint32_t token,
    const ink_wifi_setup_request_t *request);
static bool queue_scan(ink_wifi_setup_app_state_t *state);
static void keyboard_reset_cursor(ink_wifi_setup_app_state_t *state);
static void keyboard_step(ink_wifi_setup_app_state_t *state, int delta);
static const char *current_keyboard_label(const ink_wifi_setup_app_state_t *state);
static bool handle_list_mode(
    ink_system_runtime_t *runtime,
    ink_wifi_setup_app_state_t *state,
    const ink_app_event_t *event);
static bool handle_password_mode(
    ink_system_runtime_t *runtime,
    ink_wifi_setup_app_state_t *state,
    const ink_app_event_t *event);
static bool handle_saved_menu_mode(
    ink_system_runtime_t *runtime,
    ink_wifi_setup_app_state_t *state,
    const ink_app_event_t *event);
static bool handle_result_mode(
    ink_system_runtime_t *runtime,
    ink_wifi_setup_app_state_t *state,
    const ink_app_event_t *event);
static void init_render_state(
    ink_wifi_setup_app_state_t *state,
    const ink_system_services_t *services);
static bool apply_work_result(
    ink_wifi_setup_app_state_t *state,
    const ink_wifi_setup_app_work_result_t *result);
static bool wifi_setup_worker_stack_budget_self_test(void);
static bool wifi_setup_queue_flush_self_test(void);
static bool wifi_setup_initial_refresh_self_test(void);
static bool wifi_setup_back_returns_launcher_self_test(void);
static bool wifi_setup_scan_completion_self_test(void);
static bool wifi_setup_ignores_inactive_completion_self_test(void);

static const ink_app_descriptor_t kWifiSetupApp = {
    .id = "wifi_setup",
    .name = "WiFi Setup",
    .enter = wifi_setup_enter,
    .exit = wifi_setup_exit,
    .input = wifi_setup_input,
    .tick = wifi_setup_tick,
    .render = wifi_setup_render,
    .state = &s_wifi_setup_state,
};

const ink_app_descriptor_t *ink_wifi_setup_app_descriptor(void)
{
    return &kWifiSetupApp;
}

static bool ensure_worker_ready(ink_wifi_setup_app_state_t *state)
{
    if (state == NULL) {
        return false;
    }
    if (state->worker_ready) {
        return true;
    }

    state->request_queue = xQueueCreate(
        INK_WIFI_SETUP_APP_QUEUE_LEN,
        sizeof(ink_wifi_setup_app_work_item_t));
    state->result_queue = xQueueCreate(
        INK_WIFI_SETUP_APP_QUEUE_LEN,
        sizeof(ink_wifi_setup_app_work_result_t));
    if (state->request_queue == NULL || state->result_queue == NULL) {
        return false;
    }
    if (xTaskCreate(
            wifi_setup_worker_task,
            "WifiSetupWorker",
            INK_WIFI_SETUP_APP_WORKER_STACK,
            state,
            3,
            &state->worker_task) != pdPASS) {
        return false;
    }
    state->worker_ready = true;
    return true;
}

static void clear_partial_refresh(ink_wifi_setup_app_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->partial_refresh_pending = false;
    state->partial_x = 0;
    state->partial_y = 0;
    state->partial_w = 0;
    state->partial_h = 0;
}

static void set_partial_refresh(
    ink_wifi_setup_app_state_t *state,
    int x,
    int y,
    int w,
    int h)
{
    if (state == NULL) {
        return;
    }

    state->partial_refresh_pending = true;
    state->partial_x = x;
    state->partial_y = y;
    state->partial_w = w;
    state->partial_h = h;
}

static void set_partial_refresh_region(
    ink_wifi_setup_app_state_t *state,
    const ink_wifi_setup_ui_region_t *region,
    int pad)
{
    ink_wifi_setup_ui_region_t area;

    if (state == NULL || region == NULL) {
        return;
    }

    area = *region;
    if (!ink_wifi_setup_ui_region_is_valid(&area)) {
        return;
    }

    ink_wifi_setup_ui_pad_align_region(&area, pad);
    set_partial_refresh(state, area.x, area.y, area.w, area.h);
}

static void keyboard_reset_cursor(ink_wifi_setup_app_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->view.keyboard_column = 0;
    state->view.keyboard_row = 0;
}

static void reset_view_for_enter(ink_wifi_setup_app_state_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(&state->view, 0, sizeof(state->view));
    state->view.wifi.mode = WIFI_SETUP_UI_LIST;
    state->view.wifi.selected_index = 0;
    state->view.keyboard_layer = INK_WIFI_SETUP_KEYBOARD_LAYER_LOWER;
    keyboard_reset_cursor(state);
    clear_partial_refresh(state);
}

static void init_render_state(
    ink_wifi_setup_app_state_t *state,
    const ink_system_services_t *services)
{
    if (state == NULL) {
        return;
    }

    state->render_state.view = &state->view;
    state->render_state.fonts.menu = services != NULL ? &services->menu_font : NULL;
    state->render_state.fonts.footer = services != NULL ? &services->footer_font : NULL;
}

static bool post_work_request(
    ink_wifi_setup_app_state_t *state,
    uint32_t token,
    const ink_wifi_setup_request_t *request)
{
    ink_wifi_setup_app_work_item_t item;

    if (state == NULL || request == NULL || state->request_queue == NULL) {
        return false;
    }

    memset(&item, 0, sizeof(item));
    item.token = token;
    item.request = *request;
    return xQueueSend(state->request_queue, &item, 0) == pdTRUE;
}

static bool queue_scan(ink_wifi_setup_app_state_t *state)
{
    ink_wifi_setup_request_t request;

    if (state == NULL || !ink_wifi_setup_prepare_scan_request(&request)) {
        return false;
    }
    state->view.wifi.scan_in_progress = true;
    return post_work_request(state, state->active_token, &request);
}

static bool request_switch_to_launcher(ink_system_runtime_t *runtime)
{
    const ink_app_descriptor_t *launcher = ink_system_runtime_find_app_by_id(runtime, "launcher");

    return launcher != NULL
        && ink_system_runtime_request_switch(runtime, launcher);
}

static void wifi_setup_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_wifi_setup_app_state_t *state = NULL;
    ink_wifi_setup_ui_region_t full_screen;

    if (runtime == NULL || app == NULL || app->state == NULL) {
        return;
    }

    state = (ink_wifi_setup_app_state_t *)app->state;
    init_render_state(state, runtime->services);
    if (!ensure_worker_ready(state)) {
        ESP_LOGW(TAG, "worker init failed");
        return;
    }

    state->active_token++;
    reset_view_for_enter(state);
    (void)ink_wifi_manager_status(&state->view.wifi.status);
    if (state->request_queue != NULL) {
        xQueueReset(state->request_queue);
    }
    if (state->result_queue != NULL) {
        xQueueReset(state->result_queue);
    }
    if (!queue_scan(state)) {
        state->view.wifi.scan_in_progress = false;
        state->view.wifi.result_error = ESP_ERR_NO_MEM;
    }
    runtime->force_full_refresh_on_next_render = true;
    ink_wifi_setup_ui_full_screen_region(&full_screen);
    set_partial_refresh_region(state, &full_screen, 0);
}

static void wifi_setup_exit(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_wifi_setup_app_state_t *state = NULL;

    (void)runtime;
    if (app == NULL || app->state == NULL) {
        return;
    }

    state = (ink_wifi_setup_app_state_t *)app->state;
    state->active_token++;
    clear_partial_refresh(state);
    if (state->request_queue != NULL) {
        xQueueReset(state->request_queue);
    }
    if (state->result_queue != NULL) {
        xQueueReset(state->result_queue);
    }
}

static void keyboard_step(ink_wifi_setup_app_state_t *state, int delta)
{
    int current_index = 0;
    int row = 0;
    int col = 0;
    int total = 0;

    if (state == NULL || delta == 0) {
        return;
    }

    for (row = 0; row < INK_WIFI_SETUP_KEYBOARD_ROWS; ++row) {
        total += ink_wifi_setup_keyboard_row_key_count(row);
    }
    if (total <= 0) {
        return;
    }

    for (row = 0; row < state->view.keyboard_row; ++row) {
        current_index += ink_wifi_setup_keyboard_row_key_count(row);
    }
    current_index += state->view.keyboard_column;
    current_index = (current_index + delta + total) % total;

    row = 0;
    while (row < INK_WIFI_SETUP_KEYBOARD_ROWS) {
        const int row_count = ink_wifi_setup_keyboard_row_key_count(row);
        if (current_index < row_count) {
            col = current_index;
            break;
        }
        current_index -= row_count;
        row++;
    }
    if (row >= INK_WIFI_SETUP_KEYBOARD_ROWS) {
        row = INK_WIFI_SETUP_KEYBOARD_ROWS - 1;
        col = ink_wifi_setup_keyboard_row_key_count(row) - 1;
    }
    state->view.keyboard_row = row;
    state->view.keyboard_column = col;
}

static const char *current_keyboard_label(const ink_wifi_setup_app_state_t *state)
{
    if (state == NULL) {
        return "";
    }

    return ink_wifi_setup_keyboard_label(
        state->view.keyboard_layer,
        state->view.keyboard_column,
        state->view.keyboard_row);
}

static bool handle_list_mode(
    ink_system_runtime_t *runtime,
    ink_wifi_setup_app_state_t *state,
    const ink_app_event_t *event)
{
    const ink_wifi_scan_result_t *ap = NULL;
    ink_wifi_setup_ui_region_t region;

    if (runtime == NULL || state == NULL || event == NULL) {
        return false;
    }

    switch (event->kind) {
        case INK_APP_EVENT_BUTTON_BACK:
            return request_switch_to_launcher(runtime);
        case INK_APP_EVENT_NAV_PREVIOUS:
        case INK_APP_EVENT_TILT_PREVIOUS:
            ink_wifi_setup_cycle_selection(&state->view.wifi, -1);
            ink_wifi_setup_ui_list_body_region(&region);
            set_partial_refresh_region(state, &region, 4);
            return true;
        case INK_APP_EVENT_NAV_NEXT:
        case INK_APP_EVENT_TILT_NEXT:
            ink_wifi_setup_cycle_selection(&state->view.wifi, 1);
            ink_wifi_setup_ui_list_body_region(&region);
            set_partial_refresh_region(state, &region, 4);
            return true;
        case INK_APP_EVENT_BUTTON_CONFIRM:
            if (ink_wifi_setup_is_scan_selected(&state->view.wifi)) {
                if (!queue_scan(state)) {
                    state->view.wifi.scan_in_progress = false;
                    return false;
                }
                ink_wifi_setup_ui_list_body_region(&region);
                set_partial_refresh_region(state, &region, 4);
                return true;
            }
            ap = ink_wifi_setup_selected_ap(&state->view.wifi);
            if (ap == NULL) {
                return false;
            }
            if (ap->saved) {
                ink_wifi_setup_open_saved_menu(&state->view.wifi);
                ink_wifi_setup_ui_saved_menu_region(&region);
                set_partial_refresh_region(state, &region, 4);
                return true;
            }
            ink_wifi_setup_open_password(&state->view.wifi);
            ink_wifi_setup_keyboard_text_clear(&state->view.keyboard_text);
            state->view.keyboard_layer = INK_WIFI_SETUP_KEYBOARD_LAYER_LOWER;
            keyboard_reset_cursor(state);
            ink_wifi_setup_ui_password_screen_region(&region);
            set_partial_refresh_region(state, &region, 4);
            return true;
        default:
            return false;
    }
}

static bool handle_password_mode(
    ink_system_runtime_t *runtime,
    ink_wifi_setup_app_state_t *state,
    const ink_app_event_t *event)
{
    ink_wifi_setup_request_t request;
    const char *label = NULL;
    ink_wifi_setup_ui_region_t region;

    (void)runtime;
    if (state == NULL || event == NULL) {
        return false;
    }

    switch (event->kind) {
        case INK_APP_EVENT_BUTTON_BACK:
            return request_switch_to_launcher(runtime);
        case INK_APP_EVENT_NAV_PREVIOUS:
        case INK_APP_EVENT_TILT_PREVIOUS:
            keyboard_step(state, -1);
            ink_wifi_setup_ui_keyboard_footer_region(&region);
            set_partial_refresh_region(state, &region, 6);
            return true;
        case INK_APP_EVENT_NAV_NEXT:
        case INK_APP_EVENT_TILT_NEXT:
            keyboard_step(state, 1);
            ink_wifi_setup_ui_keyboard_footer_region(&region);
            set_partial_refresh_region(state, &region, 6);
            return true;
        case INK_APP_EVENT_BUTTON_CONFIRM:
            label = current_keyboard_label(state);
            if (strcmp(label, "ok") == 0) {
                if (!ink_wifi_setup_prepare_selected_connect_request(
                        &state->view.keyboard_text,
                        &state->view.wifi,
                        &request)) {
                    ink_wifi_setup_finish_connecting(&state->view.wifi, ESP_ERR_INVALID_STATE);
                    ink_wifi_setup_ui_full_screen_region(&region);
                    set_partial_refresh_region(state, &region, 0);
                    return true;
                }
                ink_wifi_setup_begin_connecting(&state->view.wifi);
                if (!post_work_request(state, state->active_token, &request)) {
                    ink_wifi_setup_finish_connecting(&state->view.wifi, ESP_ERR_NO_MEM);
                }
                ink_wifi_setup_ui_full_screen_region(&region);
                set_partial_refresh_region(state, &region, 0);
                return true;
            }
            if (!ink_wifi_setup_keyboard_activate_label(
                    &state->view.keyboard_text,
                    &state->view.keyboard_layer,
                    label)) {
                return false;
            }
            if (strcmp(label, "abc") == 0 || strcmp(label, "ABC") == 0 || strcmp(label, "sym") == 0) {
                ink_wifi_setup_ui_keyboard_footer_region(&region);
                set_partial_refresh_region(state, &region, 6);
            } else {
                ink_wifi_setup_ui_password_box_region(&region);
                set_partial_refresh_region(state, &region, 4);
            }
            return true;
        default:
            return false;
    }
}

static bool handle_saved_menu_mode(
    ink_system_runtime_t *runtime,
    ink_wifi_setup_app_state_t *state,
    const ink_app_event_t *event)
{
    ink_wifi_setup_request_t request;
    ink_wifi_setup_ui_region_t region;

    (void)runtime;
    if (state == NULL || event == NULL) {
        return false;
    }

    switch (event->kind) {
        case INK_APP_EVENT_BUTTON_BACK:
            return request_switch_to_launcher(runtime);
        case INK_APP_EVENT_NAV_PREVIOUS:
        case INK_APP_EVENT_NAV_NEXT:
        case INK_APP_EVENT_TILT_PREVIOUS:
        case INK_APP_EVENT_TILT_NEXT:
            state->view.wifi.menu_index = state->view.wifi.menu_index == 0 ? 1 : 0;
            ink_wifi_setup_ui_saved_menu_region(&region);
            set_partial_refresh_region(state, &region, 4);
            return true;
        case INK_APP_EVENT_BUTTON_CONFIRM:
            if (state->view.wifi.menu_index == 0) {
                if (!ink_wifi_setup_prepare_selected_saved_request(
                        &state->view.wifi,
                        INK_WIFI_SETUP_REQUEST_CONNECT_SAVED,
                        &request)) {
                    ink_wifi_setup_finish_connecting(&state->view.wifi, ESP_ERR_INVALID_STATE);
                    return true;
                }
                ink_wifi_setup_begin_connecting(&state->view.wifi);
                if (!post_work_request(state, state->active_token, &request)) {
                    ink_wifi_setup_finish_connecting(&state->view.wifi, ESP_ERR_NO_MEM);
                }
                ink_wifi_setup_ui_full_screen_region(&region);
                set_partial_refresh_region(state, &region, 0);
                return true;
            }
            if (!ink_wifi_setup_prepare_selected_saved_request(
                    &state->view.wifi,
                    INK_WIFI_SETUP_REQUEST_DELETE_SAVED,
                    &request)) {
                return false;
            }
            ink_wifi_setup_cancel_popup(&state->view.wifi);
            if (!post_work_request(state, state->active_token, &request)) {
                return false;
            }
            ink_wifi_setup_ui_list_body_region(&region);
            set_partial_refresh_region(state, &region, 4);
            return true;
        default:
            return false;
    }
}

static bool handle_result_mode(
    ink_system_runtime_t *runtime,
    ink_wifi_setup_app_state_t *state,
    const ink_app_event_t *event)
{
    ink_wifi_setup_ui_region_t region;

    (void)runtime;
    if (state == NULL || event == NULL) {
        return false;
    }

    if (event->kind == INK_APP_EVENT_BUTTON_BACK) {
        return request_switch_to_launcher(runtime);
    }
    if (event->kind == INK_APP_EVENT_BUTTON_CONFIRM) {
        ink_wifi_setup_confirm_result(&state->view.wifi);
        ink_wifi_setup_ui_list_body_region(&region);
        set_partial_refresh_region(state, &region, 4);
        return true;
    }
    return false;
}

static bool wifi_setup_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    ink_wifi_setup_app_state_t *state = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL || event == NULL) {
        return false;
    }

    state = (ink_wifi_setup_app_state_t *)app->state;
    switch (state->view.wifi.mode) {
        case WIFI_SETUP_UI_LIST:
            return handle_list_mode(runtime, state, event);
        case WIFI_SETUP_UI_PASSWORD:
            return handle_password_mode(runtime, state, event);
        case WIFI_SETUP_UI_SAVED_MENU:
            return handle_saved_menu_mode(runtime, state, event);
        case WIFI_SETUP_UI_RESULT:
            return handle_result_mode(runtime, state, event);
        case WIFI_SETUP_UI_CONNECTING:
            return event->kind == INK_APP_EVENT_BUTTON_BACK
                ? request_switch_to_launcher(runtime)
                : false;
        default:
            return false;
    }
}

static bool apply_work_result(
    ink_wifi_setup_app_state_t *state,
    const ink_wifi_setup_app_work_result_t *result)
{
    uint16_t index = 0;

    if (state == NULL || result == NULL) {
        return false;
    }
    if (result->token != state->active_token) {
        return false;
    }

    switch (result->type) {
        case INK_WIFI_SETUP_REQUEST_SCAN:
            state->view.wifi.scan_in_progress = false;
            if (result->result == ESP_OK) {
                state->view.wifi.scan = result->scan;
                ink_wifi_setup_sort_scan(&state->view.wifi);
                if (state->view.wifi.selected_index < 0
                    || state->view.wifi.selected_index >= ink_wifi_setup_selectable_count(&state->view.wifi)) {
                    ink_wifi_setup_select_strongest(&state->view.wifi);
                }
            } else {
                state->view.wifi.selected_index = (int)state->view.wifi.scan.count;
            }
            {
                ink_wifi_setup_ui_region_t region;
                ink_wifi_setup_ui_list_body_region(&region);
                set_partial_refresh_region(state, &region, 4);
            }
            return true;
        case INK_WIFI_SETUP_REQUEST_CONNECT_PASSWORD:
            state->view.wifi.status = result->status;
            ink_wifi_setup_finish_connecting(&state->view.wifi, result->result);
            for (index = 0; index < state->view.wifi.scan.count; ++index) {
                if (strcmp(state->view.wifi.scan.results[index].ssid, result->ssid) == 0) {
                    state->view.wifi.scan.results[index].saved = result->result == ESP_OK;
                }
            }
            ink_wifi_setup_sort_scan(&state->view.wifi);
            {
                ink_wifi_setup_ui_region_t region;
                ink_wifi_setup_ui_full_screen_region(&region);
                set_partial_refresh_region(state, &region, 0);
            }
            return true;
        case INK_WIFI_SETUP_REQUEST_CONNECT_SAVED:
            state->view.wifi.status = result->status;
            ink_wifi_setup_finish_connecting(&state->view.wifi, result->result);
            ink_wifi_setup_sort_scan(&state->view.wifi);
            {
                ink_wifi_setup_ui_region_t region;
                ink_wifi_setup_ui_full_screen_region(&region);
                set_partial_refresh_region(state, &region, 0);
            }
            return true;
        case INK_WIFI_SETUP_REQUEST_DELETE_SAVED:
            for (index = 0; index < state->view.wifi.scan.count; ++index) {
                if (strcmp(state->view.wifi.scan.results[index].ssid, result->ssid) == 0) {
                    state->view.wifi.scan.results[index].saved = false;
                }
            }
            ink_wifi_setup_sort_scan(&state->view.wifi);
            {
                ink_wifi_setup_ui_region_t region;
                ink_wifi_setup_ui_list_body_region(&region);
                set_partial_refresh_region(state, &region, 4);
            }
            return true;
        default:
            return false;
    }
}

static bool wifi_setup_tick(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    uint32_t now_ms)
{
    ink_wifi_setup_app_state_t *state = NULL;
    ink_wifi_setup_app_work_result_t result;
    bool dirty = false;

    (void)runtime;
    (void)now_ms;
    if (app == NULL || app->state == NULL) {
        return false;
    }

    state = (ink_wifi_setup_app_state_t *)app->state;
    if (state->result_queue == NULL) {
        return false;
    }

    while (xQueueReceive(state->result_queue, &result, 0) == pdTRUE) {
        dirty |= apply_work_result(state, &result);
    }
    return dirty;
}

static bool wifi_setup_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model)
{
    ink_wifi_setup_app_state_t *state = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL || out_model == NULL) {
        return false;
    }

    state = (ink_wifi_setup_app_state_t *)app->state;
    memset(out_model, 0, sizeof(*out_model));
    out_model->mode = INK_APP_RENDER_MODE_WIFI_SETUP;
    out_model->request_full_refresh = runtime->force_full_refresh_on_next_render;
    out_model->request_partial_refresh = !out_model->request_full_refresh
        && state->partial_refresh_pending;
    out_model->partial_x = state->partial_x;
    out_model->partial_y = state->partial_y;
    out_model->partial_w = state->partial_w;
    out_model->partial_h = state->partial_h;
    out_model->state = &state->render_state;
    runtime->force_full_refresh_on_next_render = false;
    clear_partial_refresh(state);
    return true;
}

static void wifi_setup_worker_task(void *arg)
{
    ink_wifi_setup_app_state_t *state = (ink_wifi_setup_app_state_t *)arg;
    ink_wifi_setup_app_work_item_t item;

    for (;;) {
        ink_wifi_setup_app_work_result_t result;

        if (state == NULL || state->request_queue == NULL || state->result_queue == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (xQueueReceive(state->request_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        memset(&result, 0, sizeof(result));
        result.token = item.token;
        result.type = item.request.type;
        snprintf(result.ssid, sizeof(result.ssid), "%s", item.request.ssid);

        switch (item.request.type) {
            case INK_WIFI_SETUP_REQUEST_SCAN:
                result.result = ink_wifi_manager_scan(&result.scan);
                (void)ink_wifi_manager_status(&result.status);
                break;
            case INK_WIFI_SETUP_REQUEST_CONNECT_PASSWORD:
                result.result = ink_wifi_manager_connect_password(
                    item.request.ssid,
                    item.request.password,
                    10000,
                    &result.status);
                if (result.result == ESP_OK) {
                    result.result = ink_wifi_manager_save_credential(
                        item.request.ssid,
                        item.request.password);
                    if (result.result != ESP_OK) {
                        result.status.connected = false;
                        result.status.last_error = result.result;
                    }
                }
                break;
            case INK_WIFI_SETUP_REQUEST_CONNECT_SAVED:
                result.result = ink_wifi_manager_connect_saved(
                    item.request.ssid,
                    10000,
                    &result.status);
                break;
            case INK_WIFI_SETUP_REQUEST_DELETE_SAVED:
                result.result = ink_wifi_manager_delete_credential(item.request.ssid);
                break;
            default:
                result.result = ESP_ERR_INVALID_ARG;
                break;
        }

        if (xQueueSend(state->result_queue, &result, portMAX_DELAY) != pdTRUE) {
            ESP_LOGW(TAG, "dropping worker result type=%d", (int)item.request.type);
        }
    }
}

static bool wifi_setup_initial_refresh_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_render_model_t model;
    const ink_app_descriptor_t *app = ink_wifi_setup_app_descriptor();

    ink_system_runtime_init(&runtime);
    runtime.force_full_refresh_on_next_render = true;
    if (!app->render(&runtime, app, &model)) {
        return false;
    }
    return model.mode == INK_APP_RENDER_MODE_WIFI_SETUP
        && model.request_full_refresh;
}

static bool wifi_setup_worker_stack_budget_self_test(void)
{
    return INK_WIFI_SETUP_APP_WORKER_STACK >= 6144;
}

static bool wifi_setup_queue_flush_self_test(void)
{
    ink_wifi_setup_app_state_t state;

    memset(&state, 0, sizeof(state));
    state.partial_refresh_pending = true;
    state.partial_x = 1;
    state.partial_y = 2;
    state.partial_w = 3;
    state.partial_h = 4;

    clear_partial_refresh(&state);
    return !state.partial_refresh_pending
        && state.partial_x == 0
        && state.partial_y == 0
        && state.partial_w == 0
        && state.partial_h == 0;
}

static bool wifi_setup_back_returns_launcher_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_event_t event = {
        .kind = INK_APP_EVENT_BUTTON_BACK,
    };
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *wifi = ink_wifi_setup_app_descriptor();
    ink_wifi_setup_app_state_t *state = &s_wifi_setup_state;

    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, wifi)) {
        return false;
    }

    runtime.active_app = wifi;
    state->view.wifi.mode = WIFI_SETUP_UI_LIST;
    if (!wifi->input(&runtime, wifi, &event)
        || runtime.pending_app != launcher
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    runtime.pending_app = NULL;
    runtime.force_full_refresh_on_next_render = false;
    state->view.wifi.mode = WIFI_SETUP_UI_PASSWORD;
    if (!wifi->input(&runtime, wifi, &event)
        || runtime.pending_app != launcher
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    runtime.pending_app = NULL;
    runtime.force_full_refresh_on_next_render = false;
    state->view.wifi.mode = WIFI_SETUP_UI_SAVED_MENU;
    if (!wifi->input(&runtime, wifi, &event)
        || runtime.pending_app != launcher
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    runtime.pending_app = NULL;
    runtime.force_full_refresh_on_next_render = false;
    state->view.wifi.mode = WIFI_SETUP_UI_CONNECTING;
    if (!wifi->input(&runtime, wifi, &event)
        || runtime.pending_app != launcher
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    runtime.pending_app = NULL;
    runtime.force_full_refresh_on_next_render = false;
    state->view.wifi.mode = WIFI_SETUP_UI_RESULT;
    if (!wifi->input(&runtime, wifi, &event)
        || runtime.pending_app != launcher
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    return true;
}

static bool wifi_setup_scan_completion_self_test(void)
{
    ink_wifi_setup_app_state_t state;
    ink_wifi_setup_app_work_result_t result;

    memset(&state, 0, sizeof(state));
    state.active_token = 7U;
    state.view.wifi.scan_in_progress = true;
    result.token = 7U;
    result.type = INK_WIFI_SETUP_REQUEST_SCAN;
    result.result = ESP_OK;
    result.scan.count = 2;
    snprintf(result.scan.results[0].ssid, sizeof(result.scan.results[0].ssid), "%s", "weak");
    result.scan.results[0].rssi = -75;
    snprintf(result.scan.results[1].ssid, sizeof(result.scan.results[1].ssid), "%s", "strong");
    result.scan.results[1].rssi = -35;

    if (!apply_work_result(&state, &result)) {
        return false;
    }
    return !state.view.wifi.scan_in_progress
        && state.view.wifi.scan.count == 2
        && state.view.wifi.selected_index == 0
        && strcmp(state.view.wifi.scan.results[0].ssid, "strong") == 0;
}

static bool wifi_setup_ignores_inactive_completion_self_test(void)
{
    ink_wifi_setup_app_state_t state;
    ink_wifi_setup_app_work_result_t result;

    memset(&state, 0, sizeof(state));
    state.active_token = 3U;
    result.token = 2U;
    result.type = INK_WIFI_SETUP_REQUEST_SCAN;
    result.result = ESP_OK;
    result.scan.count = 1;

    if (apply_work_result(&state, &result)) {
        return false;
    }
    return state.view.wifi.scan.count == 0;
}

bool ink_wifi_setup_app_self_test(void)
{
    return wifi_setup_worker_stack_budget_self_test()
        && wifi_setup_queue_flush_self_test()
        && wifi_setup_initial_refresh_self_test()
        && wifi_setup_back_returns_launcher_self_test()
        && wifi_setup_scan_completion_self_test()
        && wifi_setup_ignores_inactive_completion_self_test();
}
