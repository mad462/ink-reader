#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apps/ink_app_iface.h"

struct ink_system_services;

enum {
    INK_SYSTEM_RUNTIME_APP_CAPACITY = 7,
};

typedef struct ink_system_runtime {
    const ink_app_descriptor_t *apps[INK_SYSTEM_RUNTIME_APP_CAPACITY];
    size_t app_count;
    const ink_app_descriptor_t *active_app;
    const ink_app_descriptor_t *pending_app;
    struct ink_system_services *services;
    bool force_full_refresh_on_next_render;
} ink_system_runtime_t;

void ink_system_runtime_init(ink_system_runtime_t *runtime);
void ink_system_runtime_bind_services(
    ink_system_runtime_t *runtime,
    struct ink_system_services *services);
bool ink_system_runtime_register_app(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
const ink_app_descriptor_t *ink_system_runtime_find_app_by_id(
    const ink_system_runtime_t *runtime,
    const char *app_id);
bool ink_system_runtime_has_active_app(const ink_system_runtime_t *runtime);
const ink_app_descriptor_t *ink_system_runtime_active_app(const ink_system_runtime_t *runtime);
bool ink_system_runtime_set_active_app(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
bool ink_system_runtime_switch_now(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
bool ink_system_runtime_request_switch(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
bool ink_system_runtime_dispatch_input(
    ink_system_runtime_t *runtime,
    const ink_app_event_t *event);
bool ink_system_runtime_dispatch_tick(ink_system_runtime_t *runtime, uint32_t now_ms);
void ink_system_runtime_handle_display_done(ink_system_runtime_t *runtime, uint32_t event_ms);
bool ink_system_runtime_self_test(void);
