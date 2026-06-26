#pragma once

#include <stdbool.h>

#include "ink_app_iface.h"

typedef struct {
    bool first_frame_pending;
} ink_reader_app_state_t;

const ink_app_descriptor_t *ink_reader_app_descriptor(void);
bool ink_reader_app_self_test(void);
