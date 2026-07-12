#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_hw.h"
#include "ink_input.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "launcher";

void app_main(void)
{
    ESP_LOGI(TAG, "APP_START name=launcher");
    if (!ink_epd_ui_self_test() || !ink_input_self_test()) {
        ESP_LOGE(TAG, "component self test failed");
        return;
    }
    uint8_t *framebuffer = heap_caps_malloc(INK_EPD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!framebuffer) {
        ESP_LOGE(TAG, "framebuffer allocation failed");
        return;
    }
    esp_err_t ret = ink_hw_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "display init failed err=%s", esp_err_to_name(ret)); return; }
    ret = ink_input_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "input init failed err=%s", esp_err_to_name(ret)); return; }

    int selected = 0;
    ink_epd_ui_draw_launcher(framebuffer, INK_EPD_BUFFER_SIZE, selected);
    ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
    if (ret != ESP_OK) ESP_LOGE(TAG, "launcher refresh failed err=%s", esp_err_to_name(ret));

    while (true) {
        ink_input_snapshot_t input = {0};
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (ink_input_poll(now_ms, &input) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (ink_input_was_pressed(&input, INK_BUTTON_LEFT) || ink_input_was_pressed(&input, INK_BUTTON_RIGHT)) {
            selected = 1 - selected;
            ink_epd_ui_draw_launcher(framebuffer, INK_EPD_BUFFER_SIZE, selected);
            ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
            if (ret != ESP_OK) ESP_LOGE(TAG, "selection refresh failed err=%s", esp_err_to_name(ret));
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
