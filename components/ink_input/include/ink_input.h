#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
  INK_BUTTON_BACK,
  INK_BUTTON_CONFIRM,
  INK_BUTTON_LEFT,
  INK_BUTTON_RIGHT,
  INK_BUTTON_POWER,
  INK_BUTTON_COUNT
} ink_button_t;
typedef struct {
  uint32_t down;
  uint32_t pressed;
  uint32_t released;
  uint32_t held_ms[INK_BUTTON_COUNT];
} ink_input_snapshot_t;

esp_err_t ink_input_init(void);
esp_err_t ink_input_poll(uint32_t now_ms, ink_input_snapshot_t *snapshot);
uint32_t ink_input_mask(ink_button_t button);
bool ink_input_is_down(const ink_input_snapshot_t *snapshot,
                       ink_button_t button);
bool ink_input_was_pressed(const ink_input_snapshot_t *snapshot,
                           ink_button_t button);
uint32_t ink_input_held_ms(const ink_input_snapshot_t *snapshot,
                           ink_button_t button);
bool ink_input_self_test(void);
