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

static const char *TAG = "ink_reader";
static const char *kMountPoint = "/sdcard";

enum {
    TXT_PREVIEW_TEXT_LINES = 4,
    TXT_PREVIEW_LINE_LENGTH = 23,
    TXT_PREVIEW_TITLE_LENGTH = 20,
    TXT_PREVIEW_READ_BYTES = 512,
};

typedef struct {
    char title[TXT_PREVIEW_TITLE_LENGTH + 1];
    char lines[TXT_PREVIEW_TEXT_LINES][TXT_PREVIEW_LINE_LENGTH + 1];
    char status[TXT_PREVIEW_LINE_LENGTH + 1];
    size_t non_ascii_bytes;
    bool found_file;
} txt_preview_t;

static bool is_ascii_printable(unsigned char c)
{
    return c >= 32 && c <= 126;
}

static char sanitize_preview_char(unsigned char c)
{
    if (c == '\t') {
        return ' ';
    }
    if (is_ascii_printable(c)) {
        return (char)c;
    }
    return '#';
}

static bool ascii_char_equal_ignore_case(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
    }
    return a == b;
}

static bool has_txt_extension(const char *name)
{
    size_t len = strlen(name);
    if (len < 4) {
        return false;
    }

    const char *ext = name + len - 4;
    return ext[0] == '.'
        && ascii_char_equal_ignore_case(ext[1], 't')
        && ascii_char_equal_ignore_case(ext[2], 'x')
        && ascii_char_equal_ignore_case(ext[3], 't');
}

static void sanitize_ascii_snippet(const char *src, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    size_t out = 0;
    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; ++i) {
        dst[out++] = sanitize_preview_char((unsigned char)src[i]);
    }
    dst[out] = '\0';
}

static void trim_trailing_spaces(char *text)
{
    size_t len = strlen(text);
    while (len > 0 && text[len - 1] == ' ') {
        text[--len] = '\0';
    }
}

static void prepare_default_preview(txt_preview_t *preview)
{
    memset(preview, 0, sizeof(*preview));
    strcpy(preview->title, "TXT PREVIEW");
    strcpy(preview->lines[0], "NO TXT FILE FOUND");
    strcpy(preview->lines[1], "PUT A FAT32 TXT IN TF");
    strcpy(preview->lines[2], "UTF-8 CHARS -> #");
    strcpy(preview->status, "WAITING FOR SAMPLE");
}

static esp_err_t find_first_txt_file(char *path, size_t path_size, char *name, size_t name_size)
{
    DIR *dir = opendir(kMountPoint);
    if (dir == NULL) {
        ESP_LOGE(TAG, "opendir(%s) failed during TXT scan: errno=%d", kMountPoint, errno);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!has_txt_extension(entry->d_name)) {
            continue;
        }

        snprintf(path, path_size, "%s/%s", kMountPoint, entry->d_name);
        sanitize_ascii_snippet(entry->d_name, name, name_size);
        ret = ESP_OK;
        break;
    }

    closedir(dir);
    return ret;
}

static void append_preview_char(txt_preview_t *preview, size_t *line_index, size_t *column, char c)
{
    if (*line_index >= TXT_PREVIEW_TEXT_LINES) {
        return;
    }

    if (c == '\r') {
        return;
    }

    if (c == '\n') {
        trim_trailing_spaces(preview->lines[*line_index]);
        ++(*line_index);
        *column = 0;
        return;
    }

    if (*column >= TXT_PREVIEW_LINE_LENGTH) {
        trim_trailing_spaces(preview->lines[*line_index]);
        ++(*line_index);
        *column = 0;
        if (*line_index >= TXT_PREVIEW_TEXT_LINES) {
            return;
        }
    }

    preview->lines[*line_index][(*column)++] = c;
    preview->lines[*line_index][*column] = '\0';
}

static esp_err_t load_txt_preview(txt_preview_t *preview)
{
    char txt_path[256];
    char txt_name[TXT_PREVIEW_TITLE_LENGTH + 1];

    prepare_default_preview(preview);

    esp_err_t ret = find_first_txt_file(txt_path, sizeof(txt_path), txt_name, sizeof(txt_name));
    if (ret != ESP_OK) {
        return ret;
    }

    FILE *file = fopen(txt_path, "rb");
    if (file == NULL) {
        snprintf(preview->status, sizeof(preview->status), "OPEN FAIL ERR %d", errno);
        ESP_LOGE(TAG, "fopen(%s) failed: errno=%d", txt_path, errno);
        return ESP_FAIL;
    }

    preview->found_file = true;
    strcpy(preview->title, txt_name);
    strcpy(preview->lines[0], "FILE FOUND");

    uint8_t raw[TXT_PREVIEW_READ_BYTES];
    const size_t read_bytes = fread(raw, 1, sizeof(raw), file);
    fclose(file);

    size_t line_index = 1;
    size_t column = 0;
    for (size_t i = 0; i < read_bytes; ++i) {
        const unsigned char c = raw[i];
        if (c >= 128) {
            ++preview->non_ascii_bytes;
        }
        append_preview_char(preview, &line_index, &column, sanitize_preview_char(c));
        if (line_index >= TXT_PREVIEW_TEXT_LINES) {
            break;
        }
    }

    for (size_t i = 0; i < TXT_PREVIEW_TEXT_LINES; ++i) {
        trim_trailing_spaces(preview->lines[i]);
    }

    if (preview->lines[1][0] == '\0') {
        strcpy(preview->lines[1], "EMPTY OR BINARY FILE");
    }

    if (preview->non_ascii_bytes > 0) {
        strcpy(preview->status, "UTF8 CHARS -> #");
    } else {
        strcpy(preview->status, "ASCII TEXT OK");
    }

    ESP_LOGI(TAG, "TXT preview file: %s", txt_path);
    ESP_LOGI(TAG, "TXT preview bytes read: %u", (unsigned)read_bytes);
    ESP_LOGI(TAG, "TXT preview non-ASCII bytes: %u", (unsigned)preview->non_ascii_bytes);
    return ESP_OK;
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

    txt_preview_t preview;
    uint8_t *framebuffer = heap_caps_malloc(
        EPD_GDEY0426T82_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    uint8_t *gray_lsb = heap_caps_malloc(
        EPD_GDEY0426T82_GRAY_PLANE_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    uint8_t *gray_msb = heap_caps_malloc(
        EPD_GDEY0426T82_GRAY_PLANE_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (framebuffer == NULL) {
        ESP_LOGW(TAG, "PSRAM allocation failed, retrying in internal RAM");
        framebuffer = heap_caps_malloc(
            EPD_GDEY0426T82_BUFFER_SIZE,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        );
    }
    if (gray_lsb == NULL) {
        ESP_LOGW(TAG, "gray_lsb PSRAM allocation failed, retrying in internal RAM");
        gray_lsb = heap_caps_malloc(
            EPD_GDEY0426T82_GRAY_PLANE_SIZE,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        );
    }
    if (gray_msb == NULL) {
        ESP_LOGW(TAG, "gray_msb PSRAM allocation failed, retrying in internal RAM");
        gray_msb = heap_caps_malloc(
            EPD_GDEY0426T82_GRAY_PLANE_SIZE,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        );
    }

    ESP_ERROR_CHECK(framebuffer != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(gray_lsb != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(gray_msb != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(epd_test_pattern_gray_demo_self_test() ? ESP_OK : ESP_FAIL);

    ESP_LOGI(TAG, "mounting TF card over SDMMC");
    ESP_ERROR_CHECK(sd_card_mount_and_list_root());

    ESP_LOGI(TAG, "loading TXT preview from TF");
    if (load_txt_preview(&preview) != ESP_OK) {
        ESP_LOGW(TAG, "TXT preview unavailable, using fallback page");
    }

    ESP_LOGI(TAG, "initializing GDEY0426T82 panel");
    ESP_ERROR_CHECK(epd_gdey0426t82_init(&panel));

    ESP_LOGI(TAG, "displaying grayscale diagnostic base page");
    epd_test_pattern_fill_gray_demo_bw(framebuffer, EPD_GDEY0426T82_BUFFER_SIZE, 0);
    ESP_ERROR_CHECK(epd_gdey0426t82_full_refresh(framebuffer, EPD_GDEY0426T82_BUFFER_SIZE));
    vTaskDelay(pdMS_TO_TICKS(800));

    ESP_LOGI(TAG, "displaying grayscale diagnostic planes");
    epd_test_pattern_fill_gray_demo_planes(
        gray_lsb,
        EPD_GDEY0426T82_GRAY_PLANE_SIZE,
        gray_msb,
        EPD_GDEY0426T82_GRAY_PLANE_SIZE
    );
    ESP_ERROR_CHECK(epd_gdey0426t82_gray_refresh(
        gray_lsb,
        EPD_GDEY0426T82_GRAY_PLANE_SIZE,
        gray_msb,
        EPD_GDEY0426T82_GRAY_PLANE_SIZE
    ));
    vTaskDelay(pdMS_TO_TICKS(1200));

    ESP_LOGI(TAG, "running footer loader partial refresh demo");
    for (uint8_t step = 0; step < 6; ++step) {
        epd_test_pattern_fill_gray_demo_bw(
            framebuffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            (uint8_t)(step % 3)
        );
        ESP_ERROR_CHECK(epd_gdey0426t82_partial_refresh_area(
            framebuffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            EPD_TEST_PATTERN_GRAY_LOADER_X,
            EPD_TEST_PATTERN_GRAY_LOADER_Y,
            EPD_TEST_PATTERN_GRAY_LOADER_W,
            EPD_TEST_PATTERN_GRAY_LOADER_H
        ));
        vTaskDelay(pdMS_TO_TICKS(700));
    }

    ESP_LOGI(TAG, "entering panel deep sleep");
    ESP_ERROR_CHECK(epd_gdey0426t82_sleep());

    heap_caps_free(framebuffer);
    heap_caps_free(gray_lsb);
    heap_caps_free(gray_msb);
}



