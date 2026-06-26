#include "ink_system_runtime.h"

#include <stddef.h>
#include <string.h>

void ink_system_runtime_init(ink_system_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
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

bool ink_system_runtime_request_switch(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
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

    return runtime.pending_app == &kAppB
        && runtime.force_full_refresh_on_next_render;
}
