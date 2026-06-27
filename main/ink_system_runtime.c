#include "ink_system_runtime.h"

#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ink_app_priv.h"
#include "ink_system_services.h"

static void drain_ui_queue(ink_system_runtime_t *runtime);
static bool runtime_switch_clears_runtime_services_self_test(void);

void ink_system_runtime_init(ink_system_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
}

void ink_system_runtime_bind_services(
    ink_system_runtime_t *runtime,
    ink_system_services_t *services)
{
    if (runtime == NULL) {
        return;
    }

    runtime->services = services;
}

bool ink_system_runtime_register_app(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    if (runtime == NULL || app == NULL || runtime->app_count >= INK_SYSTEM_RUNTIME_APP_CAPACITY) {
        return false;
    }

    runtime->apps[runtime->app_count++] = app;
    return true;
}

const ink_app_descriptor_t *ink_system_runtime_find_app_by_id(
    const ink_system_runtime_t *runtime,
    const char *app_id)
{
    size_t index;

    if (runtime == NULL || app_id == NULL || app_id[0] == '\0') {
        return NULL;
    }

    for (index = 0U; index < runtime->app_count; ++index) {
        const ink_app_descriptor_t *app = runtime->apps[index];
        if (app != NULL && app->id != NULL && strcmp(app->id, app_id) == 0) {
            return app;
        }
    }

    return NULL;
}

bool ink_system_runtime_has_active_app(const ink_system_runtime_t *runtime)
{
    return runtime != NULL && runtime->active_app != NULL;
}

const ink_app_descriptor_t *ink_system_runtime_active_app(const ink_system_runtime_t *runtime)
{
    if (runtime == NULL) {
        return NULL;
    }

    return runtime->active_app;
}

bool ink_system_runtime_set_active_app(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    if (runtime == NULL || app == NULL) {
        return false;
    }

    if (runtime->active_app != NULL && runtime->active_app->exit != NULL) {
        runtime->active_app->exit(runtime, runtime->active_app);
    }
    runtime->active_app = app;
    runtime->pending_app = NULL;
    if (runtime->active_app->enter != NULL) {
        runtime->active_app->enter(runtime, runtime->active_app);
    }
    return true;
}

static void drain_ui_queue(ink_system_runtime_t *runtime)
{
    if (runtime == NULL || runtime->services == NULL || runtime->services->ui_queue == NULL) {
        return;
    }

    xQueueReset(runtime->services->ui_queue);
}

bool ink_system_runtime_switch_now(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    if (runtime == NULL || app == NULL) {
        return false;
    }

    if (runtime->active_app != NULL && runtime->active_app->exit != NULL) {
        runtime->active_app->exit(runtime, runtime->active_app);
    }
    drain_ui_queue(runtime);
    if (runtime->services != NULL) {
        ink_display_mailbox_discard_queued_only(&runtime->services->mailbox);
    }
    runtime->active_app = app;
    runtime->pending_app = NULL;
    runtime->force_full_refresh_on_next_render = true;
    if (runtime->active_app->enter != NULL) {
        runtime->active_app->enter(runtime, runtime->active_app);
    }
    return true;
}

bool ink_system_runtime_request_switch(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    if (runtime == NULL || app == NULL) {
        return false;
    }

    runtime->pending_app = app;
    runtime->force_full_refresh_on_next_render = true;
    return true;
}

bool ink_system_runtime_dispatch_input(
    ink_system_runtime_t *runtime,
    const ink_app_event_t *event)
{
    bool dirty = false;

    if (runtime == NULL || event == NULL || runtime->active_app == NULL) {
        return false;
    }

    if (runtime->active_app->input != NULL) {
        dirty = runtime->active_app->input(runtime, runtime->active_app, event);
    }
    if (runtime->pending_app != NULL) {
        if (!ink_system_runtime_switch_now(runtime, runtime->pending_app)) {
            return false;
        }
        dirty = true;
    }

    return dirty;
}

bool ink_system_runtime_dispatch_tick(ink_system_runtime_t *runtime, uint32_t now_ms)
{
    bool dirty = false;

    if (runtime == NULL || runtime->active_app == NULL || runtime->active_app->tick == NULL) {
        return false;
    }

    dirty = runtime->active_app->tick(runtime, runtime->active_app, now_ms);
    if (runtime->pending_app != NULL) {
        if (!ink_system_runtime_switch_now(runtime, runtime->pending_app)) {
            return false;
        }
        dirty = true;
    }
    return dirty;
}

void ink_system_runtime_handle_display_done(ink_system_runtime_t *runtime, uint32_t event_ms)
{
    ink_app_event_t event;

    if (runtime == NULL || runtime->active_app == NULL || runtime->active_app->input == NULL) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.kind = INK_APP_EVENT_DISPLAY_DONE;
    event.event_ms = event_ms;
    (void)runtime->active_app->input(runtime, runtime->active_app, &event);
}

bool ink_system_runtime_self_test(void)
{
    static const ink_app_descriptor_t kAppA = {
        .id = "app-a",
        .name = "App A",
    };
    static const ink_app_descriptor_t kAppB = {
        .id = "app-b",
        .name = "App B",
    };
    static const ink_app_descriptor_t kAppC = {
        .id = "app-c",
        .name = "App C",
    };
    static const ink_app_descriptor_t kAppD = {
        .id = "app-d",
        .name = "App D",
    };
    static const ink_app_descriptor_t kAppE = {
        .id = "app-e",
        .name = "App E",
    };
    ink_system_runtime_t runtime;

    ink_system_runtime_init(&runtime);
    if (ink_system_runtime_register_app(NULL, &kAppA)
        || ink_system_runtime_register_app(&runtime, NULL)
        || ink_system_runtime_set_active_app(NULL, &kAppA)
        || ink_system_runtime_set_active_app(&runtime, NULL)
        || ink_system_runtime_request_switch(NULL, &kAppA)
        || ink_system_runtime_request_switch(&runtime, NULL)) {
        return false;
    }
    if (!ink_system_runtime_register_app(&runtime, &kAppA)
        || !ink_system_runtime_register_app(&runtime, &kAppB)) {
        return false;
    }
    if (ink_system_runtime_find_app_by_id(&runtime, "app-a") != &kAppA
        || ink_system_runtime_find_app_by_id(&runtime, "missing") != NULL
        || ink_system_runtime_find_app_by_id(NULL, "app-a") != NULL
        || ink_system_runtime_find_app_by_id(&runtime, NULL) != NULL
        || ink_system_runtime_has_active_app(NULL)) {
        return false;
    }
    if (runtime.app_count != 2U) {
        return false;
    }
    if (!ink_system_runtime_register_app(&runtime, &kAppC)
        || !ink_system_runtime_register_app(&runtime, &kAppD)) {
        return false;
    }
    if (runtime.app_count != INK_SYSTEM_RUNTIME_APP_CAPACITY) {
        return false;
    }
    if (ink_system_runtime_register_app(&runtime, &kAppE)) {
        return false;
    }
    if (!ink_system_runtime_set_active_app(&runtime, &kAppA)) {
        return false;
    }
    if (runtime.active_app != &kAppA || !ink_system_runtime_has_active_app(&runtime)) {
        return false;
    }
    if (!ink_system_runtime_request_switch(&runtime, &kAppB)) {
        return false;
    }

    if (runtime.pending_app != &kAppB
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    return runtime_switch_clears_runtime_services_self_test();
}

static bool runtime_switch_clears_runtime_services_self_test(void)
{
    static const ink_app_descriptor_t kLauncher = {
        .id = "launcher",
        .name = "Launcher",
    };
    static const ink_app_descriptor_t kWifi = {
        .id = "wifi",
        .name = "WiFi",
    };
    ink_system_runtime_t runtime;
    ink_system_services_t services;
    ink_display_request_t request;
    ink_display_request_t claimed;
    ink_ui_event_t queued_event;

    memset(&services, 0, sizeof(services));
    memset(&request, 0, sizeof(request));
    memset(&claimed, 0, sizeof(claimed));
    memset(&queued_event, 0, sizeof(queued_event));

    services.ui_queue = xQueueCreate(2, sizeof(ink_ui_event_t));
    if (services.ui_queue == NULL) {
        return false;
    }
    ink_display_mailbox_init(&services.mailbox, NULL, NULL, NULL, NULL, NULL);

    ink_system_runtime_init(&runtime);
    ink_system_runtime_bind_services(&runtime, &services);
    if (!ink_system_runtime_register_app(&runtime, &kLauncher)
        || !ink_system_runtime_register_app(&runtime, &kWifi)
        || !ink_system_runtime_set_active_app(&runtime, &kLauncher)) {
        vQueueDelete(services.ui_queue);
        return false;
    }

    queued_event.kind = INK_UI_EVENT_BUTTON;
    if (xQueueSend(services.ui_queue, &queued_event, 0) != pdTRUE) {
        vQueueDelete(services.ui_queue);
        return false;
    }
    request.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    if (ink_display_mailbox_submit(&services.mailbox, &request) == 0U) {
        vQueueDelete(services.ui_queue);
        return false;
    }

    if (!ink_system_runtime_switch_now(&runtime, &kWifi)) {
        vQueueDelete(services.ui_queue);
        return false;
    }

    if (uxQueueMessagesWaiting(services.ui_queue) != 0U
        || !ink_display_mailbox_is_idle(&services.mailbox)
        || ink_display_mailbox_try_claim_latest(&services.mailbox, &claimed)
        || runtime.active_app != &kWifi
        || !runtime.force_full_refresh_on_next_render) {
        vQueueDelete(services.ui_queue);
        return false;
    }

    vQueueDelete(services.ui_queue);
    return true;
}
