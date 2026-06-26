#include "ink_system_runtime.h"

#include <stddef.h>
#include <string.h>

enum {
    INK_SYSTEM_RUNTIME_APP_CAPACITY = 4,
};

static void ink_system_runtime_init(ink_system_runtime_t *runtime);
static bool ink_system_runtime_register_app(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool ink_system_runtime_set_active_app(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool ink_system_runtime_request_switch(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);

static void ink_system_runtime_init(ink_system_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
}

static bool ink_system_runtime_register_app(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    if (runtime == NULL || app == NULL || runtime->app_count >= INK_SYSTEM_RUNTIME_APP_CAPACITY) {
        return false;
    }

    runtime->apps[runtime->app_count++] = app;
    return true;
}

static bool ink_system_runtime_set_active_app(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    if (runtime == NULL || app == NULL) {
        return false;
    }

    runtime->active_app = app;
    return true;
}

static bool ink_system_runtime_request_switch(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    if (runtime == NULL || app == NULL) {
        return false;
    }

    runtime->pending_app = app;
    runtime->force_full_refresh_on_next_render = true;
    return true;
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
    ink_system_runtime_t runtime;

    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, &kAppA)
        || !ink_system_runtime_register_app(&runtime, &kAppB)) {
        return false;
    }
    if (runtime.app_count != 2U) {
        return false;
    }
    if (!ink_system_runtime_set_active_app(&runtime, &kAppA)) {
        return false;
    }
    if (runtime.active_app != &kAppA) {
        return false;
    }
    if (!ink_system_runtime_request_switch(&runtime, &kAppB)) {
        return false;
    }

    return runtime.pending_app == &kAppB
        && runtime.force_full_refresh_on_next_render;
}
