#include "audio_asr_preview_state.h"
#include "audio_asr_web.h"

#include <string.h>

bool test_audio_asr_web_self_test(void)
{
    char escaped[64];
    audio_asr_preview_snapshot_t snapshot = {
        .status = AUDIO_ASR_WEB_STATUS_DONE,
        .sequence = 7U,
        .duration_ms = 3200U,
        .pcm_bytes = 102400U,
        .wav_bytes = 102444U,
        .wifi_elapsed_ms = 1234U,
        .request_elapsed_ms = 2345U,
        .has_audio = true,
    };
    char json[512];

    strcpy(snapshot.transcript_text, "say \"hi\"\npath\\clip");
    strcpy(snapshot.error_text, "line1\r\nline2");

    return audio_asr_web_self_test()
        && audio_asr_web_escape_json_string("say \"hi\"\npath\\clip", escaped, sizeof(escaped))
        && strcmp(escaped, "say \\\"hi\\\"\\npath\\\\clip") == 0
        && audio_asr_web_build_latest_json(&snapshot, json, sizeof(json))
        && strstr(json, "\\\"hi\\\"") != NULL
        && strstr(json, "path\\\\clip") != NULL
        && strstr(json, "line1\\r\\nline2") != NULL
        && strstr(json, "AudioContext") != NULL
        && strstr(json, "gainNode.gain.value=4.0") != NULL
        && strstr(json, "Use boosted playback") != NULL;
}
