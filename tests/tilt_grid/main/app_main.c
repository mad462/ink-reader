#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "epd_gdey0426t82.h"
#include "ink_cpfont.h"
#include "ink_wifi_manager.h"
#include "tilt_display_refresh.h"
#include "tilt_grid_input.h"
#include "tilt_wifi_setup.h"

static const char *TAG = "tilt_grid";

#define MPU_I2C_SCL_GPIO GPIO_NUM_1
#define MPU_I2C_SDA_GPIO GPIO_NUM_2
#define MPU_I2C_PORT I2C_NUM_0
#define MPU_I2C_SPEED_HZ 100000
#define MPU_I2C_XFER_TIMEOUT_MS 1000
#define MPU60X0_ADDR_LOW 0x68
#define MPU60X0_ADDR_HIGH 0x69
#define MPU60X0_REG_WHO_AM_I 0x75
#define MPU60X0_REG_PWR_MGMT_1 0x6B
#define MPU60X0_REG_ACCEL_XOUT_H 0x3B

enum {
    SAMPLE_PERIOD_MS = 50,
    CALIBRATION_SAMPLES = 64,
    DISPLAY_TASK_STACK_SIZE = 8192,
    INPUT_TASK_STACK_SIZE = 6144,
    WIFI_TASK_STACK_SIZE = 6144,
    DISPLAY_QUEUE_LEN = 8,
    WIFI_QUEUE_LEN = 4,
};

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} accel_sample_t;

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    uint8_t address;
    int neutral_x;
    int neutral_y;
    bool use_bitbang;
} mpu6050_t;

typedef struct {
    uint32_t previous_mask;
    uint32_t hold_start_ms[4];
    uint32_t long_report_mask;
} local_button_state_t;

enum {
    LOCAL_BUTTON_BACK = 1U << 0,
    LOCAL_BUTTON_CONFIRM = 1U << 1,
    LOCAL_BUTTON_LOWER = 1U << 2,
    LOCAL_BUTTON_UPPER = 1U << 3,
    LOCAL_BUTTON_COUNT = 4,
    LOCAL_BUTTON_HOLD_MS = 900,
};

typedef struct {
    ink_cpfont_t menu;
    ink_cpfont_t footer;
    bool tf_mounted;
} ui_fonts_t;

typedef enum {
    DISPLAY_REQUEST_FULL = 0,
    DISPLAY_REQUEST_SCREEN_PARTIAL,
    DISPLAY_REQUEST_WIFI_LIST_BODY,
    DISPLAY_REQUEST_WIFI_LIST_SELECTION,
    DISPLAY_REQUEST_KEYBOARD_SELECTION,
} display_request_type_t;

typedef struct {
    display_request_type_t type;
    int old_index;
    int new_index;
    int old_first;
    int new_first;
    int old_col;
    int old_row;
    int new_col;
    int new_row;
    int area_x;
    int area_y;
    int area_w;
    int area_h;
} display_request_t;

typedef struct {
    mpu6050_t mpu;
    tilt_grid_input_t input;
    keyboard_text_t keyboard_text;
    keyboard_layer_t keyboard_layer;
    local_button_state_t buttons;
    ui_fonts_t fonts;
    wifi_setup_state_t wifi_setup;
    uint8_t *framebuffer;
    SemaphoreHandle_t lock;
    QueueHandle_t display_queue;
    QueueHandle_t wifi_queue;
    volatile bool display_busy;
} tilt_app_context_t;

static tilt_app_context_t s_app;

static esp_err_t mpu6050_read_reg(mpu6050_t *mpu, uint8_t reg, uint8_t *value);
static esp_err_t mpu6050_read_regs(mpu6050_t *mpu, uint8_t reg, uint8_t *data, size_t len);
static esp_err_t mpu6050_write_reg(mpu6050_t *mpu, uint8_t reg, uint8_t value);

static int16_t read_be_i16(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static esp_err_t mpu6050_hw_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(dev, &reg, 1, value, 1, MPU_I2C_XFER_TIMEOUT_MS);
}

static esp_err_t mpu6050_hw_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, MPU_I2C_XFER_TIMEOUT_MS);
}

static esp_err_t mpu6050_hw_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    const uint8_t data[2] = {reg, value};
    return i2c_master_transmit(dev, data, sizeof(data), MPU_I2C_XFER_TIMEOUT_MS);
}

static esp_err_t mpu6050_read_accel(mpu6050_t *mpu, accel_sample_t *sample)
{
    uint8_t raw[6] = {0};

    if (mpu == NULL || sample == NULL || (!mpu->use_bitbang && mpu->dev == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(mpu6050_read_regs(mpu, MPU60X0_REG_ACCEL_XOUT_H, raw, sizeof(raw)), TAG, "accel read");
    sample->x = read_be_i16(&raw[0]);
    sample->y = read_be_i16(&raw[2]);
    sample->z = read_be_i16(&raw[4]);
    return ESP_OK;
}

static void mpu6050_recover_i2c_lines(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << MPU_I2C_SCL_GPIO) | (1ULL << MPU_I2C_SDA_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&cfg);
    gpio_set_level(MPU_I2C_SDA_GPIO, 1);
    gpio_set_level(MPU_I2C_SCL_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    for (int i = 0; i < 9; ++i) {
        gpio_set_level(MPU_I2C_SCL_GPIO, 0);
        esp_rom_delay_us(5);
        gpio_set_level(MPU_I2C_SCL_GPIO, 1);
        esp_rom_delay_us(5);
    }

    gpio_set_level(MPU_I2C_SDA_GPIO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(MPU_I2C_SCL_GPIO, 1);
    esp_rom_delay_us(5);
    gpio_set_level(MPU_I2C_SDA_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
}

static void mpu6050_log_i2c_line_levels(const char *label)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << MPU_I2C_SCL_GPIO) | (1ULL << MPU_I2C_SDA_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&cfg);
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_LOGI(
        TAG,
        "I2C line levels %s: SCL(GPIO1)=%d SDA(GPIO2)=%d",
        label,
        gpio_get_level(MPU_I2C_SCL_GPIO),
        gpio_get_level(MPU_I2C_SDA_GPIO));
}

static void bitbang_i2c_scl_release(void)
{
    gpio_set_level(MPU_I2C_SCL_GPIO, 1);
    esp_rom_delay_us(25);
}

static void bitbang_i2c_scl_low(void)
{
    gpio_set_level(MPU_I2C_SCL_GPIO, 0);
    esp_rom_delay_us(25);
}

static void bitbang_i2c_sda_release(void)
{
    gpio_set_level(MPU_I2C_SDA_GPIO, 1);
    esp_rom_delay_us(25);
}

static void bitbang_i2c_sda_low(void)
{
    gpio_set_level(MPU_I2C_SDA_GPIO, 0);
    esp_rom_delay_us(25);
}

static bool bitbang_i2c_wait_scl_high(void)
{
    for (int i = 0; i < 100; ++i) {
        if (gpio_get_level(MPU_I2C_SCL_GPIO) == 1) {
            return true;
        }
        esp_rom_delay_us(10);
    }
    return false;
}

static void bitbang_i2c_stop(void)
{
    bitbang_i2c_sda_low();
    bitbang_i2c_scl_release();
    (void)bitbang_i2c_wait_scl_high();
    bitbang_i2c_sda_release();
}

static void bitbang_i2c_start(void)
{
    bitbang_i2c_sda_release();
    bitbang_i2c_scl_release();
    (void)bitbang_i2c_wait_scl_high();
    bitbang_i2c_sda_low();
    bitbang_i2c_scl_low();
}

static bool bitbang_i2c_write_byte(uint8_t value, bool *ack)
{
    for (int bit = 7; bit >= 0; --bit) {
        if ((value & (1U << bit)) != 0) {
            bitbang_i2c_sda_release();
        } else {
            bitbang_i2c_sda_low();
        }
        bitbang_i2c_scl_release();
        if (!bitbang_i2c_wait_scl_high()) {
            return false;
        }
        bitbang_i2c_scl_low();
    }

    bitbang_i2c_sda_release();
    bitbang_i2c_scl_release();
    if (!bitbang_i2c_wait_scl_high()) {
        return false;
    }
    *ack = gpio_get_level(MPU_I2C_SDA_GPIO) == 0;
    bitbang_i2c_scl_low();
    return true;
}

static bool bitbang_i2c_read_byte(uint8_t *value, bool ack)
{
    uint8_t data = 0;

    if (value == NULL) {
        return false;
    }

    bitbang_i2c_sda_release();
    for (int bit = 7; bit >= 0; --bit) {
        bitbang_i2c_scl_release();
        if (!bitbang_i2c_wait_scl_high()) {
            return false;
        }
        if (gpio_get_level(MPU_I2C_SDA_GPIO) != 0) {
            data |= (uint8_t)(1U << bit);
        }
        bitbang_i2c_scl_low();
    }

    if (ack) {
        bitbang_i2c_sda_low();
    } else {
        bitbang_i2c_sda_release();
    }
    bitbang_i2c_scl_release();
    if (!bitbang_i2c_wait_scl_high()) {
        return false;
    }
    bitbang_i2c_scl_low();
    bitbang_i2c_sda_release();
    *value = data;
    return true;
}

static void bitbang_i2c_configure_bus(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << MPU_I2C_SCL_GPIO) | (1ULL << MPU_I2C_SDA_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    (void)gpio_config(&cfg);
    bitbang_i2c_sda_release();
    bitbang_i2c_scl_release();
    vTaskDelay(pdMS_TO_TICKS(2));
}

static bool bitbang_i2c_probe_address(uint8_t addr)
{
    bool ack = false;
    bool clock_ok = false;

    bitbang_i2c_start();
    clock_ok = bitbang_i2c_write_byte((uint8_t)(addr << 1), &ack);
    bitbang_i2c_stop();
    ESP_LOGI(TAG, "bitbang I2C address 0x%02x write ACK=%d clock_ok=%d", addr, ack ? 1 : 0, clock_ok ? 1 : 0);
    return clock_ok && ack;
}

static uint8_t mpu6050_bitbang_probe(void)
{
    const uint8_t candidates[] = {MPU60X0_ADDR_LOW, MPU60X0_ADDR_HIGH};

    bitbang_i2c_configure_bus();

    ESP_LOGI(TAG, "bitbang I2C idle levels: SCL(GPIO1)=%d SDA(GPIO2)=%d", gpio_get_level(MPU_I2C_SCL_GPIO), gpio_get_level(MPU_I2C_SDA_GPIO));
    if (gpio_get_level(MPU_I2C_SCL_GPIO) == 0 || gpio_get_level(MPU_I2C_SDA_GPIO) == 0) {
        ESP_LOGW(TAG, "bitbang I2C skipped because bus is not idle high");
        return 0;
    }

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        const uint8_t addr = candidates[i];

        if (bitbang_i2c_probe_address(addr)) {
            return addr;
        }
    }
    return 0;
}

static esp_err_t mpu6050_bitbang_write_reg(uint8_t address, uint8_t reg, uint8_t value)
{
    bool ack = false;

    bitbang_i2c_configure_bus();
    bitbang_i2c_start();
    if (!bitbang_i2c_write_byte((uint8_t)(address << 1), &ack) || !ack) {
        bitbang_i2c_stop();
        return ESP_ERR_NOT_FOUND;
    }
    if (!bitbang_i2c_write_byte(reg, &ack) || !ack) {
        bitbang_i2c_stop();
        return ESP_FAIL;
    }
    if (!bitbang_i2c_write_byte(value, &ack) || !ack) {
        bitbang_i2c_stop();
        return ESP_FAIL;
    }
    bitbang_i2c_stop();
    return ESP_OK;
}

static esp_err_t mpu6050_bitbang_read_regs(uint8_t address, uint8_t reg, uint8_t *data, size_t len)
{
    bool ack = false;

    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    bitbang_i2c_configure_bus();
    bitbang_i2c_start();
    if (!bitbang_i2c_write_byte((uint8_t)(address << 1), &ack) || !ack) {
        bitbang_i2c_stop();
        return ESP_ERR_NOT_FOUND;
    }
    if (!bitbang_i2c_write_byte(reg, &ack) || !ack) {
        bitbang_i2c_stop();
        return ESP_FAIL;
    }

    bitbang_i2c_start();
    if (!bitbang_i2c_write_byte((uint8_t)((address << 1) | 1U), &ack) || !ack) {
        bitbang_i2c_stop();
        return ESP_FAIL;
    }

    for (size_t i = 0; i < len; ++i) {
        if (!bitbang_i2c_read_byte(&data[i], i + 1 < len)) {
            bitbang_i2c_stop();
            return ESP_ERR_TIMEOUT;
        }
    }
    bitbang_i2c_stop();
    return ESP_OK;
}

static esp_err_t mpu6050_read_reg(mpu6050_t *mpu, uint8_t reg, uint8_t *value)
{
    if (mpu == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mpu->use_bitbang) {
        return mpu6050_bitbang_read_regs(mpu->address, reg, value, 1);
    }
    return mpu6050_hw_read_reg(mpu->dev, reg, value);
}

static esp_err_t mpu6050_read_regs(mpu6050_t *mpu, uint8_t reg, uint8_t *data, size_t len)
{
    if (mpu == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mpu->use_bitbang) {
        return mpu6050_bitbang_read_regs(mpu->address, reg, data, len);
    }
    return mpu6050_hw_read_regs(mpu->dev, reg, data, len);
}

static esp_err_t mpu6050_write_reg(mpu6050_t *mpu, uint8_t reg, uint8_t value)
{
    if (mpu == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mpu->use_bitbang) {
        return mpu6050_bitbang_write_reg(mpu->address, reg, value);
    }
    return mpu6050_hw_write_reg(mpu->dev, reg, value);
}

static void mpu6050_deinit(mpu6050_t *mpu)
{
    if (mpu == NULL) {
        return;
    }
    if (mpu->dev != NULL) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_bus_rm_device(mpu->dev));
        mpu->dev = NULL;
    }
    if (mpu->bus != NULL) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_del_master_bus(mpu->bus));
        mpu->bus = NULL;
    }
}

static esp_err_t mpu6050_init(mpu6050_t *mpu)
{
    esp_err_t ret;
    uint8_t found_addr = 0;
    uint8_t who = 0;

    if (mpu == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(mpu, 0, sizeof(*mpu));

    mpu6050_log_i2c_line_levels("before recovery");
    mpu6050_recover_i2c_lines();
    mpu6050_log_i2c_line_levels("after recovery");

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = MPU_I2C_PORT,
        .sda_io_num = MPU_I2C_SDA_GPIO,
        .scl_io_num = MPU_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &mpu->bus), TAG, "i2c bus");

    ESP_LOGI(TAG, "MPU-60X0 probe SCL=%d SDA=%d speed=%d", MPU_I2C_SCL_GPIO, MPU_I2C_SDA_GPIO, MPU_I2C_SPEED_HZ);
    ESP_LOGI(TAG, "I2C straps: AD0/SDO=GND for 0x68 or VCC for 0x69, NCS=VCC, FSYNC=GND when unused");
    const uint8_t candidates[] = {MPU60X0_ADDR_LOW, MPU60X0_ADDR_HIGH};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        const uint8_t addr = candidates[i];
        ret = i2c_master_probe(mpu->bus, addr, MPU_I2C_XFER_TIMEOUT_MS);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C device ACK at 0x%02x", addr);
            found_addr = addr;
            break;
        }
    }
    if (found_addr == 0) {
        ESP_LOGW(TAG, "hardware I2C did not find MPU-60X0 at 0x68/0x69; trying low-speed bitbang I2C");
        mpu6050_log_i2c_line_levels("after failed probe");
        found_addr = mpu6050_bitbang_probe();
        mpu6050_deinit(mpu);
        if (found_addr == 0) {
            ESP_LOGE(TAG, "MPU-60X0 not found at 0x68/0x69 by hardware or bitbang I2C");
            return ESP_ERR_NOT_FOUND;
        }
        mpu->address = found_addr;
        mpu->use_bitbang = true;
        ESP_LOGW(TAG, "Using low-speed bitbang I2C fallback at addr=0x%02x", found_addr);
    } else {
        const i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = found_addr,
            .scl_speed_hz = MPU_I2C_SPEED_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(mpu->bus, &dev_cfg, &mpu->dev), TAG, "mpu device");
        mpu->address = found_addr;
    }

    ESP_RETURN_ON_ERROR(mpu6050_read_reg(mpu, MPU60X0_REG_WHO_AM_I, &who), TAG, "whoami");
    ESP_LOGI(TAG, "MPU-60X0 addr=0x%02x WHO_AM_I=0x%02x bus=%s", found_addr, who, mpu->use_bitbang ? "bitbang" : "hardware");

    ESP_RETURN_ON_ERROR(mpu6050_write_reg(mpu, MPU60X0_REG_PWR_MGMT_1, 0x00), TAG, "wake");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static esp_err_t mpu6050_calibrate_neutral(mpu6050_t *mpu)
{
    int64_t sum_x = 0;
    int64_t sum_y = 0;
    int count = 0;

    for (int i = 0; i < CALIBRATION_SAMPLES; ++i) {
        accel_sample_t sample = {0};
        if (mpu6050_read_accel(mpu, &sample) == ESP_OK) {
            sum_x += sample.x;
            sum_y += sample.y;
            ++count;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (count == 0) {
        return ESP_FAIL;
    }
    mpu->neutral_x = (int)(sum_x / count);
    mpu->neutral_y = (int)(sum_y / count);
    ESP_LOGI(TAG, "GY6500 neutral=(x:%d y:%d) samples=%d", mpu->neutral_x, mpu->neutral_y, count);
    return ESP_OK;
}

static const char *keyboard_label(keyboard_layer_t layer, int column, int row)
{
    return ink_wifi_setup_keyboard_label(layer, column, row);
}

static wifi_ui_cursor_t make_wifi_ui_cursor(const tilt_grid_input_t *state)
{
    wifi_ui_cursor_t cursor = {0};
    if (state != NULL) {
        cursor.column = state->column;
        cursor.row = state->row;
    }
    return cursor;
}

static wifi_ui_fonts_t make_wifi_ui_fonts(ui_fonts_t *fonts)
{
    wifi_ui_fonts_t wifi_fonts = {0};
    if (fonts != NULL) {
        wifi_fonts.menu = &fonts->menu;
        wifi_fonts.footer = &fonts->footer;
    }
    return wifi_fonts;
}

static void keyboard_normalize_cursor(tilt_grid_input_t *state, tilt_grid_direction_t direction, int old_row)
{
    if (state == NULL) {
        return;
    }
    ink_wifi_setup_normalize_selection(&state->column, &state->row, direction, old_row);
}

static void keyboard_text_clear(keyboard_text_t *text)
{
    ink_wifi_setup_keyboard_text_clear(text);
}

static bool keyboard_activate_label(keyboard_text_t *text, keyboard_layer_t *layer, const char *label)
{
    const bool handled = ink_wifi_setup_keyboard_activate_label(text, layer, label);
    if (handled && strcmp(label, "ok") == 0) {
        ESP_LOGI(TAG, "keyboard OK text=\"%s\"", text->text);
    }
    return handled;
}

static bool wifi_prepare_selected_connect_request(
    const keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    wifi_work_request_t *request)
{
    return ink_wifi_setup_prepare_selected_connect_request(text, wifi, request);
}

static bool wifi_prepare_selected_saved_request(
    const wifi_setup_state_t *wifi,
    wifi_work_type_t type,
    wifi_work_request_t *request)
{
    return ink_wifi_setup_prepare_selected_saved_request(wifi, type, request);
}

static void post_display_request(
    tilt_app_context_t *ctx,
    display_request_type_t type,
    int old_index,
    int new_index,
    int old_first,
    int new_first)
{
    if (ctx == NULL || ctx->display_queue == NULL) {
        return;
    }
    const display_request_t request = {
        .type = type,
        .old_index = old_index,
        .new_index = new_index,
        .old_first = old_first,
        .new_first = new_first,
        .old_col = -1,
        .old_row = -1,
        .new_col = -1,
        .new_row = -1,
        .area_x = 0,
        .area_y = 0,
        .area_w = EPD_GDEY0426T82_WIDTH,
        .area_h = EPD_GDEY0426T82_HEIGHT,
    };
    (void)xQueueSend(ctx->display_queue, &request, 0);
}

static void post_screen_partial_request(tilt_app_context_t *ctx, int x, int y, int w, int h)
{
    if (ctx == NULL || ctx->display_queue == NULL) {
        return;
    }
    const display_request_t request = {
        .type = DISPLAY_REQUEST_SCREEN_PARTIAL,
        .old_index = -1,
        .new_index = -1,
        .old_first = -1,
        .new_first = -1,
        .old_col = -1,
        .old_row = -1,
        .new_col = -1,
        .new_row = -1,
        .area_x = x,
        .area_y = y,
        .area_w = w,
        .area_h = h,
    };
    (void)xQueueSend(ctx->display_queue, &request, 0);
}

static void clear_display_requests(tilt_app_context_t *ctx)
{
    if (ctx == NULL || ctx->display_queue == NULL) {
        return;
    }

    display_request_t dropped;
    while (xQueueReceive(ctx->display_queue, &dropped, 0) == pdTRUE) {
    }
}

static bool display_can_accept_input(const tilt_app_context_t *ctx)
{
    if (ctx == NULL || ctx->display_queue == NULL || ctx->display_busy) {
        return false;
    }
    return uxQueueMessagesWaiting(ctx->display_queue) == 0;
}

static void post_keyboard_selection_request(
    tilt_app_context_t *ctx,
    int old_col,
    int old_row,
    int new_col,
    int new_row)
{
    if (ctx == NULL || ctx->display_queue == NULL) {
        return;
    }
    const display_request_t request = {
        .type = DISPLAY_REQUEST_KEYBOARD_SELECTION,
        .old_index = -1,
        .new_index = -1,
        .old_first = -1,
        .new_first = -1,
        .old_col = old_col,
        .old_row = old_row,
        .new_col = new_col,
        .new_row = new_row,
    };
    (void)xQueueSend(ctx->display_queue, &request, 0);
}

static void post_wifi_work(tilt_app_context_t *ctx, const wifi_work_request_t *request)
{
    if (ctx == NULL || ctx->wifi_queue == NULL || request == NULL) {
        return;
    }
    (void)xQueueSend(ctx->wifi_queue, request, 0);
}

static esp_err_t local_buttons_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_NUM_9) | (1ULL << GPIO_NUM_10) | (1ULL << GPIO_NUM_11) | (1ULL << GPIO_NUM_12),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&cfg);
}

static uint32_t local_buttons_read_mask(void)
{
    uint32_t mask = 0;

    if (gpio_get_level(GPIO_NUM_9) == 0) {
        mask |= LOCAL_BUTTON_BACK;
    }
    if (gpio_get_level(GPIO_NUM_10) == 0) {
        mask |= LOCAL_BUTTON_CONFIRM;
    }
    if (gpio_get_level(GPIO_NUM_12) == 0) {
        mask |= LOCAL_BUTTON_LOWER;
    }
    if (gpio_get_level(GPIO_NUM_11) == 0) {
        mask |= LOCAL_BUTTON_UPPER;
    }
    return mask;
}

static uint32_t local_button_mask_from_index(int index)
{
    switch (index) {
        case 0:
            return LOCAL_BUTTON_BACK;
        case 1:
            return LOCAL_BUTTON_CONFIRM;
        case 2:
            return LOCAL_BUTTON_LOWER;
        case 3:
            return LOCAL_BUTTON_UPPER;
        default:
            return 0;
    }
}

static void local_buttons_poll(local_button_state_t *state, uint32_t now_ms, uint32_t *pressed_mask, uint32_t *long_pressed_mask)
{
    if (pressed_mask != NULL) {
        *pressed_mask = 0;
    }
    if (long_pressed_mask != NULL) {
        *long_pressed_mask = 0;
    }
    if (state == NULL) {
        return;
    }

    const uint32_t current = local_buttons_read_mask();
    const uint32_t pressed = current & ~state->previous_mask;
    const uint32_t released = state->previous_mask & ~current;

    for (int i = 0; i < LOCAL_BUTTON_COUNT; ++i) {
        const uint32_t mask = local_button_mask_from_index(i);
        if ((pressed & mask) != 0) {
            state->hold_start_ms[i] = now_ms;
            state->long_report_mask &= ~mask;
        }
        if ((released & mask) != 0) {
            if ((state->long_report_mask & mask) == 0 && pressed_mask != NULL) {
                *pressed_mask |= mask;
            }
            state->hold_start_ms[i] = 0;
            state->long_report_mask &= ~mask;
        }
        if ((current & mask) != 0
            && (state->long_report_mask & mask) == 0
            && state->hold_start_ms[i] != 0
            && now_ms - state->hold_start_ms[i] >= LOCAL_BUTTON_HOLD_MS) {
            if (long_pressed_mask != NULL) {
                *long_pressed_mask |= mask;
            }
            state->long_report_mask |= mask;
        }
    }

    state->previous_mask = current;
}

static void draw_keyboard_key(uint8_t *buffer, keyboard_layer_t layer, int column, int row, bool selected)
{
    ink_wifi_setup_ui_draw_keyboard_key(buffer, layer, column, row, selected);
}

static int wifi_list_visible_first(const wifi_setup_state_t *wifi)
{
    return ink_wifi_setup_ui_wifi_list_visible_first(wifi);
}

static void draw_wifi_list_row(uint8_t *buffer, const wifi_setup_state_t *wifi, ui_fonts_t *fonts, int index)
{
    const wifi_ui_fonts_t wifi_fonts = make_wifi_ui_fonts(fonts);
    ink_wifi_setup_ui_draw_wifi_list_row(buffer, wifi, &wifi_fonts, index);
}

static void draw_wifi_list(uint8_t *buffer, const wifi_setup_state_t *wifi, ui_fonts_t *fonts)
{
    const wifi_ui_fonts_t wifi_fonts = make_wifi_ui_fonts(fonts);
    ink_wifi_setup_ui_draw_screen(buffer, KEYBOARD_LAYER_LOWER, NULL, NULL, wifi, &wifi_fonts);
}

static void draw_wifi_setup_screen(
    uint8_t *buffer,
    keyboard_layer_t layer,
    const tilt_grid_input_t *keyboard_state,
    const keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    ui_fonts_t *fonts)
{
    const wifi_ui_cursor_t cursor = make_wifi_ui_cursor(keyboard_state);
    const wifi_ui_fonts_t wifi_fonts = make_wifi_ui_fonts(fonts);
    const wifi_ui_cursor_t *cursor_ptr = keyboard_state != NULL ? &cursor : NULL;
    ink_wifi_setup_ui_draw_screen(buffer, layer, cursor_ptr, text, wifi, &wifi_fonts);
}

static uint8_t *alloc_display_buffer(void)
{
    uint8_t *buffer = heap_caps_malloc(EPD_GDEY0426T82_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(EPD_GDEY0426T82_BUFFER_SIZE, MALLOC_CAP_8BIT);
    }
    return buffer;
}

static esp_err_t mount_tf_card_for_fonts(ui_fonts_t *fonts)
{
    static const int kSdSdioClk = 40;
    static const int kSdSdioCmd = 39;
    static const int kSdSdioD0 = 41;
    static const int kSdSdioD1 = 42;
    static const int kSdSdioD2 = 48;
    static const int kSdSdioD3 = 38;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = kSdSdioClk;
    slot_config.cmd = kSdSdioCmd;
    slot_config.d0 = kSdSdioD0;
    slot_config.d1 = kSdSdioD1;
    slot_config.d2 = kSdSdioD2;
    slot_config.d3 = kSdSdioD3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
    sdmmc_card_t *card = NULL;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret == ESP_OK && fonts != NULL) {
        fonts->tf_mounted = true;
    }
    return ret;
}

static bool load_first_font(ink_cpfont_t *font, const char *const *paths, size_t path_count)
{
    ink_cpfont_init(font);
    for (size_t i = 0; i < path_count; ++i) {
        if (ink_cpfont_load(font, paths[i]) == ESP_OK) {
            ESP_LOGI(TAG, "loaded ui font path=%s", paths[i]);
            return true;
        }
    }
    return false;
}

static void ui_fonts_init(ui_fonts_t *fonts)
{
    static const char *const menu_paths[] = {
        "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_24.cpfont",
        "/sdcard/fonts/LXGWWenKai_24.cpfont",
        "/sdcard/FONTS/LXGWWENKAI_24.CPFONT",
    };
    static const char *const footer_paths[] = {
        "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_24.cpfont",
        "/sdcard/fonts/LXGWWenKai_24.cpfont",
        "/sdcard/FONTS/LXGWWENKAI_24.CPFONT",
    };

    if (fonts == NULL) {
        return;
    }
    memset(fonts, 0, sizeof(*fonts));

    esp_err_t ret = mount_tf_card_for_fonts(fonts);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TF mount failed for ui fonts: %s; Chinese UI falls back to ASCII font", esp_err_to_name(ret));
        return;
    }

    if (!load_first_font(&fonts->menu, menu_paths, sizeof(menu_paths) / sizeof(menu_paths[0]))) {
        ESP_LOGW(TAG, "LXGWWenKai_24.cpfont not found; copy it to /sdcard/fonts/LXGWWenKai_24.cpfont for Chinese UI");
    }
    if (!load_first_font(&fonts->footer, footer_paths, sizeof(footer_paths) / sizeof(footer_paths[0]))) {
        ESP_LOGW(TAG, "footer uses LXGWWenKai_24.cpfont too; Chinese UI stays ASCII until font is present");
    }
}

static esp_err_t display_refresh_wifi_selection(tilt_app_context_t *ctx, int old_index, int new_index)
{
    if (ctx == NULL || ctx->framebuffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int first_before = 0;
    int first_after = 0;
    xSemaphoreTake(ctx->lock, portMAX_DELAY);
    if (ctx->wifi_setup.mode != WIFI_SETUP_UI_LIST) {
        xSemaphoreGive(ctx->lock);
        return ESP_ERR_INVALID_STATE;
    }
    first_before = wifi_list_visible_first(&ctx->wifi_setup);
    draw_wifi_list_row(ctx->framebuffer, &ctx->wifi_setup, &ctx->fonts, old_index);
    draw_wifi_list_row(ctx->framebuffer, &ctx->wifi_setup, &ctx->fonts, new_index);
    first_after = wifi_list_visible_first(&ctx->wifi_setup);
    xSemaphoreGive(ctx->lock);

    if (first_before != first_after) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_ui_region_t area = {
        .x = EPD_GDEY0426T82_WIDTH,
        .y = EPD_GDEY0426T82_HEIGHT,
        .w = -EPD_GDEY0426T82_WIDTH,
        .h = -EPD_GDEY0426T82_HEIGHT,
    };
    wifi_ui_region_t row_region = {0};

    xSemaphoreTake(ctx->lock, portMAX_DELAY);
    if (ctx->wifi_setup.mode != WIFI_SETUP_UI_LIST) {
        xSemaphoreGive(ctx->lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (ink_wifi_setup_ui_wifi_list_row_region(&ctx->wifi_setup, old_index, &row_region)) {
        ink_wifi_setup_ui_expand_region(&area, &row_region);
    }
    if (ink_wifi_setup_ui_wifi_list_row_region(&ctx->wifi_setup, new_index, &row_region)) {
        ink_wifi_setup_ui_expand_region(&area, &row_region);
    }
    xSemaphoreGive(ctx->lock);

    if (!ink_wifi_setup_ui_region_is_valid(&area)) {
        return ESP_ERR_INVALID_STATE;
    }
    ink_wifi_setup_ui_pad_align_region(&area, 4);

    return epd_gdey0426t82_partial_refresh_area(
        ctx->framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        (uint16_t)area.x,
        (uint16_t)area.y,
        (uint16_t)area.w,
        (uint16_t)area.h);
}

static esp_err_t display_refresh_wifi_list_body(tilt_app_context_t *ctx)
{
    if (ctx == NULL || ctx->framebuffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(ctx->lock, portMAX_DELAY);
    if (ctx->wifi_setup.mode != WIFI_SETUP_UI_LIST) {
        xSemaphoreGive(ctx->lock);
        return ESP_ERR_INVALID_STATE;
    }
    draw_wifi_list(ctx->framebuffer, &ctx->wifi_setup, &ctx->fonts);
    xSemaphoreGive(ctx->lock);

    wifi_ui_region_t area;
    ink_wifi_setup_ui_list_body_region(&area);
    ink_wifi_setup_ui_pad_align_region(&area, 4);

    return epd_gdey0426t82_partial_refresh_area(
        ctx->framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        (uint16_t)area.x,
        (uint16_t)area.y,
        (uint16_t)area.w,
        (uint16_t)area.h);
}

static esp_err_t display_refresh_screen_partial(tilt_app_context_t *ctx, const display_request_t *request)
{
    if (ctx == NULL || ctx->framebuffer == NULL || request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(ctx->lock, portMAX_DELAY);
    draw_wifi_setup_screen(ctx->framebuffer, ctx->keyboard_layer, &ctx->input, &ctx->keyboard_text, &ctx->wifi_setup, &ctx->fonts);
    xSemaphoreGive(ctx->lock);

    wifi_ui_region_t area = {
        .x = request->area_x,
        .y = request->area_y,
        .w = request->area_w,
        .h = request->area_h,
    };
    if (!ink_wifi_setup_ui_region_is_valid(&area)) {
        return ESP_ERR_INVALID_ARG;
    }
    ink_wifi_setup_ui_pad_align_region(&area, 4);

    return epd_gdey0426t82_partial_refresh_area(
        ctx->framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        (uint16_t)area.x,
        (uint16_t)area.y,
        (uint16_t)area.w,
        (uint16_t)area.h);
}

static esp_err_t display_refresh_keyboard_selection(
    tilt_app_context_t *ctx,
    int old_col,
    int old_row,
    int new_col,
    int new_row)
{
    if (ctx == NULL || ctx->framebuffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_ui_region_t area = {
        .x = EPD_GDEY0426T82_WIDTH,
        .y = EPD_GDEY0426T82_HEIGHT,
        .w = -EPD_GDEY0426T82_WIDTH,
        .h = -EPD_GDEY0426T82_HEIGHT,
    };
    wifi_ui_region_t key_region = {0};

    xSemaphoreTake(ctx->lock, portMAX_DELAY);
    if (ctx->wifi_setup.mode != WIFI_SETUP_UI_PASSWORD) {
        xSemaphoreGive(ctx->lock);
        return ESP_ERR_INVALID_STATE;
    }
    draw_keyboard_key(ctx->framebuffer, ctx->keyboard_layer, old_col, old_row, false);
    draw_keyboard_key(ctx->framebuffer, ctx->keyboard_layer, new_col, new_row, true);
    if (ink_wifi_setup_ui_keyboard_key_region(old_col, old_row, &key_region)) {
        ink_wifi_setup_ui_expand_region(&area, &key_region);
    }
    if (ink_wifi_setup_ui_keyboard_key_region(new_col, new_row, &key_region)) {
        ink_wifi_setup_ui_expand_region(&area, &key_region);
    }
    xSemaphoreGive(ctx->lock);

    if (!ink_wifi_setup_ui_region_is_valid(&area)) {
        return ESP_ERR_INVALID_STATE;
    }
    ink_wifi_setup_ui_pad_align_region(&area, 6);

    return epd_gdey0426t82_partial_refresh_area(
        ctx->framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        (uint16_t)area.x,
        (uint16_t)area.y,
        (uint16_t)area.w,
        (uint16_t)area.h);
}

static void display_task(void *arg)
{
    tilt_app_context_t *ctx = (tilt_app_context_t *)arg;
    display_request_t request;

    for (;;) {
        if (xQueueReceive(ctx->display_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_err_t ret = ESP_OK;
        ctx->display_busy = true;
        if (request.type == DISPLAY_REQUEST_SCREEN_PARTIAL) {
            ret = display_refresh_screen_partial(ctx, &request);
            if (ret == ESP_OK) {
                ctx->display_busy = false;
                continue;
            }
            if (!tilt_display_partial_error_should_full_refresh(ret)) {
                ESP_LOGD(TAG, "screen partial refresh dropped: %s", esp_err_to_name(ret));
                ctx->display_busy = false;
                continue;
            }
            ESP_LOGW(TAG, "screen partial refresh fallback: %s", esp_err_to_name(ret));
        } else if (request.type == DISPLAY_REQUEST_WIFI_LIST_BODY) {
            ret = display_refresh_wifi_list_body(ctx);
            if (ret == ESP_OK) {
                ctx->display_busy = false;
                continue;
            }
            if (!tilt_display_partial_error_should_full_refresh(ret)) {
                ESP_LOGD(TAG, "wifi list body partial refresh dropped: %s", esp_err_to_name(ret));
                ctx->display_busy = false;
                continue;
            }
            ESP_LOGW(TAG, "wifi list body partial refresh fallback: %s", esp_err_to_name(ret));
        } else if (request.type == DISPLAY_REQUEST_WIFI_LIST_SELECTION) {
            ret = display_refresh_wifi_selection(ctx, request.old_index, request.new_index);
            if (ret == ESP_OK) {
                ctx->display_busy = false;
                continue;
            }
            if (!tilt_display_partial_error_should_full_refresh(ret)) {
                ESP_LOGD(TAG, "wifi selection partial refresh dropped: %s", esp_err_to_name(ret));
                ctx->display_busy = false;
                continue;
            }
            ESP_LOGW(TAG, "wifi selection partial refresh fallback: %s", esp_err_to_name(ret));
        } else if (request.type == DISPLAY_REQUEST_KEYBOARD_SELECTION) {
            ret = display_refresh_keyboard_selection(ctx, request.old_col, request.old_row, request.new_col, request.new_row);
            if (ret == ESP_OK) {
                ctx->display_busy = false;
                continue;
            }
            if (!tilt_display_partial_error_should_full_refresh(ret)) {
                ESP_LOGD(TAG, "keyboard selection partial refresh dropped: %s", esp_err_to_name(ret));
                ctx->display_busy = false;
                continue;
            }
            ESP_LOGW(TAG, "keyboard selection partial refresh fallback: %s", esp_err_to_name(ret));
        }

        xSemaphoreTake(ctx->lock, portMAX_DELAY);
        draw_wifi_setup_screen(ctx->framebuffer, ctx->keyboard_layer, &ctx->input, &ctx->keyboard_text, &ctx->wifi_setup, &ctx->fonts);
        xSemaphoreGive(ctx->lock);
        ret = epd_gdey0426t82_full_refresh(ctx->framebuffer, EPD_GDEY0426T82_BUFFER_SIZE);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "display refresh failed: %s", esp_err_to_name(ret));
        }
        ctx->display_busy = false;
    }
}

static void wifi_task(void *arg)
{
    tilt_app_context_t *ctx = (tilt_app_context_t *)arg;
    wifi_work_request_t request;

    for (;;) {
        if (xQueueReceive(ctx->wifi_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (request.type == WIFI_WORK_SCAN) {
            xSemaphoreTake(ctx->lock, portMAX_DELAY);
            ctx->wifi_setup.scan_in_progress = true;
            xSemaphoreGive(ctx->lock);
            post_display_request(ctx, DISPLAY_REQUEST_WIFI_LIST_BODY, -1, -1, -1, -1);

            ink_wifi_scan_list_t scan = {0};
            esp_err_t ret = ink_wifi_manager_scan(&scan);

            xSemaphoreTake(ctx->lock, portMAX_DELAY);
            ctx->wifi_setup.scan_in_progress = false;
            if (ret == ESP_OK) {
                ctx->wifi_setup.scan = scan;
                tilt_wifi_setup_sort_scan(&ctx->wifi_setup);
            } else {
                ctx->wifi_setup.selected_index = (int)ctx->wifi_setup.scan.count;
            }
            xSemaphoreGive(ctx->lock);

            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "wifi scan failed: %s", esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, "wifi scan updated");
            }
            post_display_request(ctx, DISPLAY_REQUEST_WIFI_LIST_BODY, -1, -1, -1, -1);
        } else if (request.type == WIFI_WORK_CONNECT_PASSWORD) {
            ESP_LOGI(TAG, "wifi connect worker ssid=%s password_len=%u", request.ssid, (unsigned)strlen(request.password));
            ink_wifi_status_t status = {0};
            esp_err_t ret = ink_wifi_manager_connect_password(request.ssid, request.password, 10000, &status);
            if (ret == ESP_OK) {
                ret = ink_wifi_manager_save_credential(request.ssid, request.password);
                if (ret != ESP_OK) {
                    status.connected = false;
                    status.last_error = ret;
                }
            }

            xSemaphoreTake(ctx->lock, portMAX_DELAY);
            ctx->wifi_setup.status = status;
            tilt_wifi_setup_finish_connecting(&ctx->wifi_setup, ret);
            for (uint16_t i = 0; i < ctx->wifi_setup.scan.count; ++i) {
                if (strcmp(ctx->wifi_setup.scan.results[i].ssid, request.ssid) == 0) {
                    ctx->wifi_setup.scan.results[i].saved = ret == ESP_OK;
                }
            }
            tilt_wifi_setup_sort_scan(&ctx->wifi_setup);
            xSemaphoreGive(ctx->lock);

            post_display_request(ctx, DISPLAY_REQUEST_FULL, -1, -1, -1, -1);
        } else if (request.type == WIFI_WORK_CONNECT_SAVED) {
            ESP_LOGI(TAG, "wifi connect saved worker ssid=%s", request.ssid);
            ink_wifi_status_t status = {0};
            esp_err_t ret = ink_wifi_manager_connect_saved(request.ssid, 10000, &status);

            xSemaphoreTake(ctx->lock, portMAX_DELAY);
            ctx->wifi_setup.status = status;
            tilt_wifi_setup_finish_connecting(&ctx->wifi_setup, ret);
            tilt_wifi_setup_sort_scan(&ctx->wifi_setup);
            xSemaphoreGive(ctx->lock);

            post_display_request(ctx, DISPLAY_REQUEST_FULL, -1, -1, -1, -1);
        } else if (request.type == WIFI_WORK_DELETE_SAVED) {
            ESP_LOGI(TAG, "wifi delete saved worker ssid=%s", request.ssid);
            esp_err_t ret = ink_wifi_manager_delete_credential(request.ssid);

            xSemaphoreTake(ctx->lock, portMAX_DELAY);
            for (uint16_t i = 0; i < ctx->wifi_setup.scan.count; ++i) {
                if (strcmp(ctx->wifi_setup.scan.results[i].ssid, request.ssid) == 0) {
                    ctx->wifi_setup.scan.results[i].saved = false;
                }
            }
            tilt_wifi_setup_sort_scan(&ctx->wifi_setup);
            xSemaphoreGive(ctx->lock);

            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "wifi delete saved failed: %s", esp_err_to_name(ret));
            }
            post_display_request(ctx, DISPLAY_REQUEST_WIFI_LIST_BODY, -1, -1, -1, -1);
        }
    }
}

static void input_task(void *arg)
{
    tilt_app_context_t *ctx = (tilt_app_context_t *)arg;

    for (;;) {
        accel_sample_t sample = {0};
        esp_err_t ret = mpu6050_read_accel(&ctx->mpu, &sample);
        if (ret == ESP_OK) {
            if (display_can_accept_input(ctx)) {
                xSemaphoreTake(ctx->lock, portMAX_DELAY);
                const int calibrated_x = (int)sample.x - ctx->mpu.neutral_x;
                const int calibrated_y = (int)sample.y - ctx->mpu.neutral_y;
                const int old_col = ctx->input.column;
                const int prev_row = ctx->input.row;
                const tilt_grid_direction_t move = tilt_grid_input_update(&ctx->input, calibrated_x, calibrated_y);
                if (move != TILT_GRID_DIRECTION_NONE && ctx->wifi_setup.mode == WIFI_SETUP_UI_PASSWORD) {
                    keyboard_normalize_cursor(&ctx->input, move, prev_row);
                    const int new_col = ctx->input.column;
                    const int new_row = ctx->input.row;
                    if (old_col != new_col || prev_row != new_row) {
                        post_keyboard_selection_request(ctx, old_col, prev_row, new_col, new_row);
                    }
                    ESP_LOGI(TAG, "keyboard tilt move=%s cell=(%d,%d)", tilt_grid_direction_name(move), ctx->input.column, ctx->input.row);
                }
                xSemaphoreGive(ctx->lock);
            }
        } else {
            ESP_LOGW(TAG, "GY6500 accel read failed: %s", esp_err_to_name(ret));
        }

        uint32_t pressed = 0;
        uint32_t long_pressed = 0;
        local_buttons_poll(&ctx->buttons, (uint32_t)(esp_timer_get_time() / 1000), &pressed, &long_pressed);

        if (pressed != 0 || long_pressed != 0) {
            bool full_refresh = false;
            bool selection_refresh = false;
            bool list_body_refresh = false;
            bool partial_refresh = false;
            bool flush_display_queue = false;
            wifi_ui_region_t partial_region;
            ink_wifi_setup_ui_full_screen_region(&partial_region);
            int old_selection = -1;
            int new_selection = -1;
            wifi_work_request_t wifi_request = {0};
            bool post_wifi = false;

            xSemaphoreTake(ctx->lock, portMAX_DELAY);
            const wifi_setup_ui_mode_t mode_at_press = ctx->wifi_setup.mode;
            if (mode_at_press == WIFI_SETUP_UI_RESULT && (pressed & LOCAL_BUTTON_CONFIRM) != 0) {
                tilt_wifi_setup_confirm_result(&ctx->wifi_setup);
                full_refresh = true;
                flush_display_queue = true;
            } else if (mode_at_press == WIFI_SETUP_UI_LIST && (pressed & LOCAL_BUTTON_CONFIRM) != 0) {
                if (tilt_wifi_setup_is_scan_selected(&ctx->wifi_setup)) {
                    wifi_request.type = WIFI_WORK_SCAN;
                    post_wifi = true;
                } else {
                    const ink_wifi_scan_result_t *ap = tilt_wifi_setup_selected_ap(&ctx->wifi_setup);
                    if (ap != NULL && ap->saved) {
                        tilt_wifi_setup_open_saved_menu(&ctx->wifi_setup);
                        partial_refresh = true;
                        ink_wifi_setup_ui_saved_menu_region(&partial_region);
                        flush_display_queue = true;
                    } else if (ap != NULL) {
                        tilt_wifi_setup_open_password(&ctx->wifi_setup);
                        keyboard_text_clear(&ctx->keyboard_text);
                        tilt_grid_input_init(&ctx->input);
                        ctx->keyboard_layer = KEYBOARD_LAYER_LOWER;
                        partial_refresh = true;
                        ink_wifi_setup_ui_password_screen_region(&partial_region);
                        flush_display_queue = true;
                        ESP_LOGI(TAG, "wifi password popup open index=%d", ctx->wifi_setup.selected_index);
                    }
                }
            } else if (mode_at_press == WIFI_SETUP_UI_PASSWORD && (pressed & LOCAL_BUTTON_LOWER) != 0) {
                ctx->keyboard_layer = KEYBOARD_LAYER_LOWER;
                partial_refresh = true;
                ink_wifi_setup_ui_keyboard_footer_region(&partial_region);
            } else if (mode_at_press == WIFI_SETUP_UI_SAVED_MENU
                && ((pressed & LOCAL_BUTTON_LOWER) != 0 || (pressed & LOCAL_BUTTON_UPPER) != 0)) {
                ctx->wifi_setup.menu_index = ctx->wifi_setup.menu_index == 0 ? 1 : 0;
                partial_refresh = true;
                ink_wifi_setup_ui_saved_menu_region(&partial_region);
            }

            if (mode_at_press == WIFI_SETUP_UI_PASSWORD && (pressed & LOCAL_BUTTON_UPPER) != 0) {
                ctx->keyboard_layer = KEYBOARD_LAYER_UPPER;
                partial_refresh = true;
                ink_wifi_setup_ui_keyboard_footer_region(&partial_region);
            }
            if ((pressed & LOCAL_BUTTON_BACK) != 0) {
                if (mode_at_press == WIFI_SETUP_UI_PASSWORD || mode_at_press == WIFI_SETUP_UI_SAVED_MENU) {
                    tilt_wifi_setup_cancel_popup(&ctx->wifi_setup);
                    list_body_refresh = true;
                    flush_display_queue = true;
                } else if (mode_at_press == WIFI_SETUP_UI_RESULT) {
                    tilt_wifi_setup_confirm_result(&ctx->wifi_setup);
                    full_refresh = true;
                    flush_display_queue = true;
                }
            }
            if (mode_at_press == WIFI_SETUP_UI_LIST && (pressed & LOCAL_BUTTON_LOWER) != 0) {
                const int old_first = wifi_list_visible_first(&ctx->wifi_setup);
                old_selection = ctx->wifi_setup.selected_index;
                tilt_wifi_setup_cycle_selection(&ctx->wifi_setup, -1);
                new_selection = ctx->wifi_setup.selected_index;
                const int new_first = wifi_list_visible_first(&ctx->wifi_setup);
                selection_refresh = old_selection != new_selection;
                if (selection_refresh && old_first != new_first) {
                    list_body_refresh = true;
                    selection_refresh = false;
                }
            }
            if (mode_at_press == WIFI_SETUP_UI_LIST && (pressed & LOCAL_BUTTON_UPPER) != 0) {
                const int old_first = wifi_list_visible_first(&ctx->wifi_setup);
                old_selection = ctx->wifi_setup.selected_index;
                tilt_wifi_setup_cycle_selection(&ctx->wifi_setup, 1);
                new_selection = ctx->wifi_setup.selected_index;
                const int new_first = wifi_list_visible_first(&ctx->wifi_setup);
                selection_refresh = old_selection != new_selection;
                if (selection_refresh && old_first != new_first) {
                    list_body_refresh = true;
                    selection_refresh = false;
                }
            }
            if (mode_at_press == WIFI_SETUP_UI_PASSWORD && (pressed & LOCAL_BUTTON_CONFIRM) != 0) {
                const char *label = keyboard_label(ctx->keyboard_layer, ctx->input.column, ctx->input.row);
                if (strcmp(label, "ok") == 0) {
                    if (wifi_prepare_selected_connect_request(&ctx->keyboard_text, &ctx->wifi_setup, &wifi_request)) {
                        tilt_wifi_setup_begin_connecting(&ctx->wifi_setup);
                        post_wifi = true;
                    } else {
                        tilt_wifi_setup_finish_connecting(&ctx->wifi_setup, ESP_ERR_INVALID_STATE);
                    }
                    full_refresh = true;
                } else {
                    if (keyboard_activate_label(&ctx->keyboard_text, &ctx->keyboard_layer, label)) {
                        if (strcmp(label, "abc") == 0 || strcmp(label, "ABC") == 0 || strcmp(label, "sym") == 0) {
                            partial_refresh = true;
                            ink_wifi_setup_ui_keyboard_footer_region(&partial_region);
                        } else {
                            partial_refresh = true;
                            ink_wifi_setup_ui_password_box_region(&partial_region);
                        }
                    }
                }
            }
            if (mode_at_press == WIFI_SETUP_UI_SAVED_MENU && (pressed & LOCAL_BUTTON_CONFIRM) != 0) {
                if (ctx->wifi_setup.menu_index == 0) {
                    if (wifi_prepare_selected_saved_request(&ctx->wifi_setup, WIFI_WORK_CONNECT_SAVED, &wifi_request)) {
                        tilt_wifi_setup_begin_connecting(&ctx->wifi_setup);
                        post_wifi = true;
                    } else {
                        tilt_wifi_setup_finish_connecting(&ctx->wifi_setup, ESP_ERR_INVALID_STATE);
                    }
                } else {
                    if (wifi_prepare_selected_saved_request(&ctx->wifi_setup, WIFI_WORK_DELETE_SAVED, &wifi_request)) {
                        post_wifi = true;
                    }
                    tilt_wifi_setup_cancel_popup(&ctx->wifi_setup);
                    list_body_refresh = true;
                }
                if (ctx->wifi_setup.mode == WIFI_SETUP_UI_CONNECTING || ctx->wifi_setup.mode == WIFI_SETUP_UI_RESULT) {
                    full_refresh = true;
                }
                flush_display_queue = true;
            }
            xSemaphoreGive(ctx->lock);

            if (flush_display_queue) {
                clear_display_requests(ctx);
                ESP_LOGI(TAG, "display queue flushed for mode transition");
            }
            if (post_wifi) {
                post_wifi_work(ctx, &wifi_request);
            }
            if (full_refresh) {
                post_display_request(ctx, DISPLAY_REQUEST_FULL, -1, -1, -1, -1);
            } else if (partial_refresh) {
                post_screen_partial_request(
                    ctx,
                    partial_region.x,
                    partial_region.y,
                    partial_region.w,
                    partial_region.h);
            } else if (list_body_refresh) {
                post_display_request(ctx, DISPLAY_REQUEST_WIFI_LIST_BODY, -1, -1, -1, -1);
            } else if (selection_refresh) {
                post_display_request(ctx, DISPLAY_REQUEST_WIFI_LIST_SELECTION, old_selection, new_selection, -1, -1);
            }
            if ((long_pressed & LOCAL_BUTTON_BACK) != 0) {
                ESP_LOGI(TAG, "keyboard back long press; reserved for exiting keyboard");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

void app_main(void)
{
    static const epd_gdey0426t82_config_t panel = {
        .gpio_mosi = 4,
        .gpio_sclk = 5,
        .gpio_cs = 6,
        .gpio_dc = 7,
        .gpio_rst = 15,
        .gpio_busy = 16,
        .spi_host = SPI2_HOST,
        .spi_clock_hz = 20 * 1000 * 1000,
    };

    ESP_LOGI(TAG, "ink-reader tilt keyboard test start");
    ESP_LOGI(TAG, "Hold the board level during GY6500 calibration");
    memset(&s_app, 0, sizeof(s_app));
    s_app.wifi_setup.selected_index = 0;
    s_app.wifi_setup.mode = WIFI_SETUP_UI_LIST;
    s_app.keyboard_layer = KEYBOARD_LAYER_LOWER;

    s_app.framebuffer = alloc_display_buffer();
    ESP_ERROR_CHECK(s_app.framebuffer != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    s_app.lock = xSemaphoreCreateMutex();
    s_app.display_queue = xQueueCreate(DISPLAY_QUEUE_LEN, sizeof(display_request_t));
    s_app.wifi_queue = xQueueCreate(WIFI_QUEUE_LEN, sizeof(wifi_work_request_t));
    ESP_ERROR_CHECK(s_app.lock != NULL && s_app.display_queue != NULL && s_app.wifi_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(epd_gdey0426t82_init(&panel));
    ESP_ERROR_CHECK(local_buttons_init());
    ui_fonts_init(&s_app.fonts);
    ESP_ERROR_CHECK(ink_wifi_manager_init());
    while (mpu6050_init(&s_app.mpu) != ESP_OK) {
        ESP_LOGW(TAG, "GY6500 unavailable; check VIN, GND, SCL=GPIO1, SDA=GPIO2; retrying");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    while (mpu6050_calibrate_neutral(&s_app.mpu) != ESP_OK) {
        ESP_LOGW(TAG, "GY6500 calibration failed; retrying");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    tilt_grid_input_init(&s_app.input);

    ESP_ERROR_CHECK(xTaskCreate(display_task, "tilt_display", DISPLAY_TASK_STACK_SIZE, &s_app, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(wifi_task, "tilt_wifi", WIFI_TASK_STACK_SIZE, &s_app, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(input_task, "tilt_input", INPUT_TASK_STACK_SIZE, &s_app, 6, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    const wifi_work_request_t scan = {
        .type = WIFI_WORK_SCAN,
    };
    post_display_request(&s_app, DISPLAY_REQUEST_FULL, -1, -1, -1, -1);
    post_wifi_work(&s_app, &scan);
}
