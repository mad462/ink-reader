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

static int launcher_page_item_count(ink_epd_ui_launcher_page_t page) {
  return page == INK_EPD_UI_LAUNCHER_PAGE_SETTINGS
             ? INK_EPD_UI_LAUNCHER_SETTINGS_ITEM_COUNT
             : INK_EPD_UI_LAUNCHER_MAIN_ITEM_COUNT;
}

static int launcher_wrapped_selection(ink_epd_ui_launcher_page_t page,
                                      int selected, int delta) {
  const int count = launcher_page_item_count(page);
  return (selected + delta + count) % count;
}

static bool launcher_policy_self_test(void) {
  return launcher_committed_selection(0, 1, false) == 0 &&
         launcher_committed_selection(0, 1, true) == 1 &&
         launcher_wrapped_selection(INK_EPD_UI_LAUNCHER_PAGE_MAIN, 0, -1) ==
             2 &&
         launcher_wrapped_selection(INK_EPD_UI_LAUNCHER_PAGE_SETTINGS, 1,
                                    1) == 0;
}

static void show_boot_loading(uint8_t *framebuffer, const char *from,
                              const char *to) {
  if (!framebuffer) {
    ESP_LOGI(TAG, "BOOT_LOADING from=%s to=%s refresh=%s", from, to,
             "skipped");
    return;
  }
  ink_epd_ui_draw_loading(framebuffer, INK_EPD_BUFFER_SIZE);
  const ink_epd_region_t region = ink_epd_ui_loading_region();
  const esp_err_t ret = ink_hw_partial_refresh_area(
      framebuffer, INK_EPD_BUFFER_SIZE, (uint16_t)region.x,
      (uint16_t)region.y, (uint16_t)region.width, (uint16_t)region.height);
  ESP_LOGI(TAG, "BOOT_LOADING from=%s to=%s refresh=%s", from, to,
           ret == ESP_OK ? "ok" : "failed");
  if (ret != ESP_OK)
    ESP_LOGW(TAG, "boot loading refresh failed err=%s", esp_err_to_name(ret));
}

static void boot_reader(uint8_t *framebuffer) {
  show_boot_loading(framebuffer, "launcher", "reader");
  ESP_LOGI(TAG, "BOOT_SWITCH from=launcher to=reader");
  (void)ink_boot_switch_to_reader();
}

static void boot_photo(uint8_t *framebuffer) {
  show_boot_loading(framebuffer, "launcher", "photo");
  ESP_LOGI(TAG, "BOOT_SWITCH from=launcher to=photo");
  (void)ink_boot_switch_to_photo();
}

static void boot_usb_msc(uint8_t *framebuffer) {
  show_boot_loading(framebuffer, "launcher", "usb_msc");
  ESP_LOGI(TAG, "BOOT_SWITCH from=launcher to=usb_msc");
  (void)ink_boot_switch_to_usb_msc();
}

static void boot_wifi_setup(uint8_t *framebuffer) {
  show_boot_loading(framebuffer, "launcher", "wifi_setup");
  ESP_LOGI(TAG, "BOOT_SWITCH from=launcher to=wifi_setup");
  (void)ink_boot_switch_to_wifi_setup();
}

static esp_err_t refresh_page(uint8_t *framebuffer,
                              ink_epd_ui_launcher_page_t page, int selected) {
  ink_epd_ui_draw_launcher_page(framebuffer, INK_EPD_BUFFER_SIZE, page,
                                selected);
  return ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
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

  ink_epd_ui_launcher_page_t page = INK_EPD_UI_LAUNCHER_PAGE_MAIN;
  int selected = 0;
  ink_epd_ui_draw_launcher_page(framebuffer, INK_EPD_BUFFER_SIZE, page,
                                selected);
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

    int delta = 0;
    if (ink_input_was_pressed(&input, INK_BUTTON_LEFT)) delta = -1;
    if (ink_input_was_pressed(&input, INK_BUTTON_RIGHT)) delta = 1;
    if (delta != 0) {
      const int previous = selected;
      const int candidate = launcher_wrapped_selection(page, selected, delta);
      ink_epd_ui_draw_launcher_page(framebuffer, INK_EPD_BUFFER_SIZE, page,
                                    candidate);
      const ink_epd_region_t region =
          ink_epd_ui_launcher_page_selection_region(page, previous, candidate);
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
      }
      selected = launcher_committed_selection(previous, candidate,
                                              ret == ESP_OK);
      if (ret != ESP_OK)
        ink_epd_ui_draw_launcher_page(framebuffer, INK_EPD_BUFFER_SIZE, page,
                                      selected);
    }

    if (ink_input_was_pressed(&input, INK_BUTTON_BACK) &&
        page == INK_EPD_UI_LAUNCHER_PAGE_SETTINGS) {
      page = INK_EPD_UI_LAUNCHER_PAGE_MAIN;
      selected = 2;
      ret = refresh_page(framebuffer, page, selected);
      if (ret != ESP_OK)
        ESP_LOGE(TAG, "main page refresh failed err=%s", esp_err_to_name(ret));
    }

    if (ink_input_was_pressed(&input, INK_BUTTON_CONFIRM)) {
      if (page == INK_EPD_UI_LAUNCHER_PAGE_MAIN) {
        if (selected == 0)
          boot_reader(framebuffer);
        else if (selected == 1)
          boot_photo(framebuffer);
        else {
          page = INK_EPD_UI_LAUNCHER_PAGE_SETTINGS;
          selected = 0;
          ret = refresh_page(framebuffer, page, selected);
          if (ret != ESP_OK)
            ESP_LOGE(TAG, "settings page refresh failed err=%s",
                     esp_err_to_name(ret));
        }
      } else if (selected == 0) {
        boot_usb_msc(framebuffer);
      } else {
        boot_wifi_setup(framebuffer);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
