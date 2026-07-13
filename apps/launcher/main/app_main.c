#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_hw.h"
#include "ink_input.h"

static const char *TAG = "launcher";

static void log_stage(const char *stage, int64_t started_us) {
  ESP_LOGI(TAG, "APP_STAGE name=launcher stage=%s elapsed_ms=%lld", stage,
           (long long)((esp_timer_get_time() - started_us) / 1000));
}

static int launcher_committed_selection(int current, int candidate,
                                        bool refresh_succeeded) {
  return refresh_succeeded ? candidate : current;
}

static bool launcher_policy_self_test(void) {
  return launcher_committed_selection(0, 1, false) == 0 &&
         launcher_committed_selection(0, 1, true) == 1;
}

void app_main(void) {
  const int64_t started_us = esp_timer_get_time();
  ESP_LOGI(TAG, "APP_START name=launcher");
  if (!ink_epd_ui_self_test() || !ink_input_self_test() ||
      !launcher_policy_self_test()) {
    ESP_LOGE(TAG, "component self test failed");
    return;
  }
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
  log_stage("hardware_ready", started_us);

  int selected = 0;
  ink_epd_ui_draw_launcher(framebuffer, INK_EPD_BUFFER_SIZE, selected);
  log_stage("frame_drawn", started_us);
  ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
  if (ret != ESP_OK)
    ESP_LOGE(TAG, "launcher refresh failed err=%s", esp_err_to_name(ret));
  else
    log_stage("first_refresh_done", started_us);

  while (true) {
    ink_input_snapshot_t input = {0};
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (ink_input_poll(now_ms, &input) != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (ink_input_was_pressed(&input, INK_BUTTON_LEFT) ||
        ink_input_was_pressed(&input, INK_BUTTON_RIGHT)) {
      const int previous = selected;
      const int candidate = 1 - selected;
      ink_epd_ui_draw_launcher(framebuffer, INK_EPD_BUFFER_SIZE, candidate);
      const ink_epd_region_t region =
          ink_epd_ui_launcher_selection_region(previous, candidate);
      ret = ink_hw_partial_refresh_area(
          framebuffer, INK_EPD_BUFFER_SIZE, (uint16_t)region.x,
          (uint16_t)region.y, (uint16_t)region.width, (uint16_t)region.height);
      if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "selection partial refresh failed x=%d y=%d w=%d h=%d "
                 "error=%s",
                 region.x, region.y, region.width, region.height,
                 esp_err_to_name(ret));
        ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
        if (ret != ESP_OK)
          ESP_LOGE(TAG, "selection full refresh fallback failed error=%s",
                   esp_err_to_name(ret));
      }
      selected = launcher_committed_selection(previous, candidate,
                                              ret == ESP_OK);
      if (ret != ESP_OK)
        ink_epd_ui_draw_launcher(framebuffer, INK_EPD_BUFFER_SIZE, selected);
    }
    if (ink_input_was_pressed(&input, INK_BUTTON_CONFIRM)) {
      if (selected == 0) {
        ESP_LOGI(TAG, "BOOT_SWITCH from=launcher to=reader");
        (void)ink_boot_switch_to_reader();
      } else {
        ESP_LOGI(TAG, "BOOT_SWITCH from=launcher to=photo");
        (void)ink_boot_switch_to_photo();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
