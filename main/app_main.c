#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epd_test_pattern.h"
#include "ink_app_boot.h"
#include "ink_app_priv.h"
#include "ink_app_render.h"
#include "ink_app_startup.h"
#include "ink_app_ui.h"

static const char *TAG = "ink_reader";
static const bool kRunBootSelfTests = false;
static const bool kLogAutoProbe = false;

static void run_boot_self_tests(void);
static void input_task(void *arg);
static bool submit_display_request(
    ink_app_context_t *app,
    ink_runtime_shell_command_t command,
    uint32_t event_ms);
static bool submit_post_white_refresh_request(ink_app_context_t *app, uint32_t event_ms);
static bool submit_reader_auto_flip_request(ink_app_context_t *app, uint32_t event_ms);
static bool submit_grid_compare_request(ink_app_context_t *app, uint32_t event_ms);
static bool submit_fast_browse_preview_request(ink_app_context_t *app, uint32_t event_ms);
static void ui_task(void *arg);
static void epd_task(void *arg);

static void run_boot_self_tests(void)
{
    esp_rom_printf("ST epd_gray\n");
    ESP_ERROR_CHECK(epd_test_pattern_gray_demo_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK epd_gray\n");
    ESP_ERROR_CHECK(epd_test_pattern_reader_page_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(epd_test_pattern_copy_page_buffer_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(epd_test_pattern_footer_overlay_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("ST tuning\n");
    ESP_ERROR_CHECK(ink_tuning_lab_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK tuning\n");
    ESP_ERROR_CHECK(ink_button_input_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("ST mailbox\n");
    ESP_ERROR_CHECK(ink_display_mailbox_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK mailbox\n");
    esp_rom_printf("ST ui\n");
    ESP_ERROR_CHECK(ink_app_ui_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK ui\n");
    esp_rom_printf("ST render\n");
    ESP_ERROR_CHECK(ink_app_render_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK render\n");
    esp_rom_printf("ST startup\n");
    ESP_ERROR_CHECK(ink_app_startup_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK startup\n");
}

static void input_task(void *arg)
{
    ink_app_context_t *app = (ink_app_context_t *)arg;
    uint32_t last_hold_event_ms = 0U;
    ink_button_snapshot_t snapshot;
    esp_err_t poll_ret;

    ESP_LOGI(TAG, "InputTask started");
    memset(&snapshot, 0, sizeof(snapshot));
    for (;;) {
        const uint32_t now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
        poll_ret = ink_button_input_poll(&snapshot, now_ms);
        if (poll_ret != ESP_OK) {
            ESP_LOGW(TAG, "button poll failed t=%ums ret=%s", (unsigned)now_ms, esp_err_to_name(poll_ret));
            vTaskDelay(pdMS_TO_TICKS(INK_INPUT_TASK_PERIOD_MS));
            continue;
        }
        if (ink_app_should_dispatch_button_event(&snapshot, now_ms, &last_hold_event_ms)) {
            ink_ui_event_t event = {
                .kind = INK_UI_EVENT_BUTTON,
                .event_ms = now_ms,
            };
            BaseType_t send_ret;
            event.data.snapshot = snapshot;
            ink_app_log_button_snapshot(now_ms, &snapshot);
            send_ret = xQueueSend(app->ui_queue, &event, 0);
            if (send_ret != pdTRUE) {
                ESP_LOGW(
                    TAG,
                    "ui queue drop kind=button t=%ums depth=%u stable=0x%02x pressed=0x%02x released=0x%02x",
                    (unsigned)now_ms,
                    (unsigned)uxQueueMessagesWaiting(app->ui_queue),
                    (unsigned)snapshot.stable_mask,
                    (unsigned)snapshot.pressed_mask,
                    (unsigned)snapshot.released_mask);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(INK_INPUT_TASK_PERIOD_MS));
    }
}

static bool submit_display_request(
    ink_app_context_t *app,
    ink_runtime_shell_command_t command,
    uint32_t event_ms)
{
    ink_display_request_t request;
    uint32_t seq;

    if (!ink_app_build_display_request(&app->model, event_ms, 0U, command, &request)) {
        return false;
    }
    if (app->model.lab.render_counter == 0U) {
        request.full_refresh = true;
    }
    if (request.use_fast_browse_overlay) {
        app->model.fast_browse.dirty = false;
    }
    if (request.force_fast_full_commit) {
        app->model.reader_fast_full_commit_pending = false;
    }

    seq = ink_display_mailbox_submit(&app->mailbox, &request);
    ESP_LOGI(
        TAG,
        "ui submit seq=%u page=%s cmd=%s full=%d preview=%d fast_full_commit=%d input_to_ui=%ums build=%ums footer='%s' '%s'",
        (unsigned)seq,
        ink_app_shell_page_name(request.page),
        ink_app_shell_command_name(command),
        request.full_refresh ? 1 : 0,
        request.use_fast_browse_overlay ? 1 : 0,
        request.force_fast_full_commit ? 1 : 0,
        0U,
        0U,
        request.overlay_left,
        request.overlay_right);
    return true;
}

static bool submit_fast_browse_preview_request(ink_app_context_t *app, uint32_t event_ms)
{
    if (app == NULL
        || !app->model.fast_browse.active
        || !app->model.fast_browse.dirty
        || app->model.fast_browse.commit_pending
        || app->model.reader_fast_full_commit_pending) {
        return false;
    }

    if (!ink_display_mailbox_is_idle(&app->mailbox)) {
        return false;
    }

    return submit_display_request(app, INK_RUNTIME_SHELL_COMMAND_NONE, event_ms);
}

static bool submit_reader_auto_flip_request(ink_app_context_t *app, uint32_t event_ms)
{
    if (app == NULL || !app->model.lab.auto_flip_stress_enabled) {
        return false;
    }

    if (kLogAutoProbe) {
        ESP_LOGI(
            TAG,
            "ui auto advance reader probe page=%u/%u profile=%u render_counter=%u",
            (unsigned)(app->model.reader_session.current_page + 1U),
            (unsigned)app->model.reader_session.total_pages,
            (unsigned)app->model.lab.refresh_profile,
            (unsigned)app->model.lab.render_counter);
    }
    ink_app_note_command(event_ms, INK_RUNTIME_SHELL_PAGE_READER, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT);
    if (!ink_app_advance_reader_auto_flip_stress(&app->model)) {
        return false;
    }
    return submit_display_request(app, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT, event_ms);
}

static bool submit_post_white_refresh_request(ink_app_context_t *app, uint32_t event_ms)
{
    if (app == NULL
        || !app->model.lab.force_full_refresh
        || app->model.lab.reader_white_refresh_pending) {
        return false;
    }

    ESP_LOGI(
        TAG,
        "ui submit base page after white clear page=%s profile=%u",
        ink_reader_session_is_xtc_active(&app->model.reader_session) ? "READER" : "TUNING",
        (unsigned)app->model.lab.refresh_profile);
    return submit_display_request(app, INK_RUNTIME_SHELL_COMMAND_NONE, event_ms);
}

static bool submit_grid_compare_request(ink_app_context_t *app, uint32_t event_ms)
{
    if (app == NULL) {
        return false;
    }

    if (!ink_app_should_auto_advance_grid_compare(&app->model, ESP_OK)) {
        return false;
    }

    if (!ink_app_advance_grid_compare(&app->model)) {
        return false;
    }

    if (kLogAutoProbe) {
        ESP_LOGI(
            TAG,
            "ui auto advance grid compare step=%u/%u",
            (unsigned)app->model.lab.grid_compare_step,
            (unsigned)ink_tuning_lab_grid_compare_cell_count());
    }
    return submit_display_request(app, INK_RUNTIME_SHELL_COMMAND_NONE, event_ms);
}

static void ui_task(void *arg)
{
    ink_app_context_t *app = (ink_app_context_t *)arg;
    ink_ui_event_t event;

    ESP_LOGI(TAG, "UiTask started");
    (void)submit_display_request(app, INK_RUNTIME_SHELL_COMMAND_NONE, 0U);

    for (;;) {
        const uint32_t wait_ms = (app->model.fast_browse.active || app->model.reader_nav_pending)
            ? INK_FAST_BROWSE_IDLE_TICK_MS
            : INK_UI_IDLE_WAIT_MS;
        if (xQueueReceive(app->ui_queue, &event, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
            ink_runtime_shell_command_t idle_command = INK_RUNTIME_SHELL_COMMAND_NONE;
            const uint32_t now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());

            if (ink_app_fast_browse_handle_idle(app, now_ms, &idle_command)) {
                if (idle_command != INK_RUNTIME_SHELL_COMMAND_NONE) {
                    ink_app_note_command(now_ms, app->model.shell.page, idle_command);
                }
                (void)submit_display_request(app, idle_command, now_ms);
            } else if (submit_fast_browse_preview_request(app, now_ms)) {
                continue;
            }
            continue;
        }

        if (event.kind == INK_UI_EVENT_DISPLAY_DONE) {
            ESP_LOGI(
                TAG,
                "ui display done seq=%u result=%s",
                (unsigned)event.data.display_done.seq,
                esp_err_to_name(event.data.display_done.result));
            if (event.data.display_done.result == ESP_OK
                && app->model.fast_browse.active) {
                ink_app_fast_browse_note_preview_landed(&app->model);
            }
            if (submit_fast_browse_preview_request(app, event.event_ms)) {
                continue;
            }
            if (event.data.display_done.result == ESP_OK
                && submit_post_white_refresh_request(app, event.event_ms)) {
                continue;
            }
            if (event.data.display_done.result == ESP_OK
                && submit_grid_compare_request(app, event.event_ms)) {
                continue;
            }
            if (submit_reader_auto_flip_request(app, event.event_ms)) {
                continue;
            }
            if (ink_app_should_auto_repeat_tuning_probe(&app->model, event.data.display_done.result)) {
                if (kLogAutoProbe) {
                    ESP_LOGI(
                        TAG,
                        "ui auto repeat tuning probe page=%u profile=%u render_counter=%u",
                        (unsigned)app->model.lab.current_page,
                        (unsigned)app->model.lab.refresh_profile,
                        (unsigned)app->model.lab.render_counter);
                }
                if (!submit_display_request(app, INK_RUNTIME_SHELL_COMMAND_NONE, event.event_ms)) {
                    ESP_LOGW(TAG, "ui auto repeat tuning probe submit failed");
                }
            }
            continue;
        }

        ink_runtime_shell_command_t command = INK_RUNTIME_SHELL_COMMAND_NONE;
        bool dirty = false;
        ink_runtime_shell_button_state_t button_state;
        ink_app_button_state_from_snapshot(&event.data.snapshot, &button_state);
        app->model.buttons = button_state;
        if (app->model.shell.page == INK_RUNTIME_SHELL_PAGE_READER
            && ink_reader_session_is_xtc_active(&app->model.reader_session)) {
            dirty = ink_app_process_reader_xtc_buttons(app, &event.data.snapshot, event.event_ms, &command);
        } else {
            command = ink_app_command_from_snapshot(&app->model, &event.data.snapshot);
        }

        if (command != INK_RUNTIME_SHELL_COMMAND_NONE) {
            const ink_runtime_shell_page_t logged_page = app->model.shell.page;
            ink_app_note_command(event.event_ms, logged_page, command);
            dirty |= ink_app_handle_ui_command(&app->model, command);
        }

        if (dirty) {
            (void)submit_display_request(app, command, event.event_ms);
        } else if (command != INK_RUNTIME_SHELL_COMMAND_NONE) {
            ESP_LOGI(
                TAG,
                "ui command produced no dirty page=%s cmd=%s tuning_page=%u profile=%u",
                ink_app_shell_page_name(app->model.shell.page),
                ink_app_shell_command_name(command),
                (unsigned)app->model.lab.current_page,
                (unsigned)app->model.lab.refresh_profile);
        }
    }
}

static void epd_task(void *arg)
{
    ink_app_context_t *app = (ink_app_context_t *)arg;
    ink_display_request_t request;

    ESP_LOGI(TAG, "EpdTask started");
    ink_display_mailbox_set_notify_task(&app->mailbox, xTaskGetCurrentTaskHandle());

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        while (ink_display_mailbox_try_claim_latest(&app->mailbox, &request)) {
            ink_ui_event_t done_event = {
                .kind = INK_UI_EVENT_DISPLAY_DONE,
                .event_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()),
            };
            epd_gdey0426t82_phase_t phase = EPD_GDEY0426T82_PHASE_IDLE;
            esp_err_t ret;

            ESP_LOGI(
                TAG,
                "epd claim seq=%u page=%s full=%d submitted=%ums age=%ums",
                (unsigned)request.seq,
                ink_app_shell_page_name(request.page),
                request.full_refresh ? 1 : 0,
                (unsigned)request.submitted_ms,
                (unsigned)((uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - request.submitted_ms));

            ret = ink_app_render_display_request(app, &request, &phase);
            done_event.data.display_done.seq = request.seq;
            done_event.data.display_done.result = ret;
            done_event.data.display_done.phase = phase;

            if (epd_gdey0426t82_is_aborted_error(ret)) {
                ink_display_mailbox_note_cancelled(&app->mailbox);
            } else {
                ink_display_mailbox_note_completed(&app->mailbox, request.seq);
            }
            ESP_LOGI(TAG, "epd finish seq=%u result=%s phase=%d", (unsigned)request.seq, esp_err_to_name(ret), (int)phase);
            if (xQueueSend(app->ui_queue, &done_event, 0) != pdTRUE) {
                ESP_LOGW(
                    TAG,
                    "ui queue drop kind=display_done seq=%u depth=%u result=%s phase=%d",
                    (unsigned)request.seq,
                    (unsigned)uxQueueMessagesWaiting(app->ui_queue),
                    esp_err_to_name(ret),
                    (int)phase);
            }
        }
    }
}

void app_main(void)
{
    static ink_app_context_t app;
    static const epd_gdey0426t82_config_t panel = {
        .gpio_mosi = 4,
        .gpio_sclk = 5,
        .gpio_cs = 6,
        .gpio_dc = 7,
        .gpio_rst = 15,
        .gpio_busy = 16,
        .spi_host = SPI2_HOST,
        .spi_clock_hz = 20 * 1000 * 1000,
    };
    TaskHandle_t epd_handle = NULL;
    TaskHandle_t ui_handle = NULL;
    TaskHandle_t input_handle = NULL;

    ink_app_initialize_context(&app);

    if (kRunBootSelfTests) {
        run_boot_self_tests();
    } else {
        ESP_LOGI(TAG, "boot self-tests skipped; set kRunBootSelfTests=true for strict diagnostics");
    }

    ESP_ERROR_CHECK(ink_app_allocate_runtime_buffers(&app));
    ESP_ERROR_CHECK(ink_app_prepare_storage_and_library(&app));

    ESP_ERROR_CHECK(epd_gdey0426t82_init(&panel));
    ESP_ERROR_CHECK(ink_button_input_init());

    app.ui_queue = xQueueCreate(INK_UI_QUEUE_LENGTH, sizeof(ink_ui_event_t));
    ESP_ERROR_CHECK(app.ui_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ink_display_mailbox_init(
        &app.mailbox,
        NULL,
        app.bitmap_snapshot_a,
        app.bitmap_snapshot_b,
        app.native_snapshot_a,
        app.native_snapshot_b);

    ESP_LOGI(TAG, "creating EpdTask stack=%u", (unsigned)INK_EPD_TASK_STACK_BYTES);
    ESP_ERROR_CHECK(xTaskCreate(epd_task, "EpdTask", INK_EPD_TASK_STACK_BYTES, &app, 5, &epd_handle) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "create EpdTask result=1 handle=%p free_internal=%u", epd_handle, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "creating UiTask stack=%u", (unsigned)INK_UI_TASK_STACK_BYTES);
    ESP_ERROR_CHECK(xTaskCreate(ui_task, "UiTask", INK_UI_TASK_STACK_BYTES, &app, 4, &ui_handle) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "create UiTask result=1 handle=%p free_internal=%u", ui_handle, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "creating InputTask stack=%u", (unsigned)INK_INPUT_TASK_STACK_BYTES);
    ESP_ERROR_CHECK(xTaskCreate(input_task, "InputTask", INK_INPUT_TASK_STACK_BYTES, &app, 3, &input_handle) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "create InputTask result=1 handle=%p free_internal=%u", input_handle, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
