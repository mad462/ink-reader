#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_hw.h"
#include "ink_input.h"
#include "ink_photo_core.h"
#include "ink_sd.h"

static const char *TAG = "photo";

static bool show_photo(const ink_photo_catalog_t *catalog, size_t index,
                       uint8_t *lsb, uint8_t *msb) {
  if (!catalog || index >= catalog->count) return false;
  if (!ink_photo_decode_bmp(catalog->items[index].path, lsb,
                            INK_PHOTO_PLANE_SIZE, msb, INK_PHOTO_PLANE_SIZE)) {
    return false;
  }

  esp_err_t ret =
      ink_hw_gray_refresh(lsb, INK_PHOTO_PLANE_SIZE, msb, INK_PHOTO_PLANE_SIZE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "photo refresh failed err=%s", esp_err_to_name(ret));
    return false;
  }
  ESP_LOGI(TAG, "photo shown path=%s", catalog->items[index].path);
  return true;
}

void app_main(void) {
  ESP_LOGI(TAG, "APP_START name=photo");
  if (!ink_photo_core_self_test()) {
    ESP_LOGE(TAG, "photo component self test failed");
    return;
  }

  uint8_t *lsb = heap_caps_malloc(INK_PHOTO_PLANE_SIZE,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint8_t *msb = heap_caps_malloc(INK_PHOTO_PLANE_SIZE,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  ink_photo_catalog_t *catalog = heap_caps_calloc(
      1, sizeof(*catalog), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!lsb || !msb || !catalog) {
    ESP_LOGE(TAG, "photo buffers allocation failed");
    return;
  }

  esp_err_t display_ret = ink_hw_init();
  if (display_ret != ESP_OK) {
    ESP_LOGE(TAG, "display init failed err=%s", esp_err_to_name(display_ret));
  }
  esp_err_t input_ret = ink_input_init();
  if (input_ret != ESP_OK) {
    ESP_LOGE(TAG, "input init failed err=%s", esp_err_to_name(input_ret));
  }
  esp_err_t sd_ret = ink_sd_mount();
  bool catalog_ready = sd_ret == ESP_OK && ink_photo_catalog_load(catalog);
  size_t current = 0;
  bool image_ready = catalog_ready && catalog->count > 0 &&
                     display_ret == ESP_OK &&
                     show_photo(catalog, current, lsb, msb);

  if (!image_ready && display_ret == ESP_OK) {
    const char *message =
        sd_ret != ESP_OK
            ? "SD CARD ERROR"
            : (catalog->count == 0 ? "NO PHOTOS FOUND" : "IMAGE ERROR");
    ink_epd_ui_draw_status(lsb, INK_PHOTO_PLANE_SIZE, "PHOTO", message);
    esp_err_t ret = ink_hw_full_refresh(lsb, INK_PHOTO_PLANE_SIZE);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "status refresh failed err=%s", esp_err_to_name(ret));
    }
  }

  bool back_latched = false;
  while (true) {
    ink_input_snapshot_t input = {0};
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (input_ret == ESP_OK && ink_input_poll(now, &input) == ESP_OK) {
      bool previous = ink_input_was_pressed(&input, INK_BUTTON_LEFT);
      bool next = ink_input_was_pressed(&input, INK_BUTTON_RIGHT);
      if (catalog->count > 0 && (previous || next)) {
        current = previous ? (current == 0 ? catalog->count - 1 : current - 1)
                           : (current + 1) % catalog->count;
        if (!show_photo(catalog, current, lsb, msb)) {
          ESP_LOGE(TAG, "image load failed path=%s",
                   catalog->items[current].path);
        }
      }
      if (!back_latched && ink_input_held_ms(&input, INK_BUTTON_BACK) >= 1200) {
        back_latched = true;
        ESP_LOGI(TAG, "BOOT_SWITCH from=photo to=launcher");
        esp_err_t ret = ink_boot_switch_to_launcher();
        if (ret != ESP_OK) {
          ESP_LOGE(TAG, "return to launcher failed err=%s",
                   esp_err_to_name(ret));
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
