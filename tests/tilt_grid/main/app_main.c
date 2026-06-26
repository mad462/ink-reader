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
    KEYBOARD_COLS = 12,
    KEYBOARD_ROWS = 5,
    KEY_H = 44,
    KEY_GAP_X = 4,
    KEY_GAP_Y = 8,
    KEY_X0 = 16,
    KEY_Y0 = 520,
    KEY_TEXT_SCALE = 3,
    KEY_SMALL_TEXT_SCALE = 2,
    KEY_SELECTION_BAR_H = 6,
    KEYBOARD_W = EPD_GDEY0426T82_WIDTH - 2 * KEY_X0,
    KEYBOARD_H = KEYBOARD_ROWS * KEY_H + (KEYBOARD_ROWS - 1) * KEY_GAP_Y,
    PASSWORD_TEXT_MAX = 64,
    SAMPLE_PERIOD_MS = 50,
    CALIBRATION_SAMPLES = 64,
    WIFI_LIST_TOP_Y = 100,
    WIFI_LIST_ROW_H = 58,
    WIFI_LIST_BOTTOM_Y = EPD_GDEY0426T82_HEIGHT - 8,
    WIFI_LIST_CARD_X = 18,
    WIFI_LIST_CARD_W = EPD_GDEY0426T82_WIDTH - 36,
    WIFI_LIST_CARD_INSET = 14,
    WIFI_SAVED_MENU_X = 48,
    WIFI_SAVED_MENU_Y = 210,
    WIFI_SAVED_MENU_W = EPD_GDEY0426T82_WIDTH - 96,
    WIFI_SAVED_MENU_H = 220,
    PASSWORD_BOX_X = 24,
    PASSWORD_BOX_Y = 136,
    PASSWORD_BOX_W = EPD_GDEY0426T82_WIDTH - 48,
    PASSWORD_BOX_H = 72,
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
    char c;
    uint8_t rows[7];
} glyph5x7_t;

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

static const char *const s_keyboard_layer_names[KEYBOARD_LAYER_COUNT] = {
    [KEYBOARD_LAYER_LOWER] = "LOWER",
    [KEYBOARD_LAYER_UPPER] = "UPPER",
    [KEYBOARD_LAYER_SYMBOL] = "SYMBOL",
};

static const glyph5x7_t s_font5x7[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'"', {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00}},
    {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
    {'$', {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}},
    {'%', {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}},
    {'&', {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}},
    {'\'', {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'*', {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
    {'6', {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {';', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'@', {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x12, 0x11, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    {'\\', {0x10, 0x08, 0x08, 0x04, 0x02, 0x01, 0x01}},
    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'`', {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}},
    {'a', {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}},
    {'b', {0x10, 0x10, 0x16, 0x19, 0x11, 0x19, 0x16}},
    {'c', {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E}},
    {'d', {0x01, 0x01, 0x0D, 0x13, 0x11, 0x13, 0x0D}},
    {'e', {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E}},
    {'f', {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08}},
    {'g', {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E}},
    {'h', {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11}},
    {'i', {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}},
    {'j', {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}},
    {'k', {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}},
    {'l', {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'m', {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15}},
    {'n', {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}},
    {'o', {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}},
    {'p', {0x00, 0x00, 0x16, 0x19, 0x16, 0x10, 0x10}},
    {'q', {0x00, 0x00, 0x0D, 0x13, 0x0D, 0x01, 0x01}},
    {'r', {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}},
    {'s', {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E}},
    {'t', {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06}},
    {'u', {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}},
    {'v', {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'w', {0x00, 0x00, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'x', {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}},
    {'y', {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E}},
    {'z', {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}},
    {'{', {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02}},
    {'|', {0x04, 0x04, 0x04, 0x00, 0x04, 0x04, 0x04}},
    {'}', {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08}},
};

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

static void set_pixel(uint8_t *buffer, int x, int y, bool black)
{
    if (buffer == NULL || x < 0 || x >= EPD_GDEY0426T82_WIDTH || y < 0 || y >= EPD_GDEY0426T82_HEIGHT) {
        return;
    }

    const size_t index = (size_t)y * (EPD_GDEY0426T82_WIDTH / 8) + (size_t)(x / 8);
    const uint8_t mask = (uint8_t)(0x80 >> (x % 8));
    if (black) {
        buffer[index] &= (uint8_t)~mask;
    } else {
        buffer[index] |= mask;
    }
}

static void fill_rect(uint8_t *buffer, int x, int y, int w, int h, bool black)
{
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            set_pixel(buffer, xx, yy, black);
        }
    }
}

static void draw_rect_outline(uint8_t *buffer, int x, int y, int w, int h, int thickness)
{
    fill_rect(buffer, x, y, w, thickness, true);
    fill_rect(buffer, x, y + h - thickness, w, thickness, true);
    fill_rect(buffer, x, y, thickness, h, true);
    fill_rect(buffer, x + w - thickness, y, thickness, h, true);
}

static const glyph5x7_t *find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(s_font5x7) / sizeof(s_font5x7[0]); ++i) {
        if (s_font5x7[i].c == c) {
            return &s_font5x7[i];
        }
    }
    return &s_font5x7[0];
}

static int measure_text_width(const char *text, int scale)
{
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    return (int)strlen(text) * 6 * scale - scale;
}

static void draw_glyph(uint8_t *buffer, int x, int y, char c, int scale, bool black)
{
    const glyph5x7_t *glyph = find_glyph(c);

    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if ((glyph->rows[row] & (uint8_t)(1U << (4 - col))) != 0) {
                fill_rect(buffer, x + col * scale, y + row * scale, scale, scale, black);
            }
        }
    }
}

static void draw_text(uint8_t *buffer, int x, int y, const char *text, int scale, bool black)
{
    if (text == NULL) {
        return;
    }

    int cursor_x = x;
    for (const char *p = text; *p != '\0'; ++p) {
        draw_glyph(buffer, cursor_x, y, *p, scale, black);
        cursor_x += 6 * scale;
    }
}

static void draw_ui_text(
    uint8_t *buffer,
    ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor,
    bool black)
{
    if (ink_cpfont_is_loaded(font)) {
        esp_err_t ret;
        if (black) {
            ret = ink_cpfont_draw_text_bw_scaled(font, buffer, x, y, text, font_scale_divisor, NULL);
        } else {
            ret = ink_cpfont_draw_text_bw_scaled_inverted(font, buffer, x, y, text, font_scale_divisor, NULL);
        }
        if (ret == ESP_OK) {
            return;
        }
    }
    draw_text(buffer, x, y, text, fallback_scale, black);
}

static void draw_ui_text_inverted(
    uint8_t *buffer,
    ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    int fallback_scale,
    uint8_t font_scale_divisor)
{
    draw_ui_text(buffer, font, x, y, text, fallback_scale, font_scale_divisor, false);
}

static void copy_ascii_clipped(char *dst, size_t dst_size, const char *src, size_t max_chars)
{
    size_t written = 0;

    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (src[written] != '\0' && written + 1 < dst_size && written < max_chars) {
        const unsigned char c = (unsigned char)src[written];
        dst[written] = c >= 0x20 && c < 0x7f ? (char)c : '?';
        ++written;
    }
    dst[written] = '\0';
}

static const char *keyboard_label(keyboard_layer_t layer, int column, int row)
{
    return ink_wifi_setup_keyboard_label(layer, column, row);
}

static void keyboard_normalize_cursor(tilt_grid_input_t *state, tilt_grid_direction_t direction, int old_row)
{
    if (state == NULL) {
        return;
    }
    ink_wifi_setup_normalize_selection(&state->column, &state->row, direction, old_row);
}

static int keyboard_row_key_width(int row)
{
    const int key_count = ink_wifi_setup_keyboard_row_key_count(row);
    return (KEYBOARD_W - (key_count - 1) * KEY_GAP_X) / key_count;
}

static int keyboard_row_extra_width(int row)
{
    const int key_count = ink_wifi_setup_keyboard_row_key_count(row);
    return (KEYBOARD_W - (key_count - 1) * KEY_GAP_X) % key_count;
}

static void keyboard_text_append(keyboard_text_t *text, const char *value)
{
    ink_wifi_setup_keyboard_text_append(text, value);
}

static void keyboard_text_backspace(keyboard_text_t *text)
{
    ink_wifi_setup_keyboard_text_backspace(text);
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

static void expand_area(
    int *area_x,
    int *area_y,
    int *area_right,
    int *area_bottom,
    int x,
    int y,
    int right,
    int bottom)
{
    if (area_x != NULL) {
        *area_x = x < *area_x ? x : *area_x;
    }
    if (area_y != NULL) {
        *area_y = y < *area_y ? y : *area_y;
    }
    if (area_right != NULL) {
        *area_right = right > *area_right ? right : *area_right;
    }
    if (area_bottom != NULL) {
        *area_bottom = bottom > *area_bottom ? bottom : *area_bottom;
    }
}

static void pad_and_align_refresh_area(int *x, int *y, int *right, int *bottom, int pad)
{
    if (x == NULL || y == NULL || right == NULL || bottom == NULL) {
        return;
    }

    *x = *x - pad;
    *y = *y - pad;
    *right = *right + pad;
    *bottom = *bottom + pad;

    if (*x < 0) {
        *x = 0;
    }
    if (*y < 0) {
        *y = 0;
    }
    if (*right > EPD_GDEY0426T82_WIDTH) {
        *right = EPD_GDEY0426T82_WIDTH;
    }
    if (*bottom > EPD_GDEY0426T82_HEIGHT) {
        *bottom = EPD_GDEY0426T82_HEIGHT;
    }

    *x &= ~7;
    *y &= ~7;
    *right = (*right + 7) & ~7;
    *bottom = (*bottom + 7) & ~7;
    if (*right > EPD_GDEY0426T82_WIDTH) {
        *right = EPD_GDEY0426T82_WIDTH;
    }
    if (*bottom > EPD_GDEY0426T82_HEIGHT) {
        *bottom = EPD_GDEY0426T82_HEIGHT;
    }
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

static void draw_centered_text(uint8_t *buffer, int x, int y, int w, int h, const char *text, int scale, bool black)
{
    const int text_w = measure_text_width(text, scale);
    const int text_h = 7 * scale;
    const int text_x = x + (w - text_w) / 2;
    const int text_y = y + (h - text_h) / 2;

    draw_text(buffer, text_x, text_y, text, scale, black);
}

static void draw_keyboard_key(uint8_t *buffer, keyboard_layer_t layer, int column, int row, bool selected)
{
    int x = KEY_X0;
    const int base_w = keyboard_row_key_width(row);
    const int extra_w = keyboard_row_extra_width(row);
    const int w = base_w + (column < extra_w ? 1 : 0);
    const int y = KEY_Y0 + row * (KEY_H + KEY_GAP_Y);
    const char *label = keyboard_label(layer, column, row);
    const int scale = strlen(label) > 2 ? KEY_SMALL_TEXT_SCALE : KEY_TEXT_SCALE;

    for (int i = 0; i < column; ++i) {
        x += base_w + (i < extra_w ? 1 : 0) + KEY_GAP_X;
    }

    fill_rect(buffer, x, y, w, KEY_H, false);
    draw_rect_outline(buffer, x, y, w, KEY_H, 3);
    draw_centered_text(buffer, x, y, w, KEY_H, label, scale, true);
    if (selected) {
        fill_rect(buffer, x + 4, y + KEY_H - KEY_SELECTION_BAR_H - 4, w - 8, KEY_SELECTION_BAR_H, true);
    }
}

static bool keyboard_key_area(int column, int row, int *x, int *y, int *right, int *bottom)
{
    if (row < 0 || row >= KEYBOARD_ROWS || column < 0 || column >= ink_wifi_setup_keyboard_row_key_count(row)) {
        return false;
    }

    int key_x = KEY_X0;
    const int base_w = keyboard_row_key_width(row);
    const int extra_w = keyboard_row_extra_width(row);
    const int key_w = base_w + (column < extra_w ? 1 : 0);
    const int key_y = KEY_Y0 + row * (KEY_H + KEY_GAP_Y);

    for (int i = 0; i < column; ++i) {
        key_x += base_w + (i < extra_w ? 1 : 0) + KEY_GAP_X;
    }

    if (x != NULL) {
        *x = key_x;
    }
    if (y != NULL) {
        *y = key_y;
    }
    if (right != NULL) {
        *right = key_x + key_w;
    }
    if (bottom != NULL) {
        *bottom = key_y + KEY_H;
    }
    return true;
}

static void draw_keyboard_status(
    uint8_t *buffer,
    keyboard_layer_t layer,
    const tilt_grid_input_t *state,
    const keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    ui_fonts_t *fonts)
{
    char preview[96];
    char wifi_line[96];
    char wifi_status[96];
    char ssid[24];
    const char *label = state != NULL ? keyboard_label(layer, state->column, state->row) : "";
    const char *value = text != NULL ? text->text : "";
    const ink_wifi_scan_result_t *ap = tilt_wifi_setup_selected_ap(wifi);

    copy_ascii_clipped(preview, sizeof(preview), value, 24);
    (void)label;
    if (ap != NULL) {
        copy_ascii_clipped(ssid, sizeof(ssid), ap->ssid, 18);
        snprintf(
            wifi_line,
            sizeof(wifi_line),
            "AP %d/%u %s %ddBm%s",
            wifi->selected_index + 1,
            (unsigned)wifi->scan.count,
            ssid,
            (int)ap->rssi,
            ap->saved ? " SAVED" : "");
    } else {
        snprintf(wifi_line, sizeof(wifi_line), "NO WIFI SELECTED");
    }
    if (wifi != NULL && wifi->status.connected) {
        copy_ascii_clipped(ssid, sizeof(ssid), wifi->status.ssid, 18);
        snprintf(wifi_status, sizeof(wifi_status), "CONNECTED %s %ddBm", ssid, (int)wifi->status.rssi);
    } else if (wifi != NULL && wifi->status.last_error != ESP_OK) {
        snprintf(wifi_status, sizeof(wifi_status), "WIFI %s", esp_err_to_name(wifi->status.last_error));
    } else {
        snprintf(wifi_status, sizeof(wifi_status), "OFFLINE");
    }

    draw_ui_text(buffer, &fonts->menu, 24, 34, "WiFi Password", 2, 1, true);
    draw_ui_text(buffer, &fonts->footer, 24, 76, wifi_line, 1, 1, true);
    draw_ui_text(buffer, &fonts->footer, 24, 106, wifi_status, 1, 1, true);
    draw_rect_outline(buffer, PASSWORD_BOX_X, PASSWORD_BOX_Y, PASSWORD_BOX_W, PASSWORD_BOX_H, 3);
    if (preview[0] != '\0') {
        draw_text(buffer, 36, 156, preview, 3, true);
    }
    draw_ui_text(buffer, &fonts->footer, 24, 224, s_keyboard_layer_names[layer], 1, 1, true);
}

static void draw_keyboard(
    uint8_t *buffer,
    keyboard_layer_t layer,
    const tilt_grid_input_t *state,
    const keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    ui_fonts_t *fonts)
{
    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    draw_keyboard_status(buffer, layer, state, text, wifi, fonts);
    draw_rect_outline(buffer, KEY_X0 - 10, KEY_Y0 - 10, KEYBOARD_W + 20, KEYBOARD_H + 20, 3);

    for (int row = 0; row < KEYBOARD_ROWS; ++row) {
        for (int col = 0; col < ink_wifi_setup_keyboard_row_key_count(row); ++col) {
            const bool selected = state != NULL && row == state->row && col == state->column;
            draw_keyboard_key(buffer, layer, col, row, selected);
        }
    }
}

static int wifi_list_visible_first(const wifi_setup_state_t *wifi)
{
    const int count = tilt_wifi_setup_selectable_count(wifi);
    if (count <= 0) {
        return 0;
    }

    const int visible_capacity = (WIFI_LIST_BOTTOM_Y - WIFI_LIST_TOP_Y) / WIFI_LIST_ROW_H;
    const int max_visible = count < visible_capacity ? count : visible_capacity;
    int first = wifi->selected_index - max_visible / 2;
    if (first < 0) {
        first = 0;
    }
    if (first + max_visible > count) {
        first = count - max_visible;
    }
    return first < 0 ? 0 : first;
}

static bool wifi_list_index_area(const wifi_setup_state_t *wifi, int index, int *x, int *y, int *right, int *bottom)
{
    const int count = tilt_wifi_setup_selectable_count(wifi);
    if (index < 0 || index >= count) {
        return false;
    }

    const int visible_capacity = (WIFI_LIST_BOTTOM_Y - WIFI_LIST_TOP_Y) / WIFI_LIST_ROW_H;
    const int max_visible = count < visible_capacity ? count : visible_capacity;
    const int first = wifi_list_visible_first(wifi);
    if (index < first || index >= first + max_visible) {
        return false;
    }

    const int row = index - first;
    if (x != NULL) {
        *x = WIFI_LIST_CARD_X;
    }
    if (y != NULL) {
        *y = WIFI_LIST_TOP_Y + row * WIFI_LIST_ROW_H - 4;
    }
    if (right != NULL) {
        *right = WIFI_LIST_CARD_X + WIFI_LIST_CARD_W;
    }
    if (bottom != NULL) {
        *bottom = WIFI_LIST_TOP_Y + row * WIFI_LIST_ROW_H + WIFI_LIST_ROW_H - 8;
    }
    return true;
}

static void draw_wifi_list_row(uint8_t *buffer, const wifi_setup_state_t *wifi, ui_fonts_t *fonts, int index)
{
    const int count = tilt_wifi_setup_selectable_count(wifi);
    if (buffer == NULL || wifi == NULL || fonts == NULL || index < 0 || index >= count) {
        return;
    }

    int x = 0;
    int y = 0;
    int right = 0;
    int bottom = 0;
    if (!wifi_list_index_area(wifi, index, &x, &y, &right, &bottom)) {
        return;
    }

    char ssid[30];
    char meta[64];
    if (index < wifi->scan.count) {
        const ink_wifi_scan_result_t *ap = &wifi->scan.results[index];
        const bool connected = wifi->status.connected && strcmp(wifi->status.ssid, ap->ssid) == 0;
        copy_ascii_clipped(ssid, sizeof(ssid), ap->ssid, 24);
        snprintf(
            meta,
            sizeof(meta),
            ap->ap_count > 1 ? "%s  %ddBm  AP x%u" : "%s  %ddBm",
            connected ? "CONNECTED" : (ap->saved ? "SAVED" : "NEW"),
            (int)ap->rssi,
            (unsigned)ap->ap_count);
    } else {
        snprintf(ssid, sizeof(ssid), "%s", "SCAN");
        snprintf(meta, sizeof(meta), "%s", wifi->scan_in_progress ? "Scanning ..." : "Refresh WiFi list");
    }

    const bool selected = index == wifi->selected_index;
    fill_rect(buffer, x, y, right - x, bottom - y, selected);
    draw_rect_outline(buffer, x, y, right - x, bottom - y, selected ? 3 : 1);
    if (selected) {
        draw_ui_text_inverted(buffer, &fonts->menu, x + WIFI_LIST_CARD_INSET, y + 8, ssid, 2, 1);
        draw_ui_text_inverted(buffer, &fonts->footer, x + WIFI_LIST_CARD_INSET, y + 34, meta, 1, 1);
    } else {
        draw_ui_text(buffer, &fonts->menu, x + WIFI_LIST_CARD_INSET, y + 8, ssid, 2, 1, true);
        draw_ui_text(buffer, &fonts->footer, x + WIFI_LIST_CARD_INSET, y + 34, meta, 1, 1, true);
    }
}

static void draw_wifi_list(uint8_t *buffer, const wifi_setup_state_t *wifi, ui_fonts_t *fonts)
{
    const int count = tilt_wifi_setup_selectable_count(wifi);
    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    draw_ui_text(buffer, &fonts->menu, 24, 34, "WiFi Setup", 2, 1, true);
    if (wifi != NULL && wifi->mode == WIFI_SETUP_UI_CONNECTING) {
        draw_ui_text(buffer, &fonts->footer, 24, 70, "Connecting ...", 1, 1, true);
    } else if (wifi != NULL && wifi->scan_in_progress) {
        draw_ui_text(buffer, &fonts->footer, 24, 70, "Scanning nearby WiFi ...", 1, 1, true);
    } else {
        draw_ui_text(buffer, &fonts->footer, 24, 70, "WiFi networks", 1, 1, true);
    }

    if (wifi == NULL || count == 0) {
        return;
    }

    const int visible_capacity = (WIFI_LIST_BOTTOM_Y - WIFI_LIST_TOP_Y) / WIFI_LIST_ROW_H;
    const int max_visible = count < visible_capacity ? count : visible_capacity;
    const int first = wifi_list_visible_first(wifi);

    for (int i = 0; i < max_visible; ++i) {
        const int index = first + i;
        draw_wifi_list_row(buffer, wifi, fonts, index);
    }

}

static void draw_result_popup(uint8_t *buffer, const wifi_setup_state_t *wifi, ui_fonts_t *fonts)
{
    char line[96];
    const ink_wifi_scan_result_t *ap = tilt_wifi_setup_selected_ap(wifi);
    char ssid[24];

    draw_wifi_list(buffer, wifi, fonts);
    fill_rect(buffer, 48, 210, EPD_GDEY0426T82_WIDTH - 96, 190, false);
    draw_rect_outline(buffer, 48, 210, EPD_GDEY0426T82_WIDTH - 96, 190, 3);

    if (ap != NULL) {
        copy_ascii_clipped(ssid, sizeof(ssid), ap->ssid, 18);
    } else {
        snprintf(ssid, sizeof(ssid), "%s", "-");
    }

    if (wifi != NULL && wifi->mode == WIFI_SETUP_UI_CONNECTING) {
        snprintf(line, sizeof(line), "Connecting %s ...", ssid);
        draw_ui_text(buffer, &fonts->menu, 76, 250, "Connecting", 2, 1, true);
        draw_ui_text(buffer, &fonts->footer, 76, 302, line, 1, 1, true);
        draw_ui_text(buffer, &fonts->footer, 76, 344, "Please wait ...", 1, 1, true);
        return;
    }

    const bool ok = wifi != NULL && wifi->result_error == ESP_OK;
    draw_ui_text(buffer, &fonts->menu, 76, 250, ok ? "Success" : "Failed", 2, 1, true);
    snprintf(line, sizeof(line), "%s %s", ok ? "Connected" : "Not connected", ssid);
    draw_ui_text(buffer, &fonts->footer, 76, 304, line, 1, 1, true);
    if (!ok && wifi != NULL) {
        snprintf(line, sizeof(line), "Reason: %s", esp_err_to_name(wifi->result_error));
        draw_ui_text(buffer, &fonts->footer, 76, 334, line, 1, 1, true);
    }
    draw_ui_text(buffer, &fonts->footer, 76, 364, "Press OK to return", 1, 1, true);
}

static void draw_saved_wifi_menu(uint8_t *buffer, const wifi_setup_state_t *wifi, ui_fonts_t *fonts)
{
    char line[96];
    char ssid[24];
    const ink_wifi_scan_result_t *ap = tilt_wifi_setup_selected_ap(wifi);

    draw_wifi_list(buffer, wifi, fonts);
    fill_rect(buffer, 48, 210, EPD_GDEY0426T82_WIDTH - 96, 220, false);
    draw_rect_outline(buffer, 48, 210, EPD_GDEY0426T82_WIDTH - 96, 220, 3);

    if (ap != NULL) {
        copy_ascii_clipped(ssid, sizeof(ssid), ap->ssid, 18);
    } else {
        snprintf(ssid, sizeof(ssid), "%s", "-");
    }

    draw_ui_text(buffer, &fonts->menu, 76, 244, "Saved WiFi", 2, 1, true);
    snprintf(line, sizeof(line), "%s", ssid);
    draw_ui_text(buffer, &fonts->footer, 76, 292, line, 1, 1, true);

    const bool connect_selected = wifi == NULL || wifi->menu_index == 0;
    const int btn_y = 334;
    fill_rect(buffer, 76, btn_y, 150, 46, connect_selected);
    draw_rect_outline(buffer, 76, btn_y, 150, 46, connect_selected ? 3 : 1);
    if (connect_selected) {
        draw_ui_text_inverted(buffer, &fonts->footer, 96, btn_y + 14, "CONNECT", 1, 1);
    } else {
        draw_ui_text(buffer, &fonts->footer, 96, btn_y + 14, "CONNECT", 1, 1, true);
    }

    fill_rect(buffer, 254, btn_y, 150, 46, !connect_selected);
    draw_rect_outline(buffer, 254, btn_y, 150, 46, connect_selected ? 1 : 3);
    if (!connect_selected) {
        draw_ui_text_inverted(buffer, &fonts->footer, 282, btn_y + 14, "DELETE", 1, 1);
    } else {
        draw_ui_text(buffer, &fonts->footer, 282, btn_y + 14, "DELETE", 1, 1, true);
    }
}

static void draw_wifi_setup_screen(
    uint8_t *buffer,
    keyboard_layer_t layer,
    const tilt_grid_input_t *keyboard_state,
    const keyboard_text_t *text,
    const wifi_setup_state_t *wifi,
    ui_fonts_t *fonts)
{
    if (wifi != NULL && wifi->mode == WIFI_SETUP_UI_SAVED_MENU) {
        draw_saved_wifi_menu(buffer, wifi, fonts);
    } else if (wifi != NULL && wifi->mode == WIFI_SETUP_UI_PASSWORD) {
        draw_keyboard(buffer, layer, keyboard_state, text, wifi, fonts);
    } else if (wifi != NULL && (wifi->mode == WIFI_SETUP_UI_CONNECTING || wifi->mode == WIFI_SETUP_UI_RESULT)) {
        draw_result_popup(buffer, wifi, fonts);
    } else {
        draw_wifi_list(buffer, wifi, fonts);
    }
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

    int area_x = EPD_GDEY0426T82_WIDTH;
    int area_y = EPD_GDEY0426T82_HEIGHT;
    int area_right = 0;
    int area_bottom = 0;
    int x = 0;
    int y = 0;
    int right = 0;
    int bottom = 0;

    xSemaphoreTake(ctx->lock, portMAX_DELAY);
    if (ctx->wifi_setup.mode != WIFI_SETUP_UI_LIST) {
        xSemaphoreGive(ctx->lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (wifi_list_index_area(&ctx->wifi_setup, old_index, &x, &y, &right, &bottom)) {
        expand_area(&area_x, &area_y, &area_right, &area_bottom, x, y, right, bottom);
    }
    if (wifi_list_index_area(&ctx->wifi_setup, new_index, &x, &y, &right, &bottom)) {
        expand_area(&area_x, &area_y, &area_right, &area_bottom, x, y, right, bottom);
    }
    xSemaphoreGive(ctx->lock);

    if (area_right <= area_x || area_bottom <= area_y) {
        return ESP_ERR_INVALID_STATE;
    }
    pad_and_align_refresh_area(&area_x, &area_y, &area_right, &area_bottom, 4);

    return epd_gdey0426t82_partial_refresh_area(
        ctx->framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        (uint16_t)area_x,
        (uint16_t)area_y,
        (uint16_t)(area_right - area_x),
        (uint16_t)(area_bottom - area_y));
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

    int area_x = 0;
    int area_y = 24;
    int area_right = EPD_GDEY0426T82_WIDTH;
    int area_bottom = WIFI_LIST_BOTTOM_Y;
    pad_and_align_refresh_area(&area_x, &area_y, &area_right, &area_bottom, 4);

    return epd_gdey0426t82_partial_refresh_area(
        ctx->framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        (uint16_t)area_x,
        (uint16_t)area_y,
        (uint16_t)(area_right - area_x),
        (uint16_t)(area_bottom - area_y));
}

static esp_err_t display_refresh_screen_partial(tilt_app_context_t *ctx, const display_request_t *request)
{
    if (ctx == NULL || ctx->framebuffer == NULL || request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(ctx->lock, portMAX_DELAY);
    draw_wifi_setup_screen(ctx->framebuffer, ctx->keyboard_layer, &ctx->input, &ctx->keyboard_text, &ctx->wifi_setup, &ctx->fonts);
    xSemaphoreGive(ctx->lock);

    int area_x = request->area_x;
    int area_y = request->area_y;
    int area_right = request->area_x + request->area_w;
    int area_bottom = request->area_y + request->area_h;
    if (area_right > EPD_GDEY0426T82_WIDTH) {
        area_right = EPD_GDEY0426T82_WIDTH;
    }
    if (area_bottom > EPD_GDEY0426T82_HEIGHT) {
        area_bottom = EPD_GDEY0426T82_HEIGHT;
    }
    if (area_right <= area_x || area_bottom <= area_y) {
        return ESP_ERR_INVALID_ARG;
    }
    pad_and_align_refresh_area(&area_x, &area_y, &area_right, &area_bottom, 4);

    return epd_gdey0426t82_partial_refresh_area(
        ctx->framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        (uint16_t)area_x,
        (uint16_t)area_y,
        (uint16_t)(area_right - area_x),
        (uint16_t)(area_bottom - area_y));
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

    int area_x = EPD_GDEY0426T82_WIDTH;
    int area_y = EPD_GDEY0426T82_HEIGHT;
    int area_right = 0;
    int area_bottom = 0;
    int x = 0;
    int y = 0;
    int right = 0;
    int bottom = 0;

    xSemaphoreTake(ctx->lock, portMAX_DELAY);
    if (ctx->wifi_setup.mode != WIFI_SETUP_UI_PASSWORD) {
        xSemaphoreGive(ctx->lock);
        return ESP_ERR_INVALID_STATE;
    }
    draw_keyboard_key(ctx->framebuffer, ctx->keyboard_layer, old_col, old_row, false);
    draw_keyboard_key(ctx->framebuffer, ctx->keyboard_layer, new_col, new_row, true);
    if (keyboard_key_area(old_col, old_row, &x, &y, &right, &bottom)) {
        expand_area(&area_x, &area_y, &area_right, &area_bottom, x, y, right, bottom);
    }
    if (keyboard_key_area(new_col, new_row, &x, &y, &right, &bottom)) {
        expand_area(&area_x, &area_y, &area_right, &area_bottom, x, y, right, bottom);
    }
    xSemaphoreGive(ctx->lock);

    if (area_right <= area_x || area_bottom <= area_y) {
        return ESP_ERR_INVALID_STATE;
    }
    pad_and_align_refresh_area(&area_x, &area_y, &area_right, &area_bottom, 6);

    return epd_gdey0426t82_partial_refresh_area(
        ctx->framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        (uint16_t)area_x,
        (uint16_t)area_y,
        (uint16_t)(area_right - area_x),
        (uint16_t)(area_bottom - area_y));
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
            int partial_x = 0;
            int partial_y = 0;
            int partial_w = EPD_GDEY0426T82_WIDTH;
            int partial_h = EPD_GDEY0426T82_HEIGHT;
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
                        partial_x = WIFI_SAVED_MENU_X - 8;
                        partial_y = WIFI_SAVED_MENU_Y - 8;
                        partial_w = WIFI_SAVED_MENU_W + 16;
                        partial_h = WIFI_SAVED_MENU_H + 16;
                        flush_display_queue = true;
                    } else if (ap != NULL) {
                        tilt_wifi_setup_open_password(&ctx->wifi_setup);
                        keyboard_text_clear(&ctx->keyboard_text);
                        tilt_grid_input_init(&ctx->input);
                        ctx->keyboard_layer = KEYBOARD_LAYER_LOWER;
                        partial_refresh = true;
                        partial_x = 0;
                        partial_y = 24;
                        partial_w = EPD_GDEY0426T82_WIDTH;
                        partial_h = EPD_GDEY0426T82_HEIGHT - 24;
                        flush_display_queue = true;
                        ESP_LOGI(TAG, "wifi password popup open index=%d", ctx->wifi_setup.selected_index);
                    }
                }
            } else if (mode_at_press == WIFI_SETUP_UI_PASSWORD && (pressed & LOCAL_BUTTON_LOWER) != 0) {
                ctx->keyboard_layer = KEYBOARD_LAYER_LOWER;
                partial_refresh = true;
                partial_x = 0;
                partial_y = 224;
                partial_w = EPD_GDEY0426T82_WIDTH;
                partial_h = EPD_GDEY0426T82_HEIGHT - 224;
            } else if (mode_at_press == WIFI_SETUP_UI_SAVED_MENU
                && ((pressed & LOCAL_BUTTON_LOWER) != 0 || (pressed & LOCAL_BUTTON_UPPER) != 0)) {
                ctx->wifi_setup.menu_index = ctx->wifi_setup.menu_index == 0 ? 1 : 0;
                partial_refresh = true;
                partial_x = WIFI_SAVED_MENU_X - 8;
                partial_y = WIFI_SAVED_MENU_Y - 8;
                partial_w = WIFI_SAVED_MENU_W + 16;
                partial_h = WIFI_SAVED_MENU_H + 16;
            }

            if (mode_at_press == WIFI_SETUP_UI_PASSWORD && (pressed & LOCAL_BUTTON_UPPER) != 0) {
                ctx->keyboard_layer = KEYBOARD_LAYER_UPPER;
                partial_refresh = true;
                partial_x = 0;
                partial_y = 224;
                partial_w = EPD_GDEY0426T82_WIDTH;
                partial_h = EPD_GDEY0426T82_HEIGHT - 224;
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
                            partial_x = 0;
                            partial_y = 224;
                            partial_w = EPD_GDEY0426T82_WIDTH;
                            partial_h = EPD_GDEY0426T82_HEIGHT - 224;
                        } else {
                            partial_refresh = true;
                            partial_x = PASSWORD_BOX_X - 8;
                            partial_y = PASSWORD_BOX_Y - 8;
                            partial_w = PASSWORD_BOX_W + 16;
                            partial_h = PASSWORD_BOX_H + 16;
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
                post_screen_partial_request(ctx, partial_x, partial_y, partial_w, partial_h);
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
