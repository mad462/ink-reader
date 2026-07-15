#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_hw.h"
#include "ink_input.h"
#include "ink_usb_msc_core.h"

static const char *TAG = "usb_msc";

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

static void show_usb_popup(uint8_t *framebuffer,
                           ink_epd_ui_usb_msc_popup_t popup) {
  ink_epd_ui_draw_usb_msc_popup(framebuffer, INK_EPD_BUFFER_SIZE, popup);
  const ink_epd_region_t region = ink_epd_ui_usb_msc_popup_region();
  const esp_err_t ret = ink_hw_partial_refresh_area(
      framebuffer, INK_EPD_BUFFER_SIZE, (uint16_t)region.x,
      (uint16_t)region.y, (uint16_t)region.width, (uint16_t)region.height);
  ESP_LOGI(TAG, "USB_POPUP state=%s refresh=%s",
           popup == INK_EPD_UI_USB_MSC_ACTIVE ? "active" : "failed",
           ret == ESP_OK ? "ok" : "failed");
}

void app_main(void) {
  ESP_LOGI(TAG, "APP_START name=usb_msc");
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

  memset(framebuffer, 0xff, INK_EPD_BUFFER_SIZE);
  ink_epd_ui_draw_loading(framebuffer, INK_EPD_BUFFER_SIZE);
  ret = ink_hw_set_previous_frame(framebuffer, INK_EPD_BUFFER_SIZE);
  if (ret != ESP_OK)
    ESP_LOGE(TAG, "previous frame sync failed err=%s", esp_err_to_name(ret));

  ret = ink_usb_msc_core_start();
  show_usb_popup(framebuffer, ret == ESP_OK ? INK_EPD_UI_USB_MSC_ACTIVE
                                            : INK_EPD_UI_USB_MSC_FAILED);

  while (true) {
    ink_input_snapshot_t input = {0};
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (ink_input_poll(now_ms, &input) == ESP_OK &&
        ink_input_was_pressed(&input, INK_BUTTON_BACK)) {
      ret = ink_usb_msc_core_stop();
      if (ret == ESP_OK && ink_usb_msc_core_can_return_launcher()) {
        show_boot_loading(framebuffer, "usb_msc", "launcher");
        ESP_LOGI(TAG, "BOOT_SWITCH from=usb_msc to=launcher");
        (void)ink_boot_switch_to_launcher();
      } else {
        show_usb_popup(framebuffer, INK_EPD_UI_USB_MSC_FAILED);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
