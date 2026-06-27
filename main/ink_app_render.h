#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "apps/ink_app_iface.h"
#include "ink_app_priv.h"

const char *ink_app_shell_page_name(ink_runtime_shell_page_t page);
const char *ink_app_shell_command_name(ink_runtime_shell_command_t command);
bool ink_app_page_shows_live_button_state(ink_runtime_shell_page_t page);
void ink_app_log_button_snapshot(uint32_t now_ms, const ink_button_snapshot_t *snapshot);
void ink_app_note_command(
    uint32_t now_ms,
    ink_runtime_shell_page_t page,
    ink_runtime_shell_command_t command
);
bool ink_app_build_display_request(
    const ink_ui_model_t *model,
    uint32_t input_ms,
    uint32_t command_latency_ms,
    ink_runtime_shell_command_t command,
    ink_display_request_t *request
);
esp_err_t ink_app_render_display_request(
    ink_app_context_t *app,
    const ink_display_request_t *request,
    epd_gdey0426t82_phase_t *phase_out
);
bool ink_app_render_reader_subsystem_model_fill_request(
    const ink_ui_model_t *model,
    uint32_t input_ms,
    uint32_t command_latency_ms,
    ink_display_request_t *request);
bool ink_app_render_model_fill_request(
    const ink_app_render_model_t *model,
    ink_display_request_t *request);
esp_err_t ink_app_render_gray_planes_request(
    ink_app_context_t *app,
    const uint8_t *lsb_plane,
    size_t lsb_length,
    const uint8_t *msb_plane,
    size_t msb_length,
    epd_gdey0426t82_phase_t *phase_out);
bool ink_app_render_self_test(void);
