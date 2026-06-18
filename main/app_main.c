#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "epd_gdey0426t82.h"
#include "epd_test_pattern.h"
#include "ink_button_input.h"
#include "ink_runtime_shell.h"
#include "ink_txt_preview.h"

static const char *TAG = "ink_reader";
static const char *kMountPoint = "/sdcard";

static esp_err_t sd_card_mount_and_list_root(void)
{
    static const int kSdSdioClk = 40;
    static const int kSdSdioCmd = 39;
    static const int kSdSdioD0 = 41;
    static const int kSdSdioD1 = 42;
    static const int kSdSdioD2 = 48;
    static const int kSdSdioD3 = 38;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = kSdSdioClk;
    slot_config.cmd = kSdSdioCmd;
    slot_config.d0 = kSdSdioD0;
    slot_config.d1 = kSdSdioD1;
    slot_config.d2 = kSdSdioD2;
    slot_config.d3 = kSdSdioD3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TF mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TF card mounted at %s", kMountPoint);
    sdmmc_card_print_info(stdout, card);

    DIR *dir = opendir(kMountPoint);
    if (dir == NULL) {
        ESP_LOGE(TAG, "opendir(%s) failed: errno=%d", kMountPoint, errno);
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "sdcard entry: %s", entry->d_name);
    }
    closedir(dir);
    return ESP_OK;
}

static ink_runtime_shell_command_t command_from_snapshot(const ink_button_snapshot_t *snapshot)
{
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_BACK)) {
        return INK_RUNTIME_SHELL_COMMAND_BACK;
    }
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_CONFIRM)) {
        return INK_RUNTIME_SHELL_COMMAND_CONFIRM;
    }
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_NAV_PREVIOUS)) {
        return INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS;
    }
    if (ink_button_snapshot_was_pressed(snapshot, INK_LOGICAL_BUTTON_NAV_NEXT)) {
        return INK_RUNTIME_SHELL_COMMAND_NAV_NEXT;
    }
    return INK_RUNTIME_SHELL_COMMAND_NONE;
}

static void button_state_from_snapshot(
    const ink_button_snapshot_t *snapshot,
    ink_runtime_shell_button_state_t *buttons)
{
    const ink_raw_button_t raw_map[INK_RUNTIME_SHELL_BUTTON_COUNT] = {
        [INK_RUNTIME_SHELL_BUTTON_BACK] = INK_RAW_BUTTON_BACK,
        [INK_RUNTIME_SHELL_BUTTON_CONFIRM] = INK_RAW_BUTTON_CONFIRM,
        [INK_RUNTIME_SHELL_BUTTON_LEFT] = INK_RAW_BUTTON_LEFT,
        [INK_RUNTIME_SHELL_BUTTON_RIGHT] = INK_RAW_BUTTON_RIGHT,
        [INK_RUNTIME_SHELL_BUTTON_POWER] = INK_RAW_BUTTON_POWER,
    };

    for (int i = 0; i < INK_RUNTIME_SHELL_BUTTON_COUNT; ++i) {
        const uint32_t mask = ink_button_input_mask_for_raw(raw_map[i]);
        buttons->is_down[i] = (snapshot->stable_mask & mask) != 0;
        buttons->was_pressed[i] = (snapshot->pressed_mask & mask) != 0;
        buttons->was_released[i] = (snapshot->released_mask & mask) != 0;
        buttons->held_ms[i] = snapshot->held_duration_ms[raw_map[i]];
    }
}

static esp_err_t render_shell_page(
    uint8_t *framebuffer,
    size_t framebuffer_length,
    ink_runtime_shell_t *shell,
    const ink_txt_preview_t *preview)
{
    ink_runtime_shell_view_t view;

    ink_runtime_shell_render(shell, preview, &view);
    epd_test_pattern_fill_text_page(
        framebuffer,
        framebuffer_length,
        view.title,
        view.line1,
        view.line2,
        view.line3,
        view.line4,
        view.line5
    );

    esp_err_t ret = ink_runtime_shell_requires_full_refresh(shell)
        ? epd_gdey0426t82_full_refresh(framebuffer, framebuffer_length)
        : epd_gdey0426t82_partial_refresh(framebuffer, framebuffer_length);
    if (ret == ESP_OK) {
        ink_runtime_shell_mark_rendered(shell);
    }
    return ret;
}

void app_main(void)
{
    static const epd_gdey0426t82_config_t panel = {
        .gpio_mosi = 4,
        .gpio_sclk = 5,
        .gpio_cs = 6,
        .gpio_dc = 7,
        .gpio_rst = 15,
        .gpio_busy = 16,
        .spi_host = SPI2_HOST,
        .spi_clock_hz = 10 * 1000 * 1000,
    };

    ink_txt_preview_t preview;
    ink_runtime_shell_t shell;
    ink_button_snapshot_t snapshot;
    ink_runtime_shell_button_state_t buttons = {0};
    uint32_t last_render_ms = 0;
    uint8_t *framebuffer = heap_caps_malloc(
        EPD_GDEY0426T82_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (framebuffer == NULL) {
        ESP_LOGW(TAG, "PSRAM allocation failed, retrying in internal RAM");
        framebuffer = heap_caps_malloc(
            EPD_GDEY0426T82_BUFFER_SIZE,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        );
    }

    ESP_ERROR_CHECK(framebuffer != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(epd_test_pattern_gray_demo_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_button_input_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_runtime_shell_self_test() ? ESP_OK : ESP_FAIL);

    ESP_LOGI(TAG, "mounting TF card over SDMMC");
    ESP_ERROR_CHECK(sd_card_mount_and_list_root());

    ESP_LOGI(TAG, "loading TXT preview from TF");
    if (ink_txt_preview_load_from_dir(kMountPoint, &preview) != ESP_OK) {
        ESP_LOGW(TAG, "TXT preview unavailable, using fallback page");
        ink_txt_preview_prepare_default(&preview);
    }

    ESP_LOGI(TAG, "initializing GDEY0426T82 panel");
    ESP_ERROR_CHECK(epd_gdey0426t82_init(&panel));
    ESP_ERROR_CHECK(ink_button_input_init());

    ink_runtime_shell_init(&shell);
    ink_runtime_shell_note_buttons(&shell, &buttons);

    ESP_LOGI(TAG, "booting minimal CrossPoint shell");
    ESP_ERROR_CHECK(render_shell_page(
        framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        &shell,
        &preview
    ));

    for (;;) {
        const uint32_t now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
        bool dirty = false;

        ESP_ERROR_CHECK(ink_button_input_poll(&snapshot, now_ms));
        button_state_from_snapshot(&snapshot, &buttons);

        if (snapshot.pressed_mask != 0 || snapshot.released_mask != 0) {
            dirty |= ink_runtime_shell_note_buttons(&shell, &buttons);
        } else if (shell.page == INK_RUNTIME_SHELL_PAGE_BUTTON_TEST
            && snapshot.stable_mask != 0
            && (uint32_t)(now_ms - last_render_ms) >= 250U) {
            dirty |= ink_runtime_shell_note_buttons(&shell, &buttons);
        }

        ink_runtime_shell_command_t command = command_from_snapshot(&snapshot);
        if (command != INK_RUNTIME_SHELL_COMMAND_NONE) {
            dirty |= ink_runtime_shell_handle_command(&shell, command);
            dirty |= ink_runtime_shell_note_buttons(&shell, &buttons);
        }

        if (dirty) {
            ESP_ERROR_CHECK(render_shell_page(
                framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                &shell,
                &preview
            ));
            last_render_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
