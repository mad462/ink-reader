#include "ink_usb_msc_coordinator.h"

#include <string.h>

#include "esp_log.h"

#include "apps/ink_launcher_app.h"
#include "apps/ink_usb_msc_app.h"
#include "ink_system_runtime.h"
#include "ink_system_services.h"
#include "ink_usb_msc_service.h"

static const char *TAG = "ink_usb_coord";

static bool request_usb_app_switch(ink_system_runtime_t *runtime);
static bool restore_previous_app(
    ink_usb_msc_coordinator_t *coordinator,
    ink_system_runtime_t *runtime);
static bool ensure_usb_page_visible(
    ink_usb_msc_coordinator_t *coordinator,
    ink_system_runtime_t *runtime);
static bool ensure_export_mode(
    ink_usb_msc_coordinator_t *coordinator,
    ink_system_runtime_t *runtime);
static bool usb_state_wants_page(const ink_system_services_t *services);
static void sync_usb_edge_state(
    ink_usb_msc_coordinator_t *coordinator,
    const ink_system_services_t *services);

void ink_usb_msc_coordinator_init(ink_usb_msc_coordinator_t *coordinator)
{
    if (coordinator == NULL) {
        return;
    }

    memset(coordinator, 0, sizeof(*coordinator));
    coordinator->initialized = true;
}

static bool usb_state_wants_page(const ink_system_services_t *services)
{
    if (services == NULL) {
        return false;
    }

    return services->usb_msc.state == INK_USB_MSC_STATE_PROMPT
        || services->usb_msc.state == INK_USB_MSC_STATE_ACTIVE
        || services->usb_msc.state == INK_USB_MSC_STATE_ERROR;
}

static void sync_usb_edge_state(
    ink_usb_msc_coordinator_t *coordinator,
    const ink_system_services_t *services)
{
    const bool attached = services != NULL && services->usb_msc.usb_attached;

    if (coordinator == NULL) {
        return;
    }

    if (attached && !coordinator->last_usb_attached && coordinator->suppress_reentry_until_detach) {
        coordinator->suppress_reentry_until_detach = false;
        ESP_LOGI(TAG, "new attach edge, clear manual-exit suppression");
    } else if (!attached && coordinator->last_usb_attached && coordinator->suppress_reentry_until_detach) {
        coordinator->suppress_reentry_until_detach = false;
        ESP_LOGI(TAG, "detach edge, clear manual-exit suppression");
    }
    coordinator->last_usb_attached = attached;
}

static bool request_usb_app_switch(ink_system_runtime_t *runtime)
{
    const ink_app_descriptor_t *usb_app = ink_system_runtime_find_app_by_id(runtime, "usb_msc");

    return usb_app != NULL
        && ink_system_runtime_switch_now(runtime, usb_app);
}

static bool restore_previous_app(
    ink_usb_msc_coordinator_t *coordinator,
    ink_system_runtime_t *runtime)
{
    const ink_app_descriptor_t *target = NULL;

    if (coordinator == NULL || runtime == NULL) {
        return false;
    }

    if (coordinator->return_app_id[0] == '\0') {
        target = ink_system_runtime_find_app_by_id(runtime, "launcher");
    } else {
        target = ink_system_runtime_find_app_by_id(runtime, coordinator->return_app_id);
    }

    coordinator->prompt_visible = false;
    coordinator->restore_pending = false;
    coordinator->return_app_id[0] = '\0';
    return target != NULL && ink_system_runtime_switch_now(runtime, target);
}

static bool ensure_usb_page_visible(
    ink_usb_msc_coordinator_t *coordinator,
    ink_system_runtime_t *runtime)
{
    const ink_app_descriptor_t *active = NULL;

    if (coordinator == NULL || runtime == NULL) {
        return false;
    }

    active = ink_system_runtime_active_app(runtime);
    if (active != NULL && active->id != NULL && strcmp(active->id, "usb_msc") != 0) {
        snprintf(coordinator->return_app_id, sizeof(coordinator->return_app_id), "%s", active->id);
        ESP_LOGI(TAG, "remember return app=%s", coordinator->return_app_id);
    }
    coordinator->prompt_visible = true;
    ESP_LOGI(TAG, "switch to usb page state=%d", runtime->services != NULL ? runtime->services->usb_msc.state : -1);
    return request_usb_app_switch(runtime);
}

static bool ensure_export_mode(
    ink_usb_msc_coordinator_t *coordinator,
    ink_system_runtime_t *runtime)
{
    ink_system_services_t *services = NULL;

    if (coordinator == NULL || runtime == NULL || runtime->services == NULL) {
        return false;
    }

    services = runtime->services;
    if (services->usb_msc.state == INK_USB_MSC_STATE_ACTIVE) {
        ESP_LOGI(TAG, "usb already active, ensure page visible");
        return ensure_usb_page_visible(coordinator, runtime);
    }
    if (services->usb_msc.state != INK_USB_MSC_STATE_PROMPT) {
        return false;
    }
    ESP_LOGI(TAG, "enter export from prompt");
    if (ink_usb_msc_service_enter_export(&services->usb_msc, services) != ESP_OK) {
        ESP_LOGW(TAG, "enter export failed state=%d", (int)services->usb_msc.state);
        return ensure_usb_page_visible(coordinator, runtime);
    }
    return ensure_usb_page_visible(coordinator, runtime);
}

bool ink_usb_msc_coordinator_before_input(
    ink_usb_msc_coordinator_t *coordinator,
    ink_system_runtime_t *runtime,
    const ink_app_event_t *event,
    bool *handled_out,
    bool *dirty_out)
{
    ink_system_services_t *services = NULL;
    const ink_app_descriptor_t *active = NULL;

    if (handled_out != NULL) {
        *handled_out = false;
    }
    if (dirty_out != NULL) {
        *dirty_out = false;
    }
    if (coordinator == NULL || runtime == NULL || event == NULL || runtime->services == NULL) {
        return false;
    }

    services = runtime->services;
    active = ink_system_runtime_active_app(runtime);
    sync_usb_edge_state(coordinator, services);

    if (usb_state_wants_page(services)
        && !coordinator->prompt_visible
        && !coordinator->suppress_reentry_until_detach) {
        if (handled_out != NULL) {
            *handled_out = true;
        }
        if (dirty_out != NULL) {
            *dirty_out = services->usb_msc.state == INK_USB_MSC_STATE_PROMPT
                ? ensure_export_mode(coordinator, runtime)
                : ensure_usb_page_visible(coordinator, runtime);
        }
        return true;
    }

    if (active == NULL || active->id == NULL || strcmp(active->id, "usb_msc") != 0) {
        return false;
    }

    switch (event->kind) {
        case INK_APP_EVENT_BUTTON_CONFIRM:
            if (handled_out != NULL) {
                *handled_out = true;
            }
            if (dirty_out != NULL) {
                *dirty_out = true;
            }
            return true;
        case INK_APP_EVENT_BUTTON_BACK:
            if (handled_out != NULL) {
                *handled_out = true;
            }
            coordinator->suppress_reentry_until_detach = true;
            ESP_LOGI(
                TAG,
                "manual exit requested attached=%d exported=%d state=%d",
                services->usb_msc.usb_attached ? 1 : 0,
                services->usb_msc.tf_exported ? 1 : 0,
                (int)services->usb_msc.state);
            if (services->usb_msc.tf_exported) {
                (void)ink_usb_msc_service_exit_export(&services->usb_msc, services);
            }
            if (dirty_out != NULL) {
                *dirty_out = restore_previous_app(coordinator, runtime);
            }
            return true;
        default:
            break;
    }

    return false;
}

bool ink_usb_msc_coordinator_poll(
    ink_usb_msc_coordinator_t *coordinator,
    ink_system_runtime_t *runtime,
    uint32_t now_ms)
{
    ink_system_services_t *services = NULL;
    const ink_app_descriptor_t *active = NULL;

    (void)now_ms;
    if (coordinator == NULL || runtime == NULL || runtime->services == NULL) {
        return false;
    }

    services = runtime->services;
    active = ink_system_runtime_active_app(runtime);
    sync_usb_edge_state(coordinator, services);

    if (usb_state_wants_page(services)
        && !coordinator->prompt_visible
        && !coordinator->suppress_reentry_until_detach) {
        if (services->usb_msc.state == INK_USB_MSC_STATE_PROMPT) {
            return ensure_export_mode(coordinator, runtime);
        }
        return ensure_usb_page_visible(coordinator, runtime);
    }
    if (services->usb_msc.state != INK_USB_MSC_STATE_PROMPT
        && services->usb_msc.state != INK_USB_MSC_STATE_ACTIVE
        && services->usb_msc.state != INK_USB_MSC_STATE_ERROR) {
        coordinator->prompt_visible = false;
    }
    if (coordinator->prompt_visible
        && !services->usb_msc.usb_attached
        && active != NULL
        && active->id != NULL
        && strcmp(active->id, "usb_msc") == 0) {
        ESP_LOGI(TAG, "usb page visible and cable removed, restore previous app");
        return restore_previous_app(coordinator, runtime);
    }
    if (services->usb_msc.state == INK_USB_MSC_STATE_ACTIVE && !services->usb_msc.usb_attached) {
        ESP_LOGI(TAG, "active export lost attach, exit export and restore");
        (void)ink_usb_msc_service_exit_export(&services->usb_msc, services);
        return restore_previous_app(coordinator, runtime);
    }
    return false;
}

bool ink_usb_msc_coordinator_self_test(void)
{
    ink_usb_msc_coordinator_t coordinator;
    ink_system_runtime_t runtime;
    ink_system_services_t services;
    bool handled = false;
    bool dirty = false;
    ink_app_event_t event = {
        .kind = INK_APP_EVENT_BUTTON_CONFIRM,
    };
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *usb_msc = ink_usb_msc_app_descriptor();

    memset(&runtime, 0, sizeof(runtime));
    memset(&services, 0, sizeof(services));
    ink_usb_msc_coordinator_init(&coordinator);
    ink_usb_msc_service_reset(&services.usb_msc);
    services.usb_msc.initialized = true;
    services.usb_msc.usb_attached = true;
    services.usb_msc.state = INK_USB_MSC_STATE_PROMPT;
    coordinator.last_usb_attached = true;
    runtime.services = &services;
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, usb_msc)) {
        return false;
    }
    runtime.active_app = launcher;
    if (!ink_usb_msc_coordinator_poll(&coordinator, &runtime, 0U)) {
        return false;
    }
    if (!coordinator.prompt_visible
        || runtime.active_app != usb_msc
        || strcmp(coordinator.return_app_id, "launcher") != 0) {
        return false;
    }

    runtime.active_app = usb_msc;
    if (!ink_usb_msc_coordinator_before_input(&coordinator, &runtime, &event, &handled, &dirty)) {
        return false;
    }
    if (!handled || !dirty) {
        return false;
    }

    services.usb_msc.state = INK_USB_MSC_STATE_PROMPT;
    runtime.active_app = launcher;
    coordinator.prompt_visible = false;
    if (ink_usb_msc_coordinator_poll(&coordinator, &runtime, 1U)) {
        return false;
    }

    services.usb_msc.usb_attached = false;
    services.usb_msc.state = INK_USB_MSC_STATE_IDLE;
    if (ink_usb_msc_coordinator_poll(&coordinator, &runtime, 2U)) {
        return false;
    }

    services.usb_msc.usb_attached = true;
    services.usb_msc.state = INK_USB_MSC_STATE_PROMPT;
    coordinator.prompt_visible = false;
    runtime.active_app = launcher;
    return ink_usb_msc_coordinator_poll(&coordinator, &runtime, 3U)
        && runtime.active_app == usb_msc
        && !coordinator.suppress_reentry_until_detach;
}
