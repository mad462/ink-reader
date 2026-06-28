#include "epd_gdey0426t82.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EPD_NATIVE_WIDTH 800
#define EPD_NATIVE_HEIGHT 480
#define EPD_NATIVE_BUFFER_SIZE (EPD_NATIVE_WIDTH * EPD_NATIVE_HEIGHT / 8)
#define EPD_BUSY_TIMEOUT_MS 15000
#define EPD_SPI_CHUNK_SIZE 4096

static const char *TAG = "epd_gdey0426t82";
static const bool kLogPartialAreaGeometry = false;

static epd_gdey0426t82_config_t s_cfg;
static spi_device_handle_t s_spi;
static bool s_initialized;
static uint8_t *s_shadow_framebuffer;
static uint8_t *s_transfer_framebuffer;
static uint8_t *s_spi_dma_bounce_buffer;

typedef struct {
    bool active;
    const char *operation;
    int64_t start_us;
    uint64_t convert_us;
    uint64_t prepare_us;
    uint64_t update_us;
    uint64_t tx_us;
    uint64_t busy_us;
    size_t tx_bytes;
    uint32_t tx_calls;
    uint32_t busy_calls;
} epd_refresh_timing_t;

static epd_refresh_timing_t s_timing;
static epd_gdey0426t82_refresh_control_t *s_active_control;

static const uint8_t s_grayscale_lut[] = {
    0x80, 0x48, 0x4A, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x48, 0x68, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x88, 0x48, 0x60, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA8, 0x48, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x1E, 0x1C, 0x02, 0x00,
    0x05, 0x01, 0x05, 0x01, 0x02,
    0x08, 0x01, 0x01, 0x04, 0x04,
    0x00, 0x02, 0x01, 0x02, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01,
    0x22, 0x22, 0x22, 0x22, 0x22,
    0x17, 0x41, 0xA8, 0x32, 0x30,
    0x00, 0x00
};

// First-pass mono experiment: reuse the panel's known-good custom waveform hook
// so we can measure whether custom-LUT mode lowers busy time on BW partial updates.
static const uint8_t s_fast_mono_lut_a[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x54, 0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x01, 0x01, 0x01, 0x00,
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

static const uint8_t s_fast_mono_lut_b[] = {
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

static void epd_select_grid_compare_lut(uint8_t variant_index, uint8_t *lut_out, size_t length)
{
    uint8_t phase3 = 0x00;
    uint8_t repeat1 = 0x01;

    if (lut_out == NULL || length < sizeof(s_fast_mono_lut_a)) {
        return;
    }

    memcpy(lut_out, s_fast_mono_lut_a, sizeof(s_fast_mono_lut_a));

    // Keep the improved forward/no-reinit flow and nudge the
    // primary repeat byte one step stronger for all cells.
    phase3 = 0x00U;
    repeat1 = 0x01U;

    lut_out[32] = phase3;
    lut_out[50] = 0x02;
    lut_out[55] = repeat1;
}

static esp_err_t epd_write_command(uint8_t command);
static esp_err_t epd_write_data_byte(uint8_t data);
static esp_err_t epd_write_data(const uint8_t *data, size_t length);
static esp_err_t epd_write_native_area(const uint8_t *native_buffer, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
static esp_err_t epd_wait_ready(const char *label);
static esp_err_t epd_ensure_framebuffers(void);
static esp_err_t epd_ensure_spi_dma_bounce_buffer(void);
static void epd_reset(void);
static void epd_convert_portrait_to_native_impl(const uint8_t *portrait, uint8_t *native);
static void epd_convert_portrait_area_to_native(
    const uint8_t *portrait,
    uint8_t *native,
    uint16_t native_x,
    uint16_t native_y,
    uint16_t native_width,
    uint16_t native_height
);
static void epd_copy_native_area(
    uint8_t *dst,
    const uint8_t *src,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height
);
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
static esp_err_t epd_gray_init_sequence(void);
static esp_err_t epd_partial_frame_prepare(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
static esp_err_t epd_load_custom_lut(const uint8_t *lut, size_t length);
static esp_err_t epd_update_full(void);
static esp_err_t epd_update_partial(void);
static esp_err_t epd_update_gray4(void);
static esp_err_t epd_update_fast_custom_lut(bool turn_off, const char *wait_label);
static esp_err_t epd_update_partial_with_custom_lut(
    const uint8_t *lut,
    size_t length,
    bool turn_off,
    const char *wait_label
);
static bool epd_should_cancel(epd_gdey0426t82_phase_t phase);
static esp_err_t epd_set_phase(epd_gdey0426t82_phase_t phase);
static const char *epd_phase_name(epd_gdey0426t82_phase_t phase);

static uint32_t epd_us_to_ms(uint64_t us)
{
    return (uint32_t)((us + 999ULL) / 1000ULL);
}

static uint64_t epd_elapsed_us_since(int64_t start_us)
{
    return (uint64_t)(esp_timer_get_time() - start_us);
}

static void epd_timing_begin(const char *operation)
{
    memset(&s_timing, 0, sizeof(s_timing));
    s_timing.active = true;
    s_timing.operation = operation;
    s_timing.start_us = esp_timer_get_time();
}

static void epd_timing_add_tx(size_t bytes, int64_t start_us)
{
    if (!s_timing.active) {
        return;
    }

    s_timing.tx_us += epd_elapsed_us_since(start_us);
    s_timing.tx_bytes += bytes;
    ++s_timing.tx_calls;
}

static void epd_timing_add_busy(const char *label, int64_t start_us, esp_err_t ret)
{
    if (!s_timing.active) {
        return;
    }

    const uint64_t busy_us = epd_elapsed_us_since(start_us);
    s_timing.busy_us += busy_us;
    ++s_timing.busy_calls;
    ESP_LOGI(
        TAG,
        "epd busy op=%s label=%s busy=%ums ret=%s",
        s_timing.operation != NULL ? s_timing.operation : "?",
        label != NULL ? label : "?",
        (unsigned)epd_us_to_ms(busy_us),
        esp_err_to_name(ret)
    );
}

static void epd_timing_log(esp_err_t ret)
{
    if (!s_timing.active) {
        return;
    }

    const uint64_t total_us = epd_elapsed_us_since(s_timing.start_us);
    ESP_LOGI(
        TAG,
        "epd refresh op=%s total=%ums convert=%ums prepare=%ums update=%ums tx=%ums tx_bytes=%u tx_calls=%u busy=%ums busy_calls=%u spi_hz=%d ret=%s",
        s_timing.operation != NULL ? s_timing.operation : "?",
        (unsigned)epd_us_to_ms(total_us),
        (unsigned)epd_us_to_ms(s_timing.convert_us),
        (unsigned)epd_us_to_ms(s_timing.prepare_us),
        (unsigned)epd_us_to_ms(s_timing.update_us),
        (unsigned)epd_us_to_ms(s_timing.tx_us),
        (unsigned)s_timing.tx_bytes,
        (unsigned)s_timing.tx_calls,
        (unsigned)epd_us_to_ms(s_timing.busy_us),
        (unsigned)s_timing.busy_calls,
        s_cfg.spi_clock_hz,
        esp_err_to_name(ret)
    );
    s_timing.active = false;
}

static const char *epd_phase_name(epd_gdey0426t82_phase_t phase)
{
    switch (phase) {
        case EPD_GDEY0426T82_PHASE_IDLE:
            return "idle";
        case EPD_GDEY0426T82_PHASE_PREPARING:
            return "preparing";
        case EPD_GDEY0426T82_PHASE_TRANSMITTING:
            return "transmitting";
        case EPD_GDEY0426T82_PHASE_BUSY_WAIT:
            return "busy_wait";
        case EPD_GDEY0426T82_PHASE_DONE:
            return "done";
        case EPD_GDEY0426T82_PHASE_ABORTED:
            return "aborted";
        default:
            return "unknown";
    }
}

static bool epd_should_cancel(epd_gdey0426t82_phase_t phase)
{
    if (s_active_control == NULL || s_active_control->should_cancel == NULL) {
        return false;
    }
    if (phase == EPD_GDEY0426T82_PHASE_BUSY_WAIT && !s_active_control->aggressive_interrupt_mode) {
        return false;
    }
    return s_active_control->should_cancel(s_active_control->should_cancel_ctx, phase);
}

static esp_err_t epd_set_phase(epd_gdey0426t82_phase_t phase)
{
    if (s_active_control != NULL) {
        s_active_control->phase = phase;
    }
    if (phase == EPD_GDEY0426T82_PHASE_PREPARING || phase == EPD_GDEY0426T82_PHASE_TRANSMITTING) {
        if (epd_should_cancel(phase)) {
            if (s_active_control != NULL) {
                s_active_control->phase = EPD_GDEY0426T82_PHASE_ABORTED;
            }
            ESP_LOGW(TAG, "epd refresh aborted at phase=%s", epd_phase_name(phase));
            return EPD_GDEY0426T82_ERR_ABORTED;
        }
    }
    return ESP_OK;
}

static esp_err_t epd_write_command(uint8_t command)
{
    gpio_set_level(s_cfg.gpio_dc, 0);

    spi_transaction_t transaction = {
        .length = 8,
        .tx_buffer = &command,
    };

    const int64_t start_us = esp_timer_get_time();
    const esp_err_t ret = spi_device_polling_transmit(s_spi, &transaction);
    epd_timing_add_tx(1, start_us);
    return ret;
}

static esp_err_t epd_write_data_byte(uint8_t data)
{
    gpio_set_level(s_cfg.gpio_dc, 1);

    spi_transaction_t transaction = {
        .length = 8,
        .tx_buffer = &data,
    };

    const int64_t start_us = esp_timer_get_time();
    const esp_err_t ret = spi_device_polling_transmit(s_spi, &transaction);
    epd_timing_add_tx(1, start_us);
    return ret;
}

static esp_err_t epd_write_data(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_set_level(s_cfg.gpio_dc, 1);
    ESP_RETURN_ON_ERROR(
        epd_ensure_spi_dma_bounce_buffer(),
        TAG,
        "failed to allocate spi dma bounce buffer"
    );

    for (size_t offset = 0; offset < length; offset += EPD_SPI_CHUNK_SIZE) {
        ESP_RETURN_ON_ERROR(
            epd_set_phase(EPD_GDEY0426T82_PHASE_TRANSMITTING),
            TAG,
            "refresh aborted before tx chunk"
        );
        const size_t chunk = ((length - offset) > EPD_SPI_CHUNK_SIZE) ? EPD_SPI_CHUNK_SIZE : (length - offset);
        const uint8_t *tx_buffer = data + offset;

        if (!esp_ptr_dma_capable(tx_buffer) || (((uintptr_t)tx_buffer & 0x03U) != 0U)) {
            memcpy(s_spi_dma_bounce_buffer, tx_buffer, chunk);
            tx_buffer = s_spi_dma_bounce_buffer;
        }

        spi_transaction_t transaction = {
            .length = chunk * 8,
            .tx_buffer = tx_buffer,
        };
        const int64_t start_us = esp_timer_get_time();
        esp_err_t ret = spi_device_polling_transmit(s_spi, &transaction);
        epd_timing_add_tx(chunk, start_us);
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

static esp_err_t epd_wait_ready(const char *label)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(EPD_BUSY_TIMEOUT_MS);
    const int64_t start_us = esp_timer_get_time();
    esp_err_t ret = ESP_OK;

    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_BUSY_WAIT;
    }

    while (gpio_get_level(s_cfg.gpio_busy) != 0) {
        if (epd_should_cancel(EPD_GDEY0426T82_PHASE_BUSY_WAIT)
            && s_active_control != NULL
            && s_active_control->aggressive_interrupt_mode) {
            s_active_control->phase = EPD_GDEY0426T82_PHASE_ABORTED;
            ESP_LOGW(TAG, "epd busy_wait aborted by newer request phase=%s", epd_phase_name(EPD_GDEY0426T82_PHASE_BUSY_WAIT));
            ret = EPD_GDEY0426T82_ERR_ABORTED;
            break;
        }
        if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) {
            ESP_LOGE(TAG, "busy wait timed out");
            ret = ESP_ERR_TIMEOUT;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    epd_timing_add_busy(label, start_us, ret);
    return ret;
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

static esp_err_t epd_ensure_spi_dma_bounce_buffer(void)
{
    if (s_spi_dma_bounce_buffer == NULL) {
        s_spi_dma_bounce_buffer = heap_caps_malloc(
            EPD_SPI_CHUNK_SIZE,
            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (s_spi_dma_bounce_buffer == NULL) {
            return ESP_ERR_NO_MEM;
        }
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

static void epd_convert_portrait_to_native_impl(const uint8_t *portrait, uint8_t *native)
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

static void epd_convert_portrait_area_to_native(
    const uint8_t *portrait,
    uint8_t *native,
    uint16_t native_x,
    uint16_t native_y,
    uint16_t native_width,
    uint16_t native_height)
{
    for (uint16_t y = native_y; y < native_y + native_height; ++y) {
        for (uint16_t x = native_x; x < native_x + native_width; ++x) {
            const uint16_t portrait_x = EPD_GDEY0426T82_WIDTH - 1U - y;
            const uint16_t portrait_y = x;
            epd_native_set_pixel(native, x, y, epd_portrait_pixel_is_black(portrait, portrait_x, portrait_y));
        }
    }
}

static void epd_copy_native_area(
    uint8_t *dst,
    const uint8_t *src,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height)
{
    const size_t native_stride = EPD_NATIVE_WIDTH / 8;
    const size_t area_stride = width / 8;

    for (uint16_t row = 0; row < height; ++row) {
        uint8_t *dst_row = dst + ((size_t)y + row) * native_stride + (x / 8);
        const uint8_t *src_row = src + ((size_t)y + row) * native_stride + (x / 8);
        memcpy(dst_row, src_row, area_stride);
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

    ESP_RETURN_ON_ERROR(epd_wait_ready("reset"), TAG, "panel not ready after reset");
    ESP_RETURN_ON_ERROR(epd_write_command(0x12), TAG, "sw reset command failed");
    ESP_RETURN_ON_ERROR(epd_wait_ready("sw_reset"), TAG, "panel not ready after sw reset");

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

    return epd_wait_ready("init_done");
}

static esp_err_t epd_gray_init_sequence(void)
{
    epd_reset();

    ESP_RETURN_ON_ERROR(epd_wait_ready("reset"), TAG, "panel not ready after reset");
    ESP_RETURN_ON_ERROR(epd_write_command(0x12), TAG, "sw reset command failed");
    ESP_RETURN_ON_ERROR(epd_wait_ready("sw_reset"), TAG, "panel not ready after sw reset");

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
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x00), TAG, "gray border failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x18), TAG, "cmd 0x18 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x80), TAG, "data 0x18 failed");

    ESP_RETURN_ON_ERROR(
        epd_set_native_window(0, 0, EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT),
        TAG,
        "failed to set full window"
    );

    ESP_RETURN_ON_ERROR(epd_load_custom_lut(s_grayscale_lut, sizeof(s_grayscale_lut)), TAG, "gray lut load failed");
    return epd_wait_ready("gray_init_done");
}

static esp_err_t epd_partial_frame_prepare(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    const bool reuse_partial_init =
        s_active_control != NULL
        && (s_active_control->use_grid_compare_variant || s_active_control->reuse_partial_init);

    if (!reuse_partial_init) {
        ESP_RETURN_ON_ERROR(epd_full_init_sequence(), TAG, "partial init full base failed");
    }

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
    return epd_wait_ready("full_update");
}

static esp_err_t epd_update_partial(void)
{
    ESP_RETURN_ON_ERROR(epd_write_command(0x22), TAG, "partial cmd 0x22 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0xFF), TAG, "partial update control failed");
    ESP_RETURN_ON_ERROR(epd_write_command(0x20), TAG, "partial cmd 0x20 failed");
    return epd_wait_ready("partial_update");
}

static esp_err_t epd_update_gray4(void)
{
    ESP_RETURN_ON_ERROR(epd_write_command(0x21), TAG, "gray cmd 0x21 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x00), TAG, "gray ctrl1 byte0 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x00), TAG, "gray ctrl1 byte1 failed");
    ESP_RETURN_ON_ERROR(epd_write_command(0x22), TAG, "gray cmd 0x22 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0xC7), TAG, "gray update control failed");
    ESP_RETURN_ON_ERROR(epd_write_command(0x20), TAG, "gray cmd 0x20 failed");
    return epd_wait_ready("gray_update");
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

static esp_err_t epd_update_fast_custom_lut(bool turn_off, const char *wait_label)
{
    uint8_t display_mode = 0xC0 | 0x0F;
    if (turn_off) {
        display_mode |= 0x03;
    }

    ESP_RETURN_ON_ERROR(epd_write_command(0x21), TAG, "cmd 0x21 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x00), TAG, "fast ctrl1 byte0 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(0x00), TAG, "fast ctrl1 byte1 failed");

    ESP_RETURN_ON_ERROR(epd_write_command(0x22), TAG, "cmd 0x22 failed");
    ESP_RETURN_ON_ERROR(epd_write_data_byte(display_mode), TAG, "fast custom update mode failed");
    ESP_RETURN_ON_ERROR(epd_write_command(0x20), TAG, "cmd 0x20 failed");
    return epd_wait_ready(wait_label != NULL ? wait_label : "custom_lut_update");
}

static esp_err_t epd_update_partial_with_custom_lut(
    const uint8_t *lut,
    size_t length,
    bool turn_off,
    const char *wait_label)
{
    ESP_RETURN_ON_ERROR(epd_load_custom_lut(lut, length), TAG, "custom lut load failed");
    return epd_update_fast_custom_lut(turn_off, wait_label);
}

static esp_err_t epd_update_partial_with_grid_compare_variant(
    uint8_t variant_index,
    bool turn_off,
    const char *wait_label)
{
    uint8_t lut[sizeof(s_fast_mono_lut_a)];

    epd_select_grid_compare_lut(variant_index, lut, sizeof(lut));
    ESP_LOGI(
        TAG,
        "grid lut variant=%u phase3=0x%02x rep0=%u rep1=%u",
        (unsigned)variant_index,
        (unsigned)lut[32],
        (unsigned)lut[50],
        (unsigned)lut[55]);
    return epd_update_partial_with_custom_lut(lut, sizeof(lut), turn_off, wait_label);
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
    return epd_gdey0426t82_full_refresh_ex(buffer, length, NULL);
}

esp_err_t epd_gdey0426t82_full_refresh_ex(
    const uint8_t *buffer,
    size_t length,
    epd_gdey0426t82_refresh_control_t *control)
{
    esp_err_t ret = ESP_OK;
    int64_t phase_us;

    if (!s_initialized || buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    s_active_control = control;
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_IDLE;
    }
    epd_timing_begin("full");

    ret = epd_ensure_framebuffers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "framebuffers unavailable");
        goto done;
    }

    phase_us = esp_timer_get_time();
    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_PREPARING);
    if (ret != ESP_OK) {
        goto done;
    }
    epd_convert_portrait_to_native_impl(buffer, s_transfer_framebuffer);
    s_timing.convert_us += epd_elapsed_us_since(phase_us);

    phase_us = esp_timer_get_time();
    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_PREPARING);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_full_init_sequence();
    s_timing.prepare_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "panel init sequence failed");
        goto done;
    }

    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_TRANSMITTING);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_write_command(0x24);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write ram command failed");
        goto done;
    }
    ret = epd_write_data(s_transfer_framebuffer, EPD_NATIVE_BUFFER_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "frame write failed");
        goto done;
    }

    phase_us = esp_timer_get_time();
    if ((ret = epd_set_phase(EPD_GDEY0426T82_PHASE_BUSY_WAIT)) != ESP_OK) {
        goto done;
    }
    ret = epd_update_full();
    s_timing.update_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "full update failed");
        goto done;
    }

    memcpy(s_shadow_framebuffer, s_transfer_framebuffer, EPD_NATIVE_BUFFER_SIZE);
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_DONE;
    }

done:
    epd_timing_log(ret);
    s_active_control = NULL;
    return ret;
}

esp_err_t epd_gdey0426t82_partial_refresh(const uint8_t *buffer, size_t length)
{
    return epd_gdey0426t82_partial_refresh_ex(buffer, length, NULL);
}

esp_err_t epd_gdey0426t82_partial_refresh_ex(
    const uint8_t *buffer,
    size_t length,
    epd_gdey0426t82_refresh_control_t *control)
{
    esp_err_t ret = ESP_OK;
    int64_t phase_us;
    const bool use_custom_lut_a = control != NULL && control->use_custom_lut_a;
    const bool use_custom_lut_b = control != NULL && control->use_custom_lut_b;
    const bool use_grid_compare_variant = control != NULL && control->use_grid_compare_variant;

    if (!s_initialized || buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    s_active_control = control;
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_IDLE;
    }
    epd_timing_begin(
        use_grid_compare_variant ? "partial_full_window_grid"
        : use_custom_lut_a ? "partial_full_window_lut_a"
        : (use_custom_lut_b ? "partial_full_window_lut_b" : "partial_full_window"));

    ret = epd_ensure_framebuffers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "framebuffers unavailable");
        goto done;
    }

    phase_us = esp_timer_get_time();
    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_PREPARING);
    if (ret != ESP_OK) {
        goto done;
    }
    epd_convert_portrait_to_native_impl(buffer, s_transfer_framebuffer);
    s_timing.convert_us += epd_elapsed_us_since(phase_us);

    phase_us = esp_timer_get_time();
    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_PREPARING);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_partial_frame_prepare(0, 0, EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT);
    s_timing.prepare_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "partial prepare failed");
        goto done;
    }

    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_TRANSMITTING);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_write_command(0x24);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write current ram command failed");
        goto done;
    }
    ret = epd_write_data(s_transfer_framebuffer, EPD_NATIVE_BUFFER_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "current frame write failed");
        goto done;
    }

    ret = epd_write_command(0x26);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write previous ram command failed");
        goto done;
    }
    ret = epd_write_data(s_shadow_framebuffer, EPD_NATIVE_BUFFER_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "previous frame write failed");
        goto done;
    }

    phase_us = esp_timer_get_time();
    if ((ret = epd_set_phase(EPD_GDEY0426T82_PHASE_BUSY_WAIT)) != ESP_OK) {
        goto done;
    }
    if (use_grid_compare_variant) {
        ret = epd_update_partial_with_grid_compare_variant(
            control->grid_compare_variant_index,
            false,
            "grid_compare_update");
    } else if (use_custom_lut_a) {
        ret = epd_update_partial_with_custom_lut(
            s_fast_mono_lut_a,
            sizeof(s_fast_mono_lut_a),
            false,
            "custom_lut_a_update");
    } else if (use_custom_lut_b) {
        ret = epd_update_partial_with_custom_lut(
            s_fast_mono_lut_b,
            sizeof(s_fast_mono_lut_b),
            false,
            "custom_lut_b_update");
    } else {
        ret = epd_update_partial();
    }
    s_timing.update_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "partial update failed");
        goto done;
    }

    memcpy(s_shadow_framebuffer, s_transfer_framebuffer, EPD_NATIVE_BUFFER_SIZE);
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_DONE;
    }

done:
    epd_timing_log(ret);
    s_active_control = NULL;
    return ret;
}

esp_err_t epd_gdey0426t82_gray_refresh(
    const uint8_t *lsb_buffer,
    size_t lsb_length,
    const uint8_t *msb_buffer,
    size_t msb_length,
    epd_gdey0426t82_refresh_control_t *control)
{
    esp_err_t ret = ESP_OK;
    int64_t phase_us;

    if (!s_initialized
        || lsb_buffer == NULL
        || msb_buffer == NULL
        || lsb_length < EPD_GDEY0426T82_GRAY_PLANE_SIZE
        || msb_length < EPD_GDEY0426T82_GRAY_PLANE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    epd_timing_begin("gray4");
    s_active_control = control;
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_IDLE;
    }

    ret = epd_ensure_framebuffers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "framebuffers unavailable");
        goto done;
    }

    phase_us = esp_timer_get_time();
    ret = epd_gray_init_sequence();
    s_timing.prepare_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gray init sequence failed");
        goto done;
    }

    phase_us = esp_timer_get_time();
    epd_convert_portrait_to_native_impl(msb_buffer, s_transfer_framebuffer);
    s_timing.convert_us += epd_elapsed_us_since(phase_us);
    ret = epd_write_native_full(s_transfer_framebuffer, 0x26);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gray previous-plane write failed");
        goto done;
    }

    phase_us = esp_timer_get_time();
    epd_convert_portrait_to_native_impl(lsb_buffer, s_transfer_framebuffer);
    s_timing.convert_us += epd_elapsed_us_since(phase_us);
    ret = epd_write_native_full(s_transfer_framebuffer, 0x24);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gray current-plane write failed");
        goto done;
    }

    phase_us = esp_timer_get_time();
    ret = epd_update_gray4();
    s_timing.update_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gray update failed");
        goto done;
    }

done:
    epd_timing_log(ret);
    return ret;
}

esp_err_t epd_gdey0426t82_partial_refresh_area(
    const uint8_t *buffer,
    size_t length,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height)
{
    return epd_gdey0426t82_partial_refresh_area_ex(buffer, length, x, y, width, height, NULL);
}

esp_err_t epd_gdey0426t82_partial_refresh_area_ex(
    const uint8_t *buffer,
    size_t length,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    epd_gdey0426t82_refresh_control_t *control)
{
    esp_err_t ret = ESP_OK;
    int64_t phase_us;
    uint16_t native_x;
    uint16_t native_y;
    uint16_t native_width;
    uint16_t native_height;

    if (!s_initialized || buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    s_active_control = control;
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_IDLE;
    }
    const bool use_custom_lut_a = s_active_control != NULL && s_active_control->use_custom_lut_a;
    const bool use_custom_lut_b = s_active_control != NULL && s_active_control->use_custom_lut_b;
    const bool use_grid_compare_variant =
        s_active_control != NULL && s_active_control->use_grid_compare_variant;
    const bool reuse_partial_init =
        s_active_control != NULL && s_active_control->reuse_partial_init;

    epd_timing_begin(
        use_grid_compare_variant ? "partial_area_grid"
        : use_custom_lut_a ? "partial_area_lut_a"
        : (use_custom_lut_b ? "partial_area_lut_b"
            : (reuse_partial_init ? "partial_area_reuse" : "partial_area")));

    ret = epd_align_portrait_area_to_native(x, y, width, height, &native_x, &native_y, &native_width, &native_height);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "invalid partial area");
        goto done;
    }
    if (kLogPartialAreaGeometry) {
        ESP_LOGI(
            TAG,
            "epd area portrait x=%u y=%u w=%u h=%u native x=%u y=%u w=%u h=%u",
            (unsigned)x,
            (unsigned)y,
            (unsigned)width,
            (unsigned)height,
            (unsigned)native_x,
            (unsigned)native_y,
            (unsigned)native_width,
            (unsigned)native_height
        );
    }

    ret = epd_ensure_framebuffers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "framebuffers unavailable");
        goto done;
    }

    phase_us = esp_timer_get_time();
    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_PREPARING);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_partial_frame_prepare(native_x, native_y, native_width, native_height);
    s_timing.prepare_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "partial area prepare failed");
        goto done;
    }

    phase_us = esp_timer_get_time();
    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_PREPARING);
    if (ret != ESP_OK) {
        goto done;
    }
    epd_convert_portrait_area_to_native(
        buffer,
        s_transfer_framebuffer,
        native_x,
        native_y,
        native_width,
        native_height
    );
    s_timing.convert_us += epd_elapsed_us_since(phase_us);

    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_TRANSMITTING);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_write_command(0x24);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write current area command failed");
        goto done;
    }
    ret = epd_write_native_area(s_transfer_framebuffer, native_x, native_y, native_width, native_height);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "current area write failed");
        goto done;
    }

    ret = epd_write_command(0x26);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "write previous area command failed");
        goto done;
    }
    ret = epd_write_native_area(s_shadow_framebuffer, native_x, native_y, native_width, native_height);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "previous area write failed");
        goto done;
    }

    phase_us = esp_timer_get_time();
    if ((ret = epd_set_phase(EPD_GDEY0426T82_PHASE_BUSY_WAIT)) != ESP_OK) {
        goto done;
    }
    if (use_grid_compare_variant) {
        ret = epd_update_partial_with_grid_compare_variant(
            s_active_control->grid_compare_variant_index,
            false,
            "grid_compare_update");
    } else if (use_custom_lut_a) {
        ret = epd_update_partial_with_custom_lut(
            s_fast_mono_lut_a,
            sizeof(s_fast_mono_lut_a),
            false,
            "custom_lut_a_update");
    } else if (use_custom_lut_b) {
        ret = epd_update_partial_with_custom_lut(
            s_fast_mono_lut_b,
            sizeof(s_fast_mono_lut_b),
            false,
            "custom_lut_b_update");
    } else {
        ret = epd_update_partial();
    }
    s_timing.update_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "partial area update failed");
        goto done;
    }

    epd_copy_native_area(
        s_shadow_framebuffer,
        s_transfer_framebuffer,
        native_x,
        native_y,
        native_width,
        native_height
    );
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_DONE;
    }

done:
    epd_timing_log(ret);
    s_active_control = NULL;
    return ret;
}

esp_err_t epd_gdey0426t82_sleep(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(epd_write_command(0x10), TAG, "sleep command failed");
    return epd_write_data_byte(0x01);
}

bool epd_gdey0426t82_is_aborted_error(esp_err_t ret)
{
    return ret == EPD_GDEY0426T82_ERR_ABORTED;
}



void epd_gdey0426t82_convert_portrait_to_native(const uint8_t *portrait, uint8_t *native_buffer)
{
    if (portrait == NULL || native_buffer == NULL) {
        return;
    }

    epd_convert_portrait_to_native_impl(portrait, native_buffer);
}

esp_err_t epd_gdey0426t82_full_refresh_native_ex(
    const uint8_t *native_buffer,
    size_t length,
    epd_gdey0426t82_refresh_control_t *control)
{
    esp_err_t ret = ESP_OK;
    int64_t phase_us;

    if (!s_initialized || native_buffer == NULL || length < EPD_NATIVE_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    s_active_control = control;
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_IDLE;
    }
    epd_timing_begin("full_native");

    ret = epd_ensure_framebuffers();
    if (ret != ESP_OK) {
        goto done;
    }

    phase_us = esp_timer_get_time();
    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_PREPARING);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_full_init_sequence();
    s_timing.prepare_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        goto done;
    }

    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_TRANSMITTING);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_write_command(0x24);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_write_data(native_buffer, EPD_NATIVE_BUFFER_SIZE);
    if (ret != ESP_OK) {
        goto done;
    }

    phase_us = esp_timer_get_time();
    if ((ret = epd_set_phase(EPD_GDEY0426T82_PHASE_BUSY_WAIT)) != ESP_OK) {
        goto done;
    }
    ret = epd_update_full();
    s_timing.update_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        goto done;
    }

    memcpy(s_shadow_framebuffer, native_buffer, EPD_NATIVE_BUFFER_SIZE);
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_DONE;
    }

done:
    epd_timing_log(ret);
    s_active_control = NULL;
    return ret;
}

esp_err_t epd_gdey0426t82_partial_refresh_area_native_ex(
    const uint8_t *native_buffer,
    size_t length,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    epd_gdey0426t82_refresh_control_t *control)
{
    esp_err_t ret = ESP_OK;
    int64_t phase_us;

    if (!s_initialized || native_buffer == NULL || length < EPD_NATIVE_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (x >= EPD_NATIVE_WIDTH || y >= EPD_NATIVE_HEIGHT || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((uint32_t)x + width > EPD_NATIVE_WIDTH || (uint32_t)y + height > EPD_NATIVE_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    s_active_control = control;
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_IDLE;
    }
    const bool use_custom_lut_a = s_active_control != NULL && s_active_control->use_custom_lut_a;

    epd_timing_begin(use_custom_lut_a ? "partial_native_lut_a" : "partial_native");

    ret = epd_ensure_framebuffers();
    if (ret != ESP_OK) {
        goto done;
    }

    phase_us = esp_timer_get_time();
    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_PREPARING);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_partial_frame_prepare(x, y, width, height);
    s_timing.prepare_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        goto done;
    }

    ret = epd_set_phase(EPD_GDEY0426T82_PHASE_TRANSMITTING);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_write_command(0x24);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_write_native_area(native_buffer, x, y, width, height);
    if (ret != ESP_OK) {
        goto done;
    }

    ret = epd_write_command(0x26);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = epd_write_native_area(s_shadow_framebuffer, x, y, width, height);
    if (ret != ESP_OK) {
        goto done;
    }

    phase_us = esp_timer_get_time();
    if ((ret = epd_set_phase(EPD_GDEY0426T82_PHASE_BUSY_WAIT)) != ESP_OK) {
        goto done;
    }
    if (use_custom_lut_a) {
        ret = epd_update_partial_with_custom_lut(
            s_fast_mono_lut_a,
            sizeof(s_fast_mono_lut_a),
            false,
            "custom_lut_a_update");
    } else {
        ret = epd_update_partial();
    }
    s_timing.update_us += epd_elapsed_us_since(phase_us);
    if (ret != ESP_OK) {
        goto done;
    }

    epd_copy_native_area(s_shadow_framebuffer, native_buffer, x, y, width, height);
    if (s_active_control != NULL) {
        s_active_control->phase = EPD_GDEY0426T82_PHASE_DONE;
    }

done:
    epd_timing_log(ret);
    s_active_control = NULL;
    return ret;
}
