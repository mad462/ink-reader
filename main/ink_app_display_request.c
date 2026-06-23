#include "ink_app_render.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/task.h"

#include "ink_app_ui.h"

static void format_reader_footer_text(
    const ink_ui_model_t *model,
    size_t page_index,
    char *left,
    size_t left_size,
    char *right,
    size_t right_size)
{
    size_t display_chapter_index = 0U;
    size_t display_chapter_total = 0U;
    const char *display_chapter_title = NULL;
    unsigned percent = 0U;

    if (left != NULL && left_size > 0U) {
        left[0] = '\0';
    }
    if (right != NULL && right_size > 0U) {
        right[0] = '\0';
    }
    if (model == NULL || !ink_reader_session_is_xtc_active(&model->reader_session)) {
        return;
    }

    if (model->reader_session.total_pages > 0U) {
        percent = (unsigned)(((page_index + 1U) * 100U) / model->reader_session.total_pages);
        if (percent > 100U) {
            percent = 100U;
        }
    }
    if (left != NULL
        && left_size > 0U
        && ink_reader_session_resolve_display_chapter_for_page(
            &model->reader_session,
            page_index,
            &display_chapter_index,
            &display_chapter_total,
            &display_chapter_title)) {
        snprintf(
            left,
            left_size,
            "%s",
            display_chapter_title != NULL ? display_chapter_title : "");
    } else if (left != NULL
        && left_size > 0U
        && model->reader_session.current_chapter_name[0] != '\0') {
        snprintf(
            left,
            left_size,
            "%s",
            model->reader_session.current_chapter_name);
    }

    if (right != NULL && right_size > 0U) {
        snprintf(
            right,
            right_size,
            "%u%% %u/%u",
            percent,
            (unsigned)(page_index + 1U),
            (unsigned)model->reader_session.total_pages);
    }
}

static void format_library_footer_text(
    const ink_ui_model_t *model,
    char *left,
    size_t left_size,
    char *right,
    size_t right_size)
{
    if (left != NULL && left_size > 0U) {
        snprintf(
            left,
            left_size,
            "%s",
            (model != NULL && model->shell.can_resume_book) ? "确认继续阅读" : "确认打开书籍");
    }

    if (right != NULL && right_size > 0U) {
        const unsigned book_count = model != NULL ? (unsigned)model->browser.entry_count : 0U;
        snprintf(right, right_size, "%u 本书", book_count);
    }
}

static void format_lab_footer_text(
    const ink_ui_model_t *model,
    char *left,
    size_t left_size,
    char *right,
    size_t right_size)
{
    const bool reader_active =
        model != NULL && ink_reader_session_is_xtc_active(&model->reader_session);

    if (left != NULL && left_size > 0U) {
        left[0] = '\0';
    }
    if (right != NULL && right_size > 0U) {
        right[0] = '\0';
    }
    if (model == NULL) {
        return;
    }

    if (reader_active) {
        const size_t footer_page = model->fast_browse.active
            ? model->fast_browse.target_page
            : model->reader_session.current_page;
        format_reader_footer_text(model, footer_page, left, left_size, right, right_size);
    } else if (model->shell.page == INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        format_library_footer_text(model, left, left_size, right, right_size);
    } else if (left != NULL && left_size > 0U) {
        snprintf(
            left,
            left_size,
            "P%u/%u %s",
            (unsigned)(model->lab.current_page + 1U),
            (unsigned)INK_TUNING_PAGE_COUNT,
            ink_tuning_lab_page_name(model->lab.current_page));
    }
    if (!reader_active
        && model->shell.page != INK_RUNTIME_SHELL_PAGE_LIBRARY
        && right != NULL
        && right_size > 0U) {
        if (model->lab.current_page == INK_TUNING_PAGE_GRID_COMPARE) {
            snprintf(
                right,
                right_size,
                "GRID %s %02u/%02u",
                ink_tuning_lab_grid_compare_sweep_tag(),
                (unsigned)model->lab.grid_compare_step,
                (unsigned)ink_tuning_lab_grid_compare_cell_count());
        } else {
            snprintf(
                right,
                right_size,
                "%s%s%s #%u",
                ink_tuning_lab_refresh_profile_name(model->lab.refresh_profile),
                model->lab.auto_flip_stress_enabled ? "+AUTO" : "",
                model->lab.reader_white_refresh_pending ? "+WHITE" : "",
                (unsigned)model->lab.render_counter);
        }
    }
}

bool ink_app_build_display_request(
    const ink_ui_model_t *model,
    uint32_t input_ms,
    uint32_t command_latency_ms,
    ink_runtime_shell_command_t command,
    ink_display_request_t *request)
{
    const bool reader_active = model != NULL
        && ink_reader_session_is_xtc_active(&model->reader_session);
    const bool fast_browse_overlay = reader_active
        && model != NULL
        && model->fast_browse.active;

    if (model == NULL || request == NULL) {
        return false;
    }

    memset(request, 0, sizeof(*request));
    request->input_ms = input_ms;
    request->command_latency_ms = command_latency_ms;
    request->submitted_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    request->command = command;
    request->page = model->shell.page;
    request->tuning_page = (uint8_t)model->lab.current_page;
    request->refresh_profile = (uint8_t)(
        reader_active
            ? INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY
            : model->lab.refresh_profile);
    request->grid_compare_step = model->lab.grid_compare_step;
    request->use_grid_compare_variant =
        !reader_active
        && model->lab.current_page == INK_TUNING_PAGE_GRID_COMPARE
        && model->lab.grid_compare_step > 0U;
    request->grid_compare_variant_index =
        request->use_grid_compare_variant
            ? (uint8_t)(model->lab.grid_compare_step - 1U)
            : 0U;
    request->force_white_page = model->lab.reader_white_refresh_pending;
    request->use_fast_browse_overlay = fast_browse_overlay;
    request->force_fixed_footer_partial =
        fast_browse_overlay
        || (!ink_reader_session_is_xtc_active(&model->reader_session)
            && (model->lab.refresh_profile == INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER
                || (model->lab.refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_A
                    && model->lab.current_page == INK_TUNING_PAGE_FOOTER)));
    request->use_footer_overlay = !request->force_white_page
        && !request->use_grid_compare_variant;
    request->force_fast_full_commit =
        model != NULL
        && model->reader_fast_full_commit_pending
        && !fast_browse_overlay;
    if (request->page == INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        request->use_footer_overlay = false;
    }
    request->full_refresh = model->lab.force_full_refresh
        || (request->page != INK_RUNTIME_SHELL_PAGE_READER
            && ink_runtime_shell_requires_full_refresh(&model->shell))
        || request->refresh_profile == INK_TUNING_REFRESH_FULL;
    if (request->force_fast_full_commit) {
        request->refresh_profile = INK_TUNING_REFRESH_FAST_FULL;
    }
    request->use_font = request->page != INK_RUNTIME_SHELL_PAGE_READER;
    request->use_reader_layout = request->page == INK_RUNTIME_SHELL_PAGE_READER
        && reader_active
        && !fast_browse_overlay;
    if (!request->force_white_page
        && !fast_browse_overlay
        && ink_reader_session_has_prepared_page(&model->reader_session)) {
        request->use_bitmap_page = true;
        request->bitmap_page_buffer = ink_reader_session_prepared_page_buffer(&model->reader_session);
        request->bitmap_page_length = ink_reader_session_prepared_page_length(&model->reader_session);
    }
    if (!request->force_white_page
        && !request->use_footer_overlay
        && !fast_browse_overlay
        && ink_reader_session_has_native_page(&model->reader_session)) {
        request->use_native_page = true;
        request->native_page_buffer = ink_reader_session_native_page_buffer(&model->reader_session);
        request->native_page_length = ink_reader_session_native_page_length(&model->reader_session);
    }
    ink_runtime_shell_render(
        &model->shell,
        &model->browser,
        &model->reader_session,
        &request->shell_view);
    (void)ink_reader_session_get_text_view(&model->reader_session, &request->reader_view);
    format_lab_footer_text(
        model,
        request->overlay_left,
        sizeof(request->overlay_left),
        request->overlay_right,
        sizeof(request->overlay_right));
    if (request->page == INK_RUNTIME_SHELL_PAGE_LIBRARY) {
        request->overlay_left[0] = '\0';
        request->overlay_right[0] = '\0';
    }

    return true;
}
