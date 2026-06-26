#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "apps/ink_app_iface.h"

enum {
    INK_SYSTEM_RUNTIME_APP_CAPACITY = 4,
};

typedef struct ink_system_runtime {
    const ink_app_descriptor_t *apps[INK_SYSTEM_RUNTIME_APP_CAPACITY];
    size_t app_count;
    const ink_app_descriptor_t *active_app;
    const ink_app_descriptor_t *pending_app;
    bool force_full_refresh_on_next_render;
} ink_system_runtime_t;

void ink_system_runtime_init(ink_system_runtime_t *runtime);
bool ink_system_runtime_register_app(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
bool ink_system_runtime_set_active_app(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
bool ink_system_runtime_request_switch(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
bool ink_system_runtime_self_test(void);
