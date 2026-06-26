#include "ink_app_render.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/task.h"

#include "epd_test_pattern.h"

static const char *TAG = "ink_reader";
static const bool kFooterPartialReuseInit = true;

static uint32_t s_render_sequence;
static uint32_t s_last_command_ms;
static ink_runtime_shell_command_t s_last_command;

static bool find_changed_region(
    const uint8_t *previous,
    const uint8_t *current,
    size_t length,
    uint16_t *x,
    uint16_t *y,
    uint16_t *width,
    uint16_t *height);
static bool should_abort_render_draw(void *ctx);
static bool epd_request_is_stale(void *ctx, epd_gdey0426t82_phase_t phase);
static void fill_tuning_page(
    uint8_t *buffer,
    size_t length,
    uint8_t tuning_page,
    const char *overlay_left,
    const char *overlay_right);
static void fill_shell_page(
    uint8_t *buffer,
    size_t length,
    const ink_cpfont_t *font,
    const ink_runtime_shell_view_t *view);
static bool render_grid_compare_variant(
    ink_app_context_t *app,
    const ink_display_request_t *request);
static bool grid_compare_cell_region(
    uint8_t cell_index,
    uint16_t *x,
    uint16_t *y,
    uint16_t *width,
    uint16_t *height);
static bool should_reuse_reader_partial_init(
    const ink_app_context_t *app,
    const ink_display_request_t *request);
static bool should_force_fast_full_commit(const ink_display_request_t *request);
static bool should_route_footer_preview_to_full_window_stock_partial(
    const ink_display_request_t *request);
static bool should_promote_reader_partial_to_full_window(
    const ink_display_request_t *request,
    uint16_t dirty_width,
    uint16_t dirty_height);
static bool should_fallback_reader_full_window_to_stock_partial(
    const ink_display_request_t *request);
static void log_render_timing(
    uint32_t now_ms,
    const ink_display_request_t *request,
    uint16_t dirty_x,
    uint16_t dirty_y,
    uint16_t dirty_width,
    uint16_t dirty_height,
    uint32_t view_ms,
    uint32_t draw_ms,
    uint32_t diff_ms,
    uint32_t epd_ms,
    uint32_t duration_ms,
    esp_err_t ret,
    uint32_t cache_hits_delta,
    uint32_t cache_misses_delta,
    uint32_t cache_evictions_delta);
static bool app_partial_region_self_test(void);
static bool app_custom_lut_request_self_test(void);
static bool app_footer_probe_delta_self_test(void);
static bool app_reader_request_self_test(void);
static bool app_reader_fast_browse_request_self_test(void);
static bool app_reader_fast_browse_commit_request_self_test(void);
static bool app_library_request_self_test(void);
static bool app_debug_self_test(void);
static bool app_full_refresh_routing_self_test(void);
static bool app_grid_compare_request_self_test(void);
static bool app_large_reader_partial_routing_self_test(void);
static bool app_reader_partial_init_reuse_self_test(void);
static bool app_reader_full_window_stock_fallback_self_test(void);
static bool app_footer_preview_route_self_test(void);
static bool app_fast_browse_cancel_policy_self_test(void);
static bool app_reader_cancel_policy_self_test(void);

const char *ink_app_shell_page_name(ink_runtime_shell_page_t page)
{
    switch (page) {
        case INK_RUNTIME_SHELL_PAGE_LIBRARY:
            return "LIBRARY";
        case INK_RUNTIME_SHELL_PAGE_READER:
            return "READER";
        default:
            return "UNKNOWN";
    }
}

const char *ink_app_shell_command_name(ink_runtime_shell_command_t command)
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

bool ink_app_page_shows_live_button_state(ink_runtime_shell_page_t page)
{
    (void)page;
    return false;
}

void ink_app_log_button_snapshot(uint32_t now_ms, const ink_button_snapshot_t *snapshot)
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
        (unsigned)snapshot->held_duration_ms[INK_RAW_BUTTON_POWER]);
}

void ink_app_note_command(
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
        ink_app_shell_page_name(page),
        ink_app_shell_command_name(command));
    s_last_command_ms = now_ms;
    s_last_command = command;
}

static void log_render_timing(
    uint32_t now_ms,
    const ink_display_request_t *request,
    uint16_t dirty_x,
    uint16_t dirty_y,
    uint16_t dirty_width,
    uint16_t dirty_height,
    uint32_t view_ms,
    uint32_t draw_ms,
    uint32_t diff_ms,
    uint32_t epd_ms,
    uint32_t duration_ms,
    esp_err_t ret,
    uint32_t cache_hits_delta,
    uint32_t cache_misses_delta,
    uint32_t cache_evictions_delta)
{
    const uint32_t since_command_ms = s_last_command_ms == 0U ? 0U : now_ms - s_last_command_ms;
    ESP_LOGI(
        TAG,
        "render seq=%u t=%ums page=%s mode=%s cmd=%s since_cmd=%ums view=%ums draw=%ums diff=%ums epd=%ums total=%ums dirty=%u,%u %ux%u cache=+%u/-%u evict+%u ret=%s",
        (unsigned)s_render_sequence,
        (unsigned)now_ms,
        ink_app_shell_page_name(request->page),
        request->full_refresh ? "full" : "partial",
        ink_app_shell_command_name(s_last_command),
        (unsigned)since_command_ms,
        (unsigned)view_ms,
        (unsigned)draw_ms,
        (unsigned)diff_ms,
        (unsigned)epd_ms,
        (unsigned)duration_ms,
        (unsigned)dirty_x,
        (unsigned)dirty_y,
        (unsigned)dirty_width,
        (unsigned)dirty_height,
        (unsigned)cache_hits_delta,
        (unsigned)cache_misses_delta,
        (unsigned)cache_evictions_delta,
        esp_err_to_name(ret));
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
    uint16_t max_x = 0U;
    uint16_t max_y = 0U;
    const size_t stride = EPD_GDEY0426T82_WIDTH / 8U;

    for (uint16_t row = 0; row < EPD_GDEY0426T82_HEIGHT; ++row) {
        for (uint16_t column_byte = 0; column_byte < stride; ++column_byte) {
            const size_t index = (size_t)row * stride + column_byte;
            const uint8_t diff = previous[index] ^ current[index];
            if (diff == 0U) {
                continue;
            }

            uint16_t first_bit = 0U;
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

static bool should_abort_render_draw(void *ctx)
{
    ink_epd_cancel_ctx_t *cancel_ctx = (ink_epd_cancel_ctx_t *)ctx;
    return cancel_ctx != NULL
        && cancel_ctx->app != NULL
        && ink_display_mailbox_has_newer_than(&cancel_ctx->app->services.mailbox, cancel_ctx->seq);
}

static bool epd_request_is_stale(void *ctx, epd_gdey0426t82_phase_t phase)
{
    ink_epd_cancel_ctx_t *cancel_ctx = (ink_epd_cancel_ctx_t *)ctx;

    if (cancel_ctx != NULL
        && cancel_ctx->pin_during_transmitting
        && phase == EPD_GDEY0426T82_PHASE_TRANSMITTING) {
        return false;
    }

    return should_abort_render_draw(ctx);
}

static bool request_pins_during_transmitting(const ink_display_request_t *request)
{
    if (request == NULL) {
        return false;
    }

    if (request->use_fast_browse_overlay && request->force_fixed_footer_partial) {
        return true;
    }

    return request->page == INK_RUNTIME_SHELL_PAGE_READER
        && !request->use_fast_browse_overlay;
}

static void fill_tuning_page(
    uint8_t *buffer,
    size_t length,
    uint8_t tuning_page,
    const char *overlay_left,
    const char *overlay_right)
{
    switch ((ink_tuning_page_t)tuning_page) {
        case INK_TUNING_PAGE_TEXT:
            epd_test_pattern_fill_text_page(
                buffer,
                length,
                "DISPLAY TUNING LAB",
                "TEXT PAGE",
                "READER LIKE BODY",
                "TEST PARTIAL SPEED",
                "LOOK FOR GHOSTING",
                "PRESS CONFIRM");
            break;
        case INK_TUNING_PAGE_FOOTER:
            epd_test_pattern_fill_text_page(
                buffer,
                length,
                "DISPLAY TUNING LAB",
                "STABLE BODY",
                "FOOTER CHANGES",
                "SMALL AREA TEST",
                "PROFILE SWITCHING",
                "PRESS BACK");
            break;
        case INK_TUNING_PAGE_DETAIL:
            epd_test_pattern_fill_layout(buffer, length);
            break;
        case INK_TUNING_PAGE_HIGH_DELTA:
            epd_test_pattern_fill_stripes(buffer, length);
            break;
        case INK_TUNING_PAGE_GRID_COMPARE:
            epd_test_pattern_fill_grid_compare_base_page(
                buffer,
                length,
                ink_tuning_lab_grid_compare_sweep_tag());
            break;
        default:
            epd_test_pattern_fill_text_page(
                buffer,
                length,
                "DISPLAY TUNING LAB",
                "UNKNOWN PAGE",
                "",
                "",
                "",
                "");
            break;
    }
    epd_test_pattern_draw_footer_overlay(buffer, length, NULL, overlay_left, overlay_right);
}

static void fill_shell_page(
    uint8_t *buffer,
    size_t length,
    const ink_cpfont_t *font,
    const ink_runtime_shell_view_t *view)
{
    if (view == NULL) {
        epd_test_pattern_fill_text_page_with_font(
            buffer,
            length,
            font,
            NULL,
            NULL,
            "INK READER",
            "NO VIEW",
            "",
            "",
            "",
            "");
        return;
    }

    epd_test_pattern_fill_text_page_with_font(
        buffer,
        length,
        font,
        NULL,
        NULL,
        view->title,
        view->line1,
        view->line2,
        view->line3,
        view->line4,
        view->line5);
}

esp_err_t ink_app_render_display_request(
    ink_app_context_t *app,
    const ink_display_request_t *request,
    epd_gdey0426t82_phase_t *phase_out)
{
    const ink_cpfont_t *page_font = NULL;
    const ink_cpfont_t *footer_font = NULL;
    ink_epd_cancel_ctx_t cancel_ctx = {
        .app = app,
        .seq = request->seq,
        .pin_during_transmitting = request_pins_during_transmitting(request),
    };
    epd_gdey0426t82_refresh_control_t control = {
        .aggressive_interrupt_mode = app->aggressive_interrupt_mode,
        .use_custom_lut_a = request != NULL
            && !request->use_grid_compare_variant
            && request->refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_A,
        .use_custom_lut_b = request != NULL
            && !request->use_grid_compare_variant
            && request->refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_B,
        .use_grid_compare_variant = request != NULL && request->use_grid_compare_variant,
        .reuse_partial_init = should_reuse_reader_partial_init(app, request),
        .grid_compare_variant_index = request != NULL ? request->grid_compare_variant_index : 0U,
        .should_cancel = epd_request_is_stale,
        .should_cancel_ctx = &cancel_ctx,
        .phase = EPD_GDEY0426T82_PHASE_IDLE,
    };
    ink_cpfont_cache_stats_t cache_before = {0};
    ink_cpfont_cache_stats_t cache_after = {0};
    uint16_t dirty_x = 0U;
    uint16_t dirty_y = 0U;
    uint16_t dirty_w = 0U;
    uint16_t dirty_h = 0U;
    const uint32_t started_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    uint32_t phase_ms = started_ms;
    uint32_t view_ms = 0U;
    uint32_t draw_ms = 0U;
    uint32_t diff_ms = 0U;
    uint32_t epd_ms = 0U;
    esp_err_t ret = ESP_OK;

    if (request == NULL
        || app == NULL
        || app->services.framebuffer == NULL
        || app->services.previous_framebuffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ink_cpfont_is_loaded(&app->services.reader_font)) {
        page_font = &app->services.reader_font;
    } else if (ink_cpfont_is_loaded(&app->services.footer_font)) {
        page_font = &app->services.footer_font;
    }

    if (ink_cpfont_is_loaded(&app->services.footer_font)) {
        footer_font = &app->services.footer_font;
    } else if (ink_cpfont_is_loaded(&app->services.reader_font)) {
        footer_font = &app->services.reader_font;
    }

    s_render_sequence = request->seq;
    if (ink_cpfont_is_loaded(&app->services.reader_font)) {
        ink_cpfont_cache_snapshot(&app->services.reader_font, &cache_before);
    }

    view_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - phase_ms;
    phase_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());

    if (request->force_white_page) {
        memset(app->services.framebuffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    } else if (request->use_fast_browse_overlay) {
        memcpy(app->services.framebuffer, app->services.previous_framebuffer, EPD_GDEY0426T82_BUFFER_SIZE);
    } else if (request->use_grid_compare_variant) {
        if (!render_grid_compare_variant(app, request)) {
            return ESP_ERR_INVALID_STATE;
        }
    } else if (request->page == INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        fill_shell_page(
            app->services.framebuffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            page_font,
            &request->shell_view);
    } else if (request->use_bitmap_page
        && request->bitmap_page_buffer != NULL
        && request->bitmap_page_length >= EPD_GDEY0426T82_BUFFER_SIZE) {
        memcpy(app->services.framebuffer, request->bitmap_page_buffer, EPD_GDEY0426T82_BUFFER_SIZE);
    } else if (request->use_reader_layout) {
        epd_test_pattern_fill_reader_page_with_font(
            app->services.framebuffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            page_font,
            should_abort_render_draw,
            &cancel_ctx,
            request->reader_view.title,
            request->reader_view.lines[0],
            request->reader_view.lines[1],
            request->reader_view.lines[2],
            request->reader_view.lines[3],
            request->reader_view.status);
    } else {
        fill_tuning_page(
            app->services.framebuffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            request->tuning_page,
            request->overlay_left,
            request->overlay_right);
    }

    if (request->use_footer_overlay) {
        epd_test_pattern_draw_footer_overlay(
            app->services.framebuffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            footer_font,
            request->overlay_left,
            request->overlay_right);
        if (request->tuning_page == INK_TUNING_PAGE_FOOTER) {
            epd_test_pattern_draw_footer_probe(
                app->services.framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                request->overlay_right);
        }
    }
    draw_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - phase_ms;

    phase_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    if (request->use_grid_compare_variant) {
        (void)grid_compare_cell_region(
            request->grid_compare_variant_index,
            &dirty_x,
            &dirty_y,
            &dirty_w,
            &dirty_h);
    } else if (!request->full_refresh) {
        (void)find_changed_region(
            app->services.previous_framebuffer,
            app->services.framebuffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            &dirty_x,
            &dirty_y,
            &dirty_w,
            &dirty_h);
    }
    diff_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - phase_ms;

    if (!request->full_refresh && dirty_w == 0U && dirty_h == 0U) {
        ret = ESP_OK;
    } else {
        phase_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
        if (request->full_refresh) {
            dirty_x = 0U;
            dirty_y = 0U;
            dirty_w = EPD_GDEY0426T82_WIDTH;
            dirty_h = EPD_GDEY0426T82_HEIGHT;
            if (request->use_native_page
                && request->native_page_buffer != NULL
                && request->native_page_length >= EPD_GDEY0426T82_NATIVE_BUFFER_SIZE) {
                ret = epd_gdey0426t82_full_refresh_native_ex(
                    request->native_page_buffer,
                    request->native_page_length,
                    &control);
            } else {
                ret = epd_gdey0426t82_full_refresh_ex(
                    app->services.framebuffer,
                    EPD_GDEY0426T82_BUFFER_SIZE,
                    &control);
            }
        } else if (request->refresh_profile == INK_TUNING_REFRESH_FAST_FULL) {
            dirty_x = 0U;
            dirty_y = 0U;
            dirty_w = EPD_GDEY0426T82_WIDTH;
            dirty_h = EPD_GDEY0426T82_HEIGHT;
            if (should_force_fast_full_commit(request)) {
                ESP_LOGI(
                    TAG,
                    "render commit seq=%u page=%s route=fast_full_commit",
                    (unsigned)request->seq,
                    ink_app_shell_page_name(request->page));
            }
            if (request->use_native_page
                && request->native_page_buffer != NULL
                && request->native_page_length >= EPD_GDEY0426T82_NATIVE_BUFFER_SIZE) {
                ret = epd_gdey0426t82_partial_refresh_area_native_ex(
                    request->native_page_buffer,
                    request->native_page_length,
                    0U,
                    0U,
                    EPD_GDEY0426T82_NATIVE_WIDTH,
                    EPD_GDEY0426T82_NATIVE_HEIGHT,
                    &control);
            } else {
                ret = epd_gdey0426t82_partial_refresh_ex(
                    app->services.framebuffer,
                    EPD_GDEY0426T82_BUFFER_SIZE,
                    &control);
            }
        } else if (request->force_fixed_footer_partial) {
            dirty_x = 0U;
            dirty_y = 768U;
            dirty_w = EPD_GDEY0426T82_WIDTH;
            dirty_h = 32U;
            if (should_route_footer_preview_to_full_window_stock_partial(request)) {
                epd_gdey0426t82_refresh_control_t full_window_control = control;
                full_window_control.use_custom_lut_a = false;
                full_window_control.use_custom_lut_b = false;
                full_window_control.use_grid_compare_variant = false;
                full_window_control.reuse_partial_init = false;
                ESP_LOGI(
                    TAG,
                    "render footer seq=%u page=%s route=footer_full_window_stock",
                    (unsigned)request->seq,
                    ink_app_shell_page_name(request->page));
                dirty_x = 0U;
                dirty_y = 0U;
                dirty_w = EPD_GDEY0426T82_WIDTH;
                dirty_h = EPD_GDEY0426T82_HEIGHT;
                ret = epd_gdey0426t82_partial_refresh_ex(
                    app->services.framebuffer,
                    EPD_GDEY0426T82_BUFFER_SIZE,
                    &full_window_control);
            } else {
                ESP_LOGI(
                    TAG,
                    "render footer seq=%u page=%s route=%s reuse_init=%d",
                    (unsigned)request->seq,
                    ink_app_shell_page_name(request->page),
                    control.reuse_partial_init ? "footer_partial_reuse" : "footer_partial_cold",
                    control.reuse_partial_init ? 1 : 0);
                ret = epd_gdey0426t82_partial_refresh_area_ex(
                    app->services.framebuffer,
                    EPD_GDEY0426T82_BUFFER_SIZE,
                    dirty_x,
                    dirty_y,
                    dirty_w,
                    dirty_h,
                    &control);
            }
        } else if (should_promote_reader_partial_to_full_window(request, dirty_w, dirty_h)) {
            epd_gdey0426t82_refresh_control_t full_window_control = control;
            const bool use_stock_partial =
                should_fallback_reader_full_window_to_stock_partial(request);

            if (use_stock_partial) {
                full_window_control.use_custom_lut_a = false;
                full_window_control.use_custom_lut_b = false;
                full_window_control.reuse_partial_init = false;
            }
            ESP_LOGI(
                TAG,
                "render promote seq=%u page=%s dirty=%u,%u %ux%u route=%s",
                (unsigned)request->seq,
                ink_app_shell_page_name(request->page),
                (unsigned)dirty_x,
                (unsigned)dirty_y,
                (unsigned)dirty_w,
                (unsigned)dirty_h,
                use_stock_partial
                    ? "partial_full_window_stock_mainline"
                    : ink_tuning_lab_refresh_profile_name((ink_tuning_refresh_profile_t)request->refresh_profile));
            dirty_x = 0U;
            dirty_y = 0U;
            dirty_w = EPD_GDEY0426T82_WIDTH;
            dirty_h = EPD_GDEY0426T82_HEIGHT;
            if (request->use_native_page
                && request->native_page_buffer != NULL
                && request->native_page_length >= EPD_GDEY0426T82_NATIVE_BUFFER_SIZE) {
                ret = epd_gdey0426t82_partial_refresh_area_native_ex(
                    request->native_page_buffer,
                    request->native_page_length,
                    0U,
                    0U,
                    EPD_GDEY0426T82_NATIVE_WIDTH,
                    EPD_GDEY0426T82_NATIVE_HEIGHT,
                    &full_window_control);
            } else {
                ret = epd_gdey0426t82_partial_refresh_ex(
                    app->services.framebuffer,
                    EPD_GDEY0426T82_BUFFER_SIZE,
                    &full_window_control);
            }
        } else {
            ESP_LOGI(
                TAG,
                "render dirty seq=%u page=%s x=%u y=%u w=%u h=%u",
                (unsigned)request->seq,
                ink_app_shell_page_name(request->page),
                (unsigned)dirty_x,
                (unsigned)dirty_y,
                (unsigned)dirty_w,
                (unsigned)dirty_h);
            ret = epd_gdey0426t82_partial_refresh_area_ex(
                app->services.framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                dirty_x,
                dirty_y,
                dirty_w,
                dirty_h,
                &control);
        }
        epd_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - phase_ms;
    }

    if (ret == ESP_OK) {
        memcpy(app->services.previous_framebuffer, app->services.framebuffer, EPD_GDEY0426T82_BUFFER_SIZE);
        app->model.lab.render_counter++;
        if (!request->full_refresh
            && (request->refresh_profile == INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY
            || request->refresh_profile == INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER
            || request->refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_A
            || request->refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_B)) {
            app->model.lab.consecutive_partial_count++;
        } else {
            app->model.lab.consecutive_partial_count = 0U;
        }
        if (request->force_white_page) {
            app->model.lab.force_full_refresh = true;
            app->model.lab.reader_white_refresh_pending = false;
        } else {
            app->model.lab.force_full_refresh = false;
            app->model.lab.reader_white_refresh_pending = false;
        }
        if (request->page != INK_RUNTIME_SHELL_PAGE_READER) {
            ink_runtime_shell_mark_rendered(&app->model.shell);
        }
    }

    if (ink_cpfont_is_loaded(&app->services.reader_font)) {
        ink_cpfont_cache_snapshot(&app->services.reader_font, &cache_after);
    }
    log_render_timing(
        (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()),
        request,
        dirty_x,
        dirty_y,
        dirty_w,
        dirty_h,
        view_ms,
        draw_ms,
        diff_ms,
        epd_ms,
        (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - started_ms,
        ret,
        cache_after.hits - cache_before.hits,
        cache_after.misses - cache_before.misses,
        cache_after.evictions - cache_before.evictions);

    if (phase_out != NULL) {
        *phase_out = control.phase;
    }
    return ret;
}

static bool render_grid_compare_variant(
    ink_app_context_t *app,
    const ink_display_request_t *request)
{
    if (app == NULL
        || request == NULL
        || app->services.framebuffer == NULL
        || app->services.previous_framebuffer == NULL) {
        return false;
    }

    memcpy(app->services.framebuffer, app->services.previous_framebuffer, EPD_GDEY0426T82_BUFFER_SIZE);
    epd_test_pattern_apply_grid_compare_cell(
        app->services.framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        request->grid_compare_variant_index,
        ink_tuning_lab_grid_compare_sweep_tag());
    ESP_LOGI(
        TAG,
        "grid compare step=%u cell=%u variant=%u",
        (unsigned)request->grid_compare_step,
        (unsigned)request->grid_compare_variant_index,
        (unsigned)request->grid_compare_variant_index);
    return true;
}

static bool grid_compare_cell_region(
    uint8_t cell_index,
    uint16_t *x,
    uint16_t *y,
    uint16_t *width,
    uint16_t *height)
{
    enum {
        GRID_COMPARE_COLS = 4,
        GRID_COMPARE_X0 = 16,
        GRID_COMPARE_Y0 = 72,
        GRID_COMPARE_W = 104,
        GRID_COMPARE_H = 166,
        GRID_COMPARE_GAP_X = 6,
        GRID_COMPARE_GAP_Y = 8,
    };
    const uint16_t col = (uint16_t)(cell_index % GRID_COMPARE_COLS);
    const uint16_t row = (uint16_t)(cell_index / GRID_COMPARE_COLS);

    if (cell_index >= 16U || x == NULL || y == NULL || width == NULL || height == NULL) {
        return false;
    }

    *x = (uint16_t)(GRID_COMPARE_X0 + col * (GRID_COMPARE_W + GRID_COMPARE_GAP_X));
    *y = (uint16_t)(GRID_COMPARE_Y0 + row * (GRID_COMPARE_H + GRID_COMPARE_GAP_Y));
    *width = GRID_COMPARE_W;
    *height = GRID_COMPARE_H;
    return true;
}

static bool should_promote_reader_partial_to_full_window(
    const ink_display_request_t *request,
    uint16_t dirty_width,
    uint16_t dirty_height)
{
    if (request == NULL
        || !request->use_bitmap_page
        || request->force_fixed_footer_partial
        || request->force_white_page
        || request->use_grid_compare_variant) {
        return false;
    }

    return dirty_width >= (uint16_t)((EPD_GDEY0426T82_WIDTH * 7U) / 8U)
        && dirty_height >= (uint16_t)((EPD_GDEY0426T82_HEIGHT * 7U) / 8U);
}

static bool should_force_fast_full_commit(const ink_display_request_t *request)
{
    return request != NULL
        && request->force_fast_full_commit
        && (request->page == INK_RUNTIME_SHELL_PAGE_READER
            || request->page == INK_RUNTIME_SHELL_PAGE_LIBRARY)
        && !request->use_fast_browse_overlay
        && !request->full_refresh
        && !request->force_fixed_footer_partial
        && !request->force_white_page;
}

static bool should_route_footer_preview_to_full_window_stock_partial(
    const ink_display_request_t *request)
{
    return request != NULL
        && request->page == INK_RUNTIME_SHELL_PAGE_READER
        && request->use_fast_browse_overlay
        && request->force_fixed_footer_partial
        && !request->full_refresh
        && !request->force_white_page
        && !request->use_grid_compare_variant;
}

static bool should_reuse_reader_partial_init(
    const ink_app_context_t *app,
    const ink_display_request_t *request)
{
    if (app == NULL
        || request == NULL
        || request->full_refresh
        || request->use_grid_compare_variant) {
        return false;
    }

    if (request->force_fixed_footer_partial) {
        return kFooterPartialReuseInit
            && app->model.lab.consecutive_partial_count > 0U;
    }

    if (request->refresh_profile != INK_TUNING_REFRESH_CUSTOM_LUT_A
        || !request->use_bitmap_page) {
        return false;
    }

    return app->model.lab.consecutive_partial_count > 0U;
}

static bool should_fallback_reader_full_window_to_stock_partial(
    const ink_display_request_t *request)
{
    if (request == NULL
        || !request->use_bitmap_page
        || request->force_fixed_footer_partial
        || request->force_white_page
        || request->use_grid_compare_variant) {
        return false;
    }

    return true;
}

static bool app_partial_region_self_test(void)
{
    uint8_t *previous = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    uint8_t *current = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    uint16_t x = 0U;
    uint16_t y = 0U;
    uint16_t width = 0U;
    uint16_t height = 0U;
    bool ok = false;

    if (previous == NULL || current == NULL) {
        free(previous);
        free(current);
        return false;
    }

    memset(previous, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(current, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    if (find_changed_region(previous, current, EPD_GDEY0426T82_BUFFER_SIZE, &x, &y, &width, &height)) {
        goto cleanup;
    }

    current[(115U * (EPD_GDEY0426T82_WIDTH / 8U)) + 4U] = 0x7FU;
    current[(185U * (EPD_GDEY0426T82_WIDTH / 8U)) + 20U] = 0xFEU;
    if (!find_changed_region(previous, current, EPD_GDEY0426T82_BUFFER_SIZE, &x, &y, &width, &height)) {
        goto cleanup;
    }
    ok = x == 32U && y == 115U && width == 136U && height == 71U;

cleanup:
    free(previous);
    free(current);
    return ok;
}

static bool app_custom_lut_request_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.lab.current_page = INK_TUNING_PAGE_FOOTER;
    model.lab.refresh_profile = INK_TUNING_REFRESH_CUSTOM_LUT_A;
    model.lab.force_full_refresh = false;
    model.lab.reader_white_refresh_pending = false;
    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_CONFIRM, &request)) {
        return false;
    }
    return !request.full_refresh
        && request.force_fixed_footer_partial
        && request.refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_A
        && strcmp(request.overlay_left, "P2/5 FOOTER") == 0
        && strcmp(request.overlay_right, "CUSTOM_LUT_A #0") == 0;
}

static bool app_footer_probe_delta_self_test(void)
{
    uint8_t *previous = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    uint8_t *current = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    uint16_t x = 0U;
    uint16_t y = 0U;
    uint16_t width = 0U;
    uint16_t height = 0U;
    bool ok = false;

    if (previous == NULL || current == NULL) {
        free(previous);
        free(current);
        return false;
    }

    epd_test_pattern_fill_text_page(
        previous,
        EPD_GDEY0426T82_BUFFER_SIZE,
        "DISPLAY TUNING LAB",
        "STABLE BODY",
        "FOOTER CHANGES",
        "SMALL AREA TEST",
        "PROFILE SWITCHING",
        "PRESS BACK");
    epd_test_pattern_draw_footer_overlay(previous, EPD_GDEY0426T82_BUFFER_SIZE, NULL, "P2/4 FOOTER", "PARTIAL_FOOTER #0");
    epd_test_pattern_draw_footer_probe(previous, EPD_GDEY0426T82_BUFFER_SIZE, "PARTIAL_FOOTER #0");

    memcpy(current, previous, EPD_GDEY0426T82_BUFFER_SIZE);
    epd_test_pattern_draw_footer_overlay(current, EPD_GDEY0426T82_BUFFER_SIZE, NULL, "P2/4 FOOTER", "PARTIAL_FOOTER #1");
    epd_test_pattern_draw_footer_probe(current, EPD_GDEY0426T82_BUFFER_SIZE, "PARTIAL_FOOTER #1");

    if (!find_changed_region(previous, current, EPD_GDEY0426T82_BUFFER_SIZE, &x, &y, &width, &height)) {
        goto cleanup;
    }
    ok = y >= 780U && height <= 20U && width > 0U;

cleanup:
    free(previous);
    free(current);
    return ok;
}

static bool app_reader_request_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;
    uint8_t *page = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool ok = false;

    if (page == NULL) {
        return false;
    }

    memset(&model, 0, sizeof(model));
    memset(page, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    ink_tuning_lab_init(&model.lab);
    model.lab.refresh_profile = INK_TUNING_REFRESH_CUSTOM_LUT_A;
    model.lab.force_full_refresh = false;
    model.lab.reader_white_refresh_pending = false;
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 64U;
    model.reader_session.total_pages = 321U;
    model.reader_session.current_chapter_index = 6U;
    model.reader_session.total_chapters = 19U;
    snprintf(
        model.reader_session.current_chapter_name,
        sizeof(model.reader_session.current_chapter_name),
        "%s",
        "二 赞成与反对");
    if (!ink_reader_session_set_prepared_page(&model.reader_session, page, EPD_GDEY0426T82_BUFFER_SIZE)) {
        free(page);
        return false;
    }

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT, &request)) {
        free(page);
        return false;
    }

    ok = request.use_bitmap_page
        && request.bitmap_page_buffer != NULL
        && request.bitmap_page_length == EPD_GDEY0426T82_BUFFER_SIZE
        && !request.use_native_page
        && !request.force_fixed_footer_partial
        && strcmp(request.overlay_left, "二 赞成与反对") == 0
        && request.refresh_profile == INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY
        && strcmp(request.overlay_right, "20% 65/321") == 0;
    free(page);
    return ok;
}

static bool app_reader_white_request_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;
    uint8_t *page = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool ok = false;

    if (page == NULL) {
        return false;
    }

    memset(&model, 0, sizeof(model));
    memset(page, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    ink_tuning_lab_init(&model.lab);
    model.lab.refresh_profile = INK_TUNING_REFRESH_CUSTOM_LUT_A;
    model.lab.force_full_refresh = false;
    model.lab.reader_white_refresh_pending = true;
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 64U;
    model.reader_session.total_pages = 321U;
    model.reader_session.current_chapter_index = 6U;
    model.reader_session.total_chapters = 19U;
    snprintf(
        model.reader_session.current_chapter_name,
        sizeof(model.reader_session.current_chapter_name),
        "%s",
        "二 赞成与反对");
    if (!ink_reader_session_set_prepared_page(&model.reader_session, page, EPD_GDEY0426T82_BUFFER_SIZE)) {
        free(page);
        return false;
    }

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_BACK, &request)) {
        free(page);
        return false;
    }

    ok = request.force_white_page
        && !request.full_refresh
        && !request.use_bitmap_page
        && !request.use_native_page
        && !request.use_footer_overlay
        && strcmp(request.overlay_left, "二 赞成与反对") == 0
        && strcmp(request.overlay_right, "20% 65/321") == 0;
    free(page);
    return ok;
}

static bool app_reader_fast_browse_request_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 64U;
    model.reader_session.total_pages = 321U;
    model.reader_session.current_chapter_index = 6U;
    model.reader_session.total_chapters = 19U;
    snprintf(
        model.reader_session.current_chapter_name,
        sizeof(model.reader_session.current_chapter_name),
        "%s",
        "二 赞成与反对");
    model.fast_browse.active = true;
    model.fast_browse.origin_page = 64U;
    model.fast_browse.target_page = 99U;
    model.fast_browse.total_pages = 321U;

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    return request.use_fast_browse_overlay
        && request.force_fixed_footer_partial
        && !request.use_bitmap_page
        && !request.use_native_page
        && strcmp(request.overlay_left, "二 赞成与反对") == 0
        && strcmp(request.overlay_right, "31% 100/321") == 0;
}

static bool app_reader_fast_browse_commit_request_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;
    uint8_t *page = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool ok = false;

    if (page == NULL) {
        return false;
    }

    memset(&model, 0, sizeof(model));
    memset(page, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 119U;
    model.reader_session.total_pages = 1679U;
    snprintf(
        model.reader_session.current_chapter_name,
        sizeof(model.reader_session.current_chapter_name),
        "%s",
        "七 种类不明的鲸鱼");
    if (!ink_reader_session_set_prepared_page(&model.reader_session, page, EPD_GDEY0426T82_BUFFER_SIZE)) {
        free(page);
        return false;
    }
    model.reader_fast_full_commit_pending = true;

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        free(page);
        return false;
    }

    ok = request.force_fast_full_commit
        && !request.use_fast_browse_overlay
        && !request.force_fixed_footer_partial
        && request.refresh_profile == INK_TUNING_REFRESH_FAST_FULL
        && request.use_bitmap_page;
    model.reader_fast_full_commit_pending = false;
    free(page);
    return ok;
}

static bool app_library_return_full_refresh_request_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    ink_runtime_shell_init(&model.shell);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    model.shell.full_refresh_requested = true;

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_BACK, &request)) {
        return false;
    }

    return request.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && request.full_refresh
        && !request.force_fast_full_commit
        && !request.use_footer_overlay;
}

static bool app_library_request_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    ink_runtime_shell_init(&model.shell);
    model.browser.entry_count = 1U;
    model.browser.selected_index = 0U;
    snprintf(model.browser.mount_point, sizeof(model.browser.mount_point), "%s", "/sdcard/books");
    snprintf(model.browser.current_path, sizeof(model.browser.current_path), "%s", "/sdcard/books");
    model.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(model.browser.entries[0].name, sizeof(model.browser.entries[0].name), "%s", "A.XTC");
    snprintf(model.browser.entries[0].full_path, sizeof(model.browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    return request.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && request.use_font
        && !request.use_bitmap_page
        && !request.use_native_page
        && !request.use_footer_overlay
        && request.full_refresh
        && strcmp(request.shell_view.title, "书库") == 0
        && strcmp(request.shell_view.line1, ">A.XTC") == 0
        && request.overlay_left[0] == '\0'
        && request.overlay_right[0] == '\0';
}

static bool app_debug_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.lab.current_page = INK_TUNING_PAGE_DETAIL;
    model.lab.refresh_profile = INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER;
    model.lab.force_full_refresh = false;
    model.lab.reader_white_refresh_pending = false;
    if (strcmp(ink_app_shell_page_name(INK_RUNTIME_SHELL_PAGE_LIBRARY), "LIBRARY") != 0) {
        return false;
    }
    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT, &request)) {
        return false;
    }
    return request.use_footer_overlay
        && !request.full_refresh
        && request.refresh_profile == INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER
        && strcmp(request.overlay_left, "P3/5 DETAIL") == 0
        && strcmp(request.overlay_right, "PARTIAL_FOOTER #0") == 0;
}

static bool app_full_refresh_routing_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.lab.refresh_profile = INK_TUNING_REFRESH_CUSTOM_LUT_A;
    model.lab.force_full_refresh = true;

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    return request.full_refresh
        && request.refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_A;
}

static bool app_grid_compare_request_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;
    uint16_t x = 0U;
    uint16_t y = 0U;
    uint16_t width = 0U;
    uint16_t height = 0U;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    model.lab.current_page = INK_TUNING_PAGE_GRID_COMPARE;
    model.lab.grid_compare_active = true;
    model.lab.force_full_refresh = false;
    model.lab.reader_white_refresh_pending = false;
    model.lab.grid_compare_step = 1U;

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    if (!request.use_grid_compare_variant
        || request.use_footer_overlay
        || request.grid_compare_variant_index != 0U
        || strcmp(request.overlay_left, "P5/5 GRID_COMPARE") != 0
        || strcmp(request.overlay_right, "GRID P1 01/16") != 0) {
        return false;
    }

    if (!grid_compare_cell_region(request.grid_compare_variant_index, &x, &y, &width, &height)) {
        return false;
    }
    return x == 16U && y == 72U && width == 104U && height == 166U;
}

static bool app_large_reader_partial_routing_self_test(void)
{
    ink_display_request_t request;

    memset(&request, 0, sizeof(request));
    request.refresh_profile = INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY;
    request.use_bitmap_page = true;
    if (!should_promote_reader_partial_to_full_window(&request, 435U, 768U)) {
        return false;
    }
    if (should_promote_reader_partial_to_full_window(&request, 200U, 768U)) {
        return false;
    }
    request.use_bitmap_page = false;
    return !should_promote_reader_partial_to_full_window(&request, 435U, 768U);
}

static bool app_reader_partial_init_reuse_self_test(void)
{
    ink_app_context_t app;
    ink_display_request_t request;

    memset(&app, 0, sizeof(app));
    memset(&request, 0, sizeof(request));

    app.model.lab.consecutive_partial_count = 1U;
    request.use_bitmap_page = true;
    request.refresh_profile = INK_TUNING_REFRESH_CUSTOM_LUT_A;
    if (!should_reuse_reader_partial_init(&app, &request)) {
        return false;
    }

    request.refresh_profile = INK_TUNING_REFRESH_CUSTOM_LUT_B;
    if (should_reuse_reader_partial_init(&app, &request)) {
        return false;
    }

    request.refresh_profile = INK_TUNING_REFRESH_CUSTOM_LUT_A;
    request.full_refresh = true;
    if (should_reuse_reader_partial_init(&app, &request)) {
        return false;
    }

    memset(&request, 0, sizeof(request));
    app.model.lab.consecutive_partial_count = 1U;
    request.force_fixed_footer_partial = true;
    request.refresh_profile = INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY;
    if (!should_reuse_reader_partial_init(&app, &request)) {
        return false;
    }

    app.model.lab.consecutive_partial_count = 0U;
    if (should_reuse_reader_partial_init(&app, &request)) {
        return false;
    }

    return true;
}

static bool app_reader_full_window_stock_fallback_self_test(void)
{
    ink_display_request_t request;

    memset(&request, 0, sizeof(request));
    request.use_bitmap_page = true;
    request.refresh_profile = INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY;
    if (!should_fallback_reader_full_window_to_stock_partial(&request)) {
        return false;
    }

    request.use_grid_compare_variant = true;
    if (should_fallback_reader_full_window_to_stock_partial(&request)) {
        return false;
    }

    return true;
}

static bool app_footer_preview_route_self_test(void)
{
    ink_display_request_t request;

    memset(&request, 0, sizeof(request));
    request.page = INK_RUNTIME_SHELL_PAGE_READER;
    request.use_fast_browse_overlay = true;
    request.force_fixed_footer_partial = true;
    if (!should_route_footer_preview_to_full_window_stock_partial(&request)) {
        return false;
    }

    request.use_fast_browse_overlay = false;
    if (should_route_footer_preview_to_full_window_stock_partial(&request)) {
        return false;
    }

    request.use_fast_browse_overlay = true;
    request.force_fixed_footer_partial = false;
    if (should_route_footer_preview_to_full_window_stock_partial(&request)) {
        return false;
    }

    return true;
}

static bool app_fast_browse_cancel_policy_self_test(void)
{
    ink_app_context_t app;
    ink_display_request_t request;
    ink_epd_cancel_ctx_t cancel_ctx;

    memset(&app, 0, sizeof(app));
    memset(&request, 0, sizeof(request));
    memset(&cancel_ctx, 0, sizeof(cancel_ctx));

    ink_display_mailbox_init(&app.services.mailbox, NULL, NULL, NULL, NULL, NULL);
    request.page = INK_RUNTIME_SHELL_PAGE_READER;
    if (ink_display_mailbox_submit(&app.services.mailbox, &request) != 1U) {
        return false;
    }
    if (ink_display_mailbox_submit(&app.services.mailbox, &request) != 2U) {
        return false;
    }

    cancel_ctx.app = &app;
    cancel_ctx.seq = 1U;
    cancel_ctx.pin_during_transmitting = true;

    return epd_request_is_stale(&cancel_ctx, EPD_GDEY0426T82_PHASE_PREPARING)
        && !epd_request_is_stale(&cancel_ctx, EPD_GDEY0426T82_PHASE_TRANSMITTING);
}

static bool app_reader_cancel_policy_self_test(void)
{
    ink_display_request_t reader_request;
    ink_display_request_t library_request;

    memset(&reader_request, 0, sizeof(reader_request));
    memset(&library_request, 0, sizeof(library_request));

    reader_request.page = INK_RUNTIME_SHELL_PAGE_READER;
    reader_request.use_bitmap_page = true;
    if (!request_pins_during_transmitting(&reader_request)) {
        return false;
    }

    library_request.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    library_request.use_bitmap_page = true;
    if (request_pins_during_transmitting(&library_request)) {
        return false;
    }

    return true;
}

bool ink_app_render_self_test(void)
{
    if (!app_partial_region_self_test()) {
        printf("FAIL render partial_region\n");
        return false;
    }
    if (!app_custom_lut_request_self_test()) {
        printf("FAIL render custom_lut_request\n");
        return false;
    }
    if (!app_footer_probe_delta_self_test()) {
        printf("FAIL render footer_probe_delta\n");
        return false;
    }
    if (!app_reader_request_self_test()) {
        printf("FAIL render reader_request\n");
        return false;
    }
    if (!app_reader_white_request_self_test()) {
        printf("FAIL render reader_white_request\n");
        return false;
    }
    if (!app_reader_fast_browse_request_self_test()) {
        printf("FAIL render reader_fast_browse_request\n");
        return false;
    }
    if (!app_reader_fast_browse_commit_request_self_test()) {
        printf("FAIL render reader_fast_browse_commit_request\n");
        return false;
    }
    if (!app_library_return_full_refresh_request_self_test()) {
        printf("FAIL render library_return_full_refresh_request\n");
        return false;
    }
    if (!app_library_request_self_test()) {
        printf("FAIL render library_request\n");
        return false;
    }
    if (!app_debug_self_test()) {
        printf("FAIL render debug\n");
        return false;
    }
    if (!app_full_refresh_routing_self_test()) {
        printf("FAIL render full_refresh_routing\n");
        return false;
    }
    if (!app_grid_compare_request_self_test()) {
        printf("FAIL render grid_compare_request\n");
        return false;
    }
    if (!app_large_reader_partial_routing_self_test()) {
        printf("FAIL render large_reader_partial_routing\n");
        return false;
    }
    if (!app_reader_partial_init_reuse_self_test()) {
        printf("FAIL render reader_partial_init_reuse\n");
        return false;
    }
    if (!app_reader_full_window_stock_fallback_self_test()) {
        printf("FAIL render reader_full_window_stock_fallback\n");
        return false;
    }
    if (!app_footer_preview_route_self_test()) {
        printf("FAIL render footer_preview_route\n");
        return false;
    }
    if (!app_fast_browse_cancel_policy_self_test()) {
        printf("FAIL render fast_browse_cancel_policy\n");
        return false;
    }
    if (!app_reader_cancel_policy_self_test()) {
        printf("FAIL render reader_cancel_policy\n");
        return false;
    }
    return true;
}
