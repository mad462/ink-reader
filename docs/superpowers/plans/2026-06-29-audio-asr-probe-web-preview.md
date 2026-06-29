# Audio ASR Probe Web Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `tests/audio_asr_probe` with a board-hosted web page that shows current status, latest ASR text, and plays only the latest recording.

**Architecture:** Keep the existing button-driven recording and on-device ASR flow intact, and add a small in-memory “latest preview” store plus a read-only ESP-IDF HTTP server. The UI remains a minimal polling page served by the board itself, accessible on the same LAN when Wi-Fi is active.

**Tech Stack:** ESP-IDF 5.5.4, `esp_http_server`, existing `esp_http_client`, PSRAM-backed latest-WAV retention, minimal HTML + JSON polling.

---

### Task 1: Add a latest-preview state module

**Files:**
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_preview_state.h`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_preview_state.c`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\test_audio_asr_preview_state.c`
- Modify: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\CMakeLists.txt`

- [ ] **Step 1: Write the failing test scaffold**

Create `test_audio_asr_preview_state.c` with self-test entry points for:
- default state is `idle`
- no audio exists at boot
- setting transcript/error/status updates the snapshot
- replacing latest WAV swaps metadata correctly

Use a shape like:

```c
#include "audio_asr_preview_state.h"

bool test_audio_asr_preview_state_self_test(void)
{
    return audio_asr_preview_state_self_test();
}
```

- [ ] **Step 2: Run build to verify missing-module failure**

Run:

```powershell
cd D:\FUCKIDF\ink-reader\tests\audio_asr_probe
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected:
- build fails because the new preview-state module is not implemented yet

- [ ] **Step 3: Implement the minimal preview-state interface**

Create `audio_asr_preview_state.h` with:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_ASR_WEB_STATUS_IDLE = 0,
    AUDIO_ASR_WEB_STATUS_RECORDING,
    AUDIO_ASR_WEB_STATUS_WIFI_CONNECTING,
    AUDIO_ASR_WEB_STATUS_ASR_REQUESTING,
    AUDIO_ASR_WEB_STATUS_DONE,
    AUDIO_ASR_WEB_STATUS_ERROR,
} audio_asr_web_status_t;

typedef struct {
    audio_asr_web_status_t status;
    uint32_t sequence;
    uint32_t duration_ms;
    uint32_t pcm_bytes;
    uint32_t wav_bytes;
    uint32_t wifi_elapsed_ms;
    uint32_t request_elapsed_ms;
    bool has_audio;
    const uint8_t *wav_data;
    char transcript_text[2048];
    char error_text[256];
} audio_asr_preview_snapshot_t;

void audio_asr_preview_state_init(void);
void audio_asr_preview_state_set_status(audio_asr_web_status_t status);
void audio_asr_preview_state_set_recording(uint32_t sequence);
void audio_asr_preview_state_set_result_text(const char *text);
void audio_asr_preview_state_set_error_text(const char *text);
void audio_asr_preview_state_set_metrics(
    uint32_t sequence,
    uint32_t duration_ms,
    uint32_t pcm_bytes,
    uint32_t wav_bytes,
    uint32_t wifi_elapsed_ms,
    uint32_t request_elapsed_ms);
esp_err_t audio_asr_preview_state_store_wav_copy(
    const uint8_t *wav_data,
    size_t wav_size,
    bool prefer_psram);
void audio_asr_preview_state_clear_audio(void);
void audio_asr_preview_state_get_snapshot(audio_asr_preview_snapshot_t *snapshot);
const char *audio_asr_preview_state_status_name(audio_asr_web_status_t status);
bool audio_asr_preview_state_self_test(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Implement the backing store in `audio_asr_preview_state.c`**

Implement:
- one static shared state
- copy-in strings with `snprintf`
- WAV retention by allocating one owned copy and freeing the previous one
- PSRAM preferred, internal fallback
- snapshot copy-out that is safe for the HTTP layer

Important behavior:
- `store_wav_copy()` should keep working even if the previous WAV exists
- if allocation fails, return `ESP_ERR_NO_MEM` without crashing ASR

- [ ] **Step 5: Add the self-test**

In `audio_asr_preview_state.c`, add `audio_asr_preview_state_self_test()` to verify:
- init gives `idle`
- status-name mapping is stable
- transcript/error setters work
- storing a tiny fake WAV marks `has_audio=true`
- `clear_audio()` resets `has_audio=false`

- [ ] **Step 6: Register the new files**

Modify `main/CMakeLists.txt` to add:

```cmake
        "audio_asr_preview_state.c"
        "test_audio_asr_preview_state.c"
```

- [ ] **Step 7: Re-run build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- preview-state self-test is now compiled into the app

### Task 2: Add the board-hosted HTTP preview server

**Files:**
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_web.h`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_web.c`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\test_audio_asr_web.c`
- Modify: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\CMakeLists.txt`

- [ ] **Step 1: Write the failing test shim**

Create `test_audio_asr_web.c`:

```c
#include "audio_asr_web.h"

bool test_audio_asr_web_self_test(void)
{
    return audio_asr_web_self_test();
}
```

- [ ] **Step 2: Run build to verify failure**

Run:

```powershell
idf.py build
```

Expected:
- build fails because the new web module does not exist yet

- [ ] **Step 3: Define the web module interface**

Create `audio_asr_web.h`:

```c
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_asr_web_start(void);
bool audio_asr_web_is_running(void);
bool audio_asr_web_self_test(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Implement the HTTP server in `audio_asr_web.c`**

Use `esp_http_server` with three handlers:
- `GET /`
- `GET /api/latest`
- `GET /audio/latest.wav`

Implementation notes:
- serve a static HTML string from memory
- JSON should be built with `snprintf`
- `/api/latest` should read from `audio_asr_preview_state_get_snapshot()`
- `/audio/latest.wav` should return `404` if `has_audio=false`
- do not introduce control endpoints

Minimal HTML requirements:
- status text
- transcript text
- error text
- duration / bytes
- `<audio id="player" controls>`
- JS `setInterval()` polling `/api/latest`

- [ ] **Step 5: Add a tiny self-test**

Implement `audio_asr_web_self_test()` to validate:
- server-not-started state reports false
- HTML string contains `/api/latest`
- HTML string contains `audio/latest.wav`

Keep this as a pure logic self-test, not a live socket test.

- [ ] **Step 6: Register the new web files**

Modify `main/CMakeLists.txt`:

```cmake
        "audio_asr_web.c"
        "test_audio_asr_web.c"
```

Add dependency:

```cmake
        esp_http_server
```

- [ ] **Step 7: Re-run build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds with the new web server module

### Task 3: Wire preview-state transitions into the existing ASR flow

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\app_main.c`

- [ ] **Step 1: Add the new includes**

At the top of `app_main.c`, add:

```c
#include "audio_asr_preview_state.h"
#include "audio_asr_web.h"
```

- [ ] **Step 2: Set boot-time default state**

After self-tests pass and before entering the main loop:

```c
audio_asr_preview_state_init();
audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_IDLE);
```

- [ ] **Step 3: Mark recording start**

Inside `audio_asr_capture_loop()` right after `audio_asr_emit_event("pressed");`, add:

```c
audio_asr_preview_state_set_recording(ctx->session.sequence);
audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_RECORDING);
```

- [ ] **Step 4: Publish capture metrics and retained WAV**

Inside `audio_asr_process_capture()` after WAV buffer creation succeeds:

```c
esp_err_t preview_ret = audio_asr_preview_state_store_wav_copy(
    wav_buffer,
    wav_size,
    true);
if (preview_ret != ESP_OK) {
    ESP_LOGW(TAG, "preview wav retention failed: %s", esp_err_to_name(preview_ret));
}
audio_asr_preview_state_set_metrics(
    ctx->session.sequence,
    duration_ms,
    (uint32_t)ctx->session.captured_bytes,
    (uint32_t)wav_size,
    0U,
    0U);
```

- [ ] **Step 5: Mark Wi-Fi and request phases**

Before calling `audio_asr_wifi_connect()`:

```c
audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_WIFI_CONNECTING);
```

After Wi-Fi connect succeeds:

```c
audio_asr_preview_state_set_metrics(
    ctx->session.sequence,
    duration_ms,
    (uint32_t)ctx->session.captured_bytes,
    (uint32_t)wav_size,
    (uint32_t)(audio_asr_now_ms() - wifi_started_ms),
    0U);
audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_ASR_REQUESTING);
```

- [ ] **Step 6: Publish success result**

After transcript parse succeeds:

```c
audio_asr_preview_state_set_result_text(transcript);
audio_asr_preview_state_set_error_text(NULL);
audio_asr_preview_state_set_metrics(
    ctx->session.sequence,
    duration_ms,
    (uint32_t)ctx->session.captured_bytes,
    (uint32_t)wav_size,
    (uint32_t)(audio_asr_now_ms() - wifi_started_ms),
    (uint32_t)(audio_asr_now_ms() - request_started_ms));
audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_DONE);
```

- [ ] **Step 7: Publish failure states**

For each failure path in `audio_asr_process_capture()`, set:

```c
audio_asr_preview_state_set_error_text(message);
audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_ERROR);
```

For empty audio:

```c
audio_asr_preview_state_set_error_text("empty_audio");
audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_ERROR);
```

- [ ] **Step 8: Return to idle after request completes**

In `audio_asr_run()`, after `audio_asr_probe_mark_completed(...)`, if the button is not still held:

```c
if (!audio_asr_confirm_pressed()) {
    audio_asr_preview_state_set_status(AUDIO_ASR_WEB_STATUS_IDLE);
}
```

Keep the latest transcript and latest WAV intact while only changing the state to `idle`.

- [ ] **Step 9: Re-run build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- no compile regressions from the new preview-state wiring

### Task 4: Start the web server and expose board IP

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\app_main.c`
- Modify: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_wifi.c`
- Modify: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_wifi.h`

- [ ] **Step 1: Extend Wi-Fi module to expose IP**

In `audio_asr_wifi.h`, add:

```c
bool audio_asr_wifi_get_ipv4_string(char *buffer, size_t buffer_size);
```

- [ ] **Step 2: Implement the IP helper**

In `audio_asr_wifi.c`, keep the latest IP from `IP_EVENT_STA_GOT_IP` and implement:

```c
bool audio_asr_wifi_get_ipv4_string(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U || !s_wifi_connected) {
        return false;
    }
    snprintf(buffer, buffer_size, IPSTR, IP2STR(&s_last_ip));
    return true;
}
```

- [ ] **Step 3: Start the HTTP server once at boot**

In `app_main()` after `audio_asr_preview_state_init()`:

```c
ESP_ERROR_CHECK(audio_asr_web_start());
ESP_LOGI(TAG, "web preview server started");
```

- [ ] **Step 4: Log access URL when Wi-Fi is connected**

Inside `audio_asr_process_capture()` after Wi-Fi connect succeeds:

```c
char ip[32];
if (audio_asr_wifi_get_ipv4_string(ip, sizeof(ip))) {
    ESP_LOGI(TAG, "web preview url: http://%s/", ip);
}
```

- [ ] **Step 5: Re-run build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- server starts without compile or link issues

### Task 5: Update README for the web preview workflow

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\README.md`

- [ ] **Step 1: Document the preview page**

Add a section explaining:
- the board hosts a page on its LAN IP
- the page only shows the most recent recording/result
- the page may be temporarily unreachable while Wi-Fi is quiet

- [ ] **Step 2: Document the tester workflow**

Add a short flow:
- flash the probe
- record once
- wait for Wi-Fi reconnect / ASR
- open `http://<board-ip>/`
- inspect status and play audio

- [ ] **Step 3: Document known limitations**

Add:
- only the latest recording is retained
- preview depends on available RAM, PSRAM preferred
- the page is read-only
- page availability follows current probe Wi-Fi behavior

### Task 6: Verify the end-to-end preview flow

**Files:**
- No new files required

- [ ] **Step 1: Fresh build verification**

Run:

```powershell
idf.py build
```

Expected:
- successful build

- [ ] **Step 2: Flash and record one sample**

Run:

```powershell
idf.py -p COM9 flash monitor
```

Expected:
- record and ASR still work
- serial shows `web preview url: http://<ip>/`

- [ ] **Step 3: Open the page on the same LAN**

Manual:
- visit `http://<board-ip>/`

Expected:
- page loads
- status appears
- latest transcript appears after recognition

- [ ] **Step 4: Verify playback**

Manual:
- press play in the browser

Expected:
- latest audio plays

- [ ] **Step 5: Verify overwrite behavior**

Manual:
- record a second sample
- refresh the page if needed

Expected:
- the page and WAV endpoint now serve only the second sample

- [ ] **Step 6: Verify degraded preview behavior**

Manual:
- if preview retention fails, confirm ASR still works

Expected:
- page still shows status/result
- `has_audio=false`
- no crash
