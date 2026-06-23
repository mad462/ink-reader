#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    INK_RAW_BUTTON_BACK = 0,
    INK_RAW_BUTTON_CONFIRM,
    INK_RAW_BUTTON_LEFT,
    INK_RAW_BUTTON_RIGHT,
    INK_RAW_BUTTON_POWER,
    INK_RAW_BUTTON_COUNT
} ink_raw_button_t;

typedef enum {
    INK_LOGICAL_BUTTON_BACK = 0,
    INK_LOGICAL_BUTTON_CONFIRM,
    INK_LOGICAL_BUTTON_LEFT,
    INK_LOGICAL_BUTTON_RIGHT,
    INK_LOGICAL_BUTTON_POWER,
    INK_LOGICAL_BUTTON_NAV_PREVIOUS,
    INK_LOGICAL_BUTTON_NAV_NEXT,
    INK_LOGICAL_BUTTON_COUNT
} ink_logical_button_t;

typedef struct {
    uint32_t stable_mask;
    uint32_t pressed_mask;
    uint32_t released_mask;
    uint32_t held_duration_ms[INK_RAW_BUTTON_COUNT];
} ink_button_snapshot_t;

esp_err_t ink_button_input_init(void);
esp_err_t ink_button_input_poll(ink_button_snapshot_t *snapshot, uint32_t now_ms);
bool ink_button_input_self_test(void);
uint32_t ink_button_input_mask_for_raw(ink_raw_button_t button);
uint32_t ink_button_input_mask_for_logical(ink_logical_button_t button);
bool ink_button_snapshot_is_pressed(const ink_button_snapshot_t *snapshot, ink_logical_button_t button);
bool ink_button_snapshot_was_pressed(const ink_button_snapshot_t *snapshot, ink_logical_button_t button);
bool ink_button_snapshot_was_released(const ink_button_snapshot_t *snapshot, ink_logical_button_t button);
uint32_t ink_button_snapshot_get_held_ms(const ink_button_snapshot_t *snapshot, ink_logical_button_t button);
