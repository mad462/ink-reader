#include "audio_asr_web.h"

#include "audio_asr_preview_state.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"

#define AUDIO_ASR_WEB_JSON_BUFFER_BYTES 4096U
#define AUDIO_ASR_WEB_SERVER_STACK_SIZE 10240U

static const char *const AUDIO_ASR_WEB_HTML =
    "<!doctype html><html><head><meta charset=\"utf-8\"><title>Audio ASR Probe</title></head>"
    "<body><h1>Audio ASR Probe</h1>"
    "<p>Status: <span id=\"status\">unknown</span></p>"
    "<p>Sequence: <span id=\"sequence\">0</span></p>"
    "<p>Duration (ms): <span id=\"duration\">0</span></p>"
    "<p>PCM bytes: <span id=\"pcm_bytes\">0</span></p>"
    "<p>WAV bytes: <span id=\"wav_bytes\">0</span></p>"
    "<p>WiFi elapsed (ms): <span id=\"wifi_elapsed\">0</span></p>"
    "<p>Request elapsed (ms): <span id=\"request_elapsed\">0</span></p>"
    "<p>Transcript: <span id=\"transcript\">n/a</span></p>"
    "<p>Error: <span id=\"error\">n/a</span></p>"
    "<label><input id=\"boost_toggle\" type=\"checkbox\" checked>Use boosted playback</label>"
    "<button id=\"boost_play\" type=\"button\">Play Boosted</button>"
    "<audio id=\"player\" controls></audio>"
    "<script>"
    "let audioContext=null;"
    "let gainNode=null;"
    "let latestAudioUrl='';"
    "async function playBoosted(){"
    "if(!latestAudioUrl){return;}"
    "if(!audioContext){"
    "audioContext=new AudioContext();"
    "gainNode=audioContext.createGain();"
    "gainNode.gain.value=4.0;"
    "gainNode.connect(audioContext.destination);"
    "}"
    "if(audioContext.state==='suspended'){await audioContext.resume();}"
    "const response=await fetch(latestAudioUrl);"
    "const arrayBuffer=await response.arrayBuffer();"
    "const audioBuffer=await audioContext.decodeAudioData(arrayBuffer);"
    "const source=audioContext.createBufferSource();"
    "source.buffer=audioBuffer;"
    "source.connect(gainNode);"
    "source.start();"
    "}"
    "async function refreshLatest(){"
    "const response=await fetch('/api/latest');"
    "const latest=await response.json();"
    "document.getElementById('status').textContent=latest.status||'unknown';"
    "document.getElementById('sequence').textContent=latest.sequence??0;"
    "document.getElementById('duration').textContent=latest.duration_ms??0;"
    "document.getElementById('pcm_bytes').textContent=latest.pcm_bytes??0;"
    "document.getElementById('wav_bytes').textContent=latest.wav_bytes??0;"
    "document.getElementById('wifi_elapsed').textContent=latest.wifi_elapsed_ms??0;"
    "document.getElementById('request_elapsed').textContent=latest.request_elapsed_ms??0;"
    "document.getElementById('transcript').textContent=latest.transcript_text||'n/a';"
    "document.getElementById('error').textContent=latest.error_text||'n/a';"
    "const player=document.getElementById('player');"
    "if(latest.has_audio&&latest.audio_url&&player.dataset.src!==latest.audio_url){"
    "player.src=latest.audio_url;"
    "player.dataset.src=latest.audio_url;"
    "latestAudioUrl=latest.audio_url;"
    "}else if(!latest.has_audio){"
    "player.removeAttribute('src');"
    "player.dataset.src='';"
    "latestAudioUrl='';"
    "}"
    "if(document.getElementById('boost_toggle').checked){"
    "player.volume=1.0;"
    "}"
    "}"
    "document.getElementById('boost_play').addEventListener('click',()=>{playBoosted().catch(console.error);});"
    "refreshLatest();"
    "setInterval(refreshLatest,1000);"
    "</script></body></html>";

static httpd_handle_t s_server;

static httpd_config_t audio_asr_web_make_config(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = AUDIO_ASR_WEB_SERVER_STACK_SIZE;
    return config;
}

bool audio_asr_web_escape_json_string(const char *src, char *dst, size_t dst_size)
{
    size_t used = 0U;
    const char *cursor = src != NULL ? src : "";

    if (dst == NULL || dst_size == 0U) {
        return false;
    }

    while (*cursor != '\0') {
        const char *replacement = NULL;
        char unicode_escape[7];
        size_t replacement_len = 0U;

        switch (*cursor) {
            case '\"':
                replacement = "\\\"";
                break;
            case '\\':
                replacement = "\\\\";
                break;
            case '\b':
                replacement = "\\b";
                break;
            case '\f':
                replacement = "\\f";
                break;
            case '\n':
                replacement = "\\n";
                break;
            case '\r':
                replacement = "\\r";
                break;
            case '\t':
                replacement = "\\t";
                break;
            default:
                if ((unsigned char)*cursor < 0x20U) {
                    int written = snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", (unsigned char)*cursor);
                    if (written != 6) {
                        return false;
                    }
                    replacement = unicode_escape;
                }
                break;
        }

        if (replacement != NULL) {
            replacement_len = strlen(replacement);
            if (used + replacement_len + 1U > dst_size) {
                return false;
            }
            memcpy(dst + used, replacement, replacement_len);
            used += replacement_len;
        } else {
            if (used + 2U > dst_size) {
                return false;
            }
            dst[used++] = *cursor;
        }
        ++cursor;
    }

    dst[used] = '\0';
    return true;
}

bool audio_asr_web_build_latest_json(
    const audio_asr_preview_snapshot_t *snapshot,
    char *buffer,
    size_t buffer_size)
{
    char transcript[2304];
    char error[512];
    int written = 0;

    if (snapshot == NULL || buffer == NULL || buffer_size == 0U) {
        return false;
    }

    if (!audio_asr_web_escape_json_string(snapshot->transcript_text, transcript, sizeof(transcript))) {
        return false;
    }
    if (!audio_asr_web_escape_json_string(snapshot->error_text, error, sizeof(error))) {
        return false;
    }

    written = snprintf(
        buffer,
        buffer_size,
        "{"
        "\"status\":\"%s\","
        "\"sequence\":%lu,"
        "\"duration_ms\":%lu,"
        "\"pcm_bytes\":%lu,"
        "\"wav_bytes\":%lu,"
        "\"wifi_elapsed_ms\":%lu,"
        "\"request_elapsed_ms\":%lu,"
        "\"transcript_text\":\"%s\","
        "\"error_text\":\"%s\","
        "\"audio_url\":\"/audio/latest.wav\","
        "\"has_audio\":%s"
        "}",
        audio_asr_preview_state_status_name(snapshot->status),
        (unsigned long)snapshot->sequence,
        (unsigned long)snapshot->duration_ms,
        (unsigned long)snapshot->pcm_bytes,
        (unsigned long)snapshot->wav_bytes,
        (unsigned long)snapshot->wifi_elapsed_ms,
        (unsigned long)snapshot->request_elapsed_ms,
        transcript,
        error,
        snapshot->has_audio ? "true" : "false");

    return written > 0 && (size_t)written < buffer_size;
}

static esp_err_t audio_asr_web_send_json(httpd_req_t *req)
{
    audio_asr_preview_snapshot_t *snapshot = NULL;
    char *json = NULL;
    esp_err_t ret = ESP_OK;

    snapshot = heap_caps_malloc(sizeof(*snapshot), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    json = heap_caps_malloc(AUDIO_ASR_WEB_JSON_BUFFER_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(snapshot != NULL && json != NULL, ESP_ERR_NO_MEM, "audio_asr_web", "json response alloc failed");

    audio_asr_preview_state_get_snapshot(snapshot);
    ESP_GOTO_ON_FALSE(
        audio_asr_web_build_latest_json(snapshot, json, AUDIO_ASR_WEB_JSON_BUFFER_BYTES),
        ESP_ERR_INVALID_SIZE,
        cleanup,
        "audio_asr_web",
        "json response too large");

    httpd_resp_set_type(req, "application/json");
    ret = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);

cleanup:
    free(json);
    free(snapshot);
    return ret;
}

static esp_err_t audio_asr_web_index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, AUDIO_ASR_WEB_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t audio_asr_web_latest_get_handler(httpd_req_t *req)
{
    return audio_asr_web_send_json(req);
}

static esp_err_t audio_asr_web_audio_get_handler(httpd_req_t *req)
{
    audio_asr_preview_snapshot_t snapshot;

    audio_asr_preview_state_get_snapshot(&snapshot);
    if (!snapshot.has_audio || snapshot.wav_data == NULL || snapshot.wav_bytes == 0U) {
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_send(req, "no audio yet", HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_set_type(req, "audio/wav");
    char content_length[16];
    int written = snprintf(content_length, sizeof(content_length), "%lu", (unsigned long)snapshot.wav_bytes);
    if (written > 0 && (size_t)written < sizeof(content_length)) {
        httpd_resp_set_hdr(req, "Content-Length", content_length);
    }
    return httpd_resp_send(req, (const char *)snapshot.wav_data, (ssize_t)snapshot.wav_bytes);
}

esp_err_t audio_asr_web_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = audio_asr_web_make_config();
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = audio_asr_web_index_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t latest_uri = {
        .uri = "/api/latest",
        .method = HTTP_GET,
        .handler = audio_asr_web_latest_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t audio_uri = {
        .uri = "/audio/latest.wav",
        .method = HTTP_GET,
        .handler = audio_asr_web_audio_get_handler,
        .user_ctx = NULL,
    };

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), "audio_asr_web", "start http server");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &index_uri), "audio_asr_web", "register index");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &latest_uri), "audio_asr_web", "register latest");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &audio_uri), "audio_asr_web", "register audio");
    return ESP_OK;
}

bool audio_asr_web_is_running(void)
{
    return s_server != NULL;
}

bool audio_asr_web_self_test(void)
{
    const httpd_config_t config = audio_asr_web_make_config();
    audio_asr_preview_snapshot_t snapshot = {
        .status = AUDIO_ASR_WEB_STATUS_DONE,
        .sequence = 1U,
        .duration_ms = 1000U,
        .pcm_bytes = 32000U,
        .wav_bytes = 32044U,
        .wifi_elapsed_ms = 100U,
        .request_elapsed_ms = 200U,
        .has_audio = true,
    };
    char json[512];

    snprintf(snapshot.transcript_text, sizeof(snapshot.transcript_text), "%s", "ok");
    snapshot.error_text[0] = '\0';

    return !audio_asr_web_is_running()
        && config.stack_size == AUDIO_ASR_WEB_SERVER_STACK_SIZE
        && strstr(AUDIO_ASR_WEB_HTML, "/api/latest") != NULL
        && strstr(AUDIO_ASR_WEB_HTML, "audio id=\"player\"") != NULL
        && audio_asr_web_build_latest_json(&snapshot, json, sizeof(json))
        && strstr(json, "\"audio_url\":\"/audio/latest.wav\"") != NULL;
}
