#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    INK_TUNING_PAGE_TEXT = 0,
    INK_TUNING_PAGE_FOOTER,
    INK_TUNING_PAGE_DETAIL,
    INK_TUNING_PAGE_HIGH_DELTA,
    INK_TUNING_PAGE_GRID_COMPARE,
    INK_TUNING_PAGE_GRAY_CAL,
    INK_TUNING_PAGE_COUNT
} ink_tuning_page_t;

typedef enum {
    INK_TUNING_REFRESH_FULL = 0,
    INK_TUNING_REFRESH_FAST_FULL,
    INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY,
    INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER,
    INK_TUNING_REFRESH_CUSTOM_LUT_A,
    INK_TUNING_REFRESH_CUSTOM_LUT_B,
    INK_TUNING_REFRESH_COUNT
} ink_tuning_refresh_profile_t;

typedef struct {
    ink_tuning_page_t current_page;
    ink_tuning_refresh_profile_t refresh_profile;
    uint32_t render_counter;
    uint32_t consecutive_partial_count;
    size_t auto_flip_start_page;
    size_t auto_flip_page_count;
    bool auto_flip_stress_enabled;
    bool force_full_refresh;
    bool reader_white_refresh_pending;
    bool grid_compare_active;
    uint8_t grid_compare_step;
} ink_tuning_lab_state_t;

void ink_tuning_lab_init(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_previous_page(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_next_page(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_cycle_refresh_profile(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_request_force_full_refresh(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_request_reader_white_refresh(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_restart_grid_compare(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_advance_grid_compare(ink_tuning_lab_state_t *lab);
uint8_t ink_tuning_lab_grid_compare_cell_count(void);
const char *ink_tuning_lab_grid_compare_sweep_tag(void);
const char *ink_tuning_lab_page_name(ink_tuning_page_t page);
const char *ink_tuning_lab_refresh_profile_name(ink_tuning_refresh_profile_t profile);
bool ink_tuning_lab_self_test(void);
