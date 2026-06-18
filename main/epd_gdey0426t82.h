#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"

#define EPD_GDEY0426T82_WIDTH 480
#define EPD_GDEY0426T82_HEIGHT 800
#define EPD_GDEY0426T82_BUFFER_SIZE (EPD_GDEY0426T82_WIDTH * EPD_GDEY0426T82_HEIGHT / 8)
#define EPD_GDEY0426T82_GRAY_PLANE_SIZE EPD_GDEY0426T82_BUFFER_SIZE

typedef struct {
    int gpio_mosi;
    int gpio_sclk;
    int gpio_cs;
    int gpio_dc;
    int gpio_rst;
    int gpio_busy;
    spi_host_device_t spi_host;
    int spi_clock_hz;
} epd_gdey0426t82_config_t;

esp_err_t epd_gdey0426t82_init(const epd_gdey0426t82_config_t *config);
esp_err_t epd_gdey0426t82_full_refresh(const uint8_t *buffer, size_t length);
esp_err_t epd_gdey0426t82_partial_refresh(const uint8_t *buffer, size_t length);
esp_err_t epd_gdey0426t82_gray_refresh(
    const uint8_t *lsb_buffer,
    size_t lsb_length,
    const uint8_t *msb_buffer,
    size_t msb_length
);
esp_err_t epd_gdey0426t82_partial_refresh_area(
    const uint8_t *buffer,
    size_t length,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height
);
esp_err_t epd_gdey0426t82_sleep(void);

