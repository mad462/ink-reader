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
static uint8_t *s_shadow;
static uint8_t *s_dma;
static bool s_initialized;
static const uint8_t kGrayLut[] = {
    0x80, 0x48, 0x4A, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x48, 0x68, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x88, 0x48, 0x60, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA8, 0x48, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x1E, 0x1C, 0x02, 0x00, 0x05, 0x01, 0x05, 0x01, 0x02,
    0x08, 0x01, 0x01, 0x04, 0x04, 0x00, 0x02, 0x01, 0x02, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x17, 0x41, 0xA8, 0x32, 0x30,
    0x00, 0x00,
};
static const uint8_t kGrayTemperatureCommand = 0x18;
static const uint8_t kGrayTemperatureValue = 0x80;
static const uint8_t kGrayUpdateMode = 0xC7;

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
static esp_err_t wait_ready(const char *stage, ink_hw_refresh_poll_fn poll,
                            void *context, bool cancel_when_ready) {
  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BUSY_TIMEOUT_MS);
  bool superseded = false;
  while (true) {
    if (poll && poll(context)) superseded = true;
    if (!gpio_get_level(GPIO_NUM_16))
      return cancel_when_ready && superseded ? ESP_ERR_NOT_FINISHED : ESP_OK;
    if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) {
      ESP_LOGE(TAG, "busy timeout stage=%s", stage);
      return ESP_ERR_TIMEOUT;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
static void reset_panel(void) {
  gpio_set_level(GPIO_NUM_15, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(GPIO_NUM_15, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
}
static esp_err_t set_native_window(uint16_t x, uint16_t y, uint16_t width,
                                   uint16_t height) {
  const uint16_t reversed_y = NATIVE_HEIGHT - y - height;
  const uint16_t x_end = x + width - 1;
  const uint16_t y_end = reversed_y + height - 1;

  ESP_RETURN_ON_ERROR(command(0x11), TAG, "entry cmd");
  ESP_RETURN_ON_ERROR(data_byte(1), TAG, "entry data");
  ESP_RETURN_ON_ERROR(command(0x44), TAG, "x cmd");
  const uint8_t x_data[] = {x & 0xff, x >> 8, x_end & 0xff, x_end >> 8};
  ESP_RETURN_ON_ERROR(data(x_data, sizeof(x_data)), TAG, "x data");
  ESP_RETURN_ON_ERROR(command(0x45), TAG, "y cmd");
  const uint8_t y_data[] = {y_end & 0xff, y_end >> 8, reversed_y & 0xff,
                            reversed_y >> 8};
  ESP_RETURN_ON_ERROR(data(y_data, sizeof(y_data)), TAG, "y data");
  ESP_RETURN_ON_ERROR(command(0x4e), TAG, "xc cmd");
  const uint8_t xc[] = {x & 0xff, x >> 8};
  ESP_RETURN_ON_ERROR(data(xc, sizeof(xc)), TAG, "xc data");
  ESP_RETURN_ON_ERROR(command(0x4f), TAG, "yc cmd");
  const uint8_t yc[] = {y_end & 0xff, y_end >> 8};
  return data(yc, sizeof(yc));
}
static esp_err_t window(void) {
  return set_native_window(0, 0, NATIVE_WIDTH, NATIVE_HEIGHT);
}
static esp_err_t init_sequence(bool gray, ink_hw_refresh_poll_fn poll,
                               void *context) {
  esp_err_t ret;
  reset_panel();
  ret = wait_ready("reset", poll, context, poll != NULL);
  if (ret != ESP_OK) return ret;
  ESP_RETURN_ON_ERROR(command(0x12), TAG, "sw reset");
  ret = wait_ready("sw_reset", poll, context, poll != NULL);
  if (ret != ESP_OK) return ret;
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
  if (gray) {
    ESP_RETURN_ON_ERROR(command(kGrayTemperatureCommand), TAG,
                        "gray temp cmd");
    ESP_RETURN_ON_ERROR(data_byte(kGrayTemperatureValue), TAG,
                        "gray temp data");
  }
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
  return wait_ready("init", poll, context, poll != NULL);
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
static esp_err_t write_native_area(const uint8_t *src, uint16_t x, uint16_t y,
                                   uint16_t width, uint16_t height) {
  const size_t stride = NATIVE_WIDTH / 8;
  const size_t row_bytes = width / 8;
  for (uint16_t row = 0; row < height; ++row) {
    const uint8_t *line = src + ((size_t)y + row) * stride + x / 8;
    ESP_RETURN_ON_ERROR(data(line, row_bytes), TAG, "native area row");
  }
  return ESP_OK;
}
static void copy_native_area(uint8_t *dst, const uint8_t *src, uint16_t x,
                             uint16_t y, uint16_t width, uint16_t height) {
  const size_t stride = NATIVE_WIDTH / 8;
  const size_t row_bytes = width / 8;
  for (uint16_t row = 0; row < height; ++row) {
    const size_t offset = ((size_t)y + row) * stride + x / 8;
    memcpy(dst + offset, src + offset, row_bytes);
  }
}
static esp_err_t update(uint8_t mode, bool gray, ink_hw_refresh_poll_fn poll,
                        void *context) {
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
  if (gray)
    return wait_ready("gray_update", poll, context, false);
  return wait_ready("full_update", NULL, NULL, false);
}

esp_err_t ink_hw_init(void) {
  if (s_initialized) return ESP_OK;

  esp_err_t ret;
  bool bus_initialized = false;
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
  ret = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) return ret;
  bus_initialized = true;
  spi_device_interface_config_t dev = {.clock_speed_hz = 10 * 1000 * 1000,
                                       .mode = 0,
                                       .spics_io_num = 6,
                                       .queue_size = 1};
  ret = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
  if (ret != ESP_OK) goto fail;
  s_native = heap_caps_malloc(NATIVE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_native)
    s_native =
        heap_caps_malloc(NATIVE_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_shadow = heap_caps_malloc(NATIVE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_shadow)
    s_shadow =
        heap_caps_malloc(NATIVE_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_dma = heap_caps_malloc(
      SPI_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_native || !s_shadow || !s_dma) {
    ret = ESP_ERR_NO_MEM;
    goto fail;
  }
  memset(s_native, 0xff, NATIVE_SIZE);
  memset(s_shadow, 0xff, NATIVE_SIZE);
  ret = init_sequence(false, NULL, NULL);
  if (ret != ESP_OK) goto fail;
  s_initialized = true;
  return ESP_OK;

fail:
  heap_caps_free(s_dma);
  s_dma = NULL;
  heap_caps_free(s_shadow);
  s_shadow = NULL;
  heap_caps_free(s_native);
  s_native = NULL;
  if (s_spi) {
    (void)spi_bus_remove_device(s_spi);
    s_spi = NULL;
  }
  if (bus_initialized) (void)spi_bus_free(SPI2_HOST);
  s_initialized = false;
  return ret;
}
esp_err_t ink_hw_full_refresh(const uint8_t *buffer, size_t length) {
  if (!s_initialized || !buffer || length < INK_HW_BUFFER_SIZE)
    return ESP_ERR_INVALID_ARG;
  ESP_RETURN_ON_ERROR(init_sequence(false, NULL, NULL), TAG, "full init");
  ESP_RETURN_ON_ERROR(write_plane(buffer, 0x24), TAG, "full plane");
  ESP_RETURN_ON_ERROR(update(0xf7, false, NULL, NULL), TAG, "full update");
  memcpy(s_shadow, s_native, NATIVE_SIZE);
  return ESP_OK;
}
esp_err_t ink_hw_partial_refresh_area(const uint8_t *buffer, size_t length,
                                      uint16_t x, uint16_t y, uint16_t width,
                                      uint16_t height) {
  if (!s_initialized) return ESP_ERR_INVALID_STATE;
  if (!buffer || length < INK_HW_BUFFER_SIZE || !width || !height ||
      (uint32_t)x + width > INK_HW_WIDTH ||
      (uint32_t)y + height > INK_HW_HEIGHT)
    return ESP_ERR_INVALID_ARG;

  const uint16_t portrait_x = x & (uint16_t)~7u;
  uint16_t portrait_x_end = (uint16_t)(((uint32_t)x + width + 7u) & ~7u);
  if (portrait_x_end > INK_HW_WIDTH) portrait_x_end = INK_HW_WIDTH;

  uint16_t native_x = y & (uint16_t)~7u;
  uint16_t native_x_end =
      (uint16_t)(((uint32_t)y + height + 7u) & ~7u);
  if (native_x_end > NATIVE_WIDTH) native_x_end = NATIVE_WIDTH;
  const uint16_t native_y = INK_HW_WIDTH - portrait_x_end;
  const uint16_t native_width = native_x_end - native_x;
  const uint16_t native_height = portrait_x_end - portrait_x;

  convert(buffer);
  ESP_RETURN_ON_ERROR(init_sequence(false, NULL, NULL), TAG, "partial init");
  ESP_RETURN_ON_ERROR(command(0x18), TAG, "partial temp cmd");
  ESP_RETURN_ON_ERROR(data_byte(0x80), TAG, "partial temp data");
  ESP_RETURN_ON_ERROR(command(0x3c), TAG, "partial border cmd");
  ESP_RETURN_ON_ERROR(data_byte(0x80), TAG, "partial border data");
  ESP_RETURN_ON_ERROR(set_native_window(native_x, native_y, native_width,
                                        native_height),
                      TAG, "partial window");
  ESP_RETURN_ON_ERROR(command(0x24), TAG, "partial current cmd");
  ESP_RETURN_ON_ERROR(write_native_area(s_native, native_x, native_y,
                                        native_width, native_height),
                      TAG, "partial current area");
  ESP_RETURN_ON_ERROR(command(0x26), TAG, "partial previous cmd");
  ESP_RETURN_ON_ERROR(write_native_area(s_shadow, native_x, native_y,
                                        native_width, native_height),
                      TAG, "partial previous area");
  ESP_RETURN_ON_ERROR(command(0x22), TAG, "partial update cmd");
  ESP_RETURN_ON_ERROR(data_byte(0xff), TAG, "partial update mode");
  ESP_RETURN_ON_ERROR(command(0x20), TAG, "partial activate");
  ESP_RETURN_ON_ERROR(wait_ready("partial_update", NULL, NULL, false), TAG,
                      "partial update timeout");
  copy_native_area(s_shadow, s_native, native_x, native_y, native_width,
                   native_height);
  return ESP_OK;
}
esp_err_t ink_hw_gray_refresh(const uint8_t *lsb, size_t ll, const uint8_t *msb,
                              size_t ml) {
  return ink_hw_gray_refresh_with_poll(lsb, ll, msb, ml, NULL, NULL);
}

esp_err_t ink_hw_gray_refresh_with_poll(
    const uint8_t *lsb, size_t ll, const uint8_t *msb, size_t ml,
    ink_hw_refresh_poll_fn poll, void *context) {
  if (!s_initialized || !lsb || !msb || ll < INK_HW_BUFFER_SIZE ||
      ml < INK_HW_BUFFER_SIZE)
    return ESP_ERR_INVALID_ARG;
  if (poll && poll(context)) return ESP_ERR_NOT_FINISHED;
  esp_err_t ret = init_sequence(true, poll, context);
  if (ret != ESP_OK) return ret;
  if (poll && poll(context)) return ESP_ERR_NOT_FINISHED;
  ESP_RETURN_ON_ERROR(write_plane(msb, 0x26), TAG, "gray msb");
  if (poll && poll(context)) return ESP_ERR_NOT_FINISHED;
  ESP_RETURN_ON_ERROR(write_plane(lsb, 0x24), TAG, "gray lsb");
  if (poll && poll(context)) return ESP_ERR_NOT_FINISHED;
  ESP_RETURN_ON_ERROR(update(kGrayUpdateMode, true, poll, context), TAG,
                      "gray update");
  memcpy(s_shadow, s_native, NATIVE_SIZE);
  return ESP_OK;
}
esp_err_t ink_hw_sleep(void) {
  if (!s_initialized) return ESP_ERR_INVALID_STATE;
  ESP_RETURN_ON_ERROR(command(0x10), TAG, "sleep");
  return data_byte(1);
}

bool ink_hw_self_test(void) {
  return sizeof(kGrayLut) == 112U && kGrayLut[0] == 0x80U &&
         kGrayLut[1] == 0x48U && kGrayLut[2] == 0x4AU &&
         kGrayLut[105] == 0x17U && kGrayLut[106] == 0x41U &&
         kGrayLut[107] == 0xA8U && kGrayLut[108] == 0x32U &&
         kGrayLut[109] == 0x30U && kGrayTemperatureCommand == 0x18U &&
         kGrayTemperatureValue == 0x80U && kGrayUpdateMode == 0xC7U;
}
