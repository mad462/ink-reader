#include "ink_tilt_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    INK_TILT_SAMPLE_PERIOD_MS = 50,
    INK_TILT_ENTER_THRESHOLD = 4200,
    INK_TILT_EXIT_THRESHOLD = 2200,
    INK_TILT_REPEAT_SAMPLES = 0,
};

#define INK_TILT_I2C_SCL_GPIO GPIO_NUM_1
#define INK_TILT_I2C_SDA_GPIO GPIO_NUM_2
#define INK_TILT_I2C_PORT I2C_NUM_0
#define INK_TILT_I2C_SPEED_HZ 100000
#define INK_TILT_I2C_XFER_TIMEOUT_MS 1000
#define INK_TILT_MPU_ADDR_LOW 0x68
#define INK_TILT_MPU_ADDR_HIGH 0x69
#define INK_TILT_MPU_REG_WHO_AM_I 0x75
#define INK_TILT_MPU_REG_PWR_MGMT_1 0x6B
#define INK_TILT_MPU_REG_ACCEL_XOUT_H 0x3B

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    uint8_t address;
    int neutral_x;
    int neutral_y;
    uint32_t last_sample_ms;
    bool initialized;
    bool available;
    int repeat_countdown;
    ink_tilt_direction_t active_direction;
} ink_tilt_state_t;

static ink_tilt_state_t s_tilt_state;

static int16_t read_be_i16(const uint8_t *data);
static int abs_int(int value);
static void recover_i2c_lines(void);
static esp_err_t read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value);
static esp_err_t read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len);
static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value);
static esp_err_t calibrate_neutral(ink_tilt_state_t *state);
static ink_tilt_direction_t classify_direction(int accel_x, int accel_y);
static ink_tilt_direction_t step_direction(ink_tilt_state_t *state, int accel_x, int accel_y);

static int16_t read_be_i16(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static int abs_int(int value)
{
    return value < 0 ? -value : value;
}

static void recover_i2c_lines(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << INK_TILT_I2C_SCL_GPIO) | (1ULL << INK_TILT_I2C_SDA_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    (void)gpio_config(&cfg);
    gpio_set_level(INK_TILT_I2C_SDA_GPIO, 1);
    gpio_set_level(INK_TILT_I2C_SCL_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    for (int i = 0; i < 9; ++i) {
        gpio_set_level(INK_TILT_I2C_SCL_GPIO, 0);
        esp_rom_delay_us(5);
        gpio_set_level(INK_TILT_I2C_SCL_GPIO, 1);
        esp_rom_delay_us(5);
    }

    gpio_set_level(INK_TILT_I2C_SDA_GPIO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(INK_TILT_I2C_SCL_GPIO, 1);
    esp_rom_delay_us(5);
    gpio_set_level(INK_TILT_I2C_SDA_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
}

static esp_err_t read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(dev, &reg, 1, value, 1, INK_TILT_I2C_XFER_TIMEOUT_MS);
}

static esp_err_t read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, INK_TILT_I2C_XFER_TIMEOUT_MS);
}

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    const uint8_t data[2] = {reg, value};
    return i2c_master_transmit(dev, data, sizeof(data), INK_TILT_I2C_XFER_TIMEOUT_MS);
}

static esp_err_t calibrate_neutral(ink_tilt_state_t *state)
{
    int64_t sum_x = 0;
    int64_t sum_y = 0;
    int count = 0;

    if (state == NULL || state->dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < 16; ++i) {
        uint8_t raw[6] = {0};
        if (read_regs(state->dev, INK_TILT_MPU_REG_ACCEL_XOUT_H, raw, sizeof(raw)) == ESP_OK) {
            sum_x += read_be_i16(&raw[0]);
            sum_y += read_be_i16(&raw[2]);
            ++count;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (count <= 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    state->neutral_x = (int)(sum_x / count);
    state->neutral_y = (int)(sum_y / count);
    return ESP_OK;
}

static ink_tilt_direction_t classify_direction(int accel_x, int accel_y)
{
    const int abs_x = abs_int(accel_x);
    const int abs_y = abs_int(accel_y);
    const bool right = accel_x < 0;
    const bool down = accel_y < 0;

    if (abs_x < INK_TILT_ENTER_THRESHOLD && abs_y < INK_TILT_ENTER_THRESHOLD) {
        return INK_TILT_DIRECTION_NONE;
    }

    if (abs_x >= INK_TILT_ENTER_THRESHOLD && abs_y >= INK_TILT_ENTER_THRESHOLD) {
        if (right && down) {
            return INK_TILT_DIRECTION_DOWN_RIGHT;
        }
        if (right) {
            return INK_TILT_DIRECTION_UP_RIGHT;
        }
        if (down) {
            return INK_TILT_DIRECTION_DOWN_LEFT;
        }
        return INK_TILT_DIRECTION_UP_LEFT;
    }

    if (abs_x >= abs_y) {
        return right ? INK_TILT_DIRECTION_RIGHT : INK_TILT_DIRECTION_LEFT;
    }
    return down ? INK_TILT_DIRECTION_DOWN : INK_TILT_DIRECTION_UP;
}

static ink_tilt_direction_t step_direction(ink_tilt_state_t *state, int accel_x, int accel_y)
{
    const int abs_x = abs_int(accel_x);
    const int abs_y = abs_int(accel_y);
    const ink_tilt_direction_t direction = classify_direction(accel_x, accel_y);

    if (state == NULL) {
        return INK_TILT_DIRECTION_NONE;
    }

    if (abs_x < INK_TILT_EXIT_THRESHOLD && abs_y < INK_TILT_EXIT_THRESHOLD) {
        state->active_direction = INK_TILT_DIRECTION_NONE;
        state->repeat_countdown = 0;
        return INK_TILT_DIRECTION_NONE;
    }
    if (direction == INK_TILT_DIRECTION_NONE) {
        return INK_TILT_DIRECTION_NONE;
    }
    if (direction != state->active_direction) {
        state->active_direction = direction;
        state->repeat_countdown = 0;
    } else if (state->repeat_countdown > 0) {
        --state->repeat_countdown;
        return INK_TILT_DIRECTION_NONE;
    }

    state->repeat_countdown = INK_TILT_REPEAT_SAMPLES;
    return direction;
}

esp_err_t ink_tilt_input_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = INK_TILT_I2C_PORT,
        .scl_io_num = INK_TILT_I2C_SCL_GPIO,
        .sda_io_num = INK_TILT_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz = INK_TILT_I2C_SPEED_HZ,
    };
    uint8_t who = 0;
    esp_err_t ret = ESP_FAIL;

    memset(&s_tilt_state, 0, sizeof(s_tilt_state));
    recover_i2c_lines();

    ret = i2c_new_master_bus(&bus_cfg, &s_tilt_state.bus);
    if (ret != ESP_OK) {
        return ret;
    }

    for (int i = 0; i < 2; ++i) {
        const uint8_t addr = i == 0 ? INK_TILT_MPU_ADDR_LOW : INK_TILT_MPU_ADDR_HIGH;
        if (i2c_master_probe(s_tilt_state.bus, addr, INK_TILT_I2C_XFER_TIMEOUT_MS) != ESP_OK) {
            continue;
        }
        dev_cfg.device_address = addr;
        if (i2c_master_bus_add_device(s_tilt_state.bus, &dev_cfg, &s_tilt_state.dev) != ESP_OK) {
            continue;
        }
        s_tilt_state.address = addr;
        break;
    }

    if (s_tilt_state.dev == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(read_reg(s_tilt_state.dev, INK_TILT_MPU_REG_WHO_AM_I, &who), "ink_tilt", "whoami");
    ESP_RETURN_ON_ERROR(write_reg(s_tilt_state.dev, INK_TILT_MPU_REG_PWR_MGMT_1, 0x00), "ink_tilt", "wake");
    ESP_RETURN_ON_ERROR(calibrate_neutral(&s_tilt_state), "ink_tilt", "neutral");

    s_tilt_state.initialized = true;
    s_tilt_state.available = true;
    s_tilt_state.last_sample_ms = 0U;
    s_tilt_state.repeat_countdown = 0;
    s_tilt_state.active_direction = INK_TILT_DIRECTION_NONE;
    (void)who;
    return ESP_OK;
}

esp_err_t ink_tilt_input_poll(uint32_t now_ms, ink_tilt_direction_t *direction_out)
{
    uint8_t raw[6] = {0};
    int accel_x = 0;
    int accel_y = 0;

    if (direction_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *direction_out = INK_TILT_DIRECTION_NONE;
    if (!s_tilt_state.initialized || !s_tilt_state.available || s_tilt_state.dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_tilt_state.last_sample_ms != 0U
        && (uint32_t)(now_ms - s_tilt_state.last_sample_ms) < INK_TILT_SAMPLE_PERIOD_MS) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(read_regs(s_tilt_state.dev, INK_TILT_MPU_REG_ACCEL_XOUT_H, raw, sizeof(raw)), "ink_tilt", "accel");
    s_tilt_state.last_sample_ms = now_ms;
    accel_x = (int)read_be_i16(&raw[0]) - s_tilt_state.neutral_x;
    accel_y = (int)read_be_i16(&raw[2]) - s_tilt_state.neutral_y;
    *direction_out = step_direction(&s_tilt_state, accel_x, accel_y);
    return ESP_OK;
}

bool ink_tilt_input_available(void)
{
    return s_tilt_state.available;
}

bool ink_tilt_input_self_test(void)
{
    ink_tilt_state_t state;

    memset(&state, 0, sizeof(state));
    if (step_direction(&state, -18000, 0) != INK_TILT_DIRECTION_RIGHT) {
        return false;
    }
    if (step_direction(&state, -18000, 0) != INK_TILT_DIRECTION_RIGHT) {
        return false;
    }
    if (step_direction(&state, 0, 0) != INK_TILT_DIRECTION_NONE) {
        return false;
    }
    if (state.active_direction != INK_TILT_DIRECTION_NONE) {
        return false;
    }

    return classify_direction(-18000, 0) == INK_TILT_DIRECTION_RIGHT
        && classify_direction(18000, 0) == INK_TILT_DIRECTION_LEFT
        && classify_direction(0, -18000) == INK_TILT_DIRECTION_DOWN
        && classify_direction(0, 18000) == INK_TILT_DIRECTION_UP
        && classify_direction(-18000, 18000) == INK_TILT_DIRECTION_UP_RIGHT;
}
