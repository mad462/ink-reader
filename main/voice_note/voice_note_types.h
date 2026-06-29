#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    VOICE_NOTE_MAX_NOTES = 64,
    VOICE_NOTE_ID_LENGTH = 40,
    VOICE_NOTE_TITLE_LENGTH = 96,
    VOICE_NOTE_TEXT_LENGTH = 1024,
    VOICE_NOTE_PATH_LENGTH = 160,
    VOICE_NOTE_ERROR_LENGTH = 32,
    VOICE_NOTE_STATUS_COPY_LENGTH = 48,
};

typedef enum {
    VOICE_NOTE_TAB_ALL = 0,
    VOICE_NOTE_TAB_PENDING,
    VOICE_NOTE_TAB_DONE,
    VOICE_NOTE_TAB_COUNT,
} voice_note_tab_t;

typedef enum {
    VOICE_NOTE_STATUS_PENDING = 0,
    VOICE_NOTE_STATUS_DONE,
} voice_note_status_t;

typedef enum {
    VOICE_NOTE_TRANSCRIPT_PROCESSING = 0,
    VOICE_NOTE_TRANSCRIPT_READY,
    VOICE_NOTE_TRANSCRIPT_FAILED,
} voice_note_transcript_state_t;

typedef enum {
    VOICE_NOTE_JOB_IDLE = 0,
    VOICE_NOTE_JOB_RECORDING,
    VOICE_NOTE_JOB_PACKAGING,
    VOICE_NOTE_JOB_PERSISTING_WAV,
    VOICE_NOTE_JOB_WIFI_CONNECTING,
    VOICE_NOTE_JOB_UPLOADING,
    VOICE_NOTE_JOB_RECOGNIZING,
    VOICE_NOTE_JOB_PERSISTING_RESULT,
    VOICE_NOTE_JOB_COMPLETED,
    VOICE_NOTE_JOB_FAILED,
    VOICE_NOTE_JOB_INVALID_SHORT_RECORDING,
} voice_note_job_state_t;

typedef enum {
    VOICE_NOTE_PLAYBACK_IDLE = 0,
    VOICE_NOTE_PLAYBACK_PLAYING,
    VOICE_NOTE_PLAYBACK_PAUSED,
    VOICE_NOTE_PLAYBACK_COMPLETED,
    VOICE_NOTE_PLAYBACK_FAILED,
} voice_note_playback_state_t;

typedef struct {
    char id[VOICE_NOTE_ID_LENGTH];
    uint32_t created_at_epoch_s;
    uint32_t updated_at_epoch_s;
    voice_note_status_t status;
    voice_note_transcript_state_t transcript_state;
    char title[VOICE_NOTE_TITLE_LENGTH];
    char text[VOICE_NOTE_TEXT_LENGTH];
    char wav_path[VOICE_NOTE_PATH_LENGTH];
    uint32_t duration_ms;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    char last_error[VOICE_NOTE_ERROR_LENGTH];
} voice_note_note_t;

typedef struct {
    voice_note_job_state_t state;
    voice_note_playback_state_t playback_state;
    uint32_t sequence;
    bool busy;
    bool stop_due_to_limit;
    uint32_t started_ms;
    uint32_t capture_duration_ms;
    uint32_t transient_until_ms;
    size_t pcm_bytes;
    size_t wav_bytes;
    size_t note_count;
    char status_text[VOICE_NOTE_STATUS_COPY_LENGTH];
    char active_note_id[VOICE_NOTE_ID_LENGTH];
    char playback_note_id[VOICE_NOTE_ID_LENGTH];
    uint32_t playback_total_ms;
    uint32_t playback_position_ms;
} voice_note_service_snapshot_t;
