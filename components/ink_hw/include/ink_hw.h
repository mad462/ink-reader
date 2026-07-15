#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define INK_HW_WIDTH 480
#define INK_HW_HEIGHT 800
#define INK_HW_BUFFER_SIZE (INK_HW_WIDTH * INK_HW_HEIGHT / 8)

typedef bool (*ink_hw_refresh_poll_fn)(void *context);
typedef esp_err_t (*ink_hw_refresh_work_fn)(void *context);
typedef void (*ink_hw_refresh_pump_fn)(void *context);

esp_err_t ink_hw_init(void);
esp_err_t ink_hw_set_previous_frame(const uint8_t *buffer, size_t length);
esp_err_t ink_hw_full_refresh(const uint8_t *buffer, size_t length);
esp_err_t ink_hw_partial_refresh_area(const uint8_t *buffer, size_t length,
                                      uint16_t x, uint16_t y,
                                      uint16_t width, uint16_t height);
esp_err_t ink_hw_partial_refresh_area_with_work(
    const uint8_t *buffer, size_t length, uint16_t x, uint16_t y,
    uint16_t width, uint16_t height, ink_hw_refresh_work_fn work,
    void *context);
esp_err_t ink_hw_partial_refresh_area_with_work_and_pump(
    const uint8_t *buffer, size_t length, uint16_t x, uint16_t y,
    uint16_t width, uint16_t height, ink_hw_refresh_work_fn work,
    void *work_context, ink_hw_refresh_pump_fn pump, void *pump_context);
esp_err_t ink_hw_gray_refresh(const uint8_t *lsb, size_t lsb_length,
                              const uint8_t *msb, size_t msb_length);
esp_err_t ink_hw_gray_refresh_with_poll(
    const uint8_t *lsb, size_t lsb_length, const uint8_t *msb,
    size_t msb_length, ink_hw_refresh_poll_fn poll, void *context);
esp_err_t ink_hw_sleep(void);
bool ink_hw_self_test(void);
