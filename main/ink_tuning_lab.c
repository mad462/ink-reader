#include "ink_tuning_lab.h"

#include <string.h>

enum {
    INK_TUNING_GRID_COMPARE_CELL_COUNT = 16,
};

static const char *const s_grid_compare_sweep_tag = "P1";

void ink_tuning_lab_init(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL) {
        return;
    }
    memset(lab, 0, sizeof(*lab));
    lab->current_page = INK_TUNING_PAGE_TEXT;
    lab->refresh_profile = INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY;
    lab->force_full_refresh = false;
    lab->reader_white_refresh_pending = false;
    lab->grid_compare_active = false;
}

bool ink_tuning_lab_previous_page(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL || lab->current_page == INK_TUNING_PAGE_TEXT) {
        return false;
    }
    lab->current_page = (ink_tuning_page_t)(lab->current_page - 1);
    lab->grid_compare_active = lab->current_page == INK_TUNING_PAGE_GRID_COMPARE;
    return true;
}

bool ink_tuning_lab_next_page(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL || lab->current_page >= (INK_TUNING_PAGE_COUNT - 1)) {
        return false;
    }
    lab->current_page = (ink_tuning_page_t)(lab->current_page + 1);
    lab->grid_compare_active = lab->current_page == INK_TUNING_PAGE_GRID_COMPARE;
    return true;
}

bool ink_tuning_lab_cycle_refresh_profile(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL) {
        return false;
    }
    lab->refresh_profile = (ink_tuning_refresh_profile_t)((lab->refresh_profile + 1) % INK_TUNING_REFRESH_COUNT);
    return true;
}

bool ink_tuning_lab_request_force_full_refresh(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL) {
        return false;
    }
    lab->force_full_refresh = true;
    return true;
}

bool ink_tuning_lab_request_reader_white_refresh(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL) {
        return false;
    }
    lab->reader_white_refresh_pending = true;
    return true;
}

bool ink_tuning_lab_restart_grid_compare(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL) {
        return false;
    }
    lab->current_page = INK_TUNING_PAGE_GRID_COMPARE;
    lab->grid_compare_active = true;
    lab->grid_compare_step = 0U;
    lab->force_full_refresh = true;
    lab->reader_white_refresh_pending = true;
    return true;
}

bool ink_tuning_lab_advance_grid_compare(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL
        || !lab->grid_compare_active
        || lab->current_page != INK_TUNING_PAGE_GRID_COMPARE
        || lab->grid_compare_step >= INK_TUNING_GRID_COMPARE_CELL_COUNT) {
        return false;
    }

    lab->grid_compare_step++;
    if (lab->grid_compare_step >= INK_TUNING_GRID_COMPARE_CELL_COUNT) {
        lab->grid_compare_active = false;
    }
    return true;
}

uint8_t ink_tuning_lab_grid_compare_cell_count(void)
{
    return INK_TUNING_GRID_COMPARE_CELL_COUNT;
}

const char *ink_tuning_lab_grid_compare_sweep_tag(void)
{
    return s_grid_compare_sweep_tag;
}

const char *ink_tuning_lab_page_name(ink_tuning_page_t page)
{
    switch (page) {
        case INK_TUNING_PAGE_TEXT:
            return "TEXT";
        case INK_TUNING_PAGE_FOOTER:
            return "FOOTER";
        case INK_TUNING_PAGE_DETAIL:
            return "DETAIL";
        case INK_TUNING_PAGE_HIGH_DELTA:
            return "HIGH_DELTA";
        case INK_TUNING_PAGE_GRID_COMPARE:
            return "GRID_COMPARE";
        default:
            return "UNKNOWN";
    }
}

const char *ink_tuning_lab_refresh_profile_name(ink_tuning_refresh_profile_t profile)
{
    switch (profile) {
        case INK_TUNING_REFRESH_FULL:
            return "FULL";
        case INK_TUNING_REFRESH_FAST_FULL:
            return "FAST_FULL";
        case INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY:
            return "PARTIAL_AUTO";
        case INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER:
            return "PARTIAL_FOOTER";
        case INK_TUNING_REFRESH_CUSTOM_LUT_A:
            return "CUSTOM_LUT_A";
        case INK_TUNING_REFRESH_CUSTOM_LUT_B:
            return "CUSTOM_LUT_B";
        default:
            return "UNKNOWN";
    }
}

bool ink_tuning_lab_self_test(void)
{
    ink_tuning_lab_state_t lab;

    ink_tuning_lab_init(&lab);
    if (lab.current_page != INK_TUNING_PAGE_TEXT) {
        return false;
    }
    if (lab.refresh_profile != INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY) {
        return false;
    }
    if (lab.force_full_refresh
        || lab.reader_white_refresh_pending
        || lab.grid_compare_active
        || lab.grid_compare_step != 0U) {
        return false;
    }
    if (ink_tuning_lab_previous_page(&lab)) {
        return false;
    }
    if (!ink_tuning_lab_next_page(&lab) || lab.current_page != INK_TUNING_PAGE_FOOTER) {
        return false;
    }
    lab.current_page = INK_TUNING_PAGE_HIGH_DELTA;
    if (!ink_tuning_lab_next_page(&lab)) {
        return false;
    }
    if (lab.current_page != INK_TUNING_PAGE_GRID_COMPARE || !lab.grid_compare_active) {
        return false;
    }
    if (!ink_tuning_lab_advance_grid_compare(&lab) || lab.grid_compare_step != 1U) {
        return false;
    }
    if (!ink_tuning_lab_restart_grid_compare(&lab)) {
        return false;
    }
    if (!lab.force_full_refresh
        || !lab.reader_white_refresh_pending
        || !lab.grid_compare_active
        || lab.grid_compare_step != 0U) {
        return false;
    }
    lab.current_page = INK_TUNING_PAGE_TEXT;
    lab.grid_compare_active = false;
    if (ink_tuning_lab_previous_page(&lab)) {
        return false;
    }
    if (!ink_tuning_lab_next_page(&lab) || lab.current_page != INK_TUNING_PAGE_FOOTER) {
        return false;
    }
    if (!ink_tuning_lab_cycle_refresh_profile(&lab)
        || lab.refresh_profile != INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER) {
        return false;
    }
    if (!ink_tuning_lab_request_force_full_refresh(&lab) || !lab.force_full_refresh) {
        return false;
    }
    if (!ink_tuning_lab_request_reader_white_refresh(&lab)) {
        return false;
    }
    return lab.reader_white_refresh_pending;
}
