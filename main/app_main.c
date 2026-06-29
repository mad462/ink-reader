#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epd_test_pattern.h"
#include "apps/ink_gray_cal_app.h"
#include "apps/ink_launcher_app.h"
#include "apps/ink_photo_album_app.h"
#include "apps/ink_reader_app.h"
#include "apps/ink_usb_msc_app.h"
#include "apps/ink_voice_note_app.h"
#include "apps/ink_wifi_setup_app.h"
#include "ink_app_boot.h"
#include "ink_photo_bmp_parser.h"
#include "ink_photo_catalog.h"
#include "ink_app_priv.h"
#include "ink_app_render.h"
#include "ink_app_startup.h"
#include "ink_app_ui.h"
#include "ink_usb_msc_service.h"
#include "ink_wifi_coordinator.h"
#include "ink_wifi_setup_input.h"
#include "ink_wifi_setup_render.h"
#include "ink_wifi_setup_state.h"
#include "ink_tilt_input.h"
#include "voice_note/voice_note_service.h"

static const char *TAG = "ink_reader";
static const bool kRunBootSelfTests = false;
static const bool kLogAutoProbe = false;
static ink_display_request_t s_epd_request;

static void run_boot_self_tests(void);
static void configure_runtime_log_levels(void);
static void input_task(void *arg);
static bool queue_button_event_with_priority(ink_app_context_t *app, const ink_ui_event_t *event);
static bool submit_display_request(
    ink_app_context_t *app,
    ink_runtime_shell_command_t command,
    uint32_t event_ms);
static bool submit_active_app_display_request(ink_app_context_t *app, uint32_t event_ms);
static bool map_snapshot_to_app_event(
    const ink_button_snapshot_t *snapshot,
    uint32_t event_ms,
    ink_app_event_t *event);
static bool map_tilt_to_app_event(
    ink_tilt_direction_t direction,
    uint32_t event_ms,
    ink_app_event_t *event);
static bool handle_runtime_app_button_event(
    ink_app_context_t *app,
    const ink_button_snapshot_t *snapshot,
    uint32_t event_ms);
static bool handle_runtime_app_tilt_event(
    ink_app_context_t *app,
    ink_tilt_direction_t direction,
    uint32_t event_ms);
static bool submit_post_white_refresh_request(ink_app_context_t *app, uint32_t event_ms);
static bool submit_reader_auto_flip_request(ink_app_context_t *app, uint32_t event_ms);
static bool submit_grid_compare_request(ink_app_context_t *app, uint32_t event_ms);
static bool submit_fast_browse_preview_request(ink_app_context_t *app, uint32_t event_ms);
static bool maybe_submit_time_badge_refresh(
    ink_app_context_t *app,
    bool runtime_app_active,
    uint32_t event_ms,
    char *last_time_badge,
    size_t last_time_badge_size);
static esp_err_t perform_boot_white_clear(ink_app_context_t *app);
static void ui_task(void *arg);
static void epd_task(void *arg);
static bool epd_task_stack_budget_self_test(void);
static bool runtime_app_fast_full_commit_consumed_once_self_test(void);
static bool runtime_app_tick_budget_self_test(void);
static bool time_badge_refresh_self_test(void);
static bool runtime_app_allows_time_badge_refresh(const ink_system_runtime_t *runtime);
static bool runtime_app_time_badge_policy_self_test(void);

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
    esp_rom_printf("ST runtime\n");
    ESP_ERROR_CHECK(ink_system_runtime_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK runtime\n");
    esp_rom_printf("ST render\n");
    ESP_ERROR_CHECK(ink_app_render_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK render\n");
    esp_rom_printf("ST startup\n");
    ESP_ERROR_CHECK(ink_app_startup_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK startup\n");
    esp_rom_printf("ST wifi_setup\n");
    ESP_ERROR_CHECK(ink_wifi_setup_state_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_wifi_setup_input_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_wifi_setup_render_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_wifi_setup_ui_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_wifi_setup_app_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK wifi_setup\n");
    esp_rom_printf("ST voice_note\n");
    ESP_ERROR_CHECK(voice_note_model_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_store_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_audio_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_capture_logic_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_asr_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_service_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_voice_note_app_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK voice_note\n");
    esp_rom_printf("ST photo_album\n");
    ESP_ERROR_CHECK(ink_photo_catalog_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_photo_bmp_parser_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_photo_album_app_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK photo_album\n");
    esp_rom_printf("ST gray_cal\n");
    ESP_ERROR_CHECK(ink_gray_cal_app_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK gray_cal\n");
    esp_rom_printf("ST usb_msc\n");
    ESP_ERROR_CHECK(ink_usb_msc_service_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_usb_msc_app_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK usb_msc\n");
    esp_rom_printf("ST wifi_coord\n");
    ESP_ERROR_CHECK(ink_wifi_coordinator_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK wifi_coord\n");
    ESP_ERROR_CHECK(epd_task_stack_budget_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(runtime_app_tick_budget_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(time_badge_refresh_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(runtime_app_fast_full_commit_consumed_once_self_test() ? ESP_OK : ESP_FAIL);
}

static void configure_runtime_log_levels(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("ink_reader", ESP_LOG_INFO);
    esp_log_level_set("ink_time", ESP_LOG_INFO);
    esp_log_level_set("ink_wifi_coord", ESP_LOG_INFO);
    esp_log_level_set("ink_wifi", ESP_LOG_INFO);
    esp_log_level_set("voice_note_service", ESP_LOG_INFO);
    esp_log_level_set("wifi_setup_app", ESP_LOG_INFO);
    esp_log_level_set("ink_usb_msc", ESP_LOG_INFO);
    esp_log_level_set("ink_cpfont", ESP_LOG_ERROR);
    esp_log_level_set("tusb_desc", ESP_LOG_ERROR);
    esp_log_level_set("TinyUSB", ESP_LOG_WARN);
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("app_init", ESP_LOG_WARN);
    esp_log_level_set("heap_init", ESP_LOG_WARN);
    esp_log_level_set("cpu_start", ESP_LOG_WARN);
    esp_log_level_set("esp_psram", ESP_LOG_WARN);

    ESP_LOGI(TAG, "boot start");
    ESP_LOGI(TAG, "runtime logs configured");
}

static bool epd_task_stack_budget_self_test(void)
{
    return sizeof(s_epd_request) <= 4096U
        && INK_EPD_TASK_STACK_BYTES >= 6144U;
}

static bool runtime_app_fast_full_commit_consumed_once_self_test(void)
{
    ink_app_context_t app;
    ink_app_render_model_t model;
    ink_display_request_t request;
    const ink_app_descriptor_t *reader = ink_reader_app_descriptor();
    ink_reader_app_state_t *state = NULL;
    uint8_t *page = NULL;

    ink_app_initialize_context(&app);
    ink_system_runtime_init(&app.runtime);
    ink_system_runtime_bind_services(&app.runtime, &app.services);
    ink_display_mailbox_init(&app.services.mailbox, NULL, NULL, NULL, NULL, NULL);
    if (!ink_system_runtime_register_app(&app.runtime, reader)
        || !ink_system_runtime_set_active_app(&app.runtime, reader)) {
        return false;
    }

    state = (ink_reader_app_state_t *)reader->state;
    page = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    if (state == NULL || page == NULL) {
        free(page);
        return false;
    }

    memset(state, 0, sizeof(*state));
    memset(page, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    ink_tuning_lab_init(&state->ui.lab);
    ink_runtime_shell_init(&state->ui.shell);
    ink_reader_session_init(&state->ui.reader_session);
    state->ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    state->ui.reader_session.active = true;
    state->ui.reader_session.xtc_active = true;
    state->ui.reader_session.current_page = 41U;
    state->ui.reader_session.total_pages = 100U;
    state->ui.reader_fast_full_commit_pending = true;
    if (!ink_reader_session_set_prepared_page(
            &state->ui.reader_session,
            page,
            EPD_GDEY0426T82_BUFFER_SIZE)) {
        free(page);
        return false;
    }

    if (!reader->render(&app.runtime, reader, &model)
        || !ink_app_render_model_fill_request(&model, &request)
        || !request.force_fast_full_commit
        || !state->ui.reader_fast_full_commit_pending) {
        free(page);
        return false;
    }

    if (request.force_fast_full_commit && request.owner_ui_model != NULL) {
        ((ink_ui_model_t *)request.owner_ui_model)->reader_fast_full_commit_pending = false;
    }
    if (state->ui.reader_fast_full_commit_pending) {
        free(page);
        return false;
    }

    memset(&model, 0, sizeof(model));
    memset(&request, 0, sizeof(request));
    if (!reader->render(&app.runtime, reader, &model)
        || !ink_app_render_model_fill_request(&model, &request)
        || request.force_fast_full_commit) {
        free(page);
        return false;
    }

    free(page);
    return true;
}

static bool runtime_app_tick_budget_self_test(void)
{
    return INK_RUNTIME_APP_IDLE_TICK_MS <= 20U
        && INK_RUNTIME_APP_IDLE_TICK_MS < INK_INPUT_HOLD_EVENT_MS;
}

static bool time_badge_refresh_self_test(void)
{
    char rendered[24];
    char current[24];

    snprintf(rendered, sizeof(rendered), "%s", "UP 00:00");
    snprintf(current, sizeof(current), "%s", "12:34");
    if (strcmp(rendered, current) == 0) {
        return false;
    }

    snprintf(rendered, sizeof(rendered), "%s", current);
    return strcmp(rendered, current) == 0
        && runtime_app_time_badge_policy_self_test();
}

static bool runtime_app_allows_time_badge_refresh(const ink_system_runtime_t *runtime)
{
    const ink_app_descriptor_t *active_app = NULL;

    if (runtime == NULL || runtime->active_app == NULL) {
        return true;
    }

    active_app = runtime->active_app;
    if (active_app->id != NULL
        && strcmp(active_app->id, "photo_album") == 0) {
        return false;
    }

    return true;
}

static bool runtime_app_time_badge_policy_self_test(void)
{
    static const ink_app_descriptor_t kPhotoAlbumApp = {
        .id = "photo_album",
        .name = "Photo Album",
    };
    static const ink_app_descriptor_t kVoiceNoteApp = {
        .id = "voice_note",
        .name = "Voice Note",
    };
    ink_system_runtime_t runtime;

    ink_system_runtime_init(&runtime);
    if (!runtime_app_allows_time_badge_refresh(NULL)) {
        return false;
    }
    if (!runtime_app_allows_time_badge_refresh(&runtime)) {
        return false;
    }

    runtime.active_app = &kPhotoAlbumApp;
    if (runtime_app_allows_time_badge_refresh(&runtime)) {
        return false;
    }

    runtime.active_app = &kVoiceNoteApp;
    return runtime_app_allows_time_badge_refresh(&runtime);
}

static bool queue_button_event_with_priority(ink_app_context_t *app, const ink_ui_event_t *event)
{
    ink_ui_event_t dropped_event;
    BaseType_t send_ret;

    if (app == NULL || event == NULL || app->services.ui_queue == NULL) {
        return false;
    }

    send_ret = xQueueSend(app->services.ui_queue, event, 0);
    if (send_ret == pdTRUE) {
        return true;
    }

    if (xQueuePeek(app->services.ui_queue, &dropped_event, 0) == pdTRUE
        && dropped_event.kind == INK_UI_EVENT_DISPLAY_DONE) {
        (void)xQueueReceive(app->services.ui_queue, &dropped_event, 0);
        ESP_LOGW(
            TAG,
            "ui queue evict display_done seq=%u for button depth=%u",
            (unsigned)dropped_event.data.display_done.seq,
            (unsigned)uxQueueMessagesWaiting(app->services.ui_queue));
        send_ret = xQueueSend(app->services.ui_queue, event, 0);
        if (send_ret == pdTRUE) {
            return true;
        }
    }

    return false;
}

static esp_err_t perform_boot_white_clear(ink_app_context_t *app)
{
    if (app == NULL || app->services.framebuffer == NULL || app->services.previous_framebuffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(app->services.framebuffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(app->services.previous_framebuffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    ESP_RETURN_ON_ERROR(
        epd_gdey0426t82_full_refresh(app->services.framebuffer, EPD_GDEY0426T82_BUFFER_SIZE),
        TAG,
        "boot white clear failed");
    return ESP_OK;
}

static void input_task(void *arg)
{
    ink_app_context_t *app = (ink_app_context_t *)arg;
    uint32_t last_hold_event_ms = 0U;
    ink_button_snapshot_t snapshot;
    ink_tilt_direction_t tilt_direction = INK_TILT_DIRECTION_NONE;
    esp_err_t poll_ret;
    esp_err_t tilt_ret;

    memset(&snapshot, 0, sizeof(snapshot));
    for (;;) {
        const uint32_t now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
        const ink_app_descriptor_t *active_runtime_app =
            ink_system_runtime_active_app(&app->runtime);
        const bool allow_tilt = active_runtime_app != NULL
            && ink_wifi_setup_app_should_accept_tilt(&app->runtime, active_runtime_app)
            && uxQueueMessagesWaiting(app->services.ui_queue) == 0U;
        poll_ret = ink_button_input_poll(&snapshot, now_ms);
        if (poll_ret != ESP_OK) {
            ESP_LOGW(TAG, "button poll failed t=%ums ret=%s", (unsigned)now_ms, esp_err_to_name(poll_ret));
            vTaskDelay(pdMS_TO_TICKS(INK_INPUT_TASK_PERIOD_MS));
            continue;
        }
        {
            ink_runtime_shell_button_state_t latest_buttons;
            ink_app_button_state_from_snapshot(&snapshot, &latest_buttons);
            ink_system_services_set_latest_buttons(&app->services, &latest_buttons, &snapshot, now_ms);
        }
        if (ink_app_should_dispatch_button_event(&snapshot, now_ms, &last_hold_event_ms)) {
            ink_ui_event_t event = {
                .kind = INK_UI_EVENT_BUTTON,
                .event_ms = now_ms,
            };
            event.data.snapshot = snapshot;
            ink_app_log_button_snapshot(now_ms, &snapshot);
            if (!queue_button_event_with_priority(app, &event)) {
                ESP_LOGW(
                    TAG,
                    "ui queue drop kind=button t=%ums depth=%u stable=0x%02x pressed=0x%02x released=0x%02x",
                    (unsigned)now_ms,
                    (unsigned)uxQueueMessagesWaiting(app->services.ui_queue),
                    (unsigned)snapshot.stable_mask,
                    (unsigned)snapshot.pressed_mask,
                    (unsigned)snapshot.released_mask);
            }
        }
        if (allow_tilt) {
            tilt_ret = ink_tilt_input_poll(now_ms, &tilt_direction);
        } else {
            tilt_ret = ESP_OK;
            tilt_direction = INK_TILT_DIRECTION_NONE;
        }
        if (tilt_ret == ESP_OK && tilt_direction != INK_TILT_DIRECTION_NONE) {
            ink_ui_event_t event = {
                .kind = INK_UI_EVENT_TILT,
                .event_ms = now_ms,
            };
            event.data.tilt_direction = (int32_t)tilt_direction;
            if (!queue_button_event_with_priority(app, &event)) {
                ESP_LOGW(
                    TAG,
                    "ui queue drop kind=tilt t=%ums depth=%u dir=%d",
                    (unsigned)now_ms,
                    (unsigned)uxQueueMessagesWaiting(app->services.ui_queue),
                    (int)tilt_direction);
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
    request.owner_ui_model = &app->model;
    if (app->model.lab.render_counter == 0U) {
        request.full_refresh = true;
    }
    if (request.use_fast_browse_overlay) {
        app->model.fast_browse.dirty = false;
    }
    if (request.force_fast_full_commit) {
        app->model.reader_fast_full_commit_pending = false;
    }

    seq = ink_display_mailbox_submit(&app->services.mailbox, &request);
    ESP_LOGI(
        TAG,
        "ui submit seq=%u page=%s strategy=%s cmd=%s full=%d preview=%d hold=%d fast_commit=%d",
        (unsigned)seq,
        ink_app_shell_page_name(request.page),
        ink_refresh_strategy_name(request.refresh_strategy),
        ink_app_shell_command_name(command),
        request.full_refresh ? 1 : 0,
        request.use_fast_browse_overlay ? 1 : 0,
        request.use_reader_hold_navigation ? 1 : 0,
        request.force_fast_full_commit ? 1 : 0);
    return true;
}

static bool submit_active_app_display_request(ink_app_context_t *app, uint32_t event_ms)
{
    ink_app_render_model_t model;
    ink_display_request_t request;
    uint32_t seq;

    if (app == NULL
        || !ink_system_runtime_has_active_app(&app->runtime)
        || app->runtime.active_app->render == NULL) {
        return false;
    }

    if (!app->runtime.active_app->render(&app->runtime, app->runtime.active_app, &model)) {
        return false;
    }
    if (!ink_app_render_model_fill_request(&model, &request)) {
        return false;
    }
    if (request.force_fast_full_commit && request.owner_ui_model != NULL) {
        ((ink_ui_model_t *)request.owner_ui_model)->reader_fast_full_commit_pending = false;
    }
    request.input_ms = event_ms;
    request.submitted_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    seq = ink_display_mailbox_submit(&app->services.mailbox, &request);
    ESP_LOGI(
        TAG,
        "ui submit runtime-app seq=%u app=%s mode=%u strategy=%s full=%d",
        (unsigned)seq,
        app->runtime.active_app->id != NULL ? app->runtime.active_app->id : "unknown",
        (unsigned)request.app_render_mode,
        ink_refresh_strategy_name(request.refresh_strategy),
        request.full_refresh ? 1 : 0);
    return true;
}

static bool map_snapshot_to_app_event(
    const ink_button_snapshot_t *snapshot,
    uint32_t event_ms,
    ink_app_event_t *event)
{
    if (snapshot == NULL || event == NULL) {
        return false;
    }

    memset(event, 0, sizeof(*event));
    event->event_ms = event_ms;
    event->payload = (void *)snapshot;

    if ((snapshot->pressed_mask & ink_button_input_mask_for_raw(INK_RAW_BUTTON_BACK)) != 0U) {
        event->kind = INK_APP_EVENT_BUTTON_BACK;
        return true;
    }
    if ((snapshot->pressed_mask & ink_button_input_mask_for_raw(INK_RAW_BUTTON_CONFIRM)) != 0U) {
        event->kind = INK_APP_EVENT_BUTTON_CONFIRM;
        return true;
    }
    if ((snapshot->pressed_mask & ink_button_input_mask_for_raw(INK_RAW_BUTTON_LEFT)) != 0U) {
        event->kind = INK_APP_EVENT_NAV_PREVIOUS;
        return true;
    }
    if ((snapshot->pressed_mask & ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT)) != 0U) {
        event->kind = INK_APP_EVENT_NAV_NEXT;
        return true;
    }

    if (snapshot->released_mask != 0U || snapshot->stable_mask != 0U) {
        event->kind = INK_APP_EVENT_BUTTON_SNAPSHOT;
        return true;
    }

    return false;
}

static bool map_tilt_to_app_event(
    ink_tilt_direction_t direction,
    uint32_t event_ms,
    ink_app_event_t *event)
{
    if (event == NULL || direction == INK_TILT_DIRECTION_NONE) {
        return false;
    }

    memset(event, 0, sizeof(*event));
    event->event_ms = event_ms;
    switch (direction) {
        case INK_TILT_DIRECTION_LEFT:
            event->kind = INK_APP_EVENT_TILT_PREVIOUS;
            break;
        case INK_TILT_DIRECTION_RIGHT:
            event->kind = INK_APP_EVENT_TILT_NEXT;
            break;
        case INK_TILT_DIRECTION_UP:
            event->kind = INK_APP_EVENT_TILT_UP;
            break;
        case INK_TILT_DIRECTION_DOWN:
            event->kind = INK_APP_EVENT_TILT_DOWN;
            break;
        case INK_TILT_DIRECTION_UP_LEFT:
            event->kind = INK_APP_EVENT_TILT_UP_LEFT;
            break;
        case INK_TILT_DIRECTION_UP_RIGHT:
            event->kind = INK_APP_EVENT_TILT_UP_RIGHT;
            break;
        case INK_TILT_DIRECTION_DOWN_LEFT:
            event->kind = INK_APP_EVENT_TILT_DOWN_LEFT;
            break;
        case INK_TILT_DIRECTION_DOWN_RIGHT:
            event->kind = INK_APP_EVENT_TILT_DOWN_RIGHT;
            break;
        default:
            return false;
    }
    return true;
}

static bool handle_runtime_app_button_event(
    ink_app_context_t *app,
    const ink_button_snapshot_t *snapshot,
    uint32_t event_ms)
{
    ink_app_event_t app_event;
    bool dirty = false;

    if (app == NULL || snapshot == NULL || !ink_system_runtime_has_active_app(&app->runtime)) {
        return false;
    }
    if (!map_snapshot_to_app_event(snapshot, event_ms, &app_event)) {
        return false;
    }

    dirty = ink_system_runtime_dispatch_input(&app->runtime, &app_event);
    if (dirty) {
        return submit_active_app_display_request(app, event_ms);
    }

    return true;
}

static bool handle_runtime_app_tilt_event(
    ink_app_context_t *app,
    ink_tilt_direction_t direction,
    uint32_t event_ms)
{
    ink_app_event_t app_event;
    bool dirty = false;

    if (app == NULL || !ink_system_runtime_has_active_app(&app->runtime)) {
        return false;
    }
    if (!map_tilt_to_app_event(direction, event_ms, &app_event)) {
        return false;
    }

    dirty = ink_system_runtime_dispatch_input(&app->runtime, &app_event);
    if (dirty) {
        return submit_active_app_display_request(app, event_ms);
    }

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

    if (!ink_display_mailbox_is_idle(&app->services.mailbox)) {
        return false;
    }

    return submit_display_request(app, INK_RUNTIME_SHELL_COMMAND_NONE, event_ms);
}

static bool maybe_submit_time_badge_refresh(
    ink_app_context_t *app,
    bool runtime_app_active,
    uint32_t event_ms,
    char *last_time_badge,
    size_t last_time_badge_size)
{
    char current_time_badge[24];
    bool submitted = false;

    if (app == NULL
        || last_time_badge == NULL
        || last_time_badge_size == 0U
        || !ink_display_mailbox_is_idle(&app->services.mailbox)) {
        return false;
    }

    if (runtime_app_active
        && !runtime_app_allows_time_badge_refresh(&app->runtime)) {
        return false;
    }

    ink_system_services_get_time_badge(
        &app->services,
        current_time_badge,
        sizeof(current_time_badge));
    if (strcmp(current_time_badge, last_time_badge) == 0) {
        return false;
    }

    if (runtime_app_active) {
        submitted = submit_active_app_display_request(app, event_ms);
    } else {
        submitted = submit_display_request(app, INK_RUNTIME_SHELL_COMMAND_NONE, event_ms);
    }
    if (submitted) {
        snprintf(last_time_badge, last_time_badge_size, "%s", current_time_badge);
    }
    return submitted;
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
    char last_time_badge[24];

    ink_system_services_get_time_badge(&app->services, last_time_badge, sizeof(last_time_badge));

    if (ink_system_runtime_has_active_app(&app->runtime)) {
        (void)submit_active_app_display_request(app, 0U);
    } else {
        (void)submit_display_request(app, INK_RUNTIME_SHELL_COMMAND_NONE, 0U);
    }

    for (;;) {
        if (ink_system_runtime_has_active_app(&app->runtime)) {
            const uint32_t wait_ms = INK_RUNTIME_APP_IDLE_TICK_MS;
            if (xQueueReceive(app->services.ui_queue, &event, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
                const uint32_t now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
                if (ink_system_runtime_dispatch_tick(&app->runtime, now_ms)) {
                    (void)submit_active_app_display_request(app, now_ms);
                } else {
                    (void)maybe_submit_time_badge_refresh(
                        app,
                        true,
                        now_ms,
                        last_time_badge,
                        sizeof(last_time_badge));
                }
                continue;
            }

            if (event.kind == INK_UI_EVENT_DISPLAY_DONE) {
                ink_system_runtime_handle_display_done(&app->runtime, event.event_ms);
                if (ink_system_runtime_dispatch_tick(&app->runtime, event.event_ms)) {
                    (void)submit_active_app_display_request(app, event.event_ms);
                } else {
                    (void)maybe_submit_time_badge_refresh(
                        app,
                        true,
                        event.event_ms,
                        last_time_badge,
                        sizeof(last_time_badge));
                }
                continue;
            }

            if (event.kind == INK_UI_EVENT_TILT) {
                const ink_tilt_direction_t direction = (ink_tilt_direction_t)event.data.tilt_direction;
                (void)handle_runtime_app_tilt_event(app, direction, event.event_ms);
                continue;
            }

            (void)handle_runtime_app_button_event(app, &event.data.snapshot, event.event_ms);
            continue;
        }

        const uint32_t wait_ms = (app->model.fast_browse.active || app->model.reader_nav_pending)
            ? INK_FAST_BROWSE_IDLE_TICK_MS
            : INK_UI_IDLE_WAIT_MS;
        if (xQueueReceive(app->services.ui_queue, &event, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
            ink_runtime_shell_command_t idle_command = INK_RUNTIME_SHELL_COMMAND_NONE;
            const uint32_t now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());

            if (ink_app_fast_browse_handle_idle(app, now_ms, &idle_command)) {
                if (idle_command != INK_RUNTIME_SHELL_COMMAND_NONE) {
                    ink_app_note_command(now_ms, app->model.shell.page, idle_command);
                }
                (void)submit_display_request(app, idle_command, now_ms);
            } else if (submit_fast_browse_preview_request(app, now_ms)) {
                continue;
            } else {
                (void)maybe_submit_time_badge_refresh(
                    app,
                    false,
                    now_ms,
                    last_time_badge,
                    sizeof(last_time_badge));
            }
            continue;
        }

        if (event.kind == INK_UI_EVENT_DISPLAY_DONE) {
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
            } else {
                (void)maybe_submit_time_badge_refresh(
                    app,
                    false,
                    event.event_ms,
                    last_time_badge,
                    sizeof(last_time_badge));
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

    ink_display_mailbox_set_notify_task(&app->services.mailbox, xTaskGetCurrentTaskHandle());

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        while (ink_display_mailbox_try_claim_latest(&app->services.mailbox, &s_epd_request)) {
            ink_ui_event_t done_event = {
                .kind = INK_UI_EVENT_DISPLAY_DONE,
                .event_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()),
            };
            epd_gdey0426t82_phase_t phase = EPD_GDEY0426T82_PHASE_IDLE;
            esp_err_t ret;

            ESP_LOGD(
                TAG,
                "epd claim seq=%u page=%s strategy=%s full=%d hold=%d submitted=%ums age=%ums",
                (unsigned)s_epd_request.seq,
                ink_app_shell_page_name(s_epd_request.page),
                ink_refresh_strategy_name(s_epd_request.refresh_strategy),
                s_epd_request.full_refresh ? 1 : 0,
                s_epd_request.use_reader_hold_navigation ? 1 : 0,
                (unsigned)s_epd_request.submitted_ms,
                (unsigned)((uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - s_epd_request.submitted_ms));

            ret = ink_app_render_display_request(app, &s_epd_request, &phase);
            done_event.data.display_done.seq = s_epd_request.seq;
            done_event.data.display_done.result = ret;
            done_event.data.display_done.phase = phase;

            if (epd_gdey0426t82_is_aborted_error(ret)) {
                ink_display_mailbox_note_cancelled(&app->services.mailbox);
            } else {
                ink_display_mailbox_note_completed(&app->services.mailbox, s_epd_request.seq);
            }
            ESP_LOGD(
                TAG,
                "epd finish seq=%u result=%s phase=%d",
                (unsigned)s_epd_request.seq,
                esp_err_to_name(ret),
                (int)phase);
            if (xQueueSend(app->services.ui_queue, &done_event, 0) != pdTRUE) {
                ESP_LOGW(
                    TAG,
                    "ui queue drop kind=display_done seq=%u depth=%u result=%s phase=%d",
                    (unsigned)s_epd_request.seq,
                    (unsigned)uxQueueMessagesWaiting(app->services.ui_queue),
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
    configure_runtime_log_levels();
    app.aggressive_interrupt_mode = true;
    ink_system_runtime_init(&app.runtime);
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_launcher_app_descriptor()) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_reader_app_descriptor()) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_voice_note_app_descriptor()) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_wifi_setup_app_descriptor()) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_photo_album_app_descriptor()) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_gray_cal_app_descriptor()) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_usb_msc_app_descriptor()) ? ESP_OK : ESP_FAIL);
    app.runtime.force_full_refresh_on_next_render = true;
    ESP_ERROR_CHECK(ink_system_runtime_set_active_app(&app.runtime, ink_launcher_app_descriptor()) ? ESP_OK : ESP_FAIL);

    if (kRunBootSelfTests) {
        run_boot_self_tests();
    }

    ESP_ERROR_CHECK(ink_app_allocate_runtime_buffers(&app));
    ESP_ERROR_CHECK(ink_app_prepare_storage_and_library(&app));
    ink_system_runtime_bind_services(&app.runtime, &app.services);

    ESP_ERROR_CHECK(epd_gdey0426t82_init(&panel));
    ESP_ERROR_CHECK(perform_boot_white_clear(&app));
    ESP_ERROR_CHECK(ink_button_input_init());
    (void)ink_tilt_input_init();

    ESP_ERROR_CHECK(xTaskCreate(epd_task, "EpdTask", INK_EPD_TASK_STACK_BYTES, &app, 5, &epd_handle) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xTaskCreate(ui_task, "UiTask", INK_UI_TASK_STACK_BYTES, &app, 4, &ui_handle) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xTaskCreate(input_task, "InputTask", INK_INPUT_TASK_STACK_BYTES, &app, 3, &input_handle) == pdPASS ? ESP_OK : ESP_FAIL);
    if (ink_wifi_coordinator_start_async() != ESP_OK) {
        ESP_LOGW(TAG, "wifi coordinator background start failed");
    }
}
