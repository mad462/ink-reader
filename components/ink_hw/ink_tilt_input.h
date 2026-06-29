#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    INK_TILT_DIRECTION_NONE = 0,
    INK_TILT_DIRECTION_LEFT,
    INK_TILT_DIRECTION_RIGHT,
    INK_TILT_DIRECTION_UP,
    INK_TILT_DIRECTION_DOWN,
    INK_TILT_DIRECTION_UP_LEFT,
    INK_TILT_DIRECTION_UP_RIGHT,
    INK_TILT_DIRECTION_DOWN_LEFT,
    INK_TILT_DIRECTION_DOWN_RIGHT,
} ink_tilt_direction_t;

esp_err_t ink_tilt_input_init(void);
esp_err_t ink_tilt_input_poll(uint32_t now_ms, ink_tilt_direction_t *direction_out);
bool ink_tilt_input_available(void);
bool ink_tilt_input_self_test(void);
