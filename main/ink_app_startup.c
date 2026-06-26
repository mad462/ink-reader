#include "ink_app_startup.h"

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ink_app_boot.h"
#include "ink_system_services.h"

static const char *TAG = "ink_reader";

static void apply_initial_library_shell(ink_ui_model_t *model);
static bool app_initial_library_shell_self_test(void);

void ink_app_initialize_context(ink_app_context_t *app)
{
    if (app == NULL) {
        return;
    }

    memset(app, 0, sizeof(*app));
    ink_system_services_reset(&app->services);
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
    return ink_system_services_init(&app->services);
}

esp_err_t ink_app_prepare_storage_and_library(ink_app_context_t *app)
{
    if (app == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (app->services.tf_ready) {
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
}

static bool app_initial_library_shell_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_runtime_shell_init(&model.shell);
    ink_app_state_prepare_default(&model.app_state);
    apply_initial_library_shell(&model);
    if (model.shell.page != INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        return false;
    }

    snprintf(model.app_state.open_book_path, sizeof(model.app_state.open_book_path), "%s", "/sdcard/books/demo.xtc");
    model.app_state.has_open_book = true;
    model.app_state.open_book_kind = INK_APP_STATE_BOOK_KIND_XTC;
    apply_initial_library_shell(&model);
    return model.shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY;
}

bool ink_app_startup_self_test(void)
{
    return app_initial_library_shell_self_test()
        && ink_system_services_self_test();
}
