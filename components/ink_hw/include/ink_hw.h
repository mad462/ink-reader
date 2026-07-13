#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define INK_HW_WIDTH 480
#define INK_HW_HEIGHT 800
#define INK_HW_BUFFER_SIZE (INK_HW_WIDTH * INK_HW_HEIGHT / 8)

esp_err_t ink_hw_init(void);
esp_err_t ink_hw_full_refresh(const uint8_t *buffer, size_t length);
esp_err_t ink_hw_partial_refresh_area(const uint8_t *buffer, size_t length,
                                      uint16_t x, uint16_t y,
                                      uint16_t width, uint16_t height);
esp_err_t ink_hw_gray_refresh(const uint8_t *lsb, size_t lsb_length,
                              const uint8_t *msb, size_t msb_length);
esp_err_t ink_hw_sleep(void);
