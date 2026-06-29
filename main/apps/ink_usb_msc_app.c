#include "apps/ink_usb_msc_app.h"

#include <stdio.h>
#include <string.h>

#include "apps/ink_launcher_app.h"
#include "ink_system_runtime.h"
#include "ink_system_services.h"
#include "ink_usb_msc_service.h"

static ink_usb_msc_app_state_t s_usb_msc_state;
static ink_usb_msc_app_render_state_t s_usb_msc_render_state;
static esp_err_t (*s_usb_msc_enter_export_fn)(
    ink_usb_msc_service_t *service,
    ink_system_services_t *services) = ink_usb_msc_service_enter_export;
static esp_err_t (*s_usb_msc_exit_export_fn)(
    ink_usb_msc_service_t *service,
    ink_system_services_t *services) = ink_usb_msc_service_exit_export;

static void usb_msc_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static void usb_msc_exit(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool usb_msc_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event);
static bool usb_msc_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model);
static void sync_usb_msc_lines(ink_usb_msc_app_state_t *state, const ink_usb_msc_service_t *service);
static esp_err_t usb_msc_enter_export_self_test_stub(
    ink_usb_msc_service_t *service,
    ink_system_services_t *services);
static esp_err_t usb_msc_exit_export_self_test_stub(
    ink_usb_msc_service_t *service,
    ink_system_services_t *services);
static bool usb_msc_prompt_self_test(void);

static const ink_app_descriptor_t kUsbMscApp = {
    .id = "usb_msc",
    .name = "USB MSC",
    .enter = usb_msc_enter,
    .exit = usb_msc_exit,
    .input = usb_msc_input,
    .render = usb_msc_render,
    .state = &s_usb_msc_state,
};

const ink_app_descriptor_t *ink_usb_msc_app_descriptor(void)
{
    return &kUsbMscApp;
}

static void sync_usb_msc_lines(ink_usb_msc_app_state_t *state, const ink_usb_msc_service_t *service)
{
    if (state == NULL) {
        return;
    }

    snprintf(state->title, sizeof(state->title), "%s", "USB 磁盘");
    state->line1[0] = '\0';
    state->line2[0] = '\0';
    state->line3[0] = '\0';

    if (service == NULL) {
        state->view = INK_USB_MSC_APP_VIEW_ERROR;
        snprintf(state->line1, sizeof(state->line1), "%s", "USB SERVICE UNAVAILABLE");
        return;
    }

    switch (service->state) {
        case INK_USB_MSC_STATE_PROMPT:
            state->view = INK_USB_MSC_APP_VIEW_PROMPT;
            snprintf(state->line1, sizeof(state->line1), "%s", "U盘模式 关");
            break;
        case INK_USB_MSC_STATE_ACTIVE:
            state->view = INK_USB_MSC_APP_VIEW_ACTIVE;
            snprintf(state->line1, sizeof(state->line1), "%s", "U盘模式 开");
            break;
        case INK_USB_MSC_STATE_ERROR:
            state->view = INK_USB_MSC_APP_VIEW_ERROR;
            snprintf(state->line1, sizeof(state->line1), "%s", "U盘模式 异常");
            if (service->status_text[0] != '\0') {
                snprintf(
                    state->line2,
                    sizeof(state->line2),
                    "%.*s",
                    (int)(sizeof(state->line2) - 1U),
                    service->status_text);
            }
            break;
        case INK_USB_MSC_STATE_IDLE:
        default:
            state->view = INK_USB_MSC_APP_VIEW_PROMPT;
            snprintf(state->line1, sizeof(state->line1), "%s", "U盘模式 关");
            break;
    }
}

static esp_err_t usb_msc_enter_export_self_test_stub(
    ink_usb_msc_service_t *service,
    ink_system_services_t *services)
{
    (void)services;
    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    service->tf_exported = true;
    service->state = INK_USB_MSC_STATE_ACTIVE;
    snprintf(service->status_text, sizeof(service->status_text), "%s", "USB MSC ACTIVE");
    return ESP_OK;
}

static esp_err_t usb_msc_exit_export_self_test_stub(
    ink_usb_msc_service_t *service,
    ink_system_services_t *services)
{
    (void)services;
    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    service->tf_exported = false;
    service->state = INK_USB_MSC_STATE_IDLE;
    snprintf(service->status_text, sizeof(service->status_text), "%s", "USB MSC EXITED");
    return ESP_OK;
}

static void usb_msc_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_usb_msc_app_state_t *state = NULL;
    ink_system_services_t *services = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL) {
        return;
    }

    state = (ink_usb_msc_app_state_t *)app->state;
    if (!state->initialized) {
        memset(state, 0, sizeof(*state));
        state->initialized = true;
    }
    services = runtime->services;
    if (services != NULL) {
        if (services->usb_msc.state == INK_USB_MSC_STATE_DISABLED) {
            (void)ink_usb_msc_service_init(&services->usb_msc);
        }
        if (services->usb_msc.state == INK_USB_MSC_STATE_IDLE) {
            services->usb_msc.state = INK_USB_MSC_STATE_PROMPT;
        }
        sync_usb_msc_lines(state, &services->usb_msc);
    } else {
        sync_usb_msc_lines(state, NULL);
    }
    memset(&s_usb_msc_render_state, 0, sizeof(s_usb_msc_render_state));
    s_usb_msc_render_state.state = state;
    s_usb_msc_render_state.menu_font = services != NULL ? &services->menu_font : NULL;
    s_usb_msc_render_state.footer_font = services != NULL ? &services->footer_font : NULL;
    ink_system_services_get_time_badge(
        services,
        s_usb_msc_render_state.header_meta,
        sizeof(s_usb_msc_render_state.header_meta));
    runtime->force_full_refresh_on_next_render = true;
}

static void usb_msc_exit(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_system_services_t *services = NULL;
    ink_usb_msc_app_state_t *state = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL) {
        return;
    }

    services = runtime->services;
    state = (ink_usb_msc_app_state_t *)app->state;
    if (services != NULL && services->usb_msc.tf_exported) {
        (void)s_usb_msc_exit_export_fn(&services->usb_msc, services);
    }
    sync_usb_msc_lines(state, services != NULL ? &services->usb_msc : NULL);
}

static bool usb_msc_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    ink_usb_msc_app_state_t *state = NULL;
    ink_system_services_t *services = NULL;
    const ink_app_descriptor_t *launcher = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL || event == NULL) {
        return false;
    }

    state = (ink_usb_msc_app_state_t *)app->state;
    services = runtime->services;
    if (event->kind == INK_APP_EVENT_DISPLAY_DONE) {
        return false;
    }

    if (event->kind == INK_APP_EVENT_BUTTON_BACK) {
        launcher = ink_system_runtime_find_app_by_id(runtime, "launcher");
        return launcher != NULL
            && ink_system_runtime_request_switch(runtime, launcher);
    }

    if (event->kind == INK_APP_EVENT_BUTTON_CONFIRM
        && services != NULL) {
        if (services->usb_msc.state == INK_USB_MSC_STATE_ACTIVE) {
            (void)s_usb_msc_exit_export_fn(&services->usb_msc, services);
        } else {
            (void)s_usb_msc_enter_export_fn(&services->usb_msc, services);
        }
        sync_usb_msc_lines(state, &services->usb_msc);
        return true;
    }

    sync_usb_msc_lines(state, services != NULL ? &services->usb_msc : NULL);
    return state != NULL;
}

static bool usb_msc_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model)
{
    ink_usb_msc_app_state_t *state = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL || out_model == NULL) {
        return false;
    }

    state = (ink_usb_msc_app_state_t *)app->state;
    sync_usb_msc_lines(state, runtime->services != NULL ? &runtime->services->usb_msc : NULL);
    memset(out_model, 0, sizeof(*out_model));
    out_model->mode = INK_APP_RENDER_MODE_USB_MSC;
    out_model->request_full_refresh = runtime->force_full_refresh_on_next_render;
    out_model->refresh_strategy = out_model->request_full_refresh
        ? INK_REFRESH_STRATEGY_PAGE_TRANSITION_FULL
        : INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST;
    s_usb_msc_render_state.state = state;
    s_usb_msc_render_state.menu_font = runtime->services != NULL ? &runtime->services->menu_font : NULL;
    s_usb_msc_render_state.footer_font = runtime->services != NULL ? &runtime->services->footer_font : NULL;
    ink_system_services_get_time_badge(
        runtime->services,
        s_usb_msc_render_state.header_meta,
        sizeof(s_usb_msc_render_state.header_meta));
    out_model->state = &s_usb_msc_render_state;
    runtime->force_full_refresh_on_next_render = false;
    return true;
}

static bool usb_msc_prompt_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_system_services_t services;
    ink_app_render_model_t model;
    ink_app_event_t confirm_event = {
        .kind = INK_APP_EVENT_BUTTON_CONFIRM,
    };
    ink_app_event_t back_event = {
        .kind = INK_APP_EVENT_BUTTON_BACK,
    };
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *usb_msc = ink_usb_msc_app_descriptor();

    memset(&runtime, 0, sizeof(runtime));
    memset(&services, 0, sizeof(services));
    ink_usb_msc_service_reset(&services.usb_msc);
    services.usb_msc.initialized = true;
    services.usb_msc.state = INK_USB_MSC_STATE_IDLE;
    services.tf_ready = true;
    runtime.services = &services;
    runtime.force_full_refresh_on_next_render = true;

    s_usb_msc_enter_export_fn = usb_msc_enter_export_self_test_stub;
    s_usb_msc_exit_export_fn = usb_msc_exit_export_self_test_stub;

    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, usb_msc)
        || !ink_system_runtime_set_active_app(&runtime, usb_msc)) {
        s_usb_msc_enter_export_fn = ink_usb_msc_service_enter_export;
        s_usb_msc_exit_export_fn = ink_usb_msc_service_exit_export;
        return false;
    }

    if (!usb_msc_render(&runtime, usb_msc, &model)) {
        s_usb_msc_enter_export_fn = ink_usb_msc_service_enter_export;
        s_usb_msc_exit_export_fn = ink_usb_msc_service_exit_export;
        return false;
    }

    if (model.mode != INK_APP_RENDER_MODE_USB_MSC
        || !model.request_full_refresh
        || model.state != &s_usb_msc_render_state
        || s_usb_msc_state.view != INK_USB_MSC_APP_VIEW_PROMPT) {
        s_usb_msc_enter_export_fn = ink_usb_msc_service_enter_export;
        s_usb_msc_exit_export_fn = ink_usb_msc_service_exit_export;
        return false;
    }

    if (!usb_msc_input(&runtime, usb_msc, &confirm_event)
        || services.usb_msc.state != INK_USB_MSC_STATE_ACTIVE
        || !services.usb_msc.tf_exported) {
        s_usb_msc_enter_export_fn = ink_usb_msc_service_enter_export;
        s_usb_msc_exit_export_fn = ink_usb_msc_service_exit_export;
        return false;
    }

    if (!usb_msc_input(&runtime, usb_msc, &confirm_event)
        || services.usb_msc.state != INK_USB_MSC_STATE_IDLE
        || services.usb_msc.tf_exported) {
        s_usb_msc_enter_export_fn = ink_usb_msc_service_enter_export;
        s_usb_msc_exit_export_fn = ink_usb_msc_service_exit_export;
        return false;
    }

    if (!usb_msc_input(&runtime, usb_msc, &back_event)
        || runtime.pending_app != launcher) {
        s_usb_msc_enter_export_fn = ink_usb_msc_service_enter_export;
        s_usb_msc_exit_export_fn = ink_usb_msc_service_exit_export;
        return false;
    }

    if (!ink_system_runtime_switch_now(&runtime, launcher)
        || services.usb_msc.tf_exported) {
        s_usb_msc_enter_export_fn = ink_usb_msc_service_enter_export;
        s_usb_msc_exit_export_fn = ink_usb_msc_service_exit_export;
        return false;
    }

    s_usb_msc_enter_export_fn = ink_usb_msc_service_enter_export;
    s_usb_msc_exit_export_fn = ink_usb_msc_service_exit_export;
    return true;
}

bool ink_usb_msc_app_self_test(void)
{
    return usb_msc_prompt_self_test();
}
