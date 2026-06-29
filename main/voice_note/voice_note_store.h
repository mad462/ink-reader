#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "voice_note/voice_note_types.h"

esp_err_t voice_note_store_init(void);
esp_err_t voice_note_store_reload(void);
size_t voice_note_store_count(void);
bool voice_note_store_copy_summaries(
    voice_note_tab_t tab,
    voice_note_note_t *notes,
    size_t capacity,
    size_t *count_out);
bool voice_note_store_find_note(const char *note_id, voice_note_note_t *out_note);
esp_err_t voice_note_store_create_processing_note(const voice_note_note_t *note);
esp_err_t voice_note_store_update_note(const voice_note_note_t *note);
esp_err_t voice_note_store_delete_note(const char *note_id);
bool voice_note_store_self_test(void);
