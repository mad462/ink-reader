#include "ink_app_boot.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "ink_reader";
static const char *kFixedSampleBookPaths[] = {
    "/sdcard/books/sample.xtc",
    "/sdcard/sample.xtc",
    "/sdcard/books/海底两万里.xtc",
};
static const char *kReaderFontPaths[] = {
    "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_18.cpfont",
    "/sdcard/fonts/LXGWWenKai_18.cpfont",
    "/sdcard/FONTS/LXGWWENKAI_18.CPFONT",
    "/sdcard/fonts/NotoSansSC_18.cpfont",
    "/sdcard/.fonts/NotoSansSC/NotoSansSC_18.cpfont",
};
static const char *kFooterFontPaths[] = {
    "/sdcard/fonts/SmallSimSunEmbedded_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSunEmbedded_16.cpfont",
    "/sdcard/FONTS/SMALLSIMSUNEMBEDDED_16.CPFONT",
    "/sdcard/fonts/SmallSimSunBitmap_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSunBitmap_16.cpfont",
    "/sdcard/fonts/SmallSimSun_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSun_16.cpfont",
    "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_16.cpfont",
    "/sdcard/fonts/LXGWWenKai_16.cpfont",
    "/sdcard/FONTS/LXGWWENKAI_16.CPFONT",
    "/sdcard/fonts/NotoSansSC_16.cpfont",
    "/sdcard/.fonts/NotoSansSC/NotoSansSC_16.cpfont",
};
static const char *kReaderFontDirs[] = {
    "/sdcard/fonts",
    "/sdcard/FONTS",
    "/sdcard/.fonts/LXGWWenKai",
    "/sdcard/.fonts/NotoSansSC",
};

static esp_err_t ensure_state_directory(void);
static esp_err_t ensure_books_directory(void);
static void migrate_root_books_into_library(void);
static bool has_xtc_book_extension(const char *name);
static bool find_first_xtc_book_path(char *path, size_t path_size);
static bool load_first_cpfont_from_dir(ink_cpfont_t *font, const char *dir_path);
static size_t fixed_sample_start_page(size_t total_pages);
static bool try_fixup_state_book_path(ink_app_state_t *state);
static bool load_font_from_candidates(
    ink_cpfont_t *font,
    const char *const *paths,
    size_t path_count,
    const char *const *dirs,
    size_t dir_count);

esp_err_t ink_app_persist_state(const ink_app_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(ensure_state_directory(), TAG, "state dir");
    return ink_app_state_save_file(INK_APP_STATE_FILE_PATH, state);
}

bool ink_app_should_auto_resume_reader(const ink_app_state_t *state)
{
    return state != NULL
        && state->has_open_book
        && state->open_book_kind == INK_APP_STATE_BOOK_KIND_XTC
        && state->open_book_path[0] != '\0';
}

static esp_err_t ensure_state_directory(void)
{
    struct stat st;
    if (stat("/sdcard/.ink-reader", &st) != 0) {
        if (mkdir("/sdcard/.ink-reader", 0777) != 0 && errno != EEXIST) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static esp_err_t ensure_books_directory(void)
{
    struct stat st;
    if (stat(INK_APP_BOOKS_PATH, &st) != 0) {
        if (mkdir(INK_APP_BOOKS_PATH, 0777) != 0 && errno != EEXIST) {
            ESP_LOGW(TAG, "books dir create failed path=%s errno=%d", INK_APP_BOOKS_PATH, errno);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "created books dir path=%s", INK_APP_BOOKS_PATH);
    }
    return ESP_OK;
}

static bool has_xtc_book_extension(const char *name)
{
    const char *dot;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }
    return strcasecmp(dot, ".xtc") == 0 || strcasecmp(dot, ".xtch") == 0;
}

static bool find_first_xtc_book_path(char *path, size_t path_size)
{
    DIR *dir;
    struct dirent *entry;

    if (path == NULL || path_size == 0U) {
        return false;
    }

    dir = opendir(INK_APP_BOOKS_PATH);
    if (dir == NULL) {
        return false;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!has_xtc_book_extension(entry->d_name)) {
            continue;
        }
        if (snprintf(path, path_size, "%s/%s", INK_APP_BOOKS_PATH, entry->d_name) < (int)path_size) {
            closedir(dir);
            return true;
        }
    }

    closedir(dir);
    return false;
}

static void migrate_root_books_into_library(void)
{
    DIR *dir;
    struct dirent *entry;

    if (ensure_books_directory() != ESP_OK) {
        return;
    }

    dir = opendir(INK_APP_MOUNT_POINT);
    if (dir == NULL) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char src_path[INK_FILE_BROWSER_PATH_LENGTH + 1];
        char dst_path[INK_FILE_BROWSER_PATH_LENGTH + 1];
        struct stat st;
        const size_t name_len = strlen(entry->d_name);

        if (entry->d_name[0] == '\0'
            || strcmp(entry->d_name, ".") == 0
            || strcmp(entry->d_name, "..") == 0
            || entry->d_name[0] == '.'
            || !has_xtc_book_extension(entry->d_name)) {
            continue;
        }

        if (snprintf(src_path, sizeof(src_path), "%s/%s", INK_APP_MOUNT_POINT, entry->d_name) >= (int)sizeof(src_path)
            || snprintf(dst_path, sizeof(dst_path), "%s/%s", INK_APP_BOOKS_PATH, entry->d_name) >= (int)sizeof(dst_path)) {
            ESP_LOGW(TAG, "skip move path too long name=%s", entry->d_name);
            continue;
        }

        if (stat(src_path, &st) != 0 || (st.st_mode & S_IFDIR) != 0) {
            continue;
        }
        if (stat(dst_path, &st) == 0) {
            continue;
        }
        if (rename(src_path, dst_path) == 0) {
            ESP_LOGI(TAG, "moved book into library src=%s dst=%s", src_path, dst_path);
        } else {
            ESP_LOGW(TAG, "move book failed src=%s dst=%s errno=%d", src_path, dst_path, errno);
        }
        (void)name_len;
    }

    closedir(dir);
}

static bool load_first_cpfont_from_dir(ink_cpfont_t *font, const char *dir_path)
{
    DIR *dir;
    struct dirent *entry;
    char full_path[INK_CPFONT_PATH_LENGTH + 1];

    if (font == NULL || dir_path == NULL || dir_path[0] == '\0') {
        return false;
    }

    dir = opendir(dir_path);
    if (dir == NULL) {
        return false;
    }

    while ((entry = readdir(dir)) != NULL) {
        const char *dot = strrchr(entry->d_name, '.');
        if (dot == NULL || strcasecmp(dot, ".cpfont") != 0) {
            continue;
        }
        const size_t dir_len = strlen(dir_path);
        const size_t name_len = strlen(entry->d_name);
        if (dir_len + 1U + name_len >= sizeof(full_path)) {
            ESP_LOGW(TAG, "skip cpfont path too long dir=%s name=%s", dir_path, entry->d_name);
            continue;
        }
        memcpy(full_path, dir_path, dir_len);
        full_path[dir_len] = '/';
        memcpy(full_path + dir_len + 1U, entry->d_name, name_len + 1U);
        if (ink_cpfont_load(font, full_path) == ESP_OK) {
            closedir(dir);
            ESP_LOGI(TAG, "loaded cpfont from dir path=%s", full_path);
            return true;
        }
    }

    closedir(dir);
    return false;
}

static size_t fixed_sample_start_page(size_t total_pages)
{
    if (total_pages > 96U) {
        return 64U;
    }
    if (total_pages > 24U) {
        return 12U;
    }
    if (total_pages > 8U) {
        return 4U;
    }
    return 0U;
}

bool ink_app_load_reader_font(ink_cpfont_t *font)
{
    if (load_font_from_candidates(
            font,
            kReaderFontPaths,
            sizeof(kReaderFontPaths) / sizeof(kReaderFontPaths[0]),
            kReaderFontDirs,
            sizeof(kReaderFontDirs) / sizeof(kReaderFontDirs[0]))) {
        return true;
    }
    ESP_LOGW(TAG, "reader font not found, fallback to ASCII bitmap font");
    return false;
}

bool ink_app_load_footer_font(ink_cpfont_t *font)
{
    if (load_font_from_candidates(
            font,
            kFooterFontPaths,
            sizeof(kFooterFontPaths) / sizeof(kFooterFontPaths[0]),
            kReaderFontDirs,
            sizeof(kReaderFontDirs) / sizeof(kReaderFontDirs[0]))) {
        return true;
    }
    ESP_LOGW(TAG, "footer font 16px not found, fallback to reader font or ASCII");
    return false;
}

static bool load_font_from_candidates(
    ink_cpfont_t *font,
    const char *const *paths,
    size_t path_count,
    const char *const *dirs,
    size_t dir_count)
{
    if (font == NULL) {
        return false;
    }

    ink_cpfont_init(font);
    for (size_t i = 0; i < path_count; ++i) {
        if (ink_cpfont_load(font, paths[i]) == ESP_OK) {
            ESP_LOGI(TAG, "loaded font path=%s", paths[i]);
            return true;
        }
    }
    for (size_t i = 0; i < dir_count; ++i) {
        if (load_first_cpfont_from_dir(font, dirs[i])) {
            return true;
        }
    }
    return false;
}

uint8_t *ink_app_alloc_display_buffer(const char *name, size_t length)
{
    uint8_t *buffer = heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = malloc(length);
    }
    ESP_LOGI(
        TAG,
        "alloc %s len=%u ptr=%p free_internal=%u free_8bit=%u largest_8bit=%u largest_spiram=%u",
        name != NULL ? name : "buffer",
        (unsigned)length,
        buffer,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    return buffer;
}

bool ink_app_load_book_from_state(ink_ui_model_t *model)
{
    if (model == NULL || !ink_app_should_auto_resume_reader(&model->app_state)) {
        return false;
    }

    if (model->app_state.open_book_kind != INK_APP_STATE_BOOK_KIND_XTC) {
        return false;
    }

    (void)try_fixup_state_book_path(&model->app_state);

    if (!ink_reader_session_open_xtc(&model->reader_session, model->app_state.open_book_path, &model->app_state)) {
        return false;
    }
    if (model->app_state.open_book_page < model->reader_session.total_pages) {
        (void)ink_reader_session_jump_to_page(
            &model->reader_session,
            model->app_state.open_book_page,
            &model->app_state);
    }
    model->shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    return true;
}

bool ink_app_open_fixed_sample_book(ink_ui_model_t *model)
{
    char fallback_path[INK_APP_STATE_PATH_LENGTH + 1];
    const char *path = NULL;
    struct stat st;

    if (model == NULL) {
        return false;
    }

    for (size_t i = 0; i < sizeof(kFixedSampleBookPaths) / sizeof(kFixedSampleBookPaths[0]); ++i) {
        if (stat(kFixedSampleBookPaths[i], &st) == 0) {
            path = kFixedSampleBookPaths[i];
            break;
        }
    }
    if (path == NULL && find_first_xtc_book_path(fallback_path, sizeof(fallback_path))) {
        path = fallback_path;
    }
    if (path == NULL) {
        ESP_LOGW(TAG, "no sample XTC book found under %s", INK_APP_BOOKS_PATH);
        return false;
    }

    if (!ink_reader_session_open_xtc(&model->reader_session, path, &model->app_state)) {
        ESP_LOGW(TAG, "fixed sample XTC open failed path=%s", path);
        return false;
    }

    const size_t target_page = fixed_sample_start_page(model->reader_session.total_pages);
    if (target_page > 0U) {
        (void)ink_reader_session_jump_to_page(&model->reader_session, target_page, &model->app_state);
    }
    ink_reader_session_prefetch_next(&model->reader_session, NULL, NULL);
    model->shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    ESP_LOGI(
        TAG,
        "fixed sample reader ready path=%s page=%u/%u",
        path,
        (unsigned)(model->reader_session.current_page + 1U),
        (unsigned)model->reader_session.total_pages);
    return true;
}

static bool try_fixup_state_book_path(ink_app_state_t *state)
{
    const char *leaf;
    char candidate[INK_APP_STATE_PATH_LENGTH + 1];
    struct stat st;

    if (state == NULL || state->open_book_path[0] == '\0') {
        return false;
    }
    if (stat(state->open_book_path, &st) == 0) {
        return false;
    }

    leaf = strrchr(state->open_book_path, '/');
    leaf = (leaf != NULL && leaf[1] != '\0') ? leaf + 1 : state->open_book_path;
    if (snprintf(candidate, sizeof(candidate), "%s/%s", INK_APP_BOOKS_PATH, leaf) >= (int)sizeof(candidate)) {
        return false;
    }
    if (stat(candidate, &st) != 0) {
        return false;
    }

    ESP_LOGI(TAG, "state book path migrated old=%s new=%s", state->open_book_path, candidate);
    snprintf(state->open_book_path, sizeof(state->open_book_path), "%s", candidate);
    return true;
}

esp_err_t ink_app_mount_tf_card(void)
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
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(INK_APP_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TF mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TF card mounted at %s", INK_APP_MOUNT_POINT);
    sdmmc_card_print_info(stdout, card);
    (void)ensure_state_directory();
    (void)ensure_books_directory();
    migrate_root_books_into_library();
    return ESP_OK;
}

void ink_app_prepare_browser_fallback(ink_file_browser_t *browser)
{
    if (browser == NULL) {
        return;
    }
    memset(browser, 0, sizeof(*browser));
    snprintf(browser->mount_point, sizeof(browser->mount_point), "%s", INK_APP_BOOKS_PATH);
    snprintf(browser->current_path, sizeof(browser->current_path), "%s", INK_APP_BOOKS_PATH);
}
