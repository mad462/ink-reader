#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_hw.h"
#include "ink_input.h"

static const char *TAG = "wifi_setup";

static void show_boot_loading(uint8_t *framebuffer, const char *from,
                              const char *to) {
  ink_epd_ui_draw_loading(framebuffer, INK_EPD_BUFFER_SIZE);
  const ink_epd_region_t region = ink_epd_ui_loading_region();
  const esp_err_t ret = ink_hw_partial_refresh_area(
      framebuffer, INK_EPD_BUFFER_SIZE, (uint16_t)region.x,
      (uint16_t)region.y, (uint16_t)region.width, (uint16_t)region.height);
  ESP_LOGI(TAG, "BOOT_LOADING from=%s to=%s refresh=%s", from, to,
           ret == ESP_OK ? "ok" : "failed");
}

void app_main(void) {
  ESP_LOGI(TAG, "APP_START name=wifi_setup");
  uint8_t *framebuffer = heap_caps_malloc(INK_EPD_BUFFER_SIZE,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!framebuffer) {
    ESP_LOGE(TAG, "framebuffer allocation failed");
    return;
  }
  esp_err_t ret = ink_hw_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "display init failed err=%s", esp_err_to_name(ret));
    return;
  }
  ret = ink_input_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "input init failed err=%s", esp_err_to_name(ret));
    return;
  }

  ink_epd_ui_draw_status(framebuffer, INK_EPD_BUFFER_SIZE, "WIFI SETUP",
                         "WIFI SETUP / NOT READY");
  ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
  if (ret != ESP_OK)
    ESP_LOGE(TAG, "placeholder refresh failed err=%s", esp_err_to_name(ret));

  while (true) {
    ink_input_snapshot_t input = {0};
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (ink_input_poll(now_ms, &input) == ESP_OK &&
        ink_input_was_pressed(&input, INK_BUTTON_BACK)) {
      show_boot_loading(framebuffer, "wifi_setup", "launcher");
      ESP_LOGI(TAG, "BOOT_SWITCH from=wifi_setup to=launcher");
      (void)ink_boot_switch_to_launcher();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
