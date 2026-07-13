#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_fonts.h"
#include "ink_hw.h"
#include "ink_input.h"
#include "ink_sd.h"

static const char *TAG = "launcher";
static ink_cpfont_t s_menu_font;
static ink_cpfont_t s_footer_font;

static int launcher_committed_selection(int current, int candidate,
                                        bool refresh_succeeded) {
  return refresh_succeeded ? candidate : current;
}

static bool launcher_policy_self_test(void) {
  return launcher_committed_selection(0, 1, false) == 0 &&
         launcher_committed_selection(0, 1, true) == 1;
}

void app_main(void) {
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

  const esp_err_t sd_ret = ink_sd_mount();
  ink_epd_ui_fonts_t fonts = {0};
  if (sd_ret == ESP_OK) {
    if (ink_fonts_load(&s_menu_font, INK_FONT_MENU)) {
      fonts.title = &s_menu_font;
      fonts.body = &s_menu_font;
    } else {
      ESP_LOGW(TAG, "menu font not found; using ASCII launcher");
    }
    if (ink_fonts_load(&s_footer_font, INK_FONT_FOOTER))
      fonts.footer = &s_footer_font;
    else
      ESP_LOGW(TAG, "footer font not found; using ASCII launcher metadata");
  } else {
    ESP_LOGW(TAG, "font SD mount failed err=%s; using ASCII launcher",
             esp_err_to_name(sd_ret));
  }

  int selected = 0;
  ink_epd_ui_draw_launcher_with_fonts(framebuffer, INK_EPD_BUFFER_SIZE,
                                      selected, &fonts);
  ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
  if (ret != ESP_OK)
    ESP_LOGE(TAG, "launcher refresh failed err=%s", esp_err_to_name(ret));

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
      ink_epd_ui_draw_launcher_with_fonts(framebuffer, INK_EPD_BUFFER_SIZE,
                                          candidate, &fonts);
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
        ink_epd_ui_draw_launcher_with_fonts(framebuffer, INK_EPD_BUFFER_SIZE,
                                            selected, &fonts);
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
