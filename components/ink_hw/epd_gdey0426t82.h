#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"

#define EPD_GDEY0426T82_WIDTH 480
#define EPD_GDEY0426T82_HEIGHT 800
#define EPD_GDEY0426T82_BUFFER_SIZE (EPD_GDEY0426T82_WIDTH * EPD_GDEY0426T82_HEIGHT / 8)
#define EPD_GDEY0426T82_GRAY_PLANE_SIZE EPD_GDEY0426T82_BUFFER_SIZE
#define EPD_GDEY0426T82_NATIVE_WIDTH 800
#define EPD_GDEY0426T82_NATIVE_HEIGHT 480
#define EPD_GDEY0426T82_NATIVE_BUFFER_SIZE (EPD_GDEY0426T82_NATIVE_WIDTH * EPD_GDEY0426T82_NATIVE_HEIGHT / 8)

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

typedef enum {
    EPD_GDEY0426T82_PHASE_IDLE = 0,
    EPD_GDEY0426T82_PHASE_PREPARING,
    EPD_GDEY0426T82_PHASE_TRANSMITTING,
    EPD_GDEY0426T82_PHASE_BUSY_WAIT,
    EPD_GDEY0426T82_PHASE_DONE,
    EPD_GDEY0426T82_PHASE_ABORTED,
} epd_gdey0426t82_phase_t;

typedef bool (*epd_gdey0426t82_should_cancel_fn)(
    void *ctx,
    epd_gdey0426t82_phase_t phase);

typedef struct {
    bool aggressive_interrupt_mode;
    bool use_custom_lut_a;
    bool use_custom_lut_b;
    bool use_grid_compare_variant;
    bool reuse_partial_init;
    uint8_t grid_compare_variant_index;
    epd_gdey0426t82_should_cancel_fn should_cancel;
    void *should_cancel_ctx;
    epd_gdey0426t82_phase_t phase;
} epd_gdey0426t82_refresh_control_t;

#define EPD_GDEY0426T82_ERR_ABORTED ((esp_err_t)0xE0426)

esp_err_t epd_gdey0426t82_init(const epd_gdey0426t82_config_t *config);
esp_err_t epd_gdey0426t82_full_refresh(const uint8_t *buffer, size_t length);
esp_err_t epd_gdey0426t82_full_refresh_ex(
    const uint8_t *buffer,
    size_t length,
    epd_gdey0426t82_refresh_control_t *control
);
esp_err_t epd_gdey0426t82_full_refresh_native_ex(
    const uint8_t *native_buffer,
    size_t length,
    epd_gdey0426t82_refresh_control_t *control
);
esp_err_t epd_gdey0426t82_partial_refresh(const uint8_t *buffer, size_t length);
esp_err_t epd_gdey0426t82_partial_refresh_ex(
    const uint8_t *buffer,
    size_t length,
    epd_gdey0426t82_refresh_control_t *control
);
esp_err_t epd_gdey0426t82_gray_refresh(
    const uint8_t *lsb_buffer,
    size_t lsb_length,
    const uint8_t *msb_buffer,
    size_t msb_length,
    epd_gdey0426t82_refresh_control_t *control
);
esp_err_t epd_gdey0426t82_partial_refresh_area(
    const uint8_t *buffer,
    size_t length,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height
);
esp_err_t epd_gdey0426t82_partial_refresh_area_ex(
    const uint8_t *buffer,
    size_t length,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    epd_gdey0426t82_refresh_control_t *control
);
esp_err_t epd_gdey0426t82_partial_refresh_area_native_ex(
    const uint8_t *native_buffer,
    size_t length,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    epd_gdey0426t82_refresh_control_t *control
);
void epd_gdey0426t82_convert_portrait_to_native(const uint8_t *portrait, uint8_t *native_buffer);
esp_err_t epd_gdey0426t82_sleep(void);
bool epd_gdey0426t82_is_aborted_error(esp_err_t ret);
