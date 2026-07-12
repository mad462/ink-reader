#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_hw.h"
#include "ink_input.h"
#include "ink_sd.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "reader";

void app_main(void)
{
    ESP_LOGI(TAG, "APP_START name=reader");
    uint8_t *framebuffer = heap_caps_malloc(INK_EPD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!framebuffer) { ESP_LOGE(TAG, "framebuffer allocation failed"); return; }

    esp_err_t display_ret = ink_hw_init();
    if (display_ret != ESP_OK) ESP_LOGE(TAG, "display init failed err=%s", esp_err_to_name(display_ret));
    esp_err_t input_ret = ink_input_init();
    if (input_ret != ESP_OK) ESP_LOGE(TAG, "input init failed err=%s", esp_err_to_name(input_ret));
    esp_err_t sd_ret = ink_sd_mount();

    ink_epd_ui_draw_status(
        framebuffer,
        INK_EPD_BUFFER_SIZE,
        "READER",
        sd_ret == ESP_OK ? "NO BOOKS FOUND" : "SD CARD ERROR");
    if (display_ret == ESP_OK) {
        esp_err_t ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
        if (ret != ESP_OK) ESP_LOGE(TAG, "status refresh failed err=%s", esp_err_to_name(ret));
    }

    bool back_latched = false;
    while (true) {
        ink_input_snapshot_t input = {0};
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (input_ret == ESP_OK && ink_input_poll(now_ms, &input) == ESP_OK) {
            if (!back_latched && ink_input_held_ms(&input, INK_BUTTON_BACK) >= 1200) {
                back_latched = true;
                ESP_LOGI(TAG, "BOOT_SWITCH from=reader to=launcher");
                esp_err_t ret = ink_boot_switch_to_launcher();
                if (ret != ESP_OK) ESP_LOGE(TAG, "return to launcher failed err=%s", esp_err_to_name(ret));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
