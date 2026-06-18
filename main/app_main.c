#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
#include "ink_file_browser.h"
#include "ink_runtime_shell.h"
#include "ink_txt_reader.h"

static const char *TAG = "ink_reader";
static const char *kMountPoint = "/sdcard";

static bool page_shows_live_button_state(ink_runtime_shell_page_t page);
static bool find_changed_region(
    const uint8_t *previous,
    const uint8_t *current,
    size_t length,
    uint16_t *x,
    uint16_t *y,
    uint16_t *width,
    uint16_t *height
);
static const char *shell_page_name(ink_runtime_shell_page_t page)
{
    switch (page) {
        case INK_RUNTIME_SHELL_PAGE_HOME:
            return "HOME";
        case INK_RUNTIME_SHELL_PAGE_BUTTON_TEST:
            return "BUTTON_TEST";
        case INK_RUNTIME_SHELL_PAGE_FILE_BROWSER:
            return "FILE_BROWSER";
        case INK_RUNTIME_SHELL_PAGE_TXT_READER:
            return "TXT_READER";
        default:
            return "UNKNOWN";
    }
}

static const char *shell_command_name(ink_runtime_shell_command_t command)
{
    switch (command) {
        case INK_RUNTIME_SHELL_COMMAND_NONE:
            return "NONE";
        case INK_RUNTIME_SHELL_COMMAND_BACK:
            return "BACK";
        case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
            return "CONFIRM";
        case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
            return "NAV_PREVIOUS";
        case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
            return "NAV_NEXT";
        default:
            return "UNKNOWN";
    }
}

static void log_button_snapshot(uint32_t now_ms, const ink_button_snapshot_t *snapshot)
{
    if (snapshot == NULL || (snapshot->pressed_mask == 0 && snapshot->released_mask == 0)) {
        return;
    }

    ESP_LOGI(
        TAG,
        "button event t=%ums stable=0x%02x pressed=0x%02x released=0x%02x L=%u R=%u C=%u B=%u P=%u",
        (unsigned)now_ms,
        (unsigned)snapshot->stable_mask,
        (unsigned)snapshot->pressed_mask,
        (unsigned)snapshot->released_mask,
        (unsigned)snapshot->held_duration_ms[INK_RAW_BUTTON_LEFT],
        (unsigned)snapshot->held_duration_ms[INK_RAW_BUTTON_RIGHT],
        (unsigned)snapshot->held_duration_ms[INK_RAW_BUTTON_CONFIRM],
        (unsigned)snapshot->held_duration_ms[INK_RAW_BUTTON_BACK],
        (unsigned)snapshot->held_duration_ms[INK_RAW_BUTTON_POWER]
    );
}

static void log_shell_command(
    uint32_t now_ms,
    ink_runtime_shell_page_t page,
    ink_runtime_shell_command_t command)
{
    if (command == INK_RUNTIME_SHELL_COMMAND_NONE) {
        return;
    }

    ESP_LOGI(
        TAG,
        "command t=%ums page=%s cmd=%s",
        (unsigned)now_ms,
        shell_page_name(page),
        shell_command_name(command)
    );
}

static void log_render_timing(
    uint32_t now_ms,
    ink_runtime_shell_page_t page,
    bool full_refresh,
    uint32_t duration_ms,
    esp_err_t ret)
{
    ESP_LOGI(
        TAG,
        "render t=%ums page=%s mode=%s duration=%ums ret=%s",
        (unsigned)now_ms,
        shell_page_name(page),
        full_refresh ? "full" : "partial",
        (unsigned)duration_ms,
        esp_err_to_name(ret)
    );
}

static bool app_main_debug_self_test(void)
{
    if (strcmp(shell_page_name(INK_RUNTIME_SHELL_PAGE_HOME), "HOME") != 0) {
        return false;
    }
    if (strcmp(shell_page_name(INK_RUNTIME_SHELL_PAGE_TXT_READER), "TXT_READER") != 0) {
        return false;
    }
    if (strcmp(shell_command_name(INK_RUNTIME_SHELL_COMMAND_NAV_NEXT), "NAV_NEXT") != 0) {
        return false;
    }
    if (strcmp(shell_command_name(INK_RUNTIME_SHELL_COMMAND_BACK), "BACK") != 0) {
        return false;
    }
    if (page_shows_live_button_state(INK_RUNTIME_SHELL_PAGE_HOME)) {
        return false;
    }
    if (!page_shows_live_button_state(INK_RUNTIME_SHELL_PAGE_BUTTON_TEST)) {
        return false;
    }
    return true;
}

static bool page_shows_live_button_state(ink_runtime_shell_page_t page)
{
    return page == INK_RUNTIME_SHELL_PAGE_BUTTON_TEST;
}

static bool find_changed_region(
    const uint8_t *previous,
    const uint8_t *current,
    size_t length,
    uint16_t *x,
    uint16_t *y,
    uint16_t *width,
    uint16_t *height)
{
    if (previous == NULL
        || current == NULL
        || x == NULL
        || y == NULL
        || width == NULL
        || height == NULL
        || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return false;
    }

    bool found = false;
    uint16_t min_x = EPD_GDEY0426T82_WIDTH;
    uint16_t min_y = EPD_GDEY0426T82_HEIGHT;
    uint16_t max_x = 0;
    uint16_t max_y = 0;
    const size_t stride = EPD_GDEY0426T82_WIDTH / 8U;

    for (uint16_t row = 0; row < EPD_GDEY0426T82_HEIGHT; ++row) {
        for (uint16_t column_byte = 0; column_byte < stride; ++column_byte) {
            const size_t index = (size_t)row * stride + column_byte;
            const uint8_t diff = previous[index] ^ current[index];
            if (diff == 0) {
                continue;
            }

            uint16_t first_bit = 0;
            while (first_bit < 8U && (diff & (uint8_t)(0x80U >> first_bit)) == 0U) {
                ++first_bit;
            }

            int16_t last_bit = 7;
            while (last_bit >= 0 && (diff & (uint8_t)(0x80U >> last_bit)) == 0U) {
                --last_bit;
            }

            const uint16_t left = (uint16_t)(column_byte * 8U + first_bit);
            const uint16_t right = (uint16_t)(column_byte * 8U + (uint16_t)last_bit);

            if (!found) {
                min_x = left;
                max_x = right;
                min_y = row;
                max_y = row;
                found = true;
            } else {
                if (left < min_x) {
                    min_x = left;
                }
                if (right > max_x) {
                    max_x = right;
                }
                if (row < min_y) {
                    min_y = row;
                }
                if (row > max_y) {
                    max_y = row;
                }
            }
        }
    }

    if (!found) {
        return false;
    }

    *x = min_x;
    *y = min_y;
    *width = (uint16_t)(max_x - min_x + 1U);
    *height = (uint16_t)(max_y - min_y + 1U);
    return true;
}

static bool app_main_partial_region_self_test(void)
{
    static uint8_t previous[EPD_GDEY0426T82_BUFFER_SIZE];
    static uint8_t current[EPD_GDEY0426T82_BUFFER_SIZE];
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t width = 0;
    uint16_t height = 0;

    memset(previous, 0xFF, sizeof(previous));
    memset(current, 0xFF, sizeof(current));

    if (find_changed_region(previous, current, sizeof(previous), &x, &y, &width, &height)) {
        return false;
    }

    current[(115U * (EPD_GDEY0426T82_WIDTH / 8U)) + 4U] = 0x7F;
    current[(185U * (EPD_GDEY0426T82_WIDTH / 8U)) + 20U] = 0xFE;

    if (!find_changed_region(previous, current, sizeof(previous), &x, &y, &width, &height)) {
        return false;
    }
    if (x != 32U || y != 115U || width != 136U || height != 71U) {
        return false;
    }

    return true;
}

static void prepare_browser_fallback(ink_file_browser_t *browser)
{
    memset(browser, 0, sizeof(*browser));
    snprintf(browser->mount_point, sizeof(browser->mount_point), "%s", kMountPoint);
    snprintf(browser->current_path, sizeof(browser->current_path), "%s", kMountPoint);
}

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

static esp_err_t sd_card_mount_with_retry(void)
{
    esp_err_t ret = ESP_FAIL;

    for (int attempt = 1; attempt <= 3; ++attempt) {
        ret = sd_card_mount_and_list_root();
        if (ret == ESP_OK) {
            return ret;
        }

        ESP_LOGW(TAG, "TF mount attempt %d failed: %s", attempt, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    return ret;
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
    uint8_t *previous_framebuffer,
    ink_runtime_shell_t *shell,
    const ink_file_browser_t *browser,
    const ink_txt_reader_t *reader)
{
    ink_runtime_shell_view_t view;
    const bool full_refresh = ink_runtime_shell_requires_full_refresh(shell);
    const uint32_t start_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());

    ink_runtime_shell_render(shell, browser, reader, &view);
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

    esp_err_t ret = ESP_OK;
    if (full_refresh || previous_framebuffer == NULL) {
        ret = epd_gdey0426t82_full_refresh(framebuffer, framebuffer_length);
    } else {
        uint16_t dirty_x = 0;
        uint16_t dirty_y = 0;
        uint16_t dirty_width = 0;
        uint16_t dirty_height = 0;
        if (find_changed_region(
            previous_framebuffer,
            framebuffer,
            framebuffer_length,
            &dirty_x,
            &dirty_y,
            &dirty_width,
            &dirty_height)) {
            ret = epd_gdey0426t82_partial_refresh_area(
                framebuffer,
                framebuffer_length,
                dirty_x,
                dirty_y,
                dirty_width,
                dirty_height
            );
        }
    }
    const uint32_t end_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    log_render_timing(start_ms, shell->page, full_refresh, end_ms - start_ms, ret);
    if (ret == ESP_OK) {
        if (previous_framebuffer != NULL) {
            memcpy(previous_framebuffer, framebuffer, framebuffer_length);
        }
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
    static ink_txt_reader_t reader;
    static ink_file_browser_t browser;
    static ink_runtime_shell_t shell;
    static ink_button_snapshot_t snapshot;
    static ink_runtime_shell_button_state_t buttons;
    uint8_t *previous_framebuffer = heap_caps_malloc(
        EPD_GDEY0426T82_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    uint32_t last_render_ms = 0;
    bool tf_ready = false;
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
    if (previous_framebuffer == NULL) {
        ESP_LOGW(TAG, "previous framebuffer PSRAM allocation failed, retrying in internal RAM");
        previous_framebuffer = heap_caps_malloc(
            EPD_GDEY0426T82_BUFFER_SIZE,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        );
    }

    ESP_ERROR_CHECK(framebuffer != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(previous_framebuffer != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    memset(previous_framebuffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    ESP_ERROR_CHECK(epd_test_pattern_gray_demo_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_button_input_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_file_browser_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_txt_reader_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_runtime_shell_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(app_main_debug_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(app_main_partial_region_self_test() ? ESP_OK : ESP_FAIL);

    ESP_LOGI(TAG, "mounting TF card over SDMMC");
    tf_ready = sd_card_mount_with_retry() == ESP_OK;
    if (!tf_ready) {
        ESP_LOGE(TAG, "TF mount failed after retries, booting with empty browser");
    }

    ESP_LOGI(TAG, "preparing TXT reader");
    ink_txt_reader_prepare_default(&reader);

    ESP_LOGI(TAG, "scanning TF browser root");
    prepare_browser_fallback(&browser);
    if (tf_ready && ink_file_browser_init(&browser, kMountPoint, kMountPoint) != ESP_OK) {
        ESP_LOGE(TAG, "file browser init failed, keeping empty browser");
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
        previous_framebuffer,
        &shell,
        &browser,
        &reader
    ));

    for (;;) {
        const uint32_t now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
        bool dirty = false;

        ESP_ERROR_CHECK(ink_button_input_poll(&snapshot, now_ms));
        log_button_snapshot(now_ms, &snapshot);
        button_state_from_snapshot(&snapshot, &buttons);

        if (snapshot.pressed_mask != 0 || snapshot.released_mask != 0) {
            bool button_state_dirty = ink_runtime_shell_note_buttons(&shell, &buttons);
            if (page_shows_live_button_state(shell.page)) {
                dirty |= button_state_dirty;
            }
        } else if (shell.page == INK_RUNTIME_SHELL_PAGE_BUTTON_TEST
            && snapshot.stable_mask != 0
            && (uint32_t)(now_ms - last_render_ms) >= 250U) {
            dirty |= ink_runtime_shell_note_buttons(&shell, &buttons);
        }

        ink_runtime_shell_command_t command = command_from_snapshot(&snapshot);
        log_shell_command(now_ms, shell.page, command);
        if (command != INK_RUNTIME_SHELL_COMMAND_NONE) {
            bool browser_dirty = false;

            if (shell.page == INK_RUNTIME_SHELL_PAGE_FILE_BROWSER) {
                if (command == INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS) {
                    browser_dirty = ink_file_browser_move_previous(&browser);
                } else if (command == INK_RUNTIME_SHELL_COMMAND_NAV_NEXT) {
                    browser_dirty = ink_file_browser_move_next(&browser);
                } else if (command == INK_RUNTIME_SHELL_COMMAND_BACK) {
                    if (!ink_file_browser_go_parent(&browser)) {
                        shell.page = INK_RUNTIME_SHELL_PAGE_HOME;
                        shell.full_refresh_requested = true;
                        browser_dirty = true;
                    } else {
                        shell.full_refresh_requested = true;
                        browser_dirty = true;
                    }
                } else if (command == INK_RUNTIME_SHELL_COMMAND_CONFIRM) {
                    bool entered_directory = false;
                    bool selected_file = false;
                    ESP_ERROR_CHECK(ink_file_browser_confirm(&browser, &entered_directory, &selected_file));
                    if (entered_directory) {
                        shell.full_refresh_requested = true;
                        browser_dirty = true;
                    }
                    if (selected_file) {
                        if (!tf_ready || ink_txt_reader_load_file(&reader, browser.selected_file_path) != ESP_OK) {
                            ESP_LOGW(TAG, "selected TXT reader load failed");
                            ink_txt_reader_prepare_default(&reader);
                            strcpy(reader.pages[0][0], "OPEN FAILED");
                            strcpy(reader.pages[0][1], "CHECK TF FILE");
                        }
                        shell.page = INK_RUNTIME_SHELL_PAGE_TXT_READER;
                        shell.full_refresh_requested = true;
                        browser_dirty = true;
                    }
                }
            } else if (shell.page == INK_RUNTIME_SHELL_PAGE_TXT_READER) {
                if (command == INK_RUNTIME_SHELL_COMMAND_BACK) {
                    shell.page = INK_RUNTIME_SHELL_PAGE_FILE_BROWSER;
                    shell.full_refresh_requested = true;
                    browser_dirty = true;
                } else if (command == INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS) {
                    browser_dirty = ink_txt_reader_page_previous(&reader);
                } else if (command == INK_RUNTIME_SHELL_COMMAND_NAV_NEXT) {
                    browser_dirty = ink_txt_reader_page_next(&reader);
                }
            } else {
                browser_dirty = ink_runtime_shell_handle_command(&shell, command);
            }

            dirty |= browser_dirty;
            if (page_shows_live_button_state(shell.page)) {
                dirty |= ink_runtime_shell_note_buttons(&shell, &buttons);
            } else {
                (void)ink_runtime_shell_note_buttons(&shell, &buttons);
            }
        }

        if (dirty) {
            ESP_ERROR_CHECK(render_shell_page(
                framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                previous_framebuffer,
                &shell,
                &browser,
                &reader
            ));
            last_render_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
