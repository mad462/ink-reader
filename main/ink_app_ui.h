#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ink_app_priv.h"

void ink_app_button_state_from_snapshot(
    const ink_button_snapshot_t *snapshot,
    ink_runtime_shell_button_state_t *buttons
);
ink_runtime_shell_command_t ink_app_command_from_snapshot(
    const ink_ui_model_t *model,
    const ink_button_snapshot_t *snapshot
);
bool ink_app_should_dispatch_button_event(
    const ink_button_snapshot_t *snapshot,
    uint32_t now_ms,
    uint32_t *last_hold_event_ms
);
bool ink_app_handle_ui_command(ink_ui_model_t *model, ink_runtime_shell_command_t command);
size_t ink_app_reader_total_pages(const ink_ui_model_t *model);
size_t ink_app_reader_current_page(const ink_ui_model_t *model);
void ink_app_clear_fast_browse(ink_ui_model_t *model);
bool ink_app_process_reader_xtc_buttons(
    ink_app_context_t *app,
    const ink_button_snapshot_t *snapshot,
    uint32_t event_ms,
    ink_runtime_shell_command_t *command_out);
bool ink_app_process_reader_xtc_buttons_for_model(
    ink_ui_model_t *model,
    const ink_button_snapshot_t *snapshot,
    uint32_t event_ms,
    ink_runtime_shell_command_t *command_out);
bool ink_app_fast_browse_handle_idle(
    ink_app_context_t *app,
    uint32_t now_ms,
    ink_runtime_shell_command_t *command_out);
bool ink_app_fast_browse_handle_idle_for_model(
    ink_ui_model_t *model,
    const ink_runtime_shell_button_state_t *buttons,
    uint32_t now_ms,
    ink_runtime_shell_command_t *command_out);
bool ink_app_drive_reader_nav_hold_for_model(
    ink_ui_model_t *model,
    const ink_runtime_shell_button_state_t *buttons,
    bool display_idle,
    uint32_t now_ms);
bool ink_app_maybe_start_reader_nav_hold_from_snapshot_for_model(
    ink_ui_model_t *model,
    const ink_button_snapshot_t *snapshot,
    bool display_idle,
    uint32_t now_ms);
void ink_app_fast_browse_note_preview_landed(ink_ui_model_t *model);
bool ink_app_advance_reader_auto_flip_stress(ink_ui_model_t *model);
bool ink_app_should_auto_advance_grid_compare(const ink_ui_model_t *model, esp_err_t result);
bool ink_app_advance_grid_compare(ink_ui_model_t *model);
bool ink_app_should_auto_repeat_tuning_probe(const ink_ui_model_t *model, esp_err_t result);
bool ink_app_ui_self_test(void);
