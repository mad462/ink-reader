#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "voice_note/voice_note_types.h"

esp_err_t voice_note_service_init(void);
bool voice_note_service_start_capture(uint32_t now_ms);
bool voice_note_service_stop_capture(uint32_t now_ms);
bool voice_note_service_retry_note(const char *note_id, uint32_t now_ms);
bool voice_note_service_delete_note(const char *note_id);
bool voice_note_service_set_note_status(const char *note_id, voice_note_status_t status);
bool voice_note_service_get_snapshot(voice_note_service_snapshot_t *out_snapshot);
bool voice_note_service_copy_note_summaries(
    voice_note_tab_t tab,
    voice_note_note_t *out_notes,
    size_t capacity,
    size_t *count_out);
bool voice_note_service_load_note(const char *note_id, voice_note_note_t *out_note);
bool voice_note_service_tick(uint32_t now_ms);
bool voice_note_service_self_test(void);
