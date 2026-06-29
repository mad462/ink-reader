#include "audio_record_probe_logic.h"

bool audio_record_probe_logic_self_test(void)
{
    audio_record_probe_session_t session;
    audio_record_probe_session_init(&session, 10000U, 320000U);

    if (!audio_record_probe_try_start(&session, 100U)) {
        return false;
    }
    if (session.state != AUDIO_RECORD_PROBE_STATE_RECORDING) {
        return false;
    }

    audio_record_probe_append_bytes(&session, 640U, true, 120U);
    if (session.captured_bytes != 640U || session.state != AUDIO_RECORD_PROBE_STATE_RECORDING) {
        return false;
    }

    audio_record_probe_append_bytes(&session, 0U, false, 130U);
    if (session.state != AUDIO_RECORD_PROBE_STATE_EXPORT_PENDING) {
        return false;
    }

    audio_record_probe_mark_exported(&session, false);
    if (session.state != AUDIO_RECORD_PROBE_STATE_IDLE) {
        return false;
    }

    audio_record_probe_session_init(&session, 10000U, 320000U);
    if (!audio_record_probe_try_start(&session, 200U)) {
        return false;
    }
    audio_record_probe_append_bytes(&session, 320000U, true, 10250U);
    if (session.state != AUDIO_RECORD_PROBE_STATE_EXPORT_PENDING || !session.stop_due_to_limit) {
        return false;
    }

    audio_record_probe_mark_exported(&session, true);
    if (session.state != AUDIO_RECORD_PROBE_STATE_WAIT_FOR_RELEASE) {
        return false;
    }

    audio_record_probe_poll_release(&session, false);
    if (session.state != AUDIO_RECORD_PROBE_STATE_IDLE) {
        return false;
    }

    if (audio_record_probe_pcm_duration_ms(0U, 16000U, 16U, 1U) != 0U) {
        return false;
    }
    if (audio_record_probe_pcm_duration_ms(32000U, 16000U, 16U, 1U) != 1000U) {
        return false;
    }

    audio_record_probe_session_init(&session, 10000U, 320000U);
    if (!audio_record_probe_try_start(&session, 500U)) {
        return false;
    }
    audio_record_probe_append_bytes(&session, 3200U, true, 1500U);
    if (audio_record_probe_remaining_ms(&session) != 9000U) {
        return false;
    }
    if (!audio_record_probe_progress_due(1000U, &session.started_ms, 250U)) {
        return false;
    }
    if (audio_record_probe_progress_due(1100U, &session.started_ms, 250U)) {
        return false;
    }
    if (!audio_record_probe_progress_due(1300U, &session.started_ms, 250U)) {
        return false;
    }
    if (audio_record_probe_progress_due(2000U, &session.started_ms, 0U)) {
        return false;
    }

    return audio_record_probe_choose_storage(true, true, true) == AUDIO_RECORD_PROBE_STORAGE_PSRAM
        && audio_record_probe_choose_storage(true, false, true) == AUDIO_RECORD_PROBE_STORAGE_INTERNAL
        && audio_record_probe_choose_storage(false, false, true) == AUDIO_RECORD_PROBE_STORAGE_INTERNAL
        && audio_record_probe_choose_storage(false, false, false) == AUDIO_RECORD_PROBE_STORAGE_NONE
        && audio_record_probe_next_chunk_size(0U, 4096U) == 0U
        && audio_record_probe_next_chunk_size(1024U, 4096U) == 1024U
        && audio_record_probe_next_chunk_size(8192U, 4096U) == 4096U
        && audio_record_probe_next_chunk_size(8192U, 0U) == 0U;
}
