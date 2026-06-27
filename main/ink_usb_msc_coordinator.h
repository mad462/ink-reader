#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "apps/ink_app_iface.h"

typedef struct ink_system_runtime ink_system_runtime_t;
typedef struct ink_system_services ink_system_services_t;

typedef struct {
    bool initialized;
    bool prompt_visible;
    bool restore_pending;
    bool suppress_reentry_until_detach;
    bool last_usb_attached;
    char return_app_id[24];
} ink_usb_msc_coordinator_t;

void ink_usb_msc_coordinator_init(ink_usb_msc_coordinator_t *coordinator);
bool ink_usb_msc_coordinator_before_input(
    ink_usb_msc_coordinator_t *coordinator,
    ink_system_runtime_t *runtime,
    const ink_app_event_t *event,
    bool *handled_out,
    bool *dirty_out);
bool ink_usb_msc_coordinator_poll(
    ink_usb_msc_coordinator_t *coordinator,
    ink_system_runtime_t *runtime,
    uint32_t now_ms);
bool ink_usb_msc_coordinator_self_test(void);
