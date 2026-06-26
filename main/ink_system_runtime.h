#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "apps/ink_app_iface.h"

typedef struct ink_system_runtime {
    const ink_app_descriptor_t *apps[4];
    size_t app_count;
    const ink_app_descriptor_t *active_app;
    const ink_app_descriptor_t *pending_app;
    bool force_full_refresh_on_next_render;
} ink_system_runtime_t;

bool ink_system_runtime_self_test(void);
