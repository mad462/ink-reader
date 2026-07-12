#include "ink_hw.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NATIVE_WIDTH 800
#define NATIVE_HEIGHT 480
#define NATIVE_SIZE (NATIVE_WIDTH * NATIVE_HEIGHT / 8)
#define SPI_CHUNK 4096
#define BUSY_TIMEOUT_MS 15000

static const char *TAG = "ink_hw";
static spi_device_handle_t s_spi;
static uint8_t *s_native;
static uint8_t *s_dma;
static bool s_initialized;
static const uint8_t kGrayLut[110] = {
    0, 0, 0,    0,    0,    0,    0,    0,    0,    0,    0x54, 0x54, 0x40, 0,
    0, 0, 0,    0,    0,    0,    0xaa, 0xa0, 0xa8, 0,    0,    0,    0,    0,
    0, 0, 0xa2, 0x22, 0x20, 0,    0,    0,    0,    0,    0,    0,    0,    0,
    0, 0, 0,    0,    0,    0,    0,    0,    1,    1,    1,    1,    0,    1,
    1, 1, 1,    0,    1,    1,    1,    1,    0,    0,    0,    0,    0,    0,
    0, 0, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0, 0, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0, 0, 0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x17, 0x41, 0xa8, 0x32, 0x30, 0,    0};

static esp_err_t command(uint8_t value) {
  gpio_set_level(GPIO_NUM_7, 0);
  spi_transaction_t t = {.length = 8, .tx_buffer = &value};
  return spi_device_polling_transmit(s_spi, &t);
}
static esp_err_t data_byte(uint8_t value) {
  gpio_set_level(GPIO_NUM_7, 1);
  spi_transaction_t t = {.length = 8, .tx_buffer = &value};
  return spi_device_polling_transmit(s_spi, &t);
}
static esp_err_t data(const uint8_t *src, size_t length) {
  if (!src || !length) return ESP_ERR_INVALID_ARG;
  gpio_set_level(GPIO_NUM_7, 1);
  for (size_t offset = 0; offset < length; offset += SPI_CHUNK) {
    size_t count = length - offset;
    if (count > SPI_CHUNK) count = SPI_CHUNK;
    const uint8_t *tx = src + offset;
    if (!esp_ptr_dma_capable(tx) || ((uintptr_t)tx & 3u)) {
      memcpy(s_dma, tx, count);
      tx = s_dma;
    }
    spi_transaction_t t = {.length = count * 8, .tx_buffer = tx};
    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(s_spi, &t), TAG,
                        "spi transmit failed");
  }
  return ESP_OK;
}
static esp_err_t wait_ready(const char *stage) {
  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BUSY_TIMEOUT_MS);
  while (gpio_get_level(GPIO_NUM_16)) {
    if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) {
      ESP_LOGE(TAG, "busy timeout stage=%s", stage);
      return ESP_ERR_TIMEOUT;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return ESP_OK;
}
static void reset_panel(void) {
  gpio_set_level(GPIO_NUM_15, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(GPIO_NUM_15, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
}
static esp_err_t window(void) {
  ESP_RETURN_ON_ERROR(command(0x11), TAG, "entry cmd");
  ESP_RETURN_ON_ERROR(data_byte(1), TAG, "entry data");
  ESP_RETURN_ON_ERROR(command(0x44), TAG, "x cmd");
  const uint8_t x[] = {0, 0, 0x1f, 3};
  ESP_RETURN_ON_ERROR(data(x, sizeof(x)), TAG, "x data");
  ESP_RETURN_ON_ERROR(command(0x45), TAG, "y cmd");
  const uint8_t y[] = {0xdf, 1, 0, 0};
  ESP_RETURN_ON_ERROR(data(y, sizeof(y)), TAG, "y data");
  ESP_RETURN_ON_ERROR(command(0x4e), TAG, "xc cmd");
  const uint8_t xc[] = {0, 0};
  ESP_RETURN_ON_ERROR(data(xc, sizeof(xc)), TAG, "xc data");
  ESP_RETURN_ON_ERROR(command(0x4f), TAG, "yc cmd");
  const uint8_t yc[] = {0xdf, 1};
  return data(yc, sizeof(yc));
}
static esp_err_t init_sequence(bool gray) {
  reset_panel();
  ESP_RETURN_ON_ERROR(wait_ready("reset"), TAG, "panel reset timeout");
  ESP_RETURN_ON_ERROR(command(0x12), TAG, "sw reset");
  ESP_RETURN_ON_ERROR(wait_ready("sw_reset"), TAG, "panel sw reset timeout");
  if (!gray) {
    ESP_RETURN_ON_ERROR(command(0x18), TAG, "temp cmd");
    ESP_RETURN_ON_ERROR(data_byte(0x80), TAG, "temp data");
  }
  ESP_RETURN_ON_ERROR(command(0x0c), TAG, "boost cmd");
  const uint8_t boost[] = {0xae, 0xc7, 0xc3, 0xc0, 0x80};
  ESP_RETURN_ON_ERROR(data(boost, sizeof(boost)), TAG, "boost data");
  ESP_RETURN_ON_ERROR(command(0x01), TAG, "gate cmd");
  const uint8_t gate[] = {0xdf, 1, 2};
  ESP_RETURN_ON_ERROR(data(gate, sizeof(gate)), TAG, "gate data");
  ESP_RETURN_ON_ERROR(command(0x3c), TAG, "border cmd");
  ESP_RETURN_ON_ERROR(data_byte(gray ? 0 : 1), TAG, "border data");
  ESP_RETURN_ON_ERROR(window(), TAG, "window");
  if (gray) {
    ESP_RETURN_ON_ERROR(command(0x32), TAG, "lut cmd");
    ESP_RETURN_ON_ERROR(data(kGrayLut, 105), TAG, "lut data");
    ESP_RETURN_ON_ERROR(command(0x03), TAG, "gate voltage");
    ESP_RETURN_ON_ERROR(data_byte(kGrayLut[105]), TAG, "gate voltage data");
    ESP_RETURN_ON_ERROR(command(0x04), TAG, "source voltage");
    ESP_RETURN_ON_ERROR(data(kGrayLut + 106, 3), TAG, "source voltage data");
    ESP_RETURN_ON_ERROR(command(0x2c), TAG, "vcom cmd");
    ESP_RETURN_ON_ERROR(data_byte(kGrayLut[109]), TAG, "vcom data");
  }
  return wait_ready("init");
}
static void convert(const uint8_t *portrait) {
  memset(s_native, 0xff, NATIVE_SIZE);
  for (int y = 0; y < INK_HW_HEIGHT; ++y)
    for (int x = 0; x < INK_HW_WIDTH; ++x) {
      size_t pi = (size_t)y * (INK_HW_WIDTH / 8) + x / 8;
      if (portrait[pi] & (0x80u >> (x & 7))) continue;
      int nx = y, ny = INK_HW_WIDTH - 1 - x;
      size_t ni = (size_t)ny * (NATIVE_WIDTH / 8) + nx / 8;
      s_native[ni] &= (uint8_t)~(0x80u >> (nx & 7));
    }
}
static esp_err_t write_plane(const uint8_t *portrait, uint8_t ram_cmd) {
  convert(portrait);
  ESP_RETURN_ON_ERROR(window(), TAG, "window");
  ESP_RETURN_ON_ERROR(command(ram_cmd), TAG, "ram cmd");
  return data(s_native, NATIVE_SIZE);
}
static esp_err_t update(uint8_t mode, bool gray) {
  if (gray) {
    ESP_RETURN_ON_ERROR(command(0x21), TAG, "gray option");
    const uint8_t options[] = {0, 0};
    ESP_RETURN_ON_ERROR(data(options, 2), TAG, "gray options");
  } else {
    ESP_RETURN_ON_ERROR(command(0x21), TAG, "full option");
    const uint8_t options[] = {0x40, 0};
    ESP_RETURN_ON_ERROR(data(options, 2), TAG, "full options");
  }
  ESP_RETURN_ON_ERROR(command(0x22), TAG, "update cmd");
  ESP_RETURN_ON_ERROR(data_byte(mode), TAG, "update mode");
  ESP_RETURN_ON_ERROR(command(0x20), TAG, "activate");
  return wait_ready(gray ? "gray_update" : "full_update");
}

esp_err_t ink_hw_init(void) {
  gpio_config_t out = {
      .pin_bit_mask = (1ULL << GPIO_NUM_7) | (1ULL << GPIO_NUM_15),
      .mode = GPIO_MODE_OUTPUT,
      .intr_type = GPIO_INTR_DISABLE};
  ESP_RETURN_ON_ERROR(gpio_config(&out), TAG, "output gpio");
  gpio_config_t in = {.pin_bit_mask = 1ULL << GPIO_NUM_16,
                      .mode = GPIO_MODE_INPUT,
                      .intr_type = GPIO_INTR_DISABLE};
  ESP_RETURN_ON_ERROR(gpio_config(&in), TAG, "busy gpio");
  spi_bus_config_t bus = {.mosi_io_num = 4,
                          .miso_io_num = -1,
                          .sclk_io_num = 5,
                          .quadwp_io_num = -1,
                          .quadhd_io_num = -1,
                          .max_transfer_sz = SPI_CHUNK};
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG,
                      "spi bus");
  spi_device_interface_config_t dev = {.clock_speed_hz = 10 * 1000 * 1000,
                                       .mode = 0,
                                       .spics_io_num = 6,
                                       .queue_size = 1};
  ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &dev, &s_spi), TAG,
                      "spi device");
  s_native = heap_caps_malloc(NATIVE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_native)
    s_native =
        heap_caps_malloc(NATIVE_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_dma = heap_caps_malloc(
      SPI_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_native || !s_dma) return ESP_ERR_NO_MEM;
  s_initialized = true;
  return init_sequence(false);
}
esp_err_t ink_hw_full_refresh(const uint8_t *buffer, size_t length) {
  if (!s_initialized || !buffer || length < INK_HW_BUFFER_SIZE)
    return ESP_ERR_INVALID_ARG;
  ESP_RETURN_ON_ERROR(init_sequence(false), TAG, "full init");
  ESP_RETURN_ON_ERROR(write_plane(buffer, 0x24), TAG, "full plane");
  return update(0xf7, false);
}
esp_err_t ink_hw_gray_refresh(const uint8_t *lsb, size_t ll, const uint8_t *msb,
                              size_t ml) {
  if (!s_initialized || !lsb || !msb || ll < INK_HW_BUFFER_SIZE ||
      ml < INK_HW_BUFFER_SIZE)
    return ESP_ERR_INVALID_ARG;
  ESP_RETURN_ON_ERROR(init_sequence(true), TAG, "gray init");
  ESP_RETURN_ON_ERROR(write_plane(msb, 0x26), TAG, "gray msb");
  ESP_RETURN_ON_ERROR(write_plane(lsb, 0x24), TAG, "gray lsb");
  return update(0xc7, true);
}
esp_err_t ink_hw_sleep(void) {
  if (!s_initialized) return ESP_ERR_INVALID_STATE;
  ESP_RETURN_ON_ERROR(command(0x10), TAG, "sleep");
  return data_byte(1);
}
