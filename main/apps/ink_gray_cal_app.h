#pragma once

#include <stdbool.h>

#include "ink_app_priv.h"
#include "ink_app_iface.h"

typedef struct {
    bool initialized;
    ink_ui_model_t ui;
} ink_gray_cal_app_state_t;

const ink_app_descriptor_t *ink_gray_cal_app_descriptor(void);
bool ink_gray_cal_app_self_test(void);
