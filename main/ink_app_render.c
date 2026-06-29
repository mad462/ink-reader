#include "ink_app_render.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/ink_launcher_app.h"
#include "apps/ink_photo_album_app.h"
#include "apps/ink_reader_app.h"
#include "apps/ink_usb_msc_app.h"
#include "apps/ink_voice_note_app.h"
#include "apps/ink_wifi_setup_app.h"
#include "esp_log.h"
#include "freertos/task.h"

#include "epd_test_pattern.h"
#include "ink_wifi_setup_ui.h"
#include "voice_note/voice_note_audio.h"
#include "voice_note/voice_note_model.h"

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
static void fill_launcher_page(
    uint8_t *buffer,
    size_t length,
    const ink_launcher_app_render_state_t *render_state);
static void launcher_set_pixel(uint8_t *buffer, int x, int y, bool black);
static void launcher_fill_rect(uint8_t *buffer, int x, int y, int w, int h, bool black);
static void launcher_fill_dither_rect(uint8_t *buffer, int x, int y, int w, int h, bool black);
static void launcher_draw_rect_outline(uint8_t *buffer, int x, int y, int w, int h, int thickness);
static void launcher_draw_hline(uint8_t *buffer, int x, int y, int w, bool black);
static void launcher_draw_vline(uint8_t *buffer, int x, int y, int h, bool black);
static void launcher_draw_text(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    uint8_t scale_divisor,
    bool inverted);
static void launcher_draw_book_icon(uint8_t *buffer, int x, int y, bool inverted);
static void launcher_draw_wifi_icon(uint8_t *buffer, int x, int y, bool inverted);
static void launcher_draw_photo_icon(uint8_t *buffer, int x, int y, bool inverted);
static void launcher_draw_cal_icon(uint8_t *buffer, int x, int y, bool inverted);
static void launcher_draw_usb_icon(uint8_t *buffer, int x, int y, bool inverted);
static void launcher_draw_row_icon(uint8_t *buffer, size_t index, int x, int y, bool inverted);
static void launcher_draw_chevron(uint8_t *buffer, int x, int y, bool inverted);
static void fill_reader_placeholder_page(uint8_t *buffer, size_t length);
static void fill_wifi_setup_page(
    uint8_t *buffer,
    size_t length,
    const ink_wifi_setup_app_render_state_t *state);
static void fill_photo_album_page(
    uint8_t *buffer,
    size_t length,
    const ink_photo_album_render_state_t *state);
static void fill_usb_msc_page(
    uint8_t *buffer,
    size_t length,
    const ink_usb_msc_app_render_state_t *render_state);
static void fill_voice_note_page(
    uint8_t *buffer,
    size_t length,
    const ink_voice_note_app_render_state_t *render_state);
static void compose_voice_note_card_subtitle(
    voice_note_tab_t tab,
    const ink_voice_note_app_state_t *state,
    const voice_note_note_t *note,
    char *dst,
    size_t dst_size);
static void compose_voice_note_recording_subtitle(
    const ink_voice_note_app_state_t *state,
    char *dst,
    size_t dst_size);
static void draw_voice_note_full_text_view(
    uint8_t *buffer,
    size_t length,
    const ink_voice_note_app_render_state_t *render_state,
    epd_test_pattern_reader_menu_overlay_t *overlay);
static size_t voice_note_render_visible_index_from_selection(
    const ink_voice_note_app_state_t *state,
    size_t selection_index);
static void compose_voice_note_meta_line(
    const voice_note_note_t *note,
    char *dst,
    size_t dst_size);
static size_t utf8_codepoint_length_from_lead(unsigned char lead);
static void utf8_copy_prefix_safe(char *dst, size_t dst_size, const char *src, size_t src_bytes);
static int measure_text_width_scaled(const ink_cpfont_t *font, const char *text, uint8_t scale_divisor);
static size_t compose_voice_note_wrapped_lines(
    const ink_cpfont_t *font,
    uint8_t scale_divisor,
    const char *text,
    int max_width_px,
    char lines[][VOICE_NOTE_TEXT_LENGTH],
    size_t max_lines);
static void usb_msc_draw_pill(
    uint8_t *buffer,
    int x,
    int y,
    int w,
    int h,
    bool selected);
static void usb_msc_draw_status_chip(
    uint8_t *buffer,
    const ink_cpfont_t *title_font,
    const ink_cpfont_t *meta_font,
    const char *title,
    const char *status_text);
static void draw_photo_album_list_page(
    uint8_t *buffer,
    size_t length,
    const ink_photo_album_render_state_t *render_state,
    const ink_cpfont_t *menu_font,
    const ink_cpfont_t *footer_font);
static bool photo_album_name_without_extension(
    const char *filename,
    char *dst,
    size_t dst_size);
static void photo_album_compose_list_title(
    size_t item_index,
    const char *filename,
    char *dst,
    size_t dst_size);
static bool fill_gray_calibration_planes(
    uint8_t *scratch,
    size_t scratch_length,
    uint8_t *lsb_plane,
    size_t lsb_length,
    uint8_t *msb_plane,
    size_t msb_length);
static bool render_library_overlay_to_buffer(
    uint8_t *buffer,
    size_t length,
    const ink_cpfont_t *menu_font,
    const ink_cpfont_t *footer_font,
    const uint8_t *previous_framebuffer,
    size_t previous_length,
    const ink_display_request_t *request);
static bool render_model_to_buffer(
    uint8_t *buffer,
    size_t length,
    const ink_app_render_model_t *model);
static void populate_overlay_header_meta(
    epd_test_pattern_reader_menu_overlay_t *overlay,
    const ink_system_services_t *services);
static bool app_render_request_self_test(void);
static bool render_grid_compare_variant(
    ink_app_context_t *app,
    const ink_display_request_t *request);
static ink_ui_model_t *request_owner_ui_model(
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
    const char *route_name,
    bool gray_refresh,
    bool interrupt_allowed,
    bool pinned_during_tx,
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
static bool app_overlay_never_promotes_to_reader_full_window_self_test(void);
static bool app_reader_partial_init_reuse_self_test(void);
static bool app_reader_full_window_stock_fallback_self_test(void);
static bool app_reader_full_window_stock_fallback_avoids_native_path_self_test(void);
static bool app_footer_preview_route_self_test(void);
static bool app_fast_browse_cancel_policy_self_test(void);
static bool app_reader_cancel_policy_self_test(void);
static bool app_render_model_launcher_self_test(void);
static bool app_render_model_reader_placeholder_self_test(void);
static bool app_render_model_reader_subsystem_self_test(void);
static bool app_library_overlay_render_self_test(void);
static bool app_render_model_usb_msc_self_test(void);
static bool app_reader_menu_request_self_test(void);
static bool app_reader_text_turn_strategy_self_test(void);
static bool app_reader_overlay_strategy_self_test(void);
static bool app_reader_menu_overlay_render_self_test(void);
static bool app_reader_menu_partial_window_policy_self_test(void);
static bool app_reader_loading_overlay_request_self_test(void);
static bool app_reader_chapter_menu_request_layout_self_test(void);
static bool app_reader_loading_overlay_masks_background_self_test(void);
static bool app_library_overlay_header_meta_self_test(void);
static bool app_reader_loading_overlay_header_meta_self_test(void);
static bool app_render_model_wifi_setup_self_test(void);
static bool app_render_model_photo_album_self_test(void);
static bool app_render_model_voice_note_self_test(void);
static bool app_render_model_usb_msc_keeps_time_badge_self_test(void);
static bool app_library_overlay_tabs_follow_shell_width_self_test(void);
static bool app_library_overlay_selected_favorite_remains_visible_self_test(void);
static bool photo_album_list_truncation_self_test(void);
static bool photo_album_list_layout_self_test(void);
static bool gray_calibration_planes_self_test(void);
static bool app_overlay_menu_font_selection_self_test(void);
static bool app_render_request_strategy_roundtrip_self_test(void);
static bool app_render_request_photo_album_interrupt_self_test(void);
static bool render_pixel_is_white(const uint8_t *buffer, int x, int y);
static const ink_cpfont_t *select_page_font(const ink_system_services_t *services);
static const ink_cpfont_t *select_footer_font(const ink_system_services_t *services);
static const ink_cpfont_t *select_menu_font(const ink_system_services_t *services);
static const ink_cpfont_t *select_small_text_font(const ink_system_services_t *services);

static const ink_cpfont_t *select_page_font(const ink_system_services_t *services)
{
    if (services == NULL) {
        return NULL;
    }
    if (ink_cpfont_is_loaded(&services->reader_font)) {
        return &services->reader_font;
    }
    if (ink_cpfont_is_loaded(&services->footer_font)) {
        return &services->footer_font;
    }
    if (ink_cpfont_is_loaded(&services->menu_font)) {
        return &services->menu_font;
    }
    return NULL;
}

static const ink_cpfont_t *select_footer_font(const ink_system_services_t *services)
{
    if (services == NULL) {
        return NULL;
    }
    if (ink_cpfont_is_loaded(&services->footer_font)) {
        return &services->footer_font;
    }
    if (ink_cpfont_is_loaded(&services->reader_font)) {
        return &services->reader_font;
    }
    if (ink_cpfont_is_loaded(&services->menu_font)) {
        return &services->menu_font;
    }
    return NULL;
}

static const ink_cpfont_t *select_menu_font(const ink_system_services_t *services)
{
    if (services == NULL) {
        return NULL;
    }
    if (ink_cpfont_is_loaded(&services->menu_font)) {
        return &services->menu_font;
    }
    if (ink_cpfont_is_loaded(&services->reader_font)) {
        return &services->reader_font;
    }
    if (ink_cpfont_is_loaded(&services->footer_font)) {
        return &services->footer_font;
    }
    return NULL;
}

static const ink_cpfont_t *select_small_text_font(const ink_system_services_t *services)
{
    if (services == NULL) {
        return NULL;
    }
    if (ink_cpfont_is_loaded(&services->footer_font)) {
        return &services->footer_font;
    }
    if (ink_cpfont_is_loaded(&services->reader_font)) {
        return &services->reader_font;
    }
    if (ink_cpfont_is_loaded(&services->menu_font)) {
        return &services->menu_font;
    }
    return NULL;
}

static void populate_overlay_header_meta(
    epd_test_pattern_reader_menu_overlay_t *overlay,
    const ink_system_services_t *services)
{
    if (overlay == NULL) {
        return;
    }
    ink_system_services_get_time_badge(
        services,
        overlay->header_meta,
        sizeof(overlay->header_meta));
}

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

const char *ink_refresh_strategy_name(ink_refresh_strategy_t strategy)
{
    switch (strategy) {
        case INK_REFRESH_STRATEGY_PAGE_TRANSITION_FULL:
            return "PAGE_TRANSITION_FULL";
        case INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST:
            return "BW_UI_PAGE_FAST";
        case INK_REFRESH_STRATEGY_BW_UI_LIST_LOCAL:
            return "BW_UI_LIST_LOCAL";
        case INK_REFRESH_STRATEGY_OVERLAY_LOCAL_UPDATE:
            return "OVERLAY_LOCAL_UPDATE";
        case INK_REFRESH_STRATEGY_READER_TEXT_TURN:
            return "READER_TEXT_TURN";
        case INK_REFRESH_STRATEGY_READER_HOLD_PREVIEW:
            return "READER_HOLD_PREVIEW";
        case INK_REFRESH_STRATEGY_READER_TEXT_CLEANUP:
            return "READER_TEXT_CLEANUP";
        case INK_REFRESH_STRATEGY_GRAY_IMAGE_PREVIEW:
            return "GRAY_IMAGE_PREVIEW";
        case INK_REFRESH_STRATEGY_GRAY_IMAGE_INTERRUPTIBLE:
            return "GRAY_IMAGE_INTERRUPTIBLE";
        case INK_REFRESH_STRATEGY_GRAY_IMAGE_SETTLE:
            return "GRAY_IMAGE_SETTLE";
        case INK_REFRESH_STRATEGY_LAB_EXPLICIT_MODE:
            return "LAB_EXPLICIT_MODE";
        case INK_REFRESH_STRATEGY_NONE:
        default:
            return "NONE";
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

    ESP_LOGD(
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

    ESP_LOGD(
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
    const char *route_name,
    bool gray_refresh,
    bool interrupt_allowed,
    bool pinned_during_tx,
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
    ESP_LOGD(
        TAG,
        "render seq=%u t=%ums page=%s strategy=%s route=%s mode=%s gray=%d interrupt=%d pinned=%d cmd=%s since_cmd=%ums view=%ums draw=%ums diff=%ums epd=%ums total=%ums dirty=%u,%u %ux%u cache=+%u/-%u evict+%u ret=%s",
        (unsigned)s_render_sequence,
        (unsigned)now_ms,
        ink_app_shell_page_name(request->page),
        ink_refresh_strategy_name(request->refresh_strategy),
        route_name != NULL ? route_name : "none",
        request->full_refresh ? "full" : "partial",
        gray_refresh ? 1 : 0,
        interrupt_allowed ? 1 : 0,
        pinned_during_tx ? 1 : 0,
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

    if (request->refresh_strategy == INK_REFRESH_STRATEGY_READER_HOLD_PREVIEW
        || request->refresh_strategy == INK_REFRESH_STRATEGY_GRAY_IMAGE_INTERRUPTIBLE) {
        return false;
    }

    if (request->use_fast_browse_overlay && request->force_fixed_footer_partial) {
        return true;
    }

    if (request->use_reader_hold_navigation) {
        return false;
    }

    if (request->use_aggressive_interrupt) {
        return false;
    }

    return request->page == INK_RUNTIME_SHELL_PAGE_READER
        && !request->use_fast_browse_overlay
        && !request->use_reader_hold_navigation;
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
        case INK_TUNING_PAGE_GRAY_CAL:
            epd_test_pattern_fill_gray_calibration_page(buffer, length);
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

static void fill_launcher_page(
    uint8_t *buffer,
    size_t length,
    const ink_launcher_app_render_state_t *render_state)
{
    const ink_launcher_app_state_t *state = render_state != NULL ? render_state->state : NULL;
    const ink_cpfont_t *title_font = render_state != NULL ? render_state->menu_font : NULL;
    const ink_cpfont_t *row_font = render_state != NULL ? render_state->menu_font : NULL;
    const ink_cpfont_t *meta_font = render_state != NULL ? render_state->footer_font : NULL;
    static const char *const kTitles[6] = {
        "书库",
        "语音便签",
        "无线网络",
        "相册",
        "灰阶校准",
        "USB 磁盘",
    };
    static const char *const kMeta[6] = {
        "打开图书与最近阅读",
        "录音并生成语音标签",
        "连接网络与输入密码",
        "浏览 TF 卡灰阶图片",
        "检查四阶灰度效果",
        "共享 TF 卡到电脑",
    };
    epd_test_pattern_list_layout_t layout = epd_test_pattern_crosspoint_list_layout();
    epd_test_pattern_header_spec_t header = {0};
    size_t selected_index = 0U;

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    if (state != NULL && state->selected_app_index < 6U) {
        selected_index = state->selected_app_index;
    }

    header.title = "启动器";
    header.meta = render_state != NULL ? render_state->header_meta : "UP 00:00";
    header.title_font = title_font;
    header.meta_font = meta_font;
    epd_test_pattern_draw_crosspoint_header(buffer, &header);
    layout.content_x_inset = 64;

    for (size_t i = 0U; i < 6U; ++i) {
        const int row_y = layout.list_y + (int)i * (layout.row_h + layout.row_gap);
        const bool selected = i == selected_index;
        const int icon_x = layout.list_x + 16;
        const int chevron_x = layout.list_x + layout.row_w - 26;
        epd_test_pattern_list_row_t row = {
            .title = kTitles[i],
            .line1 = kMeta[i],
            .line2 = "",
            .selected = selected,
            .emphasized = false,
        };

        epd_test_pattern_draw_crosspoint_list_row(
            buffer,
            &layout,
            i,
            &row,
            row_font,
            meta_font);
        launcher_draw_row_icon(buffer, i, icon_x, row_y + ((layout.row_h - 20) / 2), false);
        launcher_draw_chevron(buffer, chevron_x, row_y + 22, false);
    }
}

static void launcher_set_pixel(uint8_t *buffer, int x, int y, bool black)
{
    const size_t index = (size_t)y * (EPD_GDEY0426T82_WIDTH / 8U) + (size_t)(x / 8);
    const uint8_t mask = (uint8_t)(0x80U >> (x % 8));

    if (buffer == NULL || x < 0 || x >= EPD_GDEY0426T82_WIDTH || y < 0 || y >= EPD_GDEY0426T82_HEIGHT) {
        return;
    }
    if (black) {
        buffer[index] &= (uint8_t)~mask;
    } else {
        buffer[index] |= mask;
    }
}

static void launcher_fill_rect(uint8_t *buffer, int x, int y, int w, int h, bool black)
{
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            launcher_set_pixel(buffer, xx, yy, black);
        }
    }
}

static size_t utf8_codepoint_length_from_lead(unsigned char lead)
{
    if ((lead & 0x80U) == 0U) {
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2U;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3U;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4U;
    }
    return 1U;
}

static void utf8_copy_prefix_safe(char *dst, size_t dst_size, const char *src, size_t src_bytes)
{
    size_t copied = 0U;

    if (dst == NULL || dst_size == 0U) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }

    while (copied < src_bytes && src[copied] != '\0' && copied + 1U < dst_size) {
        dst[copied] = src[copied];
        ++copied;
    }
    dst[copied] = '\0';
}

static int measure_text_width_scaled(const ink_cpfont_t *font, const char *text, uint8_t scale_divisor)
{
    int width_px = 0;

    if (font != NULL && ink_cpfont_is_loaded(font)) {
        if (scale_divisor <= 1U) {
            if (ink_cpfont_draw_text_bw((ink_cpfont_t *)font, NULL, 0, 0, text, &width_px) == ESP_OK) {
                return width_px;
            }
        } else if (ink_cpfont_draw_text_bw_scaled(
                (ink_cpfont_t *)font,
                NULL,
                0,
                0,
                text,
                scale_divisor,
                &width_px) == ESP_OK) {
            return width_px;
        }
    }

    return (int)strlen(text) * 12;
}

static size_t compose_voice_note_wrapped_lines(
    const ink_cpfont_t *font,
    uint8_t scale_divisor,
    const char *text,
    int max_width_px,
    char lines[][VOICE_NOTE_TEXT_LENGTH],
    size_t max_lines)
{
    size_t line_count = 0U;
    size_t start = 0U;

    if (text == NULL || lines == NULL || max_lines == 0U) {
        return 0U;
    }

    while (text[start] != '\0' && line_count < max_lines) {
        size_t cursor = start;
        size_t candidate_end = start;
        size_t last_break = start;
        bool saw_breakable = false;
        char candidate[VOICE_NOTE_TEXT_LENGTH];

        while (text[cursor] != '\0') {
            const size_t cp_len = utf8_codepoint_length_from_lead((unsigned char)text[cursor]);
            size_t actual_len = 0U;

            while (actual_len < cp_len && text[cursor + actual_len] != '\0') {
                ++actual_len;
            }
            if (actual_len == 0U) {
                break;
            }

            candidate_end = cursor + actual_len;
            utf8_copy_prefix_safe(candidate, sizeof(candidate), text + start, candidate_end - start);
            if (measure_text_width_scaled(font, candidate, scale_divisor) > max_width_px) {
                if (saw_breakable && last_break > start) {
                    candidate_end = last_break;
                } else if (candidate_end > start) {
                    candidate_end = cursor;
                }
                break;
            }

            if (text[cursor] == ' ') {
                saw_breakable = true;
                last_break = cursor;
            }
            cursor += actual_len;
        }

        if (candidate_end <= start) {
            candidate_end = cursor > start ? cursor : (start + 1U);
        }

        utf8_copy_prefix_safe(lines[line_count], VOICE_NOTE_TEXT_LENGTH, text + start, candidate_end - start);
        while (lines[line_count][0] == ' ') {
            memmove(lines[line_count], lines[line_count] + 1, strlen(lines[line_count]));
        }
        while (strlen(lines[line_count]) > 0U
            && lines[line_count][strlen(lines[line_count]) - 1U] == ' ') {
            lines[line_count][strlen(lines[line_count]) - 1U] = '\0';
        }
        ++line_count;

        start = candidate_end;
        while (text[start] == ' ') {
            ++start;
        }
    }

    return line_count;
}

static void launcher_fill_dither_rect(uint8_t *buffer, int x, int y, int w, int h, bool black)
{
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            if (((xx + yy) & 1) == 0) {
                launcher_set_pixel(buffer, xx, yy, black);
            }
        }
    }
}

static void launcher_draw_rect_outline(uint8_t *buffer, int x, int y, int w, int h, int thickness)
{
    launcher_fill_rect(buffer, x, y, w, thickness, true);
    launcher_fill_rect(buffer, x, y + h - thickness, w, thickness, true);
    launcher_fill_rect(buffer, x, y, thickness, h, true);
    launcher_fill_rect(buffer, x + w - thickness, y, thickness, h, true);
}

static void launcher_draw_hline(uint8_t *buffer, int x, int y, int w, bool black)
{
    launcher_fill_rect(buffer, x, y, w, 1, black);
}

static void launcher_draw_vline(uint8_t *buffer, int x, int y, int h, bool black)
{
    launcher_fill_rect(buffer, x, y, 1, h, black);
}

static void launcher_draw_text(
    uint8_t *buffer,
    const ink_cpfont_t *font,
    int x,
    int y,
    const char *text,
    uint8_t scale_divisor,
    bool inverted)
{
    if (font != NULL && ink_cpfont_is_loaded(font)) {
        if (scale_divisor == 0U) {
            scale_divisor = 1U;
        }
        if (inverted) {
            (void)ink_cpfont_draw_text_bw_scaled_inverted(
                (ink_cpfont_t *)font,
                buffer,
                x,
                y,
                text != NULL ? text : "",
                scale_divisor,
                NULL);
        } else {
            (void)ink_cpfont_draw_text_bw_scaled(
                (ink_cpfont_t *)font,
                buffer,
                x,
                y,
                text != NULL ? text : "",
                scale_divisor,
                NULL);
        }
    }
}

static void launcher_draw_book_icon(uint8_t *buffer, int x, int y, bool inverted)
{
    const bool ink = true;

    launcher_draw_rect_outline(buffer, x, y, 18, 20, 1);
    launcher_draw_vline(buffer, x + 4, y + 2, 16, ink);
    launcher_draw_hline(buffer, x + 7, y + 5, 8, ink);
    launcher_draw_hline(buffer, x + 7, y + 9, 8, ink);
    launcher_draw_hline(buffer, x + 7, y + 13, 6, ink);
}

static void launcher_draw_wifi_icon(uint8_t *buffer, int x, int y, bool inverted)
{
    const bool ink = true;

    launcher_set_pixel(buffer, x + 9, y + 16, ink);
    launcher_draw_hline(buffer, x + 7, y + 14, 5, ink);
    launcher_draw_hline(buffer, x + 5, y + 11, 9, ink);
    launcher_draw_hline(buffer, x + 3, y + 8, 13, ink);
    launcher_draw_hline(buffer, x + 1, y + 5, 17, ink);
}

static void launcher_draw_photo_icon(uint8_t *buffer, int x, int y, bool inverted)
{
    const bool ink = true;

    launcher_draw_rect_outline(buffer, x, y, 20, 18, 1);
    launcher_fill_rect(buffer, x + 4, y + 11, 5, 3, ink);
    launcher_fill_rect(buffer, x + 9, y + 9, 6, 5, ink);
    launcher_fill_rect(buffer, x + 14, y + 7, 3, 7, ink);
    launcher_fill_rect(buffer, x + 13, y + 3, 3, 3, ink);
}

static void launcher_draw_cal_icon(uint8_t *buffer, int x, int y, bool inverted)
{
    const bool ink = true;

    launcher_draw_rect_outline(buffer, x, y, 20, 20, 1);
    launcher_fill_rect(buffer, x + 2, y + 2, 8, 8, ink);
    launcher_fill_dither_rect(buffer, x + 10, y + 2, 8, 8, ink);
    launcher_fill_rect(buffer, x + 2, y + 10, 8, 8, false);
    launcher_draw_rect_outline(buffer, x + 2, y + 10, 8, 8, 1);
    launcher_fill_rect(buffer, x + 10, y + 10, 8, 8, ink);
}

static void launcher_draw_usb_icon(uint8_t *buffer, int x, int y, bool inverted)
{
    const bool ink = true;

    launcher_draw_vline(buffer, x + 9, y + 2, 12, ink);
    launcher_draw_hline(buffer, x + 7, y + 2, 5, ink);
    launcher_draw_hline(buffer, x + 3, y + 8, 13, ink);
    launcher_draw_vline(buffer, x + 4, y + 8, 7, ink);
    launcher_draw_vline(buffer, x + 14, y + 8, 7, ink);
    launcher_fill_rect(buffer, x + 2, y + 15, 5, 3, ink);
    launcher_fill_rect(buffer, x + 12, y + 15, 5, 3, ink);
}

static void launcher_draw_row_icon(uint8_t *buffer, size_t index, int x, int y, bool inverted)
{
    switch (index) {
        case 0U:
            launcher_draw_book_icon(buffer, x, y, inverted);
            break;
        case 1U:
            launcher_draw_book_icon(buffer, x, y, inverted);
            break;
        case 2U:
            launcher_draw_wifi_icon(buffer, x, y, inverted);
            break;
        case 3U:
            launcher_draw_photo_icon(buffer, x, y, inverted);
            break;
        case 4U:
            launcher_draw_cal_icon(buffer, x, y, inverted);
            break;
        case 5U:
        default:
            launcher_draw_usb_icon(buffer, x, y, inverted);
            break;
    }
}

static void launcher_draw_chevron(uint8_t *buffer, int x, int y, bool inverted)
{
    const bool ink = true;

    launcher_set_pixel(buffer, x, y, ink);
    launcher_set_pixel(buffer, x + 1, y + 1, ink);
    launcher_set_pixel(buffer, x + 2, y + 2, ink);
    launcher_set_pixel(buffer, x + 3, y + 3, ink);
    launcher_set_pixel(buffer, x + 2, y + 4, ink);
    launcher_set_pixel(buffer, x + 1, y + 5, ink);
    launcher_set_pixel(buffer, x, y + 6, ink);
}

static void fill_reader_placeholder_page(uint8_t *buffer, size_t length)
{
    epd_test_pattern_fill_text_page(
        buffer,
        length,
        "Reader App",
        "Phase 1",
        "Back to Launcher",
        "",
        "",
        "");
}

static void usb_msc_draw_pill(
    uint8_t *buffer,
    int x,
    int y,
    int w,
    int h,
    bool selected)
{
    const int radius = h / 2;
    const bool border = true;

    if (buffer == NULL || w <= 0 || h <= 0) {
        return;
    }

    launcher_fill_rect(buffer, x + radius, y, w - radius * 2, h, selected);
    launcher_fill_rect(buffer, x, y + radius, radius, h - radius * 2, selected);
    launcher_fill_rect(buffer, x + w - radius, y + radius, radius, h - radius * 2, selected);

    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < radius; ++xx) {
            const int dx = radius - 1 - xx;
            const int dy = radius - 1 - yy;
            const int dy_bottom = yy - radius;
            const bool top_half = yy < radius;
            const int cy = top_half ? dy : dy_bottom;
            if (dx * dx + cy * cy <= (radius - 1) * (radius - 1)) {
                launcher_set_pixel(buffer, x + xx, y + yy, selected);
                launcher_set_pixel(buffer, x + w - 1 - xx, y + yy, selected);
            }
        }
    }

    if (border) {
        launcher_draw_rect_outline(buffer, x + radius / 2, y, w - radius, h, 1);
    }
}

static void usb_msc_draw_status_chip(
    uint8_t *buffer,
    const ink_cpfont_t *title_font,
    const ink_cpfont_t *meta_font,
    const char *title,
    const char *status_text)
{
    const int chip_x = 48;
    const int chip_y = 286;
    const int chip_w = EPD_GDEY0426T82_WIDTH - 96;
    const int chip_h = 60;

    usb_msc_draw_pill(buffer, chip_x, chip_y, chip_w, chip_h, false);
    launcher_draw_text(buffer, title_font, chip_x + 20, chip_y + 18, title != NULL ? title : "U盘模式", 2U, false);
    if (status_text != NULL && status_text[0] != '\0') {
        launcher_draw_text(buffer, meta_font, chip_x + chip_w - 84, chip_y + 22, status_text, 1U, false);
    }
}

static void fill_wifi_setup_page(
    uint8_t *buffer,
    size_t length,
    const ink_wifi_setup_app_render_state_t *state)
{
    ink_wifi_setup_ui_cursor_t cursor = {0};
    ink_wifi_setup_ui_header_t header = {
        .title = "无线网络",
        .meta = state != NULL ? state->header_meta : "",
        .subtitle = "可用网络",
    };

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, length);
    if (state == NULL || state->view == NULL) {
        epd_test_pattern_fill_text_page(
            buffer,
            length,
            "WiFi Setup",
            "No view",
            "",
            "",
            "",
            "");
        return;
    }

    cursor.column = state->view->keyboard_column;
    cursor.row = state->view->keyboard_row;
    ink_wifi_setup_ui_draw_screen(
        buffer,
        state->view->keyboard_layer,
        &cursor,
        &state->view->keyboard_text,
        &state->view->wifi,
        &state->fonts,
        &header);
}

static void fill_photo_album_page(
    uint8_t *buffer,
    size_t length,
    const ink_photo_album_render_state_t *render_state)
{
    const ink_cpfont_t *page_font = render_state != NULL
        ? (const ink_cpfont_t *)render_state->menu_font
        : NULL;
    const ink_photo_album_app_state_t *state = render_state != NULL ? render_state->state : NULL;
    const ink_cpfont_t *small_text_font = render_state != NULL
        ? (const ink_cpfont_t *)render_state->footer_font
        : NULL;

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, length);
    if (state == NULL) {
        epd_test_pattern_fill_text_page(buffer, length, "相册", "No state", "", "", "", "");
        return;
    }

    if (state->view_mode == INK_PHOTO_ALBUM_VIEW_LIST) {
        draw_photo_album_list_page(buffer, length, render_state, page_font, small_text_font);
        return;
    }

    if (state->image_loaded && state->lsb_plane != NULL && state->msb_plane != NULL) {
        return;
    }

    {
        epd_test_pattern_list_row_t rows[2];
        epd_test_pattern_rows_page_spec_t spec;
        char meta[24];

        memset(rows, 0, sizeof(rows));
        snprintf(meta, sizeof(meta), "%u item%s", (unsigned)state->total_count, state->total_count == 1U ? "" : "s");
        rows[0].title = state->status_text[0] != '\0' ? state->status_text : "图片读取失败";
        rows[0].line1 = state->tf_unavailable ? "Check TF card and photo catalog" : "Confirm to return to the list";
        rows[0].line2 = "";
        rows[0].selected = true;
        rows[1].title = state->current_name[0] != '\0' ? state->current_name : "No current photo";
        rows[1].line1 = state->total_count > 0U ? "Preview unavailable" : "No photos found";
        rows[1].line2 = "";
        rows[1].selected = false;
        rows[1].emphasized = true;

        memset(&spec, 0, sizeof(spec));
        spec.title = "Photos";
        spec.meta = meta;
        spec.rows = rows;
        spec.row_count = 2U;
        spec.title_font = page_font;
        spec.meta_font = small_text_font;
        spec.row_title_font = page_font;
        spec.row_meta_font = small_text_font;
        epd_test_pattern_fill_crosspoint_rows_page(buffer, length, &spec);
    }
}

static void fill_usb_msc_page(
    uint8_t *buffer,
    size_t length,
    const ink_usb_msc_app_render_state_t *render_state)
{
    const ink_usb_msc_app_state_t *state = render_state != NULL ? render_state->state : NULL;
    const ink_cpfont_t *title_font = render_state != NULL ? render_state->menu_font : NULL;
    const ink_cpfont_t *meta_font = render_state != NULL ? render_state->footer_font : NULL;
    const char *title = "USB 磁盘";
    const char *meta = render_state != NULL ? render_state->header_meta : "";
    epd_test_pattern_header_spec_t header = {0};
    const char *status_text = "关";

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, length);
    if (state != NULL) {
        title = state->title[0] != '\0' ? state->title : title;
        if (state->view == INK_USB_MSC_APP_VIEW_ACTIVE) {
            status_text = "开";
        } else if (state->view == INK_USB_MSC_APP_VIEW_ERROR) {
            status_text = "异常";
        } else {
            status_text = "关";
        }
    }

    header.title = title;
    header.meta = meta;
    header.title_font = title_font;
    header.meta_font = meta_font;
    epd_test_pattern_draw_crosspoint_header(buffer, &header);
    usb_msc_draw_status_chip(buffer, title_font, meta_font, "U盘模式", status_text);
}

static void fill_voice_note_page(
    uint8_t *buffer,
    size_t length,
    const ink_voice_note_app_render_state_t *render_state)
{
    const ink_voice_note_app_state_t *state = render_state != NULL ? render_state->state : NULL;
    epd_test_pattern_reader_menu_overlay_t *overlay = state != NULL ? state->popup_overlay : NULL;
    char popup_title[96];
    char overlay_meta[24];

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, length);
    if (state == NULL || overlay == NULL) {
        epd_test_pattern_fill_text_page(buffer, length, "语音便签", "状态不可用", "", "", "", "");
        return;
    }

    memset(overlay, 0, sizeof(*overlay));
    overlay->frameless_panel = true;
    overlay->compact_cards = false;
    overlay->bookmark_cards_tall = false;
    snprintf(overlay->header_title, sizeof(overlay->header_title), "%s", "语音便签");
    snprintf(
        overlay->header_meta,
        sizeof(overlay->header_meta),
        "%s",
        render_state != NULL ? render_state->header_meta : "");
    overlay->tab_count = 3U;
    snprintf(overlay->tabs[0].label, sizeof(overlay->tabs[0].label), "%s", "全部");
    snprintf(overlay->tabs[1].label, sizeof(overlay->tabs[1].label), "%s", "未完成");
    snprintf(overlay->tabs[2].label, sizeof(overlay->tabs[2].label), "%s", "已完成");
    for (size_t i = 0U; i < overlay->tab_count; ++i) {
        overlay->tabs[i].active = (size_t)state->active_tab == i;
        overlay->tabs[i].focused = false;
    }

    {
        const bool has_new_card =
            state->active_tab == VOICE_NOTE_TAB_ALL || state->active_tab == VOICE_NOTE_TAB_PENDING;
        size_t card_count = 0U;

        if (has_new_card) {
            snprintf(overlay->cards[card_count].title, sizeof(overlay->cards[card_count].title), "%s", "新建语音标签");
            compose_voice_note_recording_subtitle(
                state,
                overlay->cards[card_count].line1,
                sizeof(overlay->cards[card_count].line1));
            overlay->cards[card_count].line2[0] = '\0';
            overlay->cards[card_count].selected = state->selected_index == card_count;
            ++card_count;
        }

        for (size_t i = 0U; i < state->visible_note_count && card_count < EPD_TEST_PATTERN_MENU_CARD_CAPACITY; ++i) {
            char note_meta[48];
            const voice_note_note_t *note = &state->visible_notes[i];

            compose_voice_note_card_subtitle(
                state->active_tab,
                state,
                note,
                note_meta,
                sizeof(note_meta));
            snprintf(
                overlay->cards[card_count].title,
                sizeof(overlay->cards[card_count].title),
                "%s",
                note->title[0] != '\0' ? note->title : "这是一条语音标签");
            snprintf(
                overlay->cards[card_count].line1,
                sizeof(overlay->cards[card_count].line1),
                "%s",
                note_meta);
            overlay->cards[card_count].line2[0] = '\0';
            overlay->cards[card_count].selected = state->selected_index == card_count;
            ++card_count;
        }

        overlay->card_count = card_count;
    }

    epd_test_pattern_draw_reader_menu_overlay(
        buffer,
        length,
        render_state != NULL ? render_state->menu_font : NULL,
        render_state != NULL ? render_state->footer_font : NULL,
        overlay);

    if (state != NULL && state->popup_open && overlay != NULL) {
        epd_test_pattern_truncate_text_tail(
            render_state != NULL ? render_state->header_meta : "",
            overlay_meta,
            sizeof(overlay_meta),
            12U);
        snprintf(overlay->header_meta, sizeof(overlay->header_meta), "%s", overlay_meta);
        overlay->action_popup_open = true;
        overlay->action_count = 3U;
        snprintf(popup_title, sizeof(popup_title), "%s", "标签操作");
        snprintf(overlay->action_popup_title, sizeof(overlay->action_popup_title), "%s", popup_title);
        snprintf(overlay->actions[0].label, sizeof(overlay->actions[0].label), "%s", "查看全文");
        snprintf(overlay->actions[1].label, sizeof(overlay->actions[1].label), "%s", "标记为完成");
        snprintf(
            overlay->actions[2].label,
            sizeof(overlay->actions[2].label),
            "%s",
            "删除");
        for (size_t i = 0U; i < overlay->action_count; ++i) {
            overlay->actions[i].selected = i == state->popup_action_index;
        }
        epd_test_pattern_draw_reader_menu_overlay(
            buffer,
            length,
            render_state != NULL ? render_state->menu_font : NULL,
            render_state != NULL ? render_state->footer_font : NULL,
            overlay);
    } else if (state != NULL && state->popup_open) {
        epd_test_pattern_fill_text_page(
            buffer,
            length,
            "语音便签",
            "标签操作不可用",
            "请退出重试",
            "",
            "",
            "");
    } else if (state != NULL && state->full_text_open) {
        draw_voice_note_full_text_view(buffer, length, render_state, overlay);
    }
}

static size_t voice_note_render_visible_index_from_selection(
    const ink_voice_note_app_state_t *state,
    size_t selection_index)
{
    if (state == NULL) {
        return VOICE_NOTE_MAX_NOTES;
    }
    if (state->active_tab == VOICE_NOTE_TAB_ALL || state->active_tab == VOICE_NOTE_TAB_PENDING) {
        if (selection_index == 0U) {
            return VOICE_NOTE_MAX_NOTES;
        }
        return selection_index - 1U;
    }
    return selection_index;
}

static void compose_voice_note_recording_subtitle(
    const ink_voice_note_app_state_t *state,
    char *dst,
    size_t dst_size)
{
    char raw[80];

    if (dst == NULL || dst_size == 0U) {
        return;
    }

    dst[0] = '\0';
    if (state == NULL) {
        return;
    }

    if (state->snapshot.state == VOICE_NOTE_JOB_RECORDING) {
        uint32_t remaining_ms = 0U;
        uint32_t remaining_s = 0U;

        remaining_ms = state->snapshot.capture_duration_ms >= VOICE_NOTE_MAX_CAPTURE_MS
            ? 0U
            : (VOICE_NOTE_MAX_CAPTURE_MS - state->snapshot.capture_duration_ms);
        remaining_s = (remaining_ms + 999U) / 1000U;
        snprintf(raw, sizeof(raw), "正在录音  倒计时 %lus", (unsigned long)remaining_s);
    } else if (state->snapshot.busy && state->snapshot.active_note_id[0] == '\0') {
        snprintf(
            raw,
            sizeof(raw),
            "%s",
            state->snapshot.status_text[0] != '\0' ? state->snapshot.status_text : "处理中");
    } else {
        snprintf(
            raw,
            sizeof(raw),
            "%s",
            state->snapshot.status_text[0] != '\0'
                ? state->snapshot.status_text
                : "按住 Confirm 开始录音");
    }

    epd_test_pattern_truncate_text_tail(raw, dst, dst_size, 22U);
}

static void compose_voice_note_card_subtitle(
    voice_note_tab_t tab,
    const ink_voice_note_app_state_t *state,
    const voice_note_note_t *note,
    char *dst,
    size_t dst_size)
{
    char raw[80];
    bool show_live_status = false;

    if (dst == NULL || dst_size == 0U) {
        return;
    }
    dst[0] = '\0';
    if (note == NULL) {
        return;
    }

    show_live_status = state != NULL
        && state->snapshot.busy
        && state->snapshot.active_note_id[0] != '\0'
        && strcmp(state->snapshot.active_note_id, note->id) == 0;

    if (show_live_status) {
        snprintf(
            raw,
            sizeof(raw),
            "%s",
            state->snapshot.status_text[0] != '\0'
                ? state->snapshot.status_text
                : "处理中");
        epd_test_pattern_truncate_text_tail(raw, dst, dst_size, 22U);
        return;
    }

    (void)tab;
    compose_voice_note_meta_line(note, raw, sizeof(raw));

    if (note->last_error[0] != '\0') {
        snprintf(
            raw + strlen(raw),
            sizeof(raw) - strlen(raw),
            "  %s",
            note->last_error);
    }

    epd_test_pattern_truncate_text_tail(raw, dst, dst_size, 22U);
}

static void compose_voice_note_meta_line(
    const voice_note_note_t *note,
    char *dst,
    size_t dst_size)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    dst[0] = '\0';
    if (note == NULL) {
        return;
    }

    (void)voice_note_model_compose_meta_line(
        note->created_at_epoch_s,
        note->duration_ms,
        note->status,
        dst,
        dst_size);
}

static void draw_voice_note_full_text_view(
    uint8_t *buffer,
    size_t length,
    const ink_voice_note_app_render_state_t *render_state,
    epd_test_pattern_reader_menu_overlay_t *overlay)
{
    const ink_voice_note_app_state_t *state = render_state != NULL ? render_state->state : NULL;
    const ink_cpfont_t *title_font = render_state != NULL ? render_state->menu_font : NULL;
    const ink_cpfont_t *meta_font = render_state != NULL ? render_state->footer_font : NULL;
    const ink_cpfont_t *body_font = render_state != NULL ? render_state->menu_font : NULL;
    epd_test_pattern_header_spec_t header;
    const voice_note_note_t *note = NULL;
    size_t note_index = VOICE_NOTE_MAX_NOTES;
    static char s_wrapped_text[VOICE_NOTE_TEXT_LENGTH];
    static char s_lines[12][VOICE_NOTE_TEXT_LENGTH];
    char overlay_meta[24];
    char meta_line[64];
    const char *text = "暂无识别文本";
    size_t line_count = 0U;
    const int text_x = 24;
    const int meta_y = 104;
    const int text_y0 = 154;
    const int line_gap = 50;
    const int max_width_px = EPD_GDEY0426T82_WIDTH - 48;
    const uint8_t meta_scale_divisor = 1U;
    const uint8_t body_scale_divisor = 1U;

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE || state == NULL || overlay == NULL) {
        return;
    }

    memset(buffer, 0xFF, length);
    memset(s_wrapped_text, 0, sizeof(s_wrapped_text));
    memset(s_lines, 0, sizeof(s_lines));

    note_index = voice_note_render_visible_index_from_selection(state, state->selected_index);
    if (note_index < state->visible_note_count) {
        note = &state->visible_notes[note_index];
    }

    header.title = "语音便签";
    header.meta = render_state != NULL ? render_state->header_meta : "";
    header.title_font = title_font;
    header.meta_font = meta_font;
    epd_test_pattern_draw_crosspoint_header(buffer, &header);

    overlay->frameless_panel = true;
    overlay->compact_cards = false;
    overlay->bookmark_cards_tall = false;
    snprintf(overlay->header_title, sizeof(overlay->header_title), "%s", "语音便签");
    epd_test_pattern_truncate_text_tail(
        render_state != NULL ? render_state->header_meta : "",
        overlay_meta,
        sizeof(overlay_meta),
        12U);
    snprintf(overlay->header_meta, sizeof(overlay->header_meta), "%s", overlay_meta);
    overlay->tab_count = 0U;
    overlay->card_count = 0U;
    overlay->action_popup_open = false;
    epd_test_pattern_draw_reader_menu_overlay(buffer, length, title_font, meta_font, overlay);

    launcher_fill_rect(buffer, 20, 98, EPD_GDEY0426T82_WIDTH - 40, 610, false);

    if (note != NULL) {
        if (note->text[0] != '\0') {
            text = note->text;
        } else if (note->last_error[0] != '\0') {
            text = note->last_error;
        }
        compose_voice_note_meta_line(note, meta_line, sizeof(meta_line));
    } else {
        snprintf(meta_line, sizeof(meta_line), "%s", "时间未同步  0s  未完成");
    }

    launcher_draw_text(buffer, meta_font, text_x, meta_y, meta_line, meta_scale_divisor, false);

    if (!voice_note_model_normalize_text(text, s_wrapped_text, sizeof(s_wrapped_text))) {
        snprintf(s_wrapped_text, sizeof(s_wrapped_text), "%s", text);
    }
    line_count = compose_voice_note_wrapped_lines(
        body_font,
        body_scale_divisor,
        s_wrapped_text,
        max_width_px,
        s_lines,
        sizeof(s_lines) / sizeof(s_lines[0]));

    for (size_t i = 0U; i < line_count; ++i) {
        launcher_draw_text(
            buffer,
            body_font,
            text_x,
            text_y0 + (int)i * line_gap,
            s_lines[i],
            body_scale_divisor,
            false);
    }
    if (line_count == 0U) {
        launcher_draw_text(buffer, body_font, text_x, text_y0, s_wrapped_text, body_scale_divisor, false);
    }
}

static void draw_photo_album_list_page(
    uint8_t *buffer,
    size_t length,
    const ink_photo_album_render_state_t *render_state,
    const ink_cpfont_t *menu_font,
    const ink_cpfont_t *footer_font)
{
    const ink_photo_album_app_state_t *state = render_state != NULL ? render_state->state : NULL;
    const ink_photo_catalog_t *catalog = render_state != NULL ? render_state->catalog : NULL;
    epd_test_pattern_list_layout_t list_layout = epd_test_pattern_crosspoint_list_layout();
    size_t start = 0U;
    char count_text[24];
    char subtitle_text[24];
    epd_test_pattern_header_spec_t header = {0};

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, length);

    if (state == NULL) {
        epd_test_pattern_list_row_t rows[1];
        epd_test_pattern_rows_page_spec_t spec;

        memset(rows, 0, sizeof(rows));
        rows[0].title = "No state";
        rows[0].line1 = "Photo album unavailable";
        rows[0].selected = true;
        memset(&spec, 0, sizeof(spec));
        spec.title = "Photos";
        spec.meta = "LIST";
        spec.rows = rows;
        spec.row_count = 1U;
        spec.title_font = menu_font;
        spec.meta_font = footer_font;
        spec.row_title_font = menu_font;
        spec.row_meta_font = footer_font;
        epd_test_pattern_fill_crosspoint_rows_page(buffer, length, &spec);
        return;
    }

    list_layout.row_h = 42;
    list_layout.row_gap = 2;
    list_layout.visible_rows = 14;
    list_layout.compact_rows = true;
    list_layout.marker_top_inset = 8;
    list_layout.marker_bottom_inset = 8;
    list_layout.title_y_offset = 8;
    list_layout.line1_y_offset = 0;
    list_layout.line2_y_offset = 0;

    snprintf(count_text, sizeof(count_text), "%s", render_state != NULL ? render_state->header_meta : "");
    snprintf(subtitle_text, sizeof(subtitle_text), "%u/%u", (unsigned)(state->list_selected_index + 1U), (unsigned)state->total_count);
    header.title = "相册";
    header.meta = count_text;
    header.title_font = menu_font;
    header.meta_font = footer_font;
    epd_test_pattern_draw_crosspoint_header(buffer, &header);

    if (state->total_count == 0U || catalog == NULL) {
        epd_test_pattern_list_row_t row = {
            .title = state->status_text[0] != '\0' ? state->status_text : "photos 目录为空",
            .line1 = "",
            .line2 = "",
            .selected = true,
            .emphasized = false,
        };
        epd_test_pattern_draw_crosspoint_list_row(buffer, &list_layout, 0U, &row, menu_font, footer_font);
        return;
    }

    if (state->list_selected_index >= (size_t)(list_layout.visible_rows / 2)) {
        start = state->list_selected_index - (size_t)(list_layout.visible_rows / 2);
    }
    if (start + (size_t)list_layout.visible_rows > state->total_count) {
        start = state->total_count > (size_t)list_layout.visible_rows
            ? state->total_count - (size_t)list_layout.visible_rows
            : 0U;
    }

    for (int row = 0; row < list_layout.visible_rows && start + (size_t)row < state->total_count; ++row) {
        const size_t item_index = start + (size_t)row;
        const ink_photo_catalog_entry_t *entry = ink_photo_catalog_entry_at(catalog, item_index);
        const bool selected = item_index == state->list_selected_index;
        epd_test_pattern_list_row_t row_spec;
        char title[48];
        char base_name[40];

        if (entry == NULL) {
            continue;
        }

        if (!photo_album_name_without_extension(entry->name, base_name, sizeof(base_name))) {
            size_t copy_len = strlen(entry->name);
            if (copy_len >= sizeof(base_name)) {
                copy_len = sizeof(base_name) - 1U;
            }
            memcpy(base_name, entry->name, copy_len);
            base_name[copy_len] = '\0';
        }
        photo_album_compose_list_title(item_index, base_name, title, sizeof(title));
        memset(&row_spec, 0, sizeof(row_spec));
        row_spec.title = title;
        row_spec.line1 = item_index == state->list_selected_index ? subtitle_text : "";
        row_spec.line2 = "";
        row_spec.selected = selected;
        row_spec.emphasized = false;
        epd_test_pattern_draw_crosspoint_list_row(
            buffer,
            &list_layout,
            (size_t)row,
            &row_spec,
            menu_font,
            footer_font);
    }
}

static bool photo_album_name_without_extension(
    const char *filename,
    char *dst,
    size_t dst_size)
{
    const char *dot = NULL;
    size_t length = 0U;

    if (dst == NULL || dst_size == 0U) {
        return false;
    }
    dst[0] = '\0';
    if (filename == NULL || filename[0] == '\0') {
        return false;
    }

    dot = strrchr(filename, '.');
    if (dot == NULL || dot == filename) {
        snprintf(dst, dst_size, "%s", filename);
        return true;
    }

    length = (size_t)(dot - filename);
    if (length >= dst_size) {
        length = dst_size - 1U;
    }
    memcpy(dst, filename, length);
    dst[length] = '\0';
    return true;
}

static void photo_album_compose_list_title(
    size_t item_index,
    const char *filename,
    char *dst,
    size_t dst_size)
{
    char truncated[32];

    if (dst == NULL || dst_size == 0U) {
        return;
    }
    dst[0] = '\0';
    epd_test_pattern_truncate_text_tail(
        filename != NULL ? filename : "",
        truncated,
        sizeof(truncated),
        20U);
    snprintf(dst, dst_size, "%02u. %s", (unsigned)(item_index + 1U), truncated);
}

static bool render_buffer_has_ink(const uint8_t *buffer, size_t length)
{
    if (buffer == NULL) {
        return false;
    }

    for (size_t i = 0U; i < length; ++i) {
        if (buffer[i] != 0xFFU && buffer[i] != 0xAAU) {
            return true;
        }
    }

    return false;
}

static bool render_pixel_is_black(const uint8_t *buffer, int x, int y)
{
    const size_t byte_index = (size_t)y * (EPD_GDEY0426T82_WIDTH / 8U) + (size_t)(x / 8);
    const uint8_t mask = (uint8_t)(0x80U >> (x % 8));

    if (buffer == NULL || x < 0 || x >= EPD_GDEY0426T82_WIDTH || y < 0 || y >= EPD_GDEY0426T82_HEIGHT) {
        return false;
    }

    return (buffer[byte_index] & mask) == 0U;
}

static bool render_pixel_is_white(const uint8_t *buffer, int x, int y)
{
    const size_t byte_index = (size_t)y * (EPD_GDEY0426T82_WIDTH / 8U) + (size_t)(x / 8);
    const uint8_t mask = (uint8_t)(0x80U >> (x % 8));

    if (buffer == NULL || x < 0 || x >= EPD_GDEY0426T82_WIDTH || y < 0 || y >= EPD_GDEY0426T82_HEIGHT) {
        return false;
    }

    return (buffer[byte_index] & mask) != 0U;
}

static bool fill_gray_calibration_planes(
    uint8_t *scratch,
    size_t scratch_length,
    uint8_t *lsb_plane,
    size_t lsb_length,
    uint8_t *msb_plane,
    size_t msb_length)
{
    enum {
        MARGIN_X = 32,
        MARGIN_Y = 80,
        GAP_X = 24,
        GAP_Y = 24,
        BLOCK_W = (EPD_GDEY0426T82_WIDTH - MARGIN_X * 2 - GAP_X) / 2,
        BLOCK_H = (EPD_GDEY0426T82_HEIGHT - MARGIN_Y * 2 - GAP_Y) / 2,
    };
    static const uint8_t kCodes[4] = {
        0U, 1U,
        2U, 3U,
    };
    const size_t stride = EPD_GDEY0426T82_WIDTH / 8U;

    if (scratch == NULL
        || lsb_plane == NULL
        || msb_plane == NULL
        || scratch_length < EPD_GDEY0426T82_BUFFER_SIZE
        || lsb_length < EPD_GDEY0426T82_BUFFER_SIZE
        || msb_length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return false;
    }

    memset(scratch, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(lsb_plane, 0x00, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(msb_plane, 0x00, EPD_GDEY0426T82_BUFFER_SIZE);

    for (int index = 0; index < 4; ++index) {
        const int row = index / 2;
        const int col = index % 2;
        const int sample_x = MARGIN_X + col * (BLOCK_W + GAP_X);
        const int sample_y = MARGIN_Y + row * (BLOCK_H + GAP_Y);
        const int sample_w = BLOCK_W;
        const int sample_h = BLOCK_H;
        const uint8_t code = kCodes[index];
        const bool lsb_bit_set = (code & 0x1U) != 0U;
        const bool msb_bit_set = (code & 0x2U) != 0U;

        if (sample_w <= 0 || sample_h <= 0) {
            return false;
        }

        for (int yy = sample_y; yy < sample_y + sample_h; ++yy) {
            uint8_t *lsb_row = lsb_plane + (size_t)yy * stride;
            uint8_t *msb_row = msb_plane + (size_t)yy * stride;

            for (int xx = sample_x; xx < sample_x + sample_w; ++xx) {
                const size_t byte_index = (size_t)(xx / 8);
                const uint8_t mask = (uint8_t)(0x80U >> (xx % 8));

                if (lsb_bit_set) {
                    lsb_row[byte_index] |= mask;
                } else {
                    lsb_row[byte_index] &= (uint8_t)~mask;
                }

                if (msb_bit_set) {
                    msb_row[byte_index] |= mask;
                } else {
                    msb_row[byte_index] &= (uint8_t)~mask;
                }
            }
        }
    }

    return true;
}

static bool render_library_overlay_to_buffer(
    uint8_t *buffer,
    size_t length,
    const ink_cpfont_t *menu_font,
    const ink_cpfont_t *footer_font,
    const uint8_t *previous_framebuffer,
    size_t previous_length,
    const ink_display_request_t *request)
{
    if (buffer == NULL || request == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return false;
    }

    if (request->use_reader_menu_overlay) {
        if (previous_framebuffer != NULL && previous_length >= EPD_GDEY0426T82_BUFFER_SIZE) {
            memcpy(buffer, previous_framebuffer, EPD_GDEY0426T82_BUFFER_SIZE);
        } else if (request->use_bitmap_page
            && request->bitmap_page_buffer != NULL
            && request->bitmap_page_length >= EPD_GDEY0426T82_BUFFER_SIZE) {
            memcpy(buffer, request->bitmap_page_buffer, EPD_GDEY0426T82_BUFFER_SIZE);
        } else if (request->use_native_page
            && request->native_page_buffer != NULL
            && request->native_page_length >= EPD_GDEY0426T82_NATIVE_BUFFER_SIZE) {
            memset(buffer, 0xFF, length);
        } else {
            return false;
        }
    } else {
        memset(buffer, 0xFF, length);
    }
    epd_test_pattern_draw_reader_menu_overlay(
        buffer,
        length,
        menu_font,
        footer_font,
        &request->menu_overlay);
    return true;
}

static bool render_model_to_buffer(
    uint8_t *buffer,
    size_t length,
    const ink_app_render_model_t *model)
{
    if (buffer == NULL || model == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return false;
    }

    switch (model->mode) {
        case INK_APP_RENDER_MODE_LAUNCHER:
            fill_launcher_page(buffer, length, (const ink_launcher_app_render_state_t *)model->state);
            return true;
        case INK_APP_RENDER_MODE_READER_PLACEHOLDER:
            fill_reader_placeholder_page(buffer, length);
            return true;
        case INK_APP_RENDER_MODE_WIFI_SETUP:
            fill_wifi_setup_page(buffer, length, (const ink_wifi_setup_app_render_state_t *)model->state);
            return true;
        case INK_APP_RENDER_MODE_PHOTO_ALBUM:
            fill_photo_album_page(buffer, length, (const ink_photo_album_render_state_t *)model->state);
            return true;
        case INK_APP_RENDER_MODE_VOICE_NOTE:
            fill_voice_note_page(buffer, length, (const ink_voice_note_app_render_state_t *)model->state);
            return true;
        case INK_APP_RENDER_MODE_USB_MSC:
            fill_usb_msc_page(buffer, length, (const ink_usb_msc_app_render_state_t *)model->state);
            return true;
        default:
            return false;
    }
}

bool ink_app_render_model_fill_request(
    const ink_app_render_model_t *model,
    ink_display_request_t *request)
{
    if (model == NULL || request == NULL) {
        return false;
    }

    if (model->mode == INK_APP_RENDER_MODE_READER_SUBSYSTEM) {
        if (!ink_app_render_reader_subsystem_model_fill_request(
                (const ink_ui_model_t *)model->state,
                0U,
                0U,
                request)) {
            return false;
        }
        request->owner_ui_model = model->state;
        if (model->request_full_refresh) {
            request->full_refresh = true;
            request->refresh_profile = INK_TUNING_REFRESH_FULL;
        }
        if (request->refresh_strategy == INK_REFRESH_STRATEGY_NONE) {
            if (request->full_refresh) {
                request->refresh_strategy = INK_REFRESH_STRATEGY_PAGE_TRANSITION_FULL;
            } else if (request->use_reader_menu_overlay || request->use_library_overlay) {
                request->refresh_strategy = INK_REFRESH_STRATEGY_OVERLAY_LOCAL_UPDATE;
            } else if (request->use_reader_hold_navigation) {
                request->refresh_strategy = INK_REFRESH_STRATEGY_READER_HOLD_PREVIEW;
            } else if (request->force_white_page || request->force_fast_full_commit) {
                request->refresh_strategy = INK_REFRESH_STRATEGY_READER_TEXT_CLEANUP;
            } else if (request->use_reader_layout || request->use_bitmap_page || request->use_native_page) {
                request->refresh_strategy = INK_REFRESH_STRATEGY_READER_TEXT_TURN;
            } else {
                request->refresh_strategy = INK_REFRESH_STRATEGY_LAB_EXPLICIT_MODE;
            }
        }
        return true;
    }

    memset(request, 0, sizeof(*request));
    request->page = INK_RUNTIME_SHELL_PAGE_READER;
    request->refresh_strategy = model->refresh_strategy;
    request->full_refresh = model->request_full_refresh;
    if (model->request_full_refresh) {
        request->refresh_profile = INK_TUNING_REFRESH_FULL;
    } else if (model->refresh_strategy == INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST) {
        request->refresh_profile = INK_TUNING_REFRESH_FAST_FULL;
    } else {
        request->refresh_profile = INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY;
    }
    request->use_app_render_model = true;
    request->app_request_partial_refresh = model->request_partial_refresh;
    request->use_aggressive_interrupt = model->request_aggressive_interrupt;
    request->app_render_mode = (uint8_t)model->mode;
    request->app_partial_x = model->partial_x;
    request->app_partial_y = model->partial_y;
    request->app_partial_w = model->partial_w;
    request->app_partial_h = model->partial_h;
    request->app_render_state = model->state;
    return true;
}

bool ink_app_render_reader_subsystem_model_fill_request(
    const ink_ui_model_t *model,
    uint32_t input_ms,
    uint32_t command_latency_ms,
    ink_display_request_t *request)
{
    if (model == NULL || request == NULL) {
        return false;
    }

    if (!ink_app_build_display_request(
        model,
        input_ms,
        command_latency_ms,
        INK_RUNTIME_SHELL_COMMAND_NONE,
        request)) {
        return false;
    }

    request->owner_ui_model = (void *)model;
    return true;
}

static ink_ui_model_t *request_owner_ui_model(
    ink_app_context_t *app,
    const ink_display_request_t *request)
{
    if (request != NULL && request->owner_ui_model != NULL) {
        return (ink_ui_model_t *)request->owner_ui_model;
    }
    if (app == NULL) {
        return NULL;
    }
    return &app->model;
}

esp_err_t ink_app_render_display_request(
    ink_app_context_t *app,
    const ink_display_request_t *request,
    epd_gdey0426t82_phase_t *phase_out)
{
    const ink_cpfont_t *page_font = NULL;
    const ink_cpfont_t *footer_font = NULL;
    const ink_cpfont_t *menu_font = NULL;
    ink_epd_cancel_ctx_t cancel_ctx = {
        .app = app,
        .seq = request->seq,
        .pin_during_transmitting = request_pins_during_transmitting(request),
    };
    epd_gdey0426t82_refresh_control_t control = {
        .aggressive_interrupt_mode = app->aggressive_interrupt_mode
            && request != NULL
            && (request->use_reader_hold_navigation || request->use_aggressive_interrupt),
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
    const char *route_name = "none";
    const bool interrupt_allowed = app->aggressive_interrupt_mode
        && request != NULL
        && (request->use_reader_hold_navigation || request->use_aggressive_interrupt);
    const bool pinned_during_tx = request_pins_during_transmitting(request);
    ink_ui_model_t *owner_model = request_owner_ui_model(app, request);

    if (request == NULL
        || app == NULL
        || app->services.framebuffer == NULL
        || app->services.previous_framebuffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!request->use_app_render_model
        && request->page == INK_RUNTIME_SHELL_PAGE_READER
        && request->tuning_page == INK_TUNING_PAGE_GRAY_CAL) {
        if (app->services.bitmap_snapshot_a == NULL || app->services.bitmap_snapshot_b == NULL) {
            return ESP_ERR_INVALID_STATE;
        }
        if (!fill_gray_calibration_planes(
                app->services.framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                app->services.bitmap_snapshot_a,
                EPD_GDEY0426T82_BUFFER_SIZE,
                app->services.bitmap_snapshot_b,
                EPD_GDEY0426T82_BUFFER_SIZE)) {
            return ESP_ERR_INVALID_STATE;
        }
        return ink_app_render_gray_planes_request(
            app,
            request,
            app->services.bitmap_snapshot_a,
            EPD_GDEY0426T82_BUFFER_SIZE,
            app->services.bitmap_snapshot_b,
            EPD_GDEY0426T82_BUFFER_SIZE,
            phase_out);
    }

    if (request->use_app_render_model
        && request->app_render_mode == (uint8_t)INK_APP_RENDER_MODE_PHOTO_ALBUM) {
        const ink_photo_album_render_state_t *album_render_state =
            (const ink_photo_album_render_state_t *)request->app_render_state;
        const ink_photo_album_app_state_t *album_state =
            album_render_state != NULL ? album_render_state->state : NULL;

        if (album_state != NULL
            && album_state->view_mode == INK_PHOTO_ALBUM_VIEW_PREVIEW
            && album_state->image_loaded
            && album_state->lsb_plane != NULL
            && album_state->msb_plane != NULL) {
            ret = ink_app_render_gray_planes_request(
                app,
                request,
                album_state->lsb_plane,
                album_state->plane_size,
                album_state->msb_plane,
                album_state->plane_size,
                phase_out);
            return ret;
        }
    }

    page_font = select_page_font(&app->services);
    footer_font = select_footer_font(&app->services);
    menu_font = select_menu_font(&app->services);

    s_render_sequence = request->seq;
    if (ink_cpfont_is_loaded(&app->services.reader_font)) {
        ink_cpfont_cache_snapshot(&app->services.reader_font, &cache_before);
    }

    view_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - phase_ms;
    phase_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());

    if (request->use_app_render_model) {
        ink_app_render_model_t model = {
            .mode = (ink_app_render_mode_t)request->app_render_mode,
            .request_full_refresh = request->full_refresh,
            .request_partial_refresh = request->app_request_partial_refresh,
            .partial_x = request->app_partial_x,
            .partial_y = request->app_partial_y,
            .partial_w = request->app_partial_w,
            .partial_h = request->app_partial_h,
            .state = request->app_render_state,
        };
        if (!render_model_to_buffer(
                app->services.framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                &model)) {
            return ESP_ERR_INVALID_ARG;
        }
    } else if (request->use_library_overlay || request->use_reader_menu_overlay) {
        populate_overlay_header_meta(
            (epd_test_pattern_reader_menu_overlay_t *)&request->menu_overlay,
            &app->services);
        if (!render_library_overlay_to_buffer(
                app->services.framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                menu_font,
                footer_font,
                app->services.previous_framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                request)) {
            return ESP_ERR_INVALID_STATE;
        }
    } else if (request->force_white_page) {
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

    if (request->use_app_render_model
        && request->app_request_partial_refresh
        && !request->full_refresh
        && request->app_partial_w > 0U
        && request->app_partial_h > 0U) {
        dirty_x = request->app_partial_x;
        dirty_y = request->app_partial_y;
        dirty_w = request->app_partial_w;
        dirty_h = request->app_partial_h;
    }
    diff_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - phase_ms;

    if (!request->full_refresh && dirty_w == 0U && dirty_h == 0U) {
        ret = ESP_OK;
    } else {
        phase_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
        if (request->full_refresh) {
            route_name = request->use_native_page ? "full_native" : "full";
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
            route_name = request->use_native_page ? "fast_full_native" : "fast_full_commit";
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
                route_name = "footer_full_window_stock";
                epd_gdey0426t82_refresh_control_t full_window_control = control;
                full_window_control.use_custom_lut_a = false;
                full_window_control.use_custom_lut_b = false;
                full_window_control.use_grid_compare_variant = false;
                full_window_control.reuse_partial_init = false;
                dirty_x = 0U;
                dirty_y = 0U;
                dirty_w = EPD_GDEY0426T82_WIDTH;
                dirty_h = EPD_GDEY0426T82_HEIGHT;
                ret = epd_gdey0426t82_partial_refresh_ex(
                    app->services.framebuffer,
                    EPD_GDEY0426T82_BUFFER_SIZE,
                    &full_window_control);
            } else {
                route_name = control.reuse_partial_init ? "footer_partial_reuse" : "footer_partial_cold";
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
            route_name = use_stock_partial
                ? "partial_full_window_stock_mainline"
                : ink_tuning_lab_refresh_profile_name((ink_tuning_refresh_profile_t)request->refresh_profile);

            if (use_stock_partial) {
                full_window_control.use_custom_lut_a = false;
                full_window_control.use_custom_lut_b = false;
                full_window_control.reuse_partial_init = false;
            }
            dirty_x = 0U;
            dirty_y = 0U;
            dirty_w = EPD_GDEY0426T82_WIDTH;
            dirty_h = EPD_GDEY0426T82_HEIGHT;
            if (!use_stock_partial
                && request->use_native_page
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
            route_name = "partial_area";
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
        if (owner_model != NULL) {
            owner_model->lab.render_counter++;
        }
        if (!request->full_refresh
            && (request->refresh_profile == INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY
            || request->refresh_profile == INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER
            || request->refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_A
            || request->refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_B)) {
            if (owner_model != NULL) {
                owner_model->lab.consecutive_partial_count++;
            }
        } else if (owner_model != NULL) {
            owner_model->lab.consecutive_partial_count = 0U;
        }
        if (owner_model != NULL) {
            if (request->force_white_page) {
                owner_model->lab.force_full_refresh = true;
                owner_model->lab.reader_white_refresh_pending = false;
            } else {
                owner_model->lab.force_full_refresh = false;
                owner_model->lab.reader_white_refresh_pending = false;
            }
        }
        if (owner_model != NULL
            && (request->page != INK_RUNTIME_SHELL_PAGE_READER || request->use_library_overlay)) {
            ink_runtime_shell_mark_rendered(&owner_model->shell);
        }
    }

    if (ink_cpfont_is_loaded(&app->services.reader_font)) {
        ink_cpfont_cache_snapshot(&app->services.reader_font, &cache_after);
    }
    log_render_timing(
        (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()),
        request,
        route_name,
        false,
        interrupt_allowed,
        pinned_during_tx,
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

esp_err_t ink_app_render_gray_planes_request(
    ink_app_context_t *app,
    const ink_display_request_t *request,
    const uint8_t *lsb_plane,
    size_t lsb_length,
    const uint8_t *msb_plane,
    size_t msb_length,
    epd_gdey0426t82_phase_t *phase_out)
{
    ink_epd_cancel_ctx_t cancel_ctx = {
        .app = app,
        .seq = request != NULL ? request->seq : 0U,
        .pin_during_transmitting = request_pins_during_transmitting(request),
    };
    epd_gdey0426t82_refresh_control_t control = {
        .aggressive_interrupt_mode = app != NULL
            && app->aggressive_interrupt_mode
            && request != NULL
            && (request->use_reader_hold_navigation || request->use_aggressive_interrupt),
        .should_cancel = epd_request_is_stale,
        .should_cancel_ctx = &cancel_ctx,
        .phase = EPD_GDEY0426T82_PHASE_IDLE,
    };
    esp_err_t ret;

    if (app == NULL || lsb_plane == NULL || msb_plane == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = epd_gdey0426t82_gray_refresh(
        lsb_plane,
        lsb_length,
        msb_plane,
        msb_length,
        &control);
    if (ret == ESP_OK) {
        memcpy(app->services.previous_framebuffer, msb_plane, EPD_GDEY0426T82_BUFFER_SIZE);
    }
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
    (void)dirty_width;
    (void)dirty_height;
    if (request == NULL
        || request->refresh_strategy != INK_REFRESH_STRATEGY_READER_TEXT_TURN
        || !request->use_bitmap_page
        || request->use_reader_menu_overlay
        || request->force_fixed_footer_partial
        || request->force_white_page
        || request->use_grid_compare_variant) {
        return false;
    }

    return false;
}

static bool should_route_footer_preview_to_full_window_stock_partial(
    const ink_display_request_t *request)
{
    return request != NULL
        && request->page == INK_RUNTIME_SHELL_PAGE_READER
        && request->use_fast_browse_overlay
        && !request->use_reader_hold_navigation
        && request->force_fixed_footer_partial
        && !request->full_refresh
        && !request->force_white_page
        && !request->use_grid_compare_variant;
}

static bool should_reuse_reader_partial_init(
    const ink_app_context_t *app,
    const ink_display_request_t *request)
{
    ink_ui_model_t *owner_model = request_owner_ui_model((ink_app_context_t *)app, request);

    if (app == NULL
        || request == NULL
        || request->full_refresh
        || request->use_grid_compare_variant
        || owner_model == NULL) {
        return false;
    }

    if (request->force_fixed_footer_partial) {
        return kFooterPartialReuseInit
            && owner_model->lab.consecutive_partial_count > 0U;
    }

    if (request->refresh_profile != INK_TUNING_REFRESH_CUSTOM_LUT_A
        || !request->use_bitmap_page) {
        return false;
    }

    return owner_model->lab.consecutive_partial_count > 0U;
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
        && request.refresh_strategy == INK_REFRESH_STRATEGY_READER_TEXT_CLEANUP
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
    model.fast_browse.active = true;
    model.fast_browse.overlay_mode = true;
    model.fast_browse.dirty = true;
    model.reader_hold_navigation_active = false;
    model.fast_browse.origin_page = 64U;
    model.fast_browse.target_page = 99U;
    model.fast_browse.visible_page = 64U;
    model.fast_browse.has_visible_page = true;
    model.fast_browse.total_pages = 321U;

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        free(page);
        return false;
    }

    ok = request.use_fast_browse_overlay
        && request.refresh_strategy == INK_REFRESH_STRATEGY_READER_HOLD_PREVIEW
        && !request.use_reader_hold_navigation
        && request.force_fixed_footer_partial
        && !request.use_bitmap_page
        && strcmp(request.overlay_left, "二 赞成与反对") == 0
        && strcmp(request.overlay_right, "31% 100/321") == 0;
    free(page);
    return ok;
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
        && request.refresh_strategy == INK_REFRESH_STRATEGY_READER_TEXT_CLEANUP
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
    snprintf(model.browser.entries[0].name, sizeof(model.browser.entries[0].name), "%s", "A");
    snprintf(model.browser.entries[0].full_path, sizeof(model.browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    return request.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && request.use_library_overlay
        && !request.use_font
        && !request.use_bitmap_page
        && !request.use_native_page
        && !request.use_footer_overlay
        && request.full_refresh
        && request.menu_overlay.frameless_panel
        && request.menu_overlay.tab_count == 3U
        && strcmp(request.menu_overlay.tabs[0].label, "最近") == 0
        && strcmp(request.menu_overlay.cards[0].title, "A") == 0
        && request.overlay_left[0] == '\0'
        && request.overlay_right[0] == '\0';
}

static bool app_library_request_long_title_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;
    const char *long_title =
        "\xE4\xBD\x9C\xE5\xAE\xB6\xE6\xA6\x9C\xE7\xBB\x8F\xE5\x85\xB8\xEF\xBC\x9A"
        "\xE7\xA3\xA8\xE5\x9D\x8A\xE4\xBF\xA1\xE6\x9C\xAD";

    memset(&model, 0, sizeof(model));
    memset(&request, 0, sizeof(request));
    ink_tuning_lab_init(&model.lab);
    ink_runtime_shell_init(&model.shell);
    model.browser.entry_count = 1U;
    model.browser.selected_index = 0U;
    snprintf(model.browser.mount_point, sizeof(model.browser.mount_point), "%s", "/sdcard/books");
    snprintf(model.browser.current_path, sizeof(model.browser.current_path), "%s", "/sdcard/books");
    model.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(model.browser.entries[0].name, sizeof(model.browser.entries[0].name), "%s", long_title);
    snprintf(
        model.browser.entries[0].full_path,
        sizeof(model.browser.entries[0].full_path),
        "%s",
        "/sdcard/books/\xE4\xBD\x9C\xE5\xAE\xB6\xE6\xA6\x9C\xE7\xBB\x8F\xE5\x85\xB8\xEF\xBC\x9A\xE7\xA3\xA8\xE5\x9D\x8A\xE4\xBF\xA1\xE6\x9C\xAD.xtc");

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    return request.use_library_overlay
        && strcmp(request.menu_overlay.cards[0].title, long_title) == 0
        && strstr(request.menu_overlay.cards[0].title, ".xtc") == NULL
        && strstr(request.menu_overlay.action_popup_title, ".xtc") == NULL;
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
        && request.refresh_strategy == INK_REFRESH_STRATEGY_PAGE_TRANSITION_FULL
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
        || request.refresh_strategy != INK_REFRESH_STRATEGY_LAB_EXPLICIT_MODE
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
    request.refresh_strategy = INK_REFRESH_STRATEGY_READER_TEXT_TURN;
    request.refresh_profile = INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY;
    request.use_bitmap_page = true;
    if (should_promote_reader_partial_to_full_window(&request, 435U, 768U)) {
        return false;
    }
    if (should_promote_reader_partial_to_full_window(&request, 200U, 768U)) {
        return false;
    }
    request.use_bitmap_page = false;
    return !should_promote_reader_partial_to_full_window(&request, 435U, 768U);
}

static bool app_overlay_never_promotes_to_reader_full_window_self_test(void)
{
    ink_display_request_t request;

    memset(&request, 0, sizeof(request));
    request.refresh_strategy = INK_REFRESH_STRATEGY_OVERLAY_LOCAL_UPDATE;
    request.refresh_profile = INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY;
    request.use_bitmap_page = true;
    request.use_reader_menu_overlay = true;

    return !should_promote_reader_partial_to_full_window(&request, 458U, 757U);
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
    request.refresh_strategy = INK_REFRESH_STRATEGY_READER_TEXT_TURN;
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

static bool app_reader_full_window_stock_fallback_avoids_native_path_self_test(void)
{
    ink_display_request_t request;
    bool use_stock_partial;
    bool would_use_native_path;

    memset(&request, 0, sizeof(request));
    request.use_bitmap_page = true;
    request.use_native_page = true;
    request.refresh_strategy = INK_REFRESH_STRATEGY_READER_TEXT_TURN;
    request.native_page_buffer = (const uint8_t *)0x1;
    request.native_page_length = EPD_GDEY0426T82_NATIVE_BUFFER_SIZE;
    request.refresh_profile = INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY;

    use_stock_partial = should_fallback_reader_full_window_to_stock_partial(&request);
    would_use_native_path =
        !use_stock_partial
        && request.use_native_page
        && request.native_page_buffer != NULL
        && request.native_page_length >= EPD_GDEY0426T82_NATIVE_BUFFER_SIZE;

    return use_stock_partial && !would_use_native_path;
}

static bool app_footer_preview_route_self_test(void)
{
    ink_display_request_t request;

    memset(&request, 0, sizeof(request));
    request.page = INK_RUNTIME_SHELL_PAGE_READER;
    request.refresh_strategy = INK_REFRESH_STRATEGY_READER_HOLD_PREVIEW;
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
    ink_display_request_t album_request;

    memset(&reader_request, 0, sizeof(reader_request));
    memset(&library_request, 0, sizeof(library_request));
    memset(&album_request, 0, sizeof(album_request));

    reader_request.page = INK_RUNTIME_SHELL_PAGE_READER;
    reader_request.use_bitmap_page = true;
    reader_request.refresh_strategy = INK_REFRESH_STRATEGY_READER_TEXT_TURN;
    if (!request_pins_during_transmitting(&reader_request)) {
        return false;
    }

    reader_request.use_reader_hold_navigation = true;
    reader_request.refresh_strategy = INK_REFRESH_STRATEGY_READER_HOLD_PREVIEW;
    if (request_pins_during_transmitting(&reader_request)) {
        return false;
    }

    library_request.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    library_request.use_bitmap_page = true;
    library_request.refresh_strategy = INK_REFRESH_STRATEGY_BW_UI_LIST_LOCAL;
    if (request_pins_during_transmitting(&library_request)) {
        return false;
    }

    album_request.page = INK_RUNTIME_SHELL_PAGE_READER;
    album_request.refresh_strategy = INK_REFRESH_STRATEGY_GRAY_IMAGE_INTERRUPTIBLE;
    album_request.use_aggressive_interrupt = true;
    return !request_pins_during_transmitting(&album_request);
}

static bool app_render_model_launcher_self_test(void)
{
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    ink_app_render_model_t model;
    ink_launcher_app_state_t state;
    ink_launcher_app_render_state_t render_state;
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(&model, 0, sizeof(model));
    memset(&render_state, 0, sizeof(render_state));
    state.selected_app_index = 1U;
    render_state.state = &state;
    snprintf(render_state.header_meta, sizeof(render_state.header_meta), "%s", "UP 01:23");
    model.mode = INK_APP_RENDER_MODE_LAUNCHER;
    model.state = &render_state;

    ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
        && render_buffer_has_ink(buffer, EPD_GDEY0426T82_BUFFER_SIZE)
        && render_pixel_is_black(buffer, 24, 50)
        && render_pixel_is_black(buffer, 28, 154)
        && render_pixel_is_white(buffer, 120, 154)
        && render_pixel_is_black(buffer, 376, 18);
    free(buffer);
    return ok;
}

static bool app_render_model_reader_placeholder_self_test(void)
{
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    ink_app_render_model_t model;
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(&model, 0, sizeof(model));
    model.mode = INK_APP_RENDER_MODE_READER_PLACEHOLDER;

    ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
        && memcmp(buffer, "\xAA", 1) != 0;
    free(buffer);
    return ok;
}

static bool app_render_model_reader_subsystem_self_test(void)
{
    ink_app_render_model_t model;
    ink_display_request_t request;
    ink_ui_model_t ui;

    memset(&model, 0, sizeof(model));
    memset(&request, 0, sizeof(request));
    memset(&ui, 0, sizeof(ui));
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    ui.browser.entry_count = 1U;
    ui.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(ui.browser.entries[0].name, sizeof(ui.browser.entries[0].name), "%s", "A.XTC");
    snprintf(ui.browser.entries[0].full_path, sizeof(ui.browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");

    model.mode = INK_APP_RENDER_MODE_READER_SUBSYSTEM;
    model.request_full_refresh = true;
    model.state = &ui;

    if (!ink_app_render_model_fill_request(&model, &request)) {
        return false;
    }

    return !request.use_app_render_model
        && request.page == INK_RUNTIME_SHELL_PAGE_LIBRARY
        && request.full_refresh
        && request.owner_ui_model == &ui
        && request.use_library_overlay
        && request.menu_overlay.frameless_panel;
}

static bool app_library_overlay_render_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    ui.library.active_tab = INK_LIBRARY_TAB_RECENT;
    ui.library.focus = INK_LIBRARY_FOCUS_POPUP;
    ui.library.popup_open = true;
    ui.library.popup_action_index = 1U;
    ui.browser.entry_count = 2U;
    ui.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    ui.browser.entries[1].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(ui.browser.entries[0].name, sizeof(ui.browser.entries[0].name), "%s", "A.XTC");
    snprintf(ui.browser.entries[0].full_path, sizeof(ui.browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");
    snprintf(ui.browser.entries[1].name, sizeof(ui.browser.entries[1].name), "%s", "B.XTC");
    snprintf(ui.browser.entries[1].full_path, sizeof(ui.browser.entries[1].full_path), "%s", "/sdcard/books/B.XTC");
    (void)ink_app_state_note_xtc_opened(&ui.app_state, "/sdcard/books/A.XTC", "A.XTC", 3U, 0U, 100U, "第一章");
    (void)ink_app_state_set_xtc_favorite(&ui.app_state, "/sdcard/books/B.XTC", "B.XTC", true);

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        free(buffer);
        return false;
    }

    ok = request.use_library_overlay
        && !request.use_font
        && !render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &(ink_app_render_model_t){
            .mode = INK_APP_RENDER_MODE_READER_SUBSYSTEM,
            .state = &ui,
        });
    free(buffer);
    return ok;
}

static bool app_library_overlay_header_meta_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;
    uint8_t *buffer = NULL;
    bool ok = false;

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    ui.browser.entry_count = 1U;
    ui.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(ui.browser.entries[0].name, sizeof(ui.browser.entries[0].name), "%s", "A.XTC");
    snprintf(ui.browser.entries[0].full_path, sizeof(ui.browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    if (buffer == NULL) {
        return false;
    }

    snprintf(request.menu_overlay.header_meta, sizeof(request.menu_overlay.header_meta), "%s", "12:34");
    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);

    ok = request.use_library_overlay
        && render_library_overlay_to_buffer(
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            NULL,
            NULL,
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            &request);
    free(buffer);
    return ok
        && strcmp(request.menu_overlay.header_title, "书库") == 0;
}

static bool app_library_overlay_tabs_follow_shell_width_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;
    uint8_t *buffer = NULL;
    bool ok = false;

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    ui.library.active_tab = INK_LIBRARY_TAB_ALL;
    ui.library.focus = INK_LIBRARY_FOCUS_TABS;
    ui.browser.entry_count = 2U;
    ui.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    ui.browser.entries[1].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(ui.browser.entries[0].name, sizeof(ui.browser.entries[0].name), "%s", "A.XTC");
    snprintf(ui.browser.entries[0].full_path, sizeof(ui.browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");
    snprintf(ui.browser.entries[1].name, sizeof(ui.browser.entries[1].name), "%s", "B.XTC");
    snprintf(ui.browser.entries[1].full_path, sizeof(ui.browser.entries[1].full_path), "%s", "/sdcard/books/B.XTC");

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    snprintf(request.menu_overlay.header_meta, sizeof(request.menu_overlay.header_meta), "%s", "12:34");

    ok = request.use_library_overlay
        && render_library_overlay_to_buffer(
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            NULL,
            NULL,
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            &request)
        && render_pixel_is_black(buffer, 24, 38)
        && render_pixel_is_black(buffer, 455, 71)
        && render_pixel_is_white(buffer, 463, 71)
        && render_pixel_is_black(buffer, 455, 158)
        && render_pixel_is_white(buffer, 463, 158);
    free(buffer);
    return ok;
}

static bool app_library_overlay_selected_favorite_remains_visible_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;
    uint8_t *buffer = NULL;
    bool ok = false;

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_LIBRARY;
    ui.library.active_tab = INK_LIBRARY_TAB_ALL;
    ui.library.focus = INK_LIBRARY_FOCUS_ITEMS;
    ui.browser.entry_count = 1U;
    ui.browser.entries[0].type = INK_FILE_BROWSER_ENTRY_XTC;
    snprintf(ui.browser.entries[0].name, sizeof(ui.browser.entries[0].name), "%s", "A.XTC");
    snprintf(ui.browser.entries[0].full_path, sizeof(ui.browser.entries[0].full_path), "%s", "/sdcard/books/A.XTC");
    (void)ink_app_state_set_xtc_favorite(&ui.app_state, "/sdcard/books/A.XTC", "A.XTC", true);

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    snprintf(request.menu_overlay.header_meta, sizeof(request.menu_overlay.header_meta), "%s", "12:34");

    ok = request.use_library_overlay
        && render_library_overlay_to_buffer(
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            NULL,
            NULL,
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            &request)
        && render_pixel_is_black(buffer, 443, 144);
    free(buffer);
    return ok;
}

static bool app_render_model_usb_msc_self_test(void)
{
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    ink_app_render_model_t model;
    ink_usb_msc_app_state_t state;
    ink_usb_msc_app_render_state_t render_state;
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(&model, 0, sizeof(model));
    memset(&state, 0, sizeof(state));
    memset(&render_state, 0, sizeof(render_state));
    state.view = INK_USB_MSC_APP_VIEW_ACTIVE;
    snprintf(state.title, sizeof(state.title), "%s", "USB DISK MODE");
    snprintf(state.line1, sizeof(state.line1), "%s", "TF CARD SHARED TO USB");
    snprintf(state.line2, sizeof(state.line2), "%s", "UNPLUG USB TO EXIT");
    snprintf(state.line3, sizeof(state.line3), "%s", "PRESS BACK TO RETURN");
    render_state.state = &state;
    model.mode = INK_APP_RENDER_MODE_USB_MSC;
    model.state = &render_state;

    ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
        && render_buffer_has_ink(buffer, EPD_GDEY0426T82_BUFFER_SIZE)
        && render_pixel_is_black(buffer, 24, 66);
    free(buffer);
    return ok;
}

static bool app_render_model_usb_msc_keeps_time_badge_self_test(void)
{
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    ink_app_render_model_t model;
    ink_usb_msc_app_state_t state;
    ink_usb_msc_app_render_state_t render_state;
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(&model, 0, sizeof(model));
    memset(&state, 0, sizeof(state));
    memset(&render_state, 0, sizeof(render_state));
    state.view = INK_USB_MSC_APP_VIEW_ACTIVE;
    snprintf(state.title, sizeof(state.title), "%s", "USB 磁盘");
    snprintf(state.line1, sizeof(state.line1), "%s", "U盘模式 开");
    snprintf(render_state.header_meta, sizeof(render_state.header_meta), "%s", "12:34");
    render_state.state = &state;
    model.mode = INK_APP_RENDER_MODE_USB_MSC;
    model.state = &render_state;

    ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
        && render_pixel_is_black(buffer, 430, 10)
        && render_pixel_is_white(buffer, 24, 250)
        && render_pixel_is_black(buffer, 48, 310)
        && render_pixel_is_white(buffer, 44, 310);
    free(buffer);
    return ok;
}

static bool app_reader_menu_request_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    ui.shell.full_refresh_requested = false;
    ui.reader_session.active = true;
    ui.reader_session.xtc_active = true;
    ui.reader_session.current_page = 11U;
    ui.reader_session.total_pages = 120U;
    snprintf(
        ui.reader_session.current_chapter_name,
        sizeof(ui.reader_session.current_chapter_name),
        "%s",
        "第一章 风起");
    ui.reader_menu.open = true;
    ui.reader_menu.active_tab = INK_READER_MENU_TAB_BOOKMARKS;
    ui.reader_menu.level = INK_READER_MENU_LEVEL_ITEMS;

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    return request.use_reader_menu_overlay
        && request.refresh_strategy == INK_REFRESH_STRATEGY_OVERLAY_LOCAL_UPDATE
        && !request.full_refresh
        && request.menu_overlay.compact_cards
        && request.menu_overlay.bookmark_cards_tall
        && !request.menu_overlay.frameless_panel
        && strcmp(request.menu_overlay.cards[0].title, "将当前页添加到书签") == 0
        && strstr(request.menu_overlay.cards[0].line1, "P12") != NULL
        && strstr(request.menu_overlay.cards[0].line1, "第一章") != NULL;
}

static bool app_reader_text_turn_strategy_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    ui.reader_session.active = true;
    ui.reader_session.xtc_active = true;
    ui.reader_session.total_pages = 12U;
    ui.reader_session.current_page = 3U;

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    return request.refresh_strategy == INK_REFRESH_STRATEGY_READER_TEXT_TURN
        && !request.use_reader_menu_overlay
        && !request.use_library_overlay;
}

static bool app_reader_overlay_strategy_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    ui.reader_opening = true;
    snprintf(ui.reader_loading_title, sizeof(ui.reader_loading_title), "%s", "正在加载...");

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    return request.refresh_strategy == INK_REFRESH_STRATEGY_OVERLAY_LOCAL_UPDATE
        && request.use_reader_menu_overlay;
}

static bool app_reader_menu_overlay_render_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    ui.shell.full_refresh_requested = false;
    ui.reader_session.active = true;
    ui.reader_session.xtc_active = true;
    ui.reader_session.current_page = 11U;
    ui.reader_session.total_pages = 120U;
    ui.reader_menu.open = true;
    ui.reader_menu.active_tab = INK_READER_MENU_TAB_BOOKMARKS;
    ui.reader_menu.level = INK_READER_MENU_LEVEL_ITEMS;
    snprintf(
        ui.reader_session.current_chapter_name,
        sizeof(ui.reader_session.current_chapter_name),
        "%s",
        "第一章 风起");

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        free(buffer);
        return false;
    }

    ok = request.use_reader_menu_overlay
        && !request.full_refresh
        && request.use_bitmap_page
        && !request.menu_overlay.frameless_panel
        && render_library_overlay_to_buffer(
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            NULL,
            NULL,
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            &request)
        && render_pixel_is_white(buffer, 24, 38)
        && render_pixel_is_white(buffer, 455, 38)
        && memcmp(buffer, "\xAA", 1) != 0;
    free(buffer);
    return ok;
}

static bool app_reader_menu_partial_window_policy_self_test(void)
{
    ink_display_request_t request;

    memset(&request, 0, sizeof(request));
    request.page = INK_RUNTIME_SHELL_PAGE_READER;
    request.use_bitmap_page = true;
    request.use_reader_menu_overlay = true;
    request.refresh_profile = INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY;

    return !should_promote_reader_partial_to_full_window(&request, 458U, 757U);
}

static bool app_reader_loading_overlay_request_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    ui.reader_opening = true;
    snprintf(ui.reader_loading_title, sizeof(ui.reader_loading_title), "%s", "正在加载...");

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    return request.use_reader_menu_overlay
        && request.menu_overlay.frameless_panel
        && !request.menu_overlay.compact_cards
        && request.menu_overlay.card_count == 0U
        && request.menu_overlay.action_popup_open
        && request.menu_overlay.action_count == 0U
        && strcmp(request.menu_overlay.action_popup_title, "正在加载...") == 0;
}

static bool app_reader_loading_overlay_header_meta_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;
    uint8_t *buffer = NULL;
    bool ok = false;

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    ui.reader_opening = true;
    snprintf(ui.reader_loading_title, sizeof(ui.reader_loading_title), "%s", "正在加载...");

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    if (buffer == NULL) {
        return false;
    }

    snprintf(request.menu_overlay.header_meta, sizeof(request.menu_overlay.header_meta), "%s", "12:34");
    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);

    ok = request.use_reader_menu_overlay
        && render_library_overlay_to_buffer(
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            NULL,
            NULL,
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            &request);
    free(buffer);
    return ok
        && strcmp(request.menu_overlay.header_title, "书库") == 0;
}

static bool app_reader_chapter_menu_request_layout_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    ui.reader_session.active = true;
    ui.reader_session.xtc_active = true;
    ui.reader_menu.open = true;
    ui.reader_menu.active_tab = INK_READER_MENU_TAB_CHAPTERS;
    ui.reader_menu.level = INK_READER_MENU_LEVEL_ITEMS;
    ui.reader_session.xtc_book.chapter_entry_count = 3U;
    snprintf(ui.reader_session.xtc_book.chapter_entries[0].name, sizeof(ui.reader_session.xtc_book.chapter_entries[0].name), "%s", "第一章");
    snprintf(ui.reader_session.xtc_book.chapter_entries[1].name, sizeof(ui.reader_session.xtc_book.chapter_entries[1].name), "%s", "第二章");
    snprintf(ui.reader_session.xtc_book.chapter_entries[2].name, sizeof(ui.reader_session.xtc_book.chapter_entries[2].name), "%s", "第三章");

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }

    return request.use_reader_menu_overlay
        && request.menu_overlay.compact_cards
        && !request.menu_overlay.bookmark_cards_tall
        && !request.menu_overlay.frameless_panel
        && request.menu_overlay.card_count == 3U
        && strcmp(request.menu_overlay.cards[0].title, "第一章") == 0;
}

static bool app_reader_loading_overlay_masks_background_self_test(void)
{
    ink_ui_model_t ui;
    ink_display_request_t request;
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }

    memset(&ui, 0, sizeof(ui));
    memset(&request, 0, sizeof(request));
    memset(buffer, 0x00, EPD_GDEY0426T82_BUFFER_SIZE);
    ink_tuning_lab_init(&ui.lab);
    ink_runtime_shell_init(&ui.shell);
    ui.shell.page = INK_RUNTIME_SHELL_PAGE_READER;
    ui.reader_opening = true;
    snprintf(ui.reader_loading_title, sizeof(ui.reader_loading_title), "%s", "正在加载...");

    if (!ink_app_build_display_request(&ui, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        free(buffer);
        return false;
    }

    ok = render_library_overlay_to_buffer(
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            NULL,
            NULL,
            buffer,
            EPD_GDEY0426T82_BUFFER_SIZE,
            &request)
        && buffer[(EPD_GDEY0426T82_WIDTH / 8) * 360 + 30] != 0x00;
    free(buffer);
    return ok;
}

static bool app_overlay_menu_font_selection_self_test(void)
{
    ink_system_services_t services;

    memset(&services, 0, sizeof(services));

    services.menu_font.loaded = true;
    services.menu_font.file = (FILE *)1;
    services.menu_font.intervals = (ink_cpfont_interval_t *)1;
    services.reader_font.loaded = true;
    services.reader_font.file = (FILE *)2;
    services.reader_font.intervals = (ink_cpfont_interval_t *)2;
    services.footer_font.loaded = true;
    services.footer_font.file = (FILE *)3;
    services.footer_font.intervals = (ink_cpfont_interval_t *)3;

    if (select_menu_font(&services) != &services.menu_font) {
        return false;
    }
    if (select_page_font(&services) != &services.reader_font) {
        return false;
    }
    if (select_small_text_font(&services) != &services.footer_font) {
        return false;
    }
    if (select_footer_font(&services) != &services.footer_font) {
        return false;
    }

    services.menu_font.loaded = false;
    if (select_menu_font(&services) != &services.reader_font) {
        return false;
    }
    if (select_small_text_font(&services) != &services.footer_font) {
        return false;
    }

    services.footer_font.loaded = false;
    if (select_small_text_font(&services) != &services.reader_font) {
        return false;
    }

    services.reader_font.loaded = false;
    return select_menu_font(&services) == &services.menu_font
        && select_page_font(&services) == &services.menu_font
        && select_footer_font(&services) == &services.menu_font
        && select_small_text_font(&services) == &services.menu_font;
}

static bool app_render_model_wifi_setup_self_test(void)
{
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    ink_app_render_model_t model;
    ink_wifi_setup_app_view_t view;
    ink_wifi_setup_app_render_state_t render_state;
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(&model, 0, sizeof(model));
    memset(&view, 0, sizeof(view));
    memset(&render_state, 0, sizeof(render_state));
    view.wifi.scan.count = 1;
    view.wifi.selected_index = 0;
    snprintf(view.wifi.scan.results[0].ssid, sizeof(view.wifi.scan.results[0].ssid), "%s", "demo");
    snprintf(render_state.header_meta, sizeof(render_state.header_meta), "%s", "12:34");
    render_state.view = &view;
    model.mode = INK_APP_RENDER_MODE_WIFI_SETUP;
    model.state = &render_state;

    ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
        && render_buffer_has_ink(buffer, EPD_GDEY0426T82_BUFFER_SIZE)
        && render_pixel_is_black(buffer, 24, 8)
        && render_pixel_is_black(buffer, 408, 10);
    free(buffer);
    return ok;
}

static bool app_render_model_photo_album_self_test(void)
{
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    ink_app_render_model_t model;
    ink_photo_album_app_state_t state;
    ink_photo_album_render_state_t render_state;
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(&model, 0, sizeof(model));
    memset(&state, 0, sizeof(state));
    memset(&render_state, 0, sizeof(render_state));
    state.view_mode = INK_PHOTO_ALBUM_VIEW_LIST;
    snprintf(state.status_text, sizeof(state.status_text), "%s", "3/24");
    snprintf(render_state.header_meta, sizeof(render_state.header_meta), "%s", "12:34");
    render_state.state = &state;
    model.mode = INK_APP_RENDER_MODE_PHOTO_ALBUM;
    model.refresh_strategy = INK_REFRESH_STRATEGY_BW_UI_LIST_LOCAL;
    model.state = &render_state;

    ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
        && render_buffer_has_ink(buffer, EPD_GDEY0426T82_BUFFER_SIZE)
        && render_pixel_is_black(buffer, 24, 8)
        && render_pixel_is_black(buffer, 408, 10);
    free(buffer);
    return ok;
}

static bool app_render_model_voice_note_self_test(void)
{
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    ink_app_render_model_t model;
    ink_voice_note_app_state_t state;
    ink_voice_note_app_render_state_t render_state;
    bool ok = false;
    epd_test_pattern_reader_menu_overlay_t overlay;
    voice_note_note_t note;

    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(&model, 0, sizeof(model));
    memset(&state, 0, sizeof(state));
    memset(&render_state, 0, sizeof(render_state));
    memset(&overlay, 0, sizeof(overlay));
    memset(&note, 0, sizeof(note));
    snprintf(state.snapshot.status_text, sizeof(state.snapshot.status_text), "%s", "正在录音");
    state.popup_overlay = &overlay;
    state.visible_notes = &note;
    state.visible_note_count = 1U;
    snprintf(note.title, sizeof(note.title), "%s", "这是一条很长很长的语音标签标题用于测试页面渲染");
    snprintf(
        note.text,
        sizeof(note.text),
        "%s",
        "这里是完整识别结果，\r\n用来覆盖全文展示路径。\n我们希望这里能按自己的断行规则显示，并且整页从上往下是更近的新标签。");
    snprintf(note.last_error, sizeof(note.last_error), "%s", "网络异常");
    snprintf(note.id, sizeof(note.id), "%s", "note-001");
    note.created_at_epoch_s = 1735689600U;
    note.status = VOICE_NOTE_STATUS_PENDING;
    note.duration_ms = 3200U;
    snprintf(render_state.header_meta, sizeof(render_state.header_meta), "%s", "12:34");
    render_state.state = &state;
    model.mode = INK_APP_RENDER_MODE_VOICE_NOTE;
    model.state = &render_state;

    ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
        && render_buffer_has_ink(buffer, EPD_GDEY0426T82_BUFFER_SIZE)
        && render_pixel_is_black(buffer, 24, 8)
        && render_pixel_is_black(buffer, 408, 10)
        && render_pixel_is_black(buffer, 24, 66);
    if (ok) {
        memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
        state.popup_open = true;
        state.selected_index = 1U;
        ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
            && render_buffer_has_ink(buffer, EPD_GDEY0426T82_BUFFER_SIZE)
            && render_pixel_is_black(buffer, 56, 82)
            && strcmp(overlay.actions[1].label, "标记为完成") == 0
            && strcmp(overlay.actions[2].label, "删除") == 0;
    }
    if (ok) {
        memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
        state.popup_open = false;
        state.full_text_open = true;
        ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
            && render_buffer_has_ink(buffer, EPD_GDEY0426T82_BUFFER_SIZE)
            && render_pixel_is_black(buffer, 24, 66)
            && render_pixel_is_black(buffer, 24, 156)
            && render_pixel_is_black(buffer, 24, 198);
    }
    if (ok) {
        char subtitle[48];

        snprintf(note.last_error, sizeof(note.last_error), "%s", "WiFi 连接失败");
        compose_voice_note_card_subtitle(VOICE_NOTE_TAB_PENDING, &state, &note, subtitle, sizeof(subtitle));
        ok = strstr(subtitle, "01-01 08:00") != NULL
            && strstr(subtitle, "3s") != NULL
            && strstr(subtitle, "未完成") != NULL
            && strstr(subtitle, "WiFi") != NULL
            && strstr(subtitle, "\n") == NULL;
    }
    if (ok) {
        char subtitle[48];

        state.snapshot.busy = true;
        snprintf(state.snapshot.active_note_id, sizeof(state.snapshot.active_note_id), "%s", "note-001");
        snprintf(state.snapshot.status_text, sizeof(state.snapshot.status_text), "%s", "上传识别中");
        compose_voice_note_card_subtitle(VOICE_NOTE_TAB_PENDING, &state, &note, subtitle, sizeof(subtitle));
        ok = strcmp(subtitle, "上传识别中") == 0;
    }
    if (ok) {
        memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
        state.snapshot.busy = false;
        note.status = VOICE_NOTE_STATUS_DONE;
        note.created_at_epoch_s = 1735689600U;
        state.full_text_open = true;
        ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
            && render_pixel_is_black(buffer, 24, 104)
            && render_pixel_is_black(buffer, 24, 120)
            && !overlay.tabs[0].active
            && overlay.tab_count == 0U;
    }
    free(buffer);
    return ok;
}

static bool photo_album_list_truncation_self_test(void)
{
    char base_name[64];
    char title[48];

    if (!photo_album_name_without_extension(
            "图书馆书架上的相册封面预览页面-2026-scan-final.bmp",
            base_name,
            sizeof(base_name))) {
        return false;
    }

    photo_album_compose_list_title(0U, base_name, title, sizeof(title));

    return strstr(base_name, ".bmp") == NULL
        && strstr(title, "...") != NULL
        && strstr(title, ".bmp") == NULL
        && strstr(title, "01. ") == title;
}

static bool photo_album_list_layout_self_test(void)
{
    ink_photo_catalog_t catalog;
    ink_photo_album_app_state_t state;
    ink_photo_album_render_state_t render_state;
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(&catalog, 0, sizeof(catalog));
    memset(&state, 0, sizeof(state));
    memset(&render_state, 0, sizeof(render_state));
    catalog.count = EPD_TEST_PATTERN_MENU_CARD_CAPACITY;
    for (size_t i = 0; i < EPD_TEST_PATTERN_MENU_CARD_CAPACITY; ++i) {
        snprintf(catalog.entries[i].name, sizeof(catalog.entries[i].name), "P%u.bmp", (unsigned)i);
    }
    state.total_count = EPD_TEST_PATTERN_MENU_CARD_CAPACITY;
    state.current_index = 1U;
    state.list_selected_index = 1U;
    state.view_mode = INK_PHOTO_ALBUM_VIEW_LIST;
    snprintf(state.current_name, sizeof(state.current_name), "%s", "P1.bmp");
    render_state.state = &state;
    render_state.catalog = &catalog;
    draw_photo_album_list_page(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &render_state, NULL, NULL);
    ok = render_buffer_has_ink(buffer, EPD_GDEY0426T82_BUFFER_SIZE)
        && render_pixel_is_black(buffer, 24, 66)
        && render_pixel_is_black(buffer, 24, 84);
    free(buffer);
    return ok;
}

static bool gray_calibration_planes_self_test(void)
{
    enum {
        MARGIN_X = 32,
        MARGIN_Y = 80,
        GAP_X = 24,
        GAP_Y = 24,
        BLOCK_W = (EPD_GDEY0426T82_WIDTH - MARGIN_X * 2 - GAP_X) / 2,
        BLOCK_H = (EPD_GDEY0426T82_HEIGHT - MARGIN_Y * 2 - GAP_Y) / 2,
    };
    static const uint8_t kCodes[4] = {
        0U, 1U,
        2U, 3U,
    };
    uint8_t *scratch = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    uint8_t *lsb_plane = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    uint8_t *msb_plane = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool ok = false;

    if (scratch == NULL || lsb_plane == NULL || msb_plane == NULL) {
        goto cleanup;
    }

    if (!fill_gray_calibration_planes(
            scratch,
            EPD_GDEY0426T82_BUFFER_SIZE,
            lsb_plane,
            EPD_GDEY0426T82_BUFFER_SIZE,
            msb_plane,
            EPD_GDEY0426T82_BUFFER_SIZE)) {
        goto cleanup;
    }

    for (int index = 0; index < 4; ++index) {
        const int row = index / 2;
        const int col = index % 2;
        const int sample_x = MARGIN_X + col * (BLOCK_W + GAP_X) + 8;
        const int sample_y = MARGIN_Y + row * (BLOCK_H + GAP_Y) + 8;
        const uint8_t code = kCodes[index];
        const size_t byte_index = (size_t)sample_y * (EPD_GDEY0426T82_WIDTH / 8U) + (size_t)(sample_x / 8);
        const uint8_t mask = (uint8_t)(0x80U >> (sample_x % 8));
        const bool lsb_bit_set = (lsb_plane[byte_index] & mask) != 0U;
        const bool msb_bit_set = (msb_plane[byte_index] & mask) != 0U;

        if (lsb_bit_set != ((code & 0x1U) != 0U)) {
            goto cleanup;
        }
        if (msb_bit_set != ((code & 0x2U) != 0U)) {
            goto cleanup;
        }
    }

    ok = true;

cleanup:
    free(scratch);
    free(lsb_plane);
    free(msb_plane);
    return ok;
}

static bool app_render_request_self_test(void)
{
    ink_app_render_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    model.mode = INK_APP_RENDER_MODE_LAUNCHER;
    model.refresh_strategy = INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST;
    model.request_full_refresh = false;
    model.request_partial_refresh = false;
    model.state = (void *)0x1234U;

    if (!ink_app_render_model_fill_request(&model, &request)) {
        return false;
    }

    if (!(request.use_app_render_model
        && !request.full_refresh
        && request.refresh_strategy == INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST
        && request.refresh_profile == INK_TUNING_REFRESH_FAST_FULL
        && !request.app_request_partial_refresh
        && request.app_render_mode == (uint8_t)INK_APP_RENDER_MODE_LAUNCHER
        && request.app_render_state == (void *)0x1234U)) {
        return false;
    }

    memset(&model, 0, sizeof(model));
    memset(&request, 0, sizeof(request));
    model.mode = INK_APP_RENDER_MODE_LAUNCHER;
    model.refresh_strategy = INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST;
    model.request_full_refresh = true;
    model.request_partial_refresh = false;
    model.state = (void *)0x1234U;

    if (!ink_app_render_model_fill_request(&model, &request)) {
        return false;
    }

    return request.use_app_render_model
        && request.full_refresh
        && request.refresh_strategy == INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST
        && request.refresh_profile == INK_TUNING_REFRESH_FULL
        && !request.app_request_partial_refresh
        && request.app_render_mode == (uint8_t)INK_APP_RENDER_MODE_LAUNCHER
        && request.app_render_state == (void *)0x1234U;
}

static bool app_render_request_strategy_roundtrip_self_test(void)
{
    ink_app_render_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    memset(&request, 0, sizeof(request));
    model.mode = INK_APP_RENDER_MODE_USB_MSC;
    model.refresh_strategy = INK_REFRESH_STRATEGY_PAGE_TRANSITION_FULL;
    model.request_full_refresh = true;

    if (!ink_app_render_model_fill_request(&model, &request)) {
        return false;
    }

    return request.refresh_strategy == INK_REFRESH_STRATEGY_PAGE_TRANSITION_FULL
        && request.full_refresh;
}

static bool app_render_request_photo_album_interrupt_self_test(void)
{
    ink_app_render_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    memset(&request, 0, sizeof(request));
    model.mode = INK_APP_RENDER_MODE_PHOTO_ALBUM;
    model.refresh_strategy = INK_REFRESH_STRATEGY_GRAY_IMAGE_INTERRUPTIBLE;
    model.request_aggressive_interrupt = true;
    model.state = (void *)0x5678U;

    if (!ink_app_render_model_fill_request(&model, &request)) {
        return false;
    }

    return request.use_app_render_model
        && request.refresh_strategy == INK_REFRESH_STRATEGY_GRAY_IMAGE_INTERRUPTIBLE
        && request.use_aggressive_interrupt
        && !request.use_reader_hold_navigation
        && !request.full_refresh
        && request.app_render_mode == (uint8_t)INK_APP_RENDER_MODE_PHOTO_ALBUM
        && request.app_render_state == (void *)0x5678U
        && !request_pins_during_transmitting(&request);
}

bool ink_app_render_self_test(void)
{
    if (!ink_launcher_app_self_test()) {
        printf("FAIL render launcher_app\n");
        return false;
    }
    if (!ink_reader_app_self_test()) {
        printf("FAIL render reader_app\n");
        return false;
    }
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
    if (!app_library_request_long_title_self_test()) {
        printf("FAIL render library_request_long_title\n");
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
    if (!app_overlay_never_promotes_to_reader_full_window_self_test()) {
        printf("FAIL render overlay_never_promotes_to_reader_full_window\n");
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
    if (!app_reader_full_window_stock_fallback_avoids_native_path_self_test()) {
        printf("FAIL render reader_full_window_stock_fallback_avoids_native\n");
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
    if (!app_render_model_launcher_self_test()) {
        printf("FAIL render model_launcher\n");
        return false;
    }
    if (!app_render_model_reader_placeholder_self_test()) {
        printf("FAIL render model_reader_placeholder\n");
        return false;
    }
    if (!app_render_model_reader_subsystem_self_test()) {
        printf("FAIL render model_reader_subsystem\n");
        return false;
    }
    if (!app_render_model_usb_msc_self_test()) {
        printf("FAIL render model_usb_msc\n");
        return false;
    }
    if (!app_render_model_usb_msc_keeps_time_badge_self_test()) {
        printf("FAIL render model_usb_msc_keeps_time_badge\n");
        return false;
    }
    if (!app_library_overlay_render_self_test()) {
        printf("FAIL render library_overlay\n");
        return false;
    }
    if (!app_library_overlay_header_meta_self_test()) {
        printf("FAIL render library_overlay_header_meta\n");
        return false;
    }
    if (!app_library_overlay_tabs_follow_shell_width_self_test()) {
        printf("FAIL render library_overlay_tabs_follow_shell_width\n");
        return false;
    }
    if (!app_library_overlay_selected_favorite_remains_visible_self_test()) {
        printf("FAIL render library_overlay_selected_favorite_remains_visible\n");
        return false;
    }
    if (!app_reader_menu_request_self_test()) {
        printf("FAIL render reader_menu_request\n");
        return false;
    }
    if (!app_reader_text_turn_strategy_self_test()) {
        printf("FAIL render reader_text_turn_strategy\n");
        return false;
    }
    if (!app_reader_overlay_strategy_self_test()) {
        printf("FAIL render reader_overlay_strategy\n");
        return false;
    }
    if (!app_reader_menu_overlay_render_self_test()) {
        printf("FAIL render reader_menu_overlay\n");
        return false;
    }
    if (!app_reader_menu_partial_window_policy_self_test()) {
        printf("FAIL render reader_menu_partial_window_policy\n");
        return false;
    }
    if (!app_reader_loading_overlay_request_self_test()) {
        printf("FAIL render reader_loading_overlay_request\n");
        return false;
    }
    if (!app_reader_loading_overlay_header_meta_self_test()) {
        printf("FAIL render reader_loading_overlay_header_meta\n");
        return false;
    }
    if (!app_reader_chapter_menu_request_layout_self_test()) {
        printf("FAIL render reader_chapter_menu_request_layout\n");
        return false;
    }
    if (!app_reader_loading_overlay_masks_background_self_test()) {
        printf("FAIL render reader_loading_overlay_masks_background\n");
        return false;
    }
    if (!app_overlay_menu_font_selection_self_test()) {
        printf("FAIL render overlay_menu_font_selection\n");
        return false;
    }
    if (!app_render_model_wifi_setup_self_test()) {
        printf("FAIL render model_wifi_setup\n");
        return false;
    }
    if (!app_render_model_photo_album_self_test()) {
        printf("FAIL render model_photo_album\n");
        return false;
    }
    if (!app_render_model_voice_note_self_test()) {
        printf("FAIL render model_voice_note\n");
        return false;
    }
    if (!photo_album_list_truncation_self_test()) {
        printf("FAIL render photo_album_list_truncation\n");
        return false;
    }
    if (!app_render_request_strategy_roundtrip_self_test()) {
        printf("FAIL render strategy_roundtrip\n");
        return false;
    }
    if (!photo_album_list_layout_self_test()) {
        printf("FAIL render photo_album_list_layout\n");
        return false;
    }
    if (!gray_calibration_planes_self_test()) {
        printf("FAIL render gray_calibration_planes\n");
        return false;
    }
    if (!app_render_request_self_test()) {
        printf("FAIL render app_render_request\n");
        return false;
    }
    if (!app_render_request_photo_album_interrupt_self_test()) {
        printf("FAIL render app_render_request_photo_album_interrupt\n");
        return false;
    }
    return true;
}
