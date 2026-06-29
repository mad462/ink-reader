# Audio Record Web Upload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace UART binary export in `tests/audio_record_probe` with Wi-Fi HTTP upload to a local Python service, then make the latest uploaded clip immediately playable from a browser page.

**Architecture:** The existing standalone ESP-IDF recorder remains the source of truth for capture. After each recording, the device wraps the PCM bytes into a WAV file, connects to local Wi-Fi using untracked credentials, and uploads the WAV with HTTP POST to a Python service on the development PC. The Python service saves the file, exposes latest-upload metadata, and serves a minimal browser page with an audio player.

**Tech Stack:** ESP-IDF 5.5.4, ESP32-S3 Wi-Fi + `esp_http_client`, Python 3 standard library HTTP server, browser HTML + minimal JavaScript polling.

---

### Task 1: Add WAV Packing Helpers For The Probe

**Files:**
- Create: `tests/audio_record_probe/main/audio_record_wav.h`
- Create: `tests/audio_record_probe/main/audio_record_wav.c`
- Create: `tests/audio_record_probe/main/test_audio_record_wav.c`
- Modify: `tests/audio_record_probe/main/CMakeLists.txt`
- Modify: `tests/audio_record_probe/main/app_main.c`

- [ ] **Step 1: Write the failing WAV packing self-test**

Create `tests/audio_record_probe/main/test_audio_record_wav.c` with:

```c
#include "audio_record_wav.h"

#include <string.h>

bool audio_record_wav_self_test(void)
{
    uint8_t header[AUDIO_RECORD_WAV_HEADER_SIZE];
    memset(header, 0, sizeof(header));

    if (!audio_record_wav_write_header(header, sizeof(header), 16000U, 16U, 1U, 32000U)) {
        return false;
    }

    return memcmp(header, "RIFF", 4) == 0
        && memcmp(header + 8, "WAVE", 4) == 0
        && memcmp(header + 12, "fmt ", 4) == 0
        && memcmp(header + 36, "data", 4) == 0
        && audio_record_wav_total_size(32000U) == (AUDIO_RECORD_WAV_HEADER_SIZE + 32000U)
        && !audio_record_wav_write_header(header, 8U, 16000U, 16U, 1U, 32000U);
}
```

- [ ] **Step 2: Run build to verify the new test fails because helpers do not exist yet**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected: FAIL with missing `audio_record_wav.h` / undefined symbols.

- [ ] **Step 3: Add the WAV helper interface**

Create `tests/audio_record_probe/main/audio_record_wav.h` with:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RECORD_WAV_HEADER_SIZE 44U

bool audio_record_wav_write_header(
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t sample_rate,
    uint16_t bits_per_sample,
    uint16_t channels,
    uint32_t pcm_data_size);

size_t audio_record_wav_total_size(uint32_t pcm_data_size);

bool audio_record_wav_self_test(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Implement minimal WAV packing**

Create `tests/audio_record_probe/main/audio_record_wav.c` with:

```c
#include "audio_record_wav.h"

#include <string.h>

static void write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8) & 0xffU);
}

static void write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8) & 0xffU);
    dst[2] = (uint8_t)((value >> 16) & 0xffU);
    dst[3] = (uint8_t)((value >> 24) & 0xffU);
}

bool audio_record_wav_write_header(
    uint8_t *buffer,
    size_t buffer_size,
    uint32_t sample_rate,
    uint16_t bits_per_sample,
    uint16_t channels,
    uint32_t pcm_data_size)
{
    if (buffer == NULL || buffer_size < AUDIO_RECORD_WAV_HEADER_SIZE || channels == 0U || bits_per_sample == 0U) {
        return false;
    }

    const uint32_t block_align = (uint32_t)channels * (uint32_t)(bits_per_sample / 8U);
    const uint32_t byte_rate = sample_rate * block_align;

    memset(buffer, 0, AUDIO_RECORD_WAV_HEADER_SIZE);
    memcpy(buffer, "RIFF", 4);
    write_le32(buffer + 4, 36U + pcm_data_size);
    memcpy(buffer + 8, "WAVE", 4);
    memcpy(buffer + 12, "fmt ", 4);
    write_le32(buffer + 16, 16U);
    write_le16(buffer + 20, 1U);
    write_le16(buffer + 22, channels);
    write_le32(buffer + 24, sample_rate);
    write_le32(buffer + 28, byte_rate);
    write_le16(buffer + 32, (uint16_t)block_align);
    write_le16(buffer + 34, bits_per_sample);
    memcpy(buffer + 36, "data", 4);
    write_le32(buffer + 40, pcm_data_size);
    return true;
}

size_t audio_record_wav_total_size(uint32_t pcm_data_size)
{
    return AUDIO_RECORD_WAV_HEADER_SIZE + (size_t)pcm_data_size;
}
```

- [ ] **Step 5: Register the new source and self-test**

Modify `tests/audio_record_probe/main/CMakeLists.txt` so `SRCS` includes:

```cmake
        "audio_record_wav.c"
        "test_audio_record_wav.c"
```

Modify `tests/audio_record_probe/main/app_main.c` so startup self-tests include:

```c
    ESP_ERROR_CHECK(audio_record_wav_self_test() ? ESP_OK : ESP_FAIL);
```

- [ ] **Step 6: Run build to verify WAV helpers pass**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add tests/audio_record_probe/main/audio_record_wav.h tests/audio_record_probe/main/audio_record_wav.c tests/audio_record_probe/main/test_audio_record_wav.c tests/audio_record_probe/main/CMakeLists.txt tests/audio_record_probe/main/app_main.c
git commit -m "feat: add wav packing helpers for audio probe"
```

### Task 2: Add Wi-Fi Config Boundaries And Connection Helper

**Files:**
- Create: `tests/audio_record_probe/main/audio_record_wifi.h`
- Create: `tests/audio_record_probe/main/audio_record_wifi.c`
- Create: `tests/audio_record_probe/main/test_audio_record_wifi.c`
- Modify: `tests/audio_record_probe/main/CMakeLists.txt`
- Modify: `tests/audio_record_probe/main/app_main.c`
- Modify: `tests/audio_record_probe/CMakeLists.txt`
- Create: `tests/audio_record_probe/local_wifi_config.example.h`
- Create: `tests/audio_record_probe/main/local_wifi_config.h`
- Modify: `tests/audio_record_probe/README.md`

- [ ] **Step 1: Write a failing pure-logic self-test for Wi-Fi config selection**

Create `tests/audio_record_probe/main/test_audio_record_wifi.c` with:

```c
#include "audio_record_wifi.h"

#include <string.h>

bool audio_record_wifi_self_test(void)
{
    audio_record_wifi_config_t cfg = {
        .ssid = "test-ssid",
        .password = "secret",
        .server_base_url = "http://192.168.1.10:8080",
    };

    return audio_record_wifi_config_valid(&cfg)
        && !audio_record_wifi_config_valid(&(audio_record_wifi_config_t){0})
        && !audio_record_wifi_config_valid(&(audio_record_wifi_config_t){ .ssid = "x", .password = "", .server_base_url = "http://x" })
        && !audio_record_wifi_config_valid(&(audio_record_wifi_config_t){ .ssid = "x", .password = "y", .server_base_url = "" });
}
```

- [ ] **Step 2: Run build to verify the Wi-Fi test fails because helper files do not exist yet**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected: FAIL with missing `audio_record_wifi.h`.

- [ ] **Step 3: Add the Wi-Fi helper interface**

Create `tests/audio_record_probe/main/audio_record_wifi.h` with:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *ssid;
    const char *password;
    const char *server_base_url;
} audio_record_wifi_config_t;

bool audio_record_wifi_config_valid(const audio_record_wifi_config_t *config);
const audio_record_wifi_config_t *audio_record_wifi_get_config(void);
esp_err_t audio_record_wifi_connect(const audio_record_wifi_config_t *config, uint32_t timeout_ms);
bool audio_record_wifi_self_test(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Add a local untracked config include**

Create tracked template `tests/audio_record_probe/local_wifi_config.example.h` with:

```c
#pragma once

#define AUDIO_RECORD_WIFI_SSID "replace-me"
#define AUDIO_RECORD_WIFI_PASSWORD "replace-me"
#define AUDIO_RECORD_SERVER_BASE_URL "http://192.168.1.2:8080"
```

Create tracked placeholder `tests/audio_record_probe/main/local_wifi_config.h` with:

```c
#pragma once

#ifndef AUDIO_RECORD_WIFI_SSID
#define AUDIO_RECORD_WIFI_SSID ""
#endif

#ifndef AUDIO_RECORD_WIFI_PASSWORD
#define AUDIO_RECORD_WIFI_PASSWORD ""
#endif

#ifndef AUDIO_RECORD_SERVER_BASE_URL
#define AUDIO_RECORD_SERVER_BASE_URL ""
#endif
```

- [ ] **Step 5: Implement config validation and minimal connection skeleton**

Create `tests/audio_record_probe/main/audio_record_wifi.c` with:

```c
#include "audio_record_wifi.h"

#include <string.h>

#include "local_wifi_config.h"
#include "esp_log.h"

static const char *TAG = "audio_record_wifi";

static const audio_record_wifi_config_t s_config = {
    .ssid = AUDIO_RECORD_WIFI_SSID,
    .password = AUDIO_RECORD_WIFI_PASSWORD,
    .server_base_url = AUDIO_RECORD_SERVER_BASE_URL,
};

bool audio_record_wifi_config_valid(const audio_record_wifi_config_t *config)
{
    return config != NULL
        && config->ssid != NULL
        && config->password != NULL
        && config->server_base_url != NULL
        && config->ssid[0] != '\0'
        && config->password[0] != '\0'
        && config->server_base_url[0] != '\0';
}

const audio_record_wifi_config_t *audio_record_wifi_get_config(void)
{
    return &s_config;
}

esp_err_t audio_record_wifi_connect(const audio_record_wifi_config_t *config, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!audio_record_wifi_config_valid(config)) {
        ESP_LOGW(TAG, "wifi config missing; upload disabled");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_ERR_NOT_SUPPORTED;
}
```

- [ ] **Step 6: Register the helper and hook startup self-test**

Modify `tests/audio_record_probe/main/CMakeLists.txt` so `SRCS` includes:

```cmake
        "audio_record_wifi.c"
        "test_audio_record_wifi.c"
```

Modify `tests/audio_record_probe/main/app_main.c` startup self-tests:

```c
    ESP_ERROR_CHECK(audio_record_wifi_self_test() ? ESP_OK : ESP_FAIL);
```

- [ ] **Step 7: Make the component include the local config directory**

Modify `tests/audio_record_probe/CMakeLists.txt` to add:

```cmake
set(EXTRA_COMPONENT_DIRS "")
```

and ensure the main component include path can resolve `main/local_wifi_config.h` without introducing tracked secrets.

- [ ] **Step 8: Document the local override workflow**

Update `tests/audio_record_probe/README.md` with a setup note telling the developer to copy `local_wifi_config.example.h` values into a local override path or build-time defines without committing credentials.

- [ ] **Step 9: Run build to verify the Wi-Fi helper compiles**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected: PASS, but connection still returns `ESP_ERR_NOT_SUPPORTED`.

- [ ] **Step 10: Commit**

```bash
git add tests/audio_record_probe/main/audio_record_wifi.h tests/audio_record_probe/main/audio_record_wifi.c tests/audio_record_probe/main/test_audio_record_wifi.c tests/audio_record_probe/main/local_wifi_config.h tests/audio_record_probe/local_wifi_config.example.h tests/audio_record_probe/main/CMakeLists.txt tests/audio_record_probe/CMakeLists.txt tests/audio_record_probe/main/app_main.c tests/audio_record_probe/README.md
git commit -m "feat: add wifi config scaffolding for audio probe"
```

### Task 3: Implement Real Wi-Fi Connection On The Probe

**Files:**
- Modify: `tests/audio_record_probe/main/audio_record_wifi.c`
- Modify: `tests/audio_record_probe/main/app_main.c`
- Modify: `tests/audio_record_probe/main/CMakeLists.txt`

- [ ] **Step 1: Add the Wi-Fi / event dependencies**

Modify `tests/audio_record_probe/main/CMakeLists.txt` so `REQUIRES` includes:

```cmake
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
```

- [ ] **Step 2: Implement minimal Wi-Fi station connect flow**

Extend `tests/audio_record_probe/main/audio_record_wifi.c` to:

```c
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
```

Add event bits and handlers so the helper:

- initializes NVS
- initializes default netif and event loop once
- starts station mode
- waits for connected + IP event
- logs SSID and assigned IP

Use a single static init guard so repeated uploads do not reinitialize Wi-Fi every time.

- [ ] **Step 3: Replace the `ESP_ERR_NOT_SUPPORTED` stub with real connection logic**

In `audio_record_wifi_connect(...)`, return:

- `ESP_OK` when already connected
- `ESP_OK` after successful connect
- `ESP_ERR_TIMEOUT` on timeout
- specific ESP-IDF error on init failure

- [ ] **Step 4: Log Wi-Fi readiness during app startup**

Modify `tests/audio_record_probe/main/app_main.c` to log whether Wi-Fi config is present:

```c
    const audio_record_wifi_config_t *wifi_config = audio_record_wifi_get_config();
    ESP_LOGI(TAG, "wifi upload config present=%s server=%s",
        audio_record_wifi_config_valid(wifi_config) ? "true" : "false",
        wifi_config->server_base_url != NULL ? wifi_config->server_base_url : "");
```

- [ ] **Step 5: Run build to verify the probe still compiles**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add tests/audio_record_probe/main/audio_record_wifi.c tests/audio_record_probe/main/app_main.c tests/audio_record_probe/main/CMakeLists.txt
git commit -m "feat: connect audio probe to local wifi"
```

### Task 4: Implement HTTP WAV Upload On The Probe

**Files:**
- Create: `tests/audio_record_probe/main/audio_record_http_upload.h`
- Create: `tests/audio_record_probe/main/audio_record_http_upload.c`
- Create: `tests/audio_record_probe/main/test_audio_record_http_upload.c`
- Modify: `tests/audio_record_probe/main/CMakeLists.txt`
- Modify: `tests/audio_record_probe/main/app_main.c`

- [ ] **Step 1: Write a failing pure helper self-test for upload URL creation**

Create `tests/audio_record_probe/main/test_audio_record_http_upload.c` with:

```c
#include "audio_record_http_upload.h"

#include <string.h>

bool audio_record_http_upload_self_test(void)
{
    char url[128];
    if (!audio_record_http_upload_build_url("http://127.0.0.1:8080", url, sizeof(url))) {
        return false;
    }
    return strcmp(url, "http://127.0.0.1:8080/api/upload") == 0
        && !audio_record_http_upload_build_url("", url, sizeof(url))
        && !audio_record_http_upload_build_url("http://x", url, 8U);
}
```

- [ ] **Step 2: Run build to verify the new upload test fails**

Run the same `idf.py build` command.  
Expected: FAIL with missing `audio_record_http_upload.h`.

- [ ] **Step 3: Add the upload helper interface**

Create `tests/audio_record_probe/main/audio_record_http_upload.h` with:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool audio_record_http_upload_build_url(const char *server_base_url, char *buffer, size_t buffer_size);

esp_err_t audio_record_http_upload_wav(
    const char *server_base_url,
    const uint8_t *wav_data,
    size_t wav_size,
    uint32_t sequence,
    uint32_t duration_ms,
    int *http_status_out);

bool audio_record_http_upload_self_test(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Implement URL building and minimal HTTP upload**

Create `tests/audio_record_probe/main/audio_record_http_upload.c` with `esp_http_client` and:

- `audio_record_http_upload_build_url(...)`
- `audio_record_http_upload_wav(...)` that:
  - builds `/api/upload`
  - sets method `POST`
  - sets `Content-Type: audio/wav`
  - sets optional headers `X-Audio-Sequence` and `X-Audio-Duration-Ms`
  - posts the complete buffer
  - returns HTTP status through `http_status_out`

- [ ] **Step 5: Register the new helper**

Modify `tests/audio_record_probe/main/CMakeLists.txt` so `SRCS` includes:

```cmake
        "audio_record_http_upload.c"
        "test_audio_record_http_upload.c"
```

and `REQUIRES` includes:

```cmake
        esp_http_client
```

Modify `tests/audio_record_probe/main/app_main.c` startup self-tests:

```c
    ESP_ERROR_CHECK(audio_record_http_upload_self_test() ? ESP_OK : ESP_FAIL);
```

- [ ] **Step 6: Replace UART binary export in `app_main.c` with upload preparation**

In the export path:

- remove the call that writes binary PCM / header to UART
- allocate a temporary WAV buffer sized with `audio_record_wav_total_size(...)`
- write the WAV header
- copy PCM after the header
- call `audio_record_wifi_connect(...)`
- call `audio_record_http_upload_wav(...)`
- log upload success or failure

Keep text logs only on UART.

- [ ] **Step 7: Run build to verify the upload path compiles**

Run the standard `idf.py build` command.  
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add tests/audio_record_probe/main/audio_record_http_upload.h tests/audio_record_probe/main/audio_record_http_upload.c tests/audio_record_probe/main/test_audio_record_http_upload.c tests/audio_record_probe/main/CMakeLists.txt tests/audio_record_probe/main/app_main.c
git commit -m "feat: upload recorded wav over http"
```

### Task 5: Build The Local Python Upload Server And Browser Page

**Files:**
- Create: `tools/audio_record_web_server.py`
- Create: `tools/test_audio_record_web_server.py`

- [ ] **Step 1: Write failing Python tests for upload, latest metadata, and captures**

Create `tools/test_audio_record_web_server.py` with tests that:

- upload a tiny valid WAV body to `POST /api/upload`
- verify a file is created in a temp captures directory
- verify `GET /api/latest` returns the latest filename and duration
- verify `GET /captures/<filename>` serves the saved bytes

Use `tempfile.TemporaryDirectory()` and the standard library `http.client`.

- [ ] **Step 2: Run the failing tests**

Run:

```powershell
python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -v
```

Expected: FAIL because `audio_record_web_server.py` does not exist.

- [ ] **Step 3: Implement the Python server**

Create `tools/audio_record_web_server.py` using only the standard library:

- `ThreadingHTTPServer`
- `BaseHTTPRequestHandler`
- `json`
- `wave`
- `pathlib`

Implement:

- `POST /api/upload`
- `GET /api/latest`
- `GET /captures/<filename>`
- `GET /`

The HTML page can be an inline string with:

```html
<audio id="player" controls></audio>
```

and polling JavaScript that refreshes `/api/latest` every second.

- [ ] **Step 4: Run the Python tests to verify they pass**

Run:

```powershell
python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -v
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/audio_record_web_server.py tools/test_audio_record_web_server.py
git commit -m "feat: add local web server for uploaded recordings"
```

### Task 6: Update Probe README For Web Upload Workflow

**Files:**
- Modify: `tests/audio_record_probe/README.md`

- [ ] **Step 1: Replace UART export instructions with web upload workflow**

Update the README sections so they explain:

- local Wi-Fi config setup without committing secrets
- how to start `tools/audio_record_web_server.py`
- how to flash the probe
- how to open the browser page
- normal operation:
  - start server
  - power board
  - hold `Confirm`
  - release to upload
  - browser page shows latest WAV

- [ ] **Step 2: Keep the known limitations explicit**

Ensure the README still states:

- max `10 seconds`
- fixed `16 kHz / 16-bit / mono`
- no main firmware integration
- no live streaming

- [ ] **Step 3: Commit**

```bash
git add tests/audio_record_probe/README.md
git commit -m "docs: describe web upload audio probe workflow"
```

### Task 7: End-To-End Verification

**Files:**
- Modify if needed: `tests/audio_record_probe/main/app_main.c`
- Modify if needed: `tools/audio_record_web_server.py`

- [ ] **Step 1: Run the Python test suites**

Run:

```powershell
python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_receiver.py D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -v
```

Expected: PASS.

- [ ] **Step 2: Build the ESP-IDF probe**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected: PASS.

- [ ] **Step 3: Start the local web server**

Run:

```powershell
python D:\FUCKIDF\ink-reader\tools\audio_record_web_server.py --host 0.0.0.0 --port 8080
```

Expected: console prints listening address and captures directory.

- [ ] **Step 4: Flash and monitor the probe**

Run:

```powershell
idf.py -p COM9 flash monitor
```

Expected serial log:

- Wi-Fi config present
- connected to the configured local SSID
- got IP
- recording start / stop
- upload success with HTTP `200`

- [ ] **Step 5: Verify browser playback**

Open:

```text
http://127.0.0.1:8080/
```

Expected:

- latest filename visible
- duration visible
- audio player points to `/captures/<filename>`
- clip plays successfully

- [ ] **Step 6: Commit final adjustments**

```bash
git add tests/audio_record_probe tools docs/superpowers/specs/2026-06-28-audio-record-web-upload-design.md docs/superpowers/plans/2026-06-28-audio-record-web-upload.md
git commit -m "feat: switch audio probe export to web upload playback"
```
