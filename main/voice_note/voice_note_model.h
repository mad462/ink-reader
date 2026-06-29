#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "voice_note/voice_note_types.h"

#define VOICE_NOTE_ROOT_DIR "/sdcard/.ink-reader/voice_notes"
#define VOICE_NOTE_INDEX_PATH "/sdcard/.ink-reader/voice_notes/index.json"

bool voice_note_model_build_note_paths(
    uint32_t epoch_s,
    uint32_t sequence,
    char *note_id,
    size_t note_id_size,
    char *json_path,
    size_t json_path_size,
    char *wav_path,
    size_t wav_path_size);
void voice_note_model_build_title(
    const char *text,
    voice_note_transcript_state_t transcript_state,
    char *title,
    size_t title_size);
bool voice_note_model_is_short_recording(uint32_t duration_ms, uint32_t min_duration_ms);
bool voice_note_model_status_copy_for_job(
    voice_note_job_state_t state,
    char *buffer,
    size_t buffer_size);
bool voice_note_model_self_test(void);
