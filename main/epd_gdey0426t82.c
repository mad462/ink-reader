#include "epd_gdey0426t82.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EPD_NATIVE_WIDTH 800
#define EPD_NATIVE_HEIGHT 480
#define EPD_NATIVE_BUFFER_SIZE (EPD_NATIVE_WIDTH * EPD_NATIVE_HEIGHT / 8)
#define EPD_BUSY_TIMEOUT_MS 15000
#define EPD_SPI_CHUNK_SIZE 4096

static const char *TAG = "epd_gdey0426t82";

static epd_gdey0426t82_config_t s_cfg;
static spi_device_handle_t s_spi;
static bool s_initialized;
static uint8_t *s_shadow_framebuffer;
static uint8_t *s_transfer_framebuffer;

static const uint8_t s_grayscale_lut[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x54, 0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,
    0x17, 0x41, 0xA8, 0x32, 0x30,
    0x00, 0x00
};

static esp_err_t epd_write_command(uint8_t command);
static esp_err_t epd_write_data_byte(uint8_t data);
static esp_err_t epd_write_data(const uint8_t *data, size_t length);
static esp_err_t epd_write_native_area(const uint8_t *native_buffer, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
static esp_err_t epd_wait_ready(void);
static esp_err_t epd_ensure_framebuffers(void);
static void epd_reset(void);
static void epd_convert_portrait_to_native(const uint8_t *portrait, uint8_t *native);
static esp_err_t epd_write_native_full(const uint8_t *native_buffer, uint8_t ram_command);
static esp_err_t epd_align_portrait_area_to_native(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    uint16_t *native_x,
    uint16_t *native_y,
    uint16_t *native_width,
    uint16_t *native_height
);
static esp_err_t epd_set_native_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
static esp_err_t epd_full_init_sequence(void);
static esp_err_t epd_partial_frame_prepare(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
static esp_err_t epd_load_custom_lut(const uint8_t *lut, size_t length);
static esp_err_t epd_update_full(void);
static esp_err_t epd_update_partial(void);
static esp_err_t epd_update_fast_custom_lut(bool turn_off);

static esp_err_t epd_write_command(uint8_t command)
{
    gpio_set_level(s_cfg.gpio_dc, 0);

    spi_transaction_t transaction = {
        .length = 8,
        .tx_buffer = &command,
    };

    return spi_device_polling_transmit(s_spi, &transaction);
}

static esp_err_t epd_write_data_byte(uint8_t data)
{
    gpio_set_level(s_cfg.gpio_dc, 1);

    spi_transaction_t transaction = {
        .length = 8,
        .tx_buffer = &data,
    };

    return spi_device_polling_transmit(s_spi, &transaction);
}

static esp_err_t epd_write_data(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_set_level(s_cfg.gpio_dc, 1);

    for (size_t offset = 0; offset < length; offset += EPD_SPI_CHUNK_SIZE) {
        const size_t chunk = ((length - offset) > EPD_SPI_CHUNK_SIZE) ? EPD_SPI_CHUNK_SIZE : (length - offset);
        spi_transaction_t transaction = {
            .length = chunk * 8,
            .tx_buffer = data + offset,
        };
        esp_err_t ret = spi_device_polling_transmit(s_spi, &transaction);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

static esp_err_t epd_write_native_area(const uint8_t *native_buffer, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    if (native_buffer == NULL || (x % 8) != 0 || (width % 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((uint32_t)x + width > EPD_NATIVE_WIDTH || (uint32_t)y + height > EPD_NATIVE_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t native_stride = EPD_NATIVE_WIDTH / 8;
    const size_t area_stride = width / 8;

    gpio_set_level(s_cfg.gpio_dc, 1);
    for (uint16_t row = 0; row < height; ++row) {
        const uint8_t *row_data = native_buffer + ((size_t)y + row) * native_stride + (x / 8);
        ESP_RETURN_ON_ERROR(epd_write_data(row_data, area_stride), TAG, "native area row write failed");
    }

    return ESP_OK;
}

static esp_err_t epd_wait_ready(void)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(EPD_BUSY_TIMEOUT_MS);

    while (gpio_get_level(s_cfg.gpio_busy) != 0) {
        if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) {
            ESP_LOGE(TAG, "busy wait timed out");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}

static esp_err_t epd_write_native_full(const uint8_t *native_buffer, uint8_t ram_command)
{
    if (native_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(
        epd_set_native_window(0, 0, EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT),
        TAG,
        "failed to set native full window"
    );
    ESP_RETURN_ON_ERROR(epd_write_command(ram_command), TAG, "write ram command failed");
    return epd_write_data(native_buffer, EPD_NATIVE_BUFFER_SIZE);
}

static esp_err_t epd_ensure_framebuffers(void)
{
    if (s_shadow_framebuffer == NULL) {
        s_shadow_framebuffer = heap_caps_malloc(EPD_NATIVE_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_shadow_framebuffer == NULL) {
            s_shadow_framebuffer = heap_caps_malloc(EPD_NATIVE_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (s_shadow_framebuffer == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memset(s_shadow_framebuffer, 0xFF, EPD_NATIVE_BUFFER_SIZE);
    }

    if (s_transfer_framebuffer == NULL) {
        s_transfer_framebuffer = heap_caps_malloc(EPD_NATIVE_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_transfer_framebuffer == NULL) {
            s_transfer_framebuffer = heap_caps_malloc(EPD_NATIVE_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (s_transfer_framebuffer == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memset(s_transfer_framebuffer, 0xFF, EPD_NATIVE_BUFFER_SIZE);
    }

    return ESP_OK;
}

static void epd_reset(void)
{
    gpio_set_level(s_cfg.gpio_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(s_cfg.gpio_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static bool epd_portrait_pixel_is_black(const uint8_t *buffer, uint16_t x, uint16_t y)
{
    const size_t index = (size_t)y * (EPD_GDEY0426T82_WIDTH / 8) + (x / 8);
    const uint8_t mask = (uint8_t)(0x80 >> (x % 8));
    return (buffer[index] & mask) == 0;
}

static void epd_native_set_pixel(uint8_t *buffer, uint16_t x, uint16_t y, bool black)
{
    const size_t index = (size_t)y * (EPD_NATIVE_WIDTH / 8) + (x / 8);
    const uint8_t mask = (uint8_t)(0x80 >> (x % 8));
    if (black) {
        buffer[index] &= (uint8_t)~mask;
    } else {
        buffer[index] |= mask;
    }
}

static void epd_convert_portrait_to_native(const uint8_t *portrait, uint8_t *native)
{
    memset(native, 0xFF, EPD_NATIVE_BUFFER_SIZE);

    for (uint16_t y = 0; y < EPD_GDEY0426T82_HEIGHT; ++y) {
        for (uint16_t x = 0; x < EPD_GDEY0426T82_WIDTH; ++x) {
            if (epd_portrait_pixel_is_black(portrait, x, y)) {
                const uint16_t native_x = y;
                const uint16_t native_y = EPD_GDEY0426T82_WIDTH - 1 - x;
                epd_native_set_pixel(native, native_x, native_y, true);
            }
        }
    }
}

static esp_err_t epd_align_portrait_area_to_native(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    uint16_t *native_x,
    uint16_t *native_y,
    uint16_t *native_width,
    uint16_t *native_height)
{
    if (native_x == NULL || native_y == NULL || native_width == NULL || native_height == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (width == 0 || height == 0 || x >= EPD_GDEY0426T82_WIDTH || y >= EPD_GDEY0426T82_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((uint32_t)x + width > EPD_GDEY0426T82_WIDTH || (uint32_t)y + height > EPD_GDEY0426T82_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t nx0 = y;
    uint16_t nx1 = y + height - 1;
    const uint16_t ny0 = EPD_GDEY0426T82_WIDTH - (x + width);
    const uint16_t ny1 = EPD_GDEY0426T82_WIDTH - 1 - x;

    nx0 = (uint16_t)(nx0 & (uint16_t)~0x07U);
    uint16_t nx_end_exclusive = (uint16_t)((nx1 + 8U) & (uint16_t)~0x07U);
    if (nx_end_exclusive > EPD_NATIVE_WIDTH) {
        nx_end_exclusive = EPD_NATIVE_WIDTH;
    }

    *native_x = nx0;
    *native_y = ny0;
    *native_width = nx_end_exclusive - nx0;
    *native_height = ny1 - ny0 + 1;
    return ESP_OK;
}

static esp_err_t epd_set_native_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    const uint16_t reversed_y = EPD_NATIVE_HEIGHT - y - height;
    const uint16_t x_end = x + width - 1;
    const uint16_t y_end = reversed_y + height - 1;

    ESP_RETURN_ON_ERROR(epd_write_command(0x11), TAG, "cmd 0x11 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x01), TAG, "data entry mode failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x44), TAG, "cmd 0x44 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(x % 256), TAG, "ram x start low failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(x / 256), TAG, "ram x start high failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(x_end % 256), TAG, "ram x end low failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(x_end / 256), TAG, "ram x end high failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x45), TAG, "cmd 0x45 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(y_end % 256), TAG, "ram y end low failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(y_end / 256), TAG, "ram y end high failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(reversed_y % 256), TAG, "ram y start low failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(reversed_y / 256), TAG, "ram y start high failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x4E), TAG, "cmd 0x4E failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(x % 256), TAG, "ram x counter low failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(x / 256), TAG, "ram x counter high failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x4F), TAG, "cmd 0x4F failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(y_end % 256), TAG, "ram y counter low failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(y_end / 256), TAG, "ram y counter high failed");

    return ESP_OK;
}

static esp_err_t epd_full_init_sequence(void)
{
    epd_reset();

    ESP_RETURN_ON_ERROR(epd_wait_ready(), TAG, "panel not ready after reset");
    ESP_RETURN_ON_ERROR(epd_write_command(0x12), TAG, "sw reset command failed");
    ESP_RETURN_ON_ERROR(epd_wait_ready(), TAG, "panel not ready after sw reset");

    ESP_RETURN_ON_ERROR(epd_write_command(0x18), TAG, "cmd 0x18 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x80), TAG, "data 0x18 failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x0C), TAG, "cmd 0x0C failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0xAE), TAG, "data 0xAE failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0xC7), TAG, "data 0xC7 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0xC3), TAG, "data 0xC3 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0xC0), TAG, "data 0xC0 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x80), TAG, "data 0x80 failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x01), TAG, "cmd 0x01 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte((EPD_NATIVE_HEIGHT - 1) % 256), TAG, "data gate low failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte((EPD_NATIVE_HEIGHT - 1) / 256), TAG, "data gate high failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x02), TAG, "data scan mode failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x3C), TAG, "cmd 0x3C failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x01), TAG, "data border failed");

    ESP_RETURN_ON_ERROR(
        epd_set_native_window(0, 0, EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT),
        TAG,
        "failed to set full window"
    );

    return epd_wait_ready();
}

static esp_err_t epd_partial_frame_prepare(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    ESP_RETURN_ON_ERROR(epd_full_init_sequence(), TAG, "partial init full base failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x18), TAG, "partial cmd 0x18 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x80), TAG, "partial data 0x18 failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x3C), TAG, "partial cmd 0x3C failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x80), TAG, "partial border failed");

    return epd_set_native_window(x, y, width, height);
}

static esp_err_t epd_update_full(void)
{
    ESP_RETURN_ON_ERROR(epd_write_command(0x21), TAG, "cmd 0x21 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x40), TAG, "update option 1 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x00), TAG, "update option 2 failed");
    ESP_RETURN_ON_ERROR(epd_write_command(0x22), TAG, "cmd 0x22 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0xF7), TAG, "update control data failed");
    ESP_RETURN_ON_ERROR(epd_write_command(0x20), TAG, "cmd 0x20 failed");
    return epd_wait_ready();
}

static esp_err_t epd_update_partial(void)
{
    ESP_RETURN_ON_ERROR(epd_write_command(0x22), TAG, "partial cmd 0x22 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0xFF), TAG, "partial update control failed");
    ESP_RETURN_ON_ERROR(epd_write_command(0x20), TAG, "partial cmd 0x20 failed");
    return epd_wait_ready();
}

static esp_err_t epd_load_custom_lut(const uint8_t *lut, size_t length)
{
    if (lut == NULL || length < 110) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(epd_write_command(0x32), TAG, "cmd 0x32 failed");
    ESP_RETURN_ON_ERROR(epd_write_data(lut, 105), TAG, "lut body write failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x03), TAG, "cmd 0x03 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(lut[105]), TAG, "gate voltage write failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x04), TAG, "cmd 0x04 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(lut[106]), TAG, "source voltage VSH1 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(lut[107]), TAG, "source voltage VSH2 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(lut[108]), TAG, "source voltage VSL failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x2C), TAG, "cmd 0x2C failed");
    return epd_write_data_byte(lut[109]);
}

static esp_err_t epd_update_fast_custom_lut(bool turn_off)
{
    uint8_t display_mode = 0xC0 | 0x0C;
    if (turn_off) {
        display_mode |= 0x03;
    }

    ESP_RETURN_ON_ERROR(epd_write_command(0x21), TAG, "cmd 0x21 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x00), TAG, "fast ctrl1 byte0 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x00), TAG, "fast ctrl1 byte1 failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x22), TAG, "cmd 0x22 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(display_mode), TAG, "fast custom update mode failed");
    ESP_RETURN_ON_ERROR(epd_write_command(0x20), TAG, "cmd 0x20 failed");
    return epd_wait_ready();
}

esp_err_t epd_gdey0426t82_init(const epd_gdey0426t82_config_t *config)
{
    esp_err_t ret;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg = *config;

    gpio_config_t output_cfg = {
        .pin_bit_mask = (1ULL << s_cfg.gpio_dc) | (1ULL << s_cfg.gpio_rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_cfg), TAG, "failed to configure output pins");

    gpio_config_t input_cfg = {
        .pin_bit_mask = (1ULL << s_cfg.gpio_busy),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input_cfg), TAG, "failed to configure busy pin");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = s_cfg.gpio_mosi,
        .miso_io_num = -1,
        .sclk_io_num = s_cfg.gpio_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_NATIVE_BUFFER_SIZE + 16,
    };
    ret = spi_bus_initialize(s_cfg.spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    ESP_RETURN_ON_ERROR(ret, TAG, "failed to init spi bus");

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = s_cfg.spi_clock_hz,
        .mode = 0,
        .spics_io_num = s_cfg.gpio_cs,
        .queue_size = 1,
    };
    ret = spi_bus_add_device(s_cfg.spi_host, &dev_cfg, &s_spi);
    ESP_RETURN_ON_ERROR(ret, TAG, "failed to add spi device");

    ESP_RETURN_ON_ERROR(epd_ensure_framebuffers(), TAG, "failed to allocate framebuffers");

    s_initialized = true;
    return epd_full_init_sequence();
}

esp_err_t epd_gdey0426t82_full_refresh(const uint8_t *buffer, size_t length)
{
    if (!s_initialized || buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(epd_ensure_framebuffers(), TAG, "framebuffers unavailable");
    epd_convert_portrait_to_native(buffer, s_transfer_framebuffer);

    ESP_RETURN_ON_ERROR(epd_full_init_sequence(), TAG, "panel init sequence failed");
    ESP_RETURN_ON_ERROR(epd_write_command(0x24), TAG, "write ram command failed");
    ESP_RETURN_ON_ERROR(epd_write_data(s_transfer_framebuffer, EPD_NATIVE_BUFFER_SIZE), TAG, "frame write failed");
    ESP_RETURN_ON_ERROR(epd_update_full(), TAG, "full update failed");

    memcpy(s_shadow_framebuffer, s_transfer_framebuffer, EPD_NATIVE_BUFFER_SIZE);
    return ESP_OK;
}

esp_err_t epd_gdey0426t82_partial_refresh(const uint8_t *buffer, size_t length)
{
    if (!s_initialized || buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(epd_ensure_framebuffers(), TAG, "framebuffers unavailable");
    epd_convert_portrait_to_native(buffer, s_transfer_framebuffer);
    ESP_RETURN_ON_ERROR(epd_partial_frame_prepare(0, 0, EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT), TAG, "partial prepare failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x24), TAG, "write current ram command failed");
    ESP_RETURN_ON_ERROR(epd_write_data(s_transfer_framebuffer, EPD_NATIVE_BUFFER_SIZE), TAG, "current frame write failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x26), TAG, "write previous ram command failed");
    ESP_RETURN_ON_ERROR(epd_write_data(s_shadow_framebuffer, EPD_NATIVE_BUFFER_SIZE), TAG, "previous frame write failed");

    ESP_RETURN_ON_ERROR(epd_update_partial(), TAG, "partial update failed");

    memcpy(s_shadow_framebuffer, s_transfer_framebuffer, EPD_NATIVE_BUFFER_SIZE);
    return ESP_OK;
}

esp_err_t epd_gdey0426t82_gray_refresh(
    const uint8_t *lsb_buffer,
    size_t lsb_length,
    const uint8_t *msb_buffer,
    size_t msb_length)
{
    if (!s_initialized
        || lsb_buffer == NULL
        || msb_buffer == NULL
        || lsb_length < EPD_GDEY0426T82_GRAY_PLANE_SIZE
        || msb_length < EPD_GDEY0426T82_GRAY_PLANE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(epd_ensure_framebuffers(), TAG, "framebuffers unavailable");
    ESP_RETURN_ON_ERROR(epd_full_init_sequence(), TAG, "gray init sequence failed");

    epd_convert_portrait_to_native(lsb_buffer, s_transfer_framebuffer);
    ESP_RETURN_ON_ERROR(epd_write_native_full(s_transfer_framebuffer, 0x24), TAG, "gray lsb write failed");

    epd_convert_portrait_to_native(msb_buffer, s_transfer_framebuffer);
    ESP_RETURN_ON_ERROR(epd_write_native_full(s_transfer_framebuffer, 0x26), TAG, "gray msb write failed");

    ESP_RETURN_ON_ERROR(epd_load_custom_lut(s_grayscale_lut, sizeof(s_grayscale_lut)), TAG, "gray lut load failed");
    return epd_update_fast_custom_lut(true);
}

esp_err_t epd_gdey0426t82_partial_refresh_area(
    const uint8_t *buffer,
    size_t length,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height)
{
    uint16_t native_x;
    uint16_t native_y;
    uint16_t native_width;
    uint16_t native_height;

    if (!s_initialized || buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(
        epd_align_portrait_area_to_native(x, y, width, height, &native_x, &native_y, &native_width, &native_height),
        TAG,
        "invalid partial area"
    );
    ESP_RETURN_ON_ERROR(epd_ensure_framebuffers(), TAG, "framebuffers unavailable");
    epd_convert_portrait_to_native(buffer, s_transfer_framebuffer);

    ESP_RETURN_ON_ERROR(
        epd_partial_frame_prepare(native_x, native_y, native_width, native_height),
        TAG,
        "partial area prepare failed"
    );

    ESP_RETURN_ON_ERROR(epd_write_command(0x24), TAG, "write current area command failed");
    ESP_RETURN_ON_ERROR(
        epd_write_native_area(s_transfer_framebuffer, native_x, native_y, native_width, native_height),
        TAG,
        "current area write failed"
    );

    ESP_RETURN_ON_ERROR(epd_write_command(0x26), TAG, "write previous area command failed");
    ESP_RETURN_ON_ERROR(
        epd_write_native_area(s_shadow_framebuffer, native_x, native_y, native_width, native_height),
        TAG,
        "previous area write failed"
    );

    ESP_RETURN_ON_ERROR(epd_update_partial(), TAG, "partial area update failed");

    memcpy(s_shadow_framebuffer, s_transfer_framebuffer, EPD_NATIVE_BUFFER_SIZE);
    return ESP_OK;
}

esp_err_t epd_gdey0426t82_sleep(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(epd_write_command(0x10), TAG, "sleep command failed");
    return epd_write_data_byte(0x01);
}

