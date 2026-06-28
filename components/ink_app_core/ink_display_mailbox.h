#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epd_gdey0426t82.h"
#include "epd_test_pattern.h"
#include "ink_runtime_shell.h"

typedef struct {
    uint32_t seq;
    uint32_t input_ms;
    uint32_t submitted_ms;
    uint32_t command_latency_ms;
    uint32_t view_build_ms;
    ink_runtime_shell_command_t command;
    ink_runtime_shell_page_t page;
    bool full_refresh;
    bool use_app_render_model;
    bool use_font;
    bool use_reader_layout;
    bool use_bitmap_page;
    bool use_native_page;
    bool use_footer_overlay;
    bool use_fast_browse_overlay;
    bool use_reader_hold_navigation;
    bool use_library_overlay;
    bool use_reader_menu_overlay;
    bool use_aggressive_interrupt;
    bool force_fixed_footer_partial;
    bool force_white_page;
    bool force_fast_full_commit;
    bool use_grid_compare_variant;
    bool app_request_partial_refresh;
    uint8_t tuning_page;
    uint8_t app_render_mode;
    uint8_t refresh_profile;
    uint8_t grid_compare_step;
    uint8_t grid_compare_variant_index;
    uint16_t app_partial_x;
    uint16_t app_partial_y;
    uint16_t app_partial_w;
    uint16_t app_partial_h;
    void *app_render_state;
    void *owner_ui_model;
    const uint8_t *bitmap_page_buffer;
    size_t bitmap_page_length;
    const uint8_t *native_page_buffer;
    size_t native_page_length;
    ink_runtime_shell_view_t shell_view;
    ink_reader_session_view_t reader_view;
    epd_test_pattern_reader_menu_overlay_t menu_overlay;
    char overlay_left[48];
    char overlay_right[64];
} ink_display_request_t;

typedef struct {
    uint32_t submitted_count;
    uint32_t merged_count;
    uint32_t cancelled_count;
    uint32_t discarded_stale_count;
    uint32_t completed_count;
} ink_display_mailbox_stats_t;

typedef struct {
    TaskHandle_t notify_task;
    ink_display_request_t latest_request;
    uint32_t latest_seq;
    uint32_t active_seq;
    uint32_t completed_seq;
    uint8_t *bitmap_snapshot_buffers[2];
    uint8_t *native_snapshot_buffers[2];
    uint8_t latest_bitmap_slot;
    uint8_t active_bitmap_slot;
    uint8_t latest_native_slot;
    uint8_t active_native_slot;
    ink_display_mailbox_stats_t stats;
} ink_display_mailbox_t;

void ink_display_mailbox_init(
    ink_display_mailbox_t *mailbox,
    TaskHandle_t notify_task,
    uint8_t *bitmap_snapshot_a,
    uint8_t *bitmap_snapshot_b,
    uint8_t *native_snapshot_a,
    uint8_t *native_snapshot_b);
void ink_display_mailbox_set_notify_task(ink_display_mailbox_t *mailbox, TaskHandle_t notify_task);
uint32_t ink_display_mailbox_submit(
    ink_display_mailbox_t *mailbox,
    const ink_display_request_t *request);
bool ink_display_mailbox_try_claim_latest(
    ink_display_mailbox_t *mailbox,
    ink_display_request_t *request);
bool ink_display_mailbox_has_newer_than(const ink_display_mailbox_t *mailbox, uint32_t seq);
bool ink_display_mailbox_is_idle(const ink_display_mailbox_t *mailbox);
void ink_display_mailbox_invalidate_pending(ink_display_mailbox_t *mailbox);
void ink_display_mailbox_discard_queued_only(ink_display_mailbox_t *mailbox);
void ink_display_mailbox_note_cancelled(ink_display_mailbox_t *mailbox);
void ink_display_mailbox_note_discarded_stale(ink_display_mailbox_t *mailbox);
void ink_display_mailbox_note_completed(ink_display_mailbox_t *mailbox, uint32_t seq);
void ink_display_mailbox_snapshot_stats(
    const ink_display_mailbox_t *mailbox,
    ink_display_mailbox_stats_t *stats);
bool ink_display_mailbox_self_test(void);
