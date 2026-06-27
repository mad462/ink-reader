#pragma once

#include "ink_app_priv.h"
#include "ink_app_iface.h"

typedef struct {
    bool initialized;
    uint32_t storage_epoch_seen;
    char pending_open_path[INK_APP_STATE_PATH_LENGTH + 1];
    ink_ui_model_t ui;
} ink_reader_app_state_t;

const ink_app_descriptor_t *ink_reader_app_descriptor(void);
bool ink_reader_app_self_test(void);
