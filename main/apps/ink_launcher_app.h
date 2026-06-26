#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ink_app_iface.h"

typedef struct {
    size_t selected_app_index;
} ink_launcher_app_state_t;

const ink_app_descriptor_t *ink_launcher_app_descriptor(void);
bool ink_launcher_app_self_test(void);
