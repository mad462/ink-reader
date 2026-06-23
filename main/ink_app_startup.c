#include "ink_app_startup.h"

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "ink_app_boot.h"

static const char *TAG = "ink_reader";

static void apply_initial_library_shell(ink_ui_model_t *model);
static bool app_initial_library_shell_self_test(void);

void ink_app_initialize_context(ink_app_context_t *app)
{
    if (app == NULL) {
        return;
    }

    memset(app, 0, sizeof(*app));
    ink_tuning_lab_init(&app->model.lab);
    ink_runtime_shell_init(&app->model.shell);
    ink_reader_session_init(&app->model.reader_session);
    ink_app_state_prepare_default(&app->model.app_state);
    apply_initial_library_shell(&app->model);
}

esp_err_t ink_app_allocate_runtime_buffers(ink_app_context_t *app)
{
    if (app == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(
        TAG,
        "before display alloc free_internal=%u free_8bit=%u largest_8bit=%u largest_spiram=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    app->framebuffer = ink_app_alloc_display_buffer("framebuffer", EPD_GDEY0426T82_BUFFER_SIZE);
    app->previous_framebuffer = ink_app_alloc_display_buffer("previous_framebuffer", EPD_GDEY0426T82_BUFFER_SIZE);
    app->bitmap_snapshot_a = ink_app_alloc_display_buffer("bitmap_snapshot_a", EPD_GDEY0426T82_BUFFER_SIZE);
    app->bitmap_snapshot_b = ink_app_alloc_display_buffer("bitmap_snapshot_b", EPD_GDEY0426T82_BUFFER_SIZE);
    app->native_snapshot_a = ink_app_alloc_display_buffer("native_snapshot_a", EPD_GDEY0426T82_NATIVE_BUFFER_SIZE);
    app->native_snapshot_b = ink_app_alloc_display_buffer("native_snapshot_b", EPD_GDEY0426T82_NATIVE_BUFFER_SIZE);
    ESP_RETURN_ON_FALSE(app->framebuffer != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer alloc");
    ESP_RETURN_ON_FALSE(app->previous_framebuffer != NULL, ESP_ERR_NO_MEM, TAG, "previous framebuffer alloc");
    ESP_RETURN_ON_FALSE(app->bitmap_snapshot_a != NULL, ESP_ERR_NO_MEM, TAG, "bitmap snapshot a alloc");
    ESP_RETURN_ON_FALSE(app->bitmap_snapshot_b != NULL, ESP_ERR_NO_MEM, TAG, "bitmap snapshot b alloc");
    ESP_RETURN_ON_FALSE(app->native_snapshot_a != NULL, ESP_ERR_NO_MEM, TAG, "native snapshot a alloc");
    ESP_RETURN_ON_FALSE(app->native_snapshot_b != NULL, ESP_ERR_NO_MEM, TAG, "native snapshot b alloc");
    memset(app->previous_framebuffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(app->framebuffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    return ESP_OK;
}

esp_err_t ink_app_prepare_storage_and_library(ink_app_context_t *app)
{
    if (app == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app->model.tf_ready = ink_app_mount_tf_card() == ESP_OK;
    if (app->model.tf_ready) {
        if (ink_file_browser_init(&app->model.browser, INK_APP_BOOKS_PATH, INK_APP_BOOKS_PATH) != ESP_OK) {
            ink_app_prepare_browser_fallback(&app->model.browser);
        }
        if (ink_app_state_load_file(INK_APP_STATE_FILE_PATH, &app->model.app_state) == ESP_OK) {
            ESP_LOGI(
                TAG,
                "state loaded path=%s page=%u",
                app->model.app_state.open_book_path,
                (unsigned)(app->model.app_state.open_book_page + 1U));
        } else {
            ink_app_state_prepare_default(&app->model.app_state);
        }
        (void)ink_app_load_reader_font(&app->model.reader_font);
        (void)ink_app_load_footer_font(&app->model.footer_font);
    } else {
        ink_app_prepare_browser_fallback(&app->model.browser);
        ESP_LOGW(TAG, "TF mount unavailable; library view only");
    }

    apply_initial_library_shell(&app->model);
    return ESP_OK;
}

static void apply_initial_library_shell(ink_ui_model_t *model)
{
    if (model == NULL) {
        return;
    }

    model->shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    (void)ink_runtime_shell_set_resume_available(
        &model->shell,
        ink_app_should_auto_resume_reader(&model->app_state));
    model->shell.resume_selected = model->shell.can_resume_book;
}

static bool app_initial_library_shell_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_runtime_shell_init(&model.shell);
    ink_app_state_prepare_default(&model.app_state);
    apply_initial_library_shell(&model);
    if (model.shell.page != INK_RUNTIME_SHELL_PAGE_LIBRARY
        || model.shell.can_resume_book
        || model.shell.resume_selected) {
        return false;
    }

    snprintf(model.app_state.open_book_path, sizeof(model.app_state.open_book_path), "%s", "/sdcard/books/demo.xtc");
    model.app_state.has_open_book = true;
    model.app_state.open_book_kind = INK_APP_STATE_BOOK_KIND_XTC;
    apply_initial_library_shell(&model);
    return model.shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && model.shell.can_resume_book
        && model.shell.resume_selected;
}

bool ink_app_startup_self_test(void)
{
    return app_initial_library_shell_self_test();
}
