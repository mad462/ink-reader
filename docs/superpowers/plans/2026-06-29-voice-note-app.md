# Voice Note App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a persistent `voice_note` mainline app that records audio on `Confirm` hold, saves WAV plus note metadata to TF card, sends the saved WAV to ASR through the shared Wi-Fi coordinator, and lets the user browse, retry, mark done, and delete notes across reboots.

**Architecture:** Add a regular launcher app descriptor plus a focused `main/voice_note/` service layer. The app owns tabs, cards, popup state, and status copy; the service owns recording, WAV packaging, TF-card persistence, ASR calls, single-job state management, and shared-status snapshots that the app polls during `tick()`. Store note metadata as per-note JSON plus an `index.json` under `/sdcard/.ink-reader/voice_notes`, and keep Wi-Fi requests inside the existing coordinator with a dedicated `VOICE_NOTE_ASR` lease identity.

**Tech Stack:** ESP-IDF 5.5.4, FreeRTOS task/queue primitives, ESP-IDF `json` component (`cJSON.h`), existing PDM/I2S capture flow from `tests/audio_asr_probe`, existing mainline app descriptor/render model pattern, FATFS-backed TF card storage at `/sdcard`.

---

### Task 1: Scaffold the voice note app, service folder, and launcher entry

**Files:**
- Create: `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.h`
- Create: `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.c`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_types.h`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.h`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`
- Modify: `D:\FUCKIDF\ink-reader\main\CMakeLists.txt`
- Modify: `D:\FUCKIDF\ink-reader\main\apps\ink_app_iface.h`
- Modify: `D:\FUCKIDF\ink-reader\main\apps\ink_launcher_app.c`
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`

- [ ] **Step 1: Add the new source files to the main component build list before they exist**

Modify `D:\FUCKIDF\ink-reader\main\CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "app_main.c"
        "apps/ink_launcher_app.c"
        "apps/ink_gray_cal_app.c"
        "apps/ink_photo_album_app.c"
        "apps/ink_reader_app.c"
        "apps/ink_usb_msc_app.c"
        "apps/ink_wifi_setup_app.c"
        "apps/ink_voice_note_app.c"
        "voice_note/voice_note_service.c"
        "ink_app_boot.c"
        "ink_app_display_request.c"
        "ink_app_fast_browse.c"
        "ink_photo_bmp_parser.c"
        "ink_photo_catalog.c"
        "ink_time_service.c"
        "ink_app_render.c"
        "ink_app_startup.c"
        "ink_system_services.c"
        "ink_system_runtime.c"
        "ink_usb_msc_service.c"
        "ink_tuning_lab.c"
        "ink_app_ui.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_driver_sdmmc
        esp_timer
        fatfs
        ink_app_core
        ink_book_xtc
        ink_hw
        ink_net
        ink_wifi_setup
        json
        lwip
        esp_tinyusb
        vfs
)
```

- [ ] **Step 2: Run the build to verify the missing-file failure**

Run:

```powershell
cd D:\FUCKIDF\ink-reader
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected:
- build fails because `apps/ink_voice_note_app.c` and `voice_note/voice_note_service.c` do not exist yet

- [ ] **Step 3: Add the shared voice note type declarations**

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_types.h`:

```c
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
} voice_note_service_snapshot_t;
```

- [ ] **Step 4: Add a minimal service API**

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.h`:

```c
#pragma once

#include <stdbool.h>
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
    voice_note_note_t *notes,
    size_t capacity,
    size_t *count_out);
bool voice_note_service_load_note(const char *note_id, voice_note_note_t *out_note);
bool voice_note_service_tick(uint32_t now_ms);
bool voice_note_service_self_test(void);
```

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`:

```c
#include "voice_note/voice_note_service.h"

#include <string.h>

static voice_note_service_snapshot_t s_snapshot;

esp_err_t voice_note_service_init(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = VOICE_NOTE_JOB_IDLE;
    snprintf(s_snapshot.status_text, sizeof(s_snapshot.status_text), "%s", "按住 Confirm 开始录音");
    return ESP_OK;
}

bool voice_note_service_start_capture(uint32_t now_ms)
{
    if (s_snapshot.busy) {
        return false;
    }
    s_snapshot.busy = true;
    s_snapshot.state = VOICE_NOTE_JOB_RECORDING;
    s_snapshot.started_ms = now_ms;
    snprintf(s_snapshot.status_text, sizeof(s_snapshot.status_text), "%s", "正在录音");
    return true;
}

bool voice_note_service_stop_capture(uint32_t now_ms)
{
    if (!s_snapshot.busy || s_snapshot.state != VOICE_NOTE_JOB_RECORDING) {
        return false;
    }
    s_snapshot.capture_duration_ms = now_ms - s_snapshot.started_ms;
    s_snapshot.state = VOICE_NOTE_JOB_COMPLETED;
    s_snapshot.busy = false;
    snprintf(s_snapshot.status_text, sizeof(s_snapshot.status_text), "%s", "识别完成");
    return true;
}

bool voice_note_service_retry_note(const char *note_id, uint32_t now_ms)
{
    (void)note_id;
    (void)now_ms;
    return false;
}

bool voice_note_service_delete_note(const char *note_id)
{
    (void)note_id;
    return false;
}

bool voice_note_service_set_note_status(const char *note_id, voice_note_status_t status)
{
    (void)note_id;
    (void)status;
    return false;
}

bool voice_note_service_get_snapshot(voice_note_service_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return false;
    }
    *out_snapshot = s_snapshot;
    return true;
}

bool voice_note_service_copy_note_summaries(
    voice_note_tab_t tab,
    voice_note_note_t *notes,
    size_t capacity,
    size_t *count_out)
{
    (void)tab;
    (void)notes;
    (void)capacity;
    if (count_out != NULL) {
        *count_out = 0U;
    }
    return true;
}

bool voice_note_service_load_note(const char *note_id, voice_note_note_t *out_note)
{
    (void)note_id;
    (void)out_note;
    return false;
}

bool voice_note_service_tick(uint32_t now_ms)
{
    (void)now_ms;
    return false;
}

bool voice_note_service_self_test(void)
{
    voice_note_service_snapshot_t snapshot;

    if (voice_note_service_init() != ESP_OK) {
        return false;
    }
    if (!voice_note_service_get_snapshot(&snapshot)) {
        return false;
    }
    return snapshot.state == VOICE_NOTE_JOB_IDLE
        && strcmp(snapshot.status_text, "按住 Confirm 开始录音") == 0;
}
```

- [ ] **Step 5: Add the minimal app descriptor and launcher wiring**

Create `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.h`:

```c
#pragma once

#include "ink_app_iface.h"
#include "ink_cpfont.h"
#include "voice_note/voice_note_types.h"

typedef struct {
    voice_note_tab_t active_tab;
    size_t selected_index;
    bool popup_open;
    uint8_t popup_action_index;
    bool full_text_open;
    voice_note_service_snapshot_t snapshot;
} ink_voice_note_app_state_t;

typedef struct {
    const ink_voice_note_app_state_t *state;
    const ink_cpfont_t *menu_font;
    const ink_cpfont_t *footer_font;
    const ink_cpfont_t *reader_font;
    char header_meta[24];
} ink_voice_note_app_render_state_t;

const ink_app_descriptor_t *ink_voice_note_app_descriptor(void);
bool ink_voice_note_app_self_test(void);
```

Create `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.c`:

```c
#include "apps/ink_voice_note_app.h"

#include <string.h>

#include "apps/ink_launcher_app.h"
#include "ink_system_runtime.h"
#include "ink_system_services.h"
#include "voice_note/voice_note_service.h"

static ink_voice_note_app_state_t s_voice_note_state;
static ink_voice_note_app_render_state_t s_voice_note_render_state;

static void voice_note_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool voice_note_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event);
static bool voice_note_tick(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    uint32_t now_ms);
static bool voice_note_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model);

static const ink_app_descriptor_t kVoiceNoteApp = {
    .id = "voice_note",
    .name = "Voice Note",
    .enter = voice_note_enter,
    .input = voice_note_input,
    .tick = voice_note_tick,
    .render = voice_note_render,
    .state = &s_voice_note_state,
};

const ink_app_descriptor_t *ink_voice_note_app_descriptor(void)
{
    return &kVoiceNoteApp;
}

static void voice_note_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_voice_note_app_state_t *state = (ink_voice_note_app_state_t *)app->state;
    ink_system_services_t *services = runtime != NULL ? runtime->services : NULL;

    memset(state, 0, sizeof(*state));
    state->active_tab = VOICE_NOTE_TAB_PENDING;
    (void)voice_note_service_init();
    (void)voice_note_service_get_snapshot(&state->snapshot);
    s_voice_note_render_state.state = state;
    s_voice_note_render_state.menu_font = services != NULL ? &services->menu_font : NULL;
    s_voice_note_render_state.footer_font = services != NULL ? &services->footer_font : NULL;
    s_voice_note_render_state.reader_font = services != NULL ? &services->reader_font : NULL;
    ink_system_services_get_time_badge(
        services,
        s_voice_note_render_state.header_meta,
        sizeof(s_voice_note_render_state.header_meta));
}

static bool voice_note_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    (void)runtime;
    (void)app;
    (void)event;
    return false;
}

static bool voice_note_tick(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    uint32_t now_ms)
{
    ink_voice_note_app_state_t *state = (ink_voice_note_app_state_t *)app->state;

    (void)runtime;
    if (voice_note_service_tick(now_ms)) {
        (void)voice_note_service_get_snapshot(&state->snapshot);
        return true;
    }
    return false;
}

static bool voice_note_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model)
{
    ink_system_services_t *services = runtime != NULL ? runtime->services : NULL;

    memset(out_model, 0, sizeof(*out_model));
    out_model->mode = INK_APP_RENDER_MODE_VOICE_NOTE;
    out_model->refresh_strategy = runtime->force_full_refresh_on_next_render
        ? INK_REFRESH_STRATEGY_PAGE_TRANSITION_FULL
        : INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST;
    out_model->request_full_refresh = runtime->force_full_refresh_on_next_render;
    s_voice_note_render_state.state = (const ink_voice_note_app_state_t *)app->state;
    s_voice_note_render_state.menu_font = services != NULL ? &services->menu_font : NULL;
    s_voice_note_render_state.footer_font = services != NULL ? &services->footer_font : NULL;
    s_voice_note_render_state.reader_font = services != NULL ? &services->reader_font : NULL;
    ink_system_services_get_time_badge(
        services,
        s_voice_note_render_state.header_meta,
        sizeof(s_voice_note_render_state.header_meta));
    out_model->state = &s_voice_note_render_state;
    runtime->force_full_refresh_on_next_render = false;
    return true;
}

bool ink_voice_note_app_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_app_render_model_t model;
    const ink_app_descriptor_t *app = ink_voice_note_app_descriptor();

    ink_system_runtime_init(&runtime);
    runtime.force_full_refresh_on_next_render = true;
    if (!app->render(&runtime, app, &model)) {
        return false;
    }
    return model.mode == INK_APP_RENDER_MODE_VOICE_NOTE
        && model.request_full_refresh;
}
```

Modify `D:\FUCKIDF\ink-reader\main\apps\ink_app_iface.h`:

```c
typedef enum {
    INK_APP_RENDER_MODE_NONE = 0,
    INK_APP_RENDER_MODE_LAUNCHER,
    INK_APP_RENDER_MODE_READER_PLACEHOLDER,
    INK_APP_RENDER_MODE_READER_SUBSYSTEM,
    INK_APP_RENDER_MODE_WIFI_SETUP,
    INK_APP_RENDER_MODE_PHOTO_ALBUM,
    INK_APP_RENDER_MODE_USB_MSC,
    INK_APP_RENDER_MODE_VOICE_NOTE,
} ink_app_render_mode_t;
```

Modify the top of `D:\FUCKIDF\ink-reader\main\apps\ink_launcher_app.c`:

```c
#include "apps/ink_voice_note_app.h"

enum {
    INK_LAUNCHER_APP_COUNT = 6,
};

static const char *const kLauncherTargetIds[INK_LAUNCHER_APP_COUNT] = {
    "reader",
    "voice_note",
    "wifi_setup",
    "photo_album",
    "gray_cal",
    "usb_msc",
};
```

Modify the registration block in `D:\FUCKIDF\ink-reader\main\app_main.c`:

```c
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_reader_app_descriptor()) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_voice_note_app_descriptor()) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_wifi_setup_app_descriptor()) ? ESP_OK : ESP_FAIL);
```

and add boot self-test:

```c
    ESP_ERROR_CHECK(ink_voice_note_app_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_service_self_test() ? ESP_OK : ESP_FAIL);
```

- [ ] **Step 6: Re-run the build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- the new app and service files compile

- [ ] **Step 7: Commit the scaffold**

Run:

```powershell
git add main/CMakeLists.txt main/apps/ink_app_iface.h main/apps/ink_launcher_app.c main/apps/ink_voice_note_app.h main/apps/ink_voice_note_app.c main/voice_note/voice_note_types.h main/voice_note/voice_note_service.h main/voice_note/voice_note_service.c main/app_main.c
git commit -m "feat: scaffold voice note app and service"
```

Expected:
- commit succeeds with only the scaffold files staged

### Task 2: Add voice note storage paths, JSON model helpers, and note-title rules

**Files:**
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.h`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.c`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_model.h`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_model.c`
- Modify: `D:\FUCKIDF\ink-reader\main\CMakeLists.txt`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.h`

- [ ] **Step 1: Wire the new model and store files into the build**

Modify `D:\FUCKIDF\ink-reader\main\CMakeLists.txt`:

```cmake
        "voice_note/voice_note_service.c"
        "voice_note/voice_note_store.c"
        "voice_note/voice_note_model.c"
```

- [ ] **Step 2: Add the shared path and title helper header**

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_model.h`:

```c
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
```

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_model.c`:

```c
#include "voice_note/voice_note_model.h"

#include <stdio.h>
#include <string.h>

static void truncate_utf8ish_copy(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0U;
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }
    for (; src[i] != '\0' && i + 1U < dst_size; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
    if (src[i] != '\0' && i >= 3U) {
        dst[i - 3U] = '.';
        dst[i - 2U] = '.';
        dst[i - 1U] = '.';
        dst[i] = '\0';
    }
}

bool voice_note_model_build_note_paths(
    uint32_t epoch_s,
    uint32_t sequence,
    char *note_id,
    size_t note_id_size,
    char *json_path,
    size_t json_path_size,
    char *wav_path,
    size_t wav_path_size)
{
    int note_id_written = 0;
    int json_written = 0;
    int wav_written = 0;

    if (note_id == NULL || json_path == NULL || wav_path == NULL) {
        return false;
    }
    note_id_written = snprintf(note_id, note_id_size, "note_%010u_%03u", (unsigned)epoch_s, (unsigned)sequence);
    json_written = snprintf(json_path, json_path_size, "%s/%s.json", VOICE_NOTE_ROOT_DIR, note_id);
    wav_written = snprintf(wav_path, wav_path_size, "%s/%s.wav", VOICE_NOTE_ROOT_DIR, note_id);
    return note_id_written > 0
        && json_written > 0
        && wav_written > 0
        && (size_t)note_id_written < note_id_size
        && (size_t)json_written < json_path_size
        && (size_t)wav_written < wav_path_size;
}

void voice_note_model_build_title(
    const char *text,
    voice_note_transcript_state_t transcript_state,
    char *title,
    size_t title_size)
{
    if (title == NULL || title_size == 0U) {
        return;
    }
    if (transcript_state != VOICE_NOTE_TRANSCRIPT_READY || text == NULL || text[0] == '\0') {
        snprintf(title, title_size, "%s", "这是一条语音标签");
        return;
    }
    truncate_utf8ish_copy(text, title, title_size);
}

bool voice_note_model_is_short_recording(uint32_t duration_ms, uint32_t min_duration_ms)
{
    return duration_ms < min_duration_ms;
}

bool voice_note_model_status_copy_for_job(
    voice_note_job_state_t state,
    char *buffer,
    size_t buffer_size)
{
    const char *text = "按住 Confirm 开始录音";

    if (buffer == NULL || buffer_size == 0U) {
        return false;
    }
    switch (state) {
        case VOICE_NOTE_JOB_RECORDING:
            text = "正在录音";
            break;
        case VOICE_NOTE_JOB_PACKAGING:
            text = "正在打包";
            break;
        case VOICE_NOTE_JOB_PERSISTING_WAV:
            text = "结束录音";
            break;
        case VOICE_NOTE_JOB_WIFI_CONNECTING:
            text = "正在连接网络";
            break;
        case VOICE_NOTE_JOB_UPLOADING:
            text = "上传中";
            break;
        case VOICE_NOTE_JOB_RECOGNIZING:
            text = "上传识别中";
            break;
        case VOICE_NOTE_JOB_COMPLETED:
            text = "识别完成";
            break;
        case VOICE_NOTE_JOB_FAILED:
            text = "识别失败";
            break;
        case VOICE_NOTE_JOB_INVALID_SHORT_RECORDING:
            text = "无效标签，请重新录入";
            break;
        default:
            break;
    }
    return snprintf(buffer, buffer_size, "%s", text) > 0;
}

bool voice_note_model_self_test(void)
{
    char note_id[VOICE_NOTE_ID_LENGTH];
    char json_path[VOICE_NOTE_PATH_LENGTH];
    char wav_path[VOICE_NOTE_PATH_LENGTH];
    char title[VOICE_NOTE_TITLE_LENGTH];
    char status[VOICE_NOTE_STATUS_COPY_LENGTH];

    if (!voice_note_model_build_note_paths(1719651000U, 1U, note_id, sizeof(note_id), json_path, sizeof(json_path), wav_path, sizeof(wav_path))) {
        return false;
    }
    if (strcmp(note_id, "note_1719651000_001") != 0) {
        return false;
    }
    voice_note_model_build_title("记得买小葱，还有蒜头。", VOICE_NOTE_TRANSCRIPT_READY, title, sizeof(title));
    if (strcmp(title, "记得买小葱，还有蒜头。") != 0) {
        return false;
    }
    voice_note_model_build_title("", VOICE_NOTE_TRANSCRIPT_FAILED, title, sizeof(title));
    if (strcmp(title, "这是一条语音标签") != 0) {
        return false;
    }
    if (!voice_note_model_status_copy_for_job(VOICE_NOTE_JOB_INVALID_SHORT_RECORDING, status, sizeof(status))) {
        return false;
    }
    return strcmp(status, "无效标签，请重新录入") == 0
        && voice_note_model_is_short_recording(1999U, 2000U)
        && !voice_note_model_is_short_recording(2000U, 2000U);
}
```

- [ ] **Step 3: Add the JSON-backed store header**

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.h`:

```c
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
```

- [ ] **Step 4: Add the in-memory store with cJSON round-trip helpers**

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.c`:

```c
#include "voice_note/voice_note_store.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_check.h"

#include "voice_note/voice_note_model.h"

static voice_note_note_t s_notes[VOICE_NOTE_MAX_NOTES];
static size_t s_note_count;

static esp_err_t ensure_voice_note_dir(void)
{
    struct stat st = {0};

    if (stat("/sdcard/.ink-reader", &st) != 0) {
        if (mkdir("/sdcard/.ink-reader") != 0) {
            return ESP_FAIL;
        }
    }
    if (stat(VOICE_NOTE_ROOT_DIR, &st) != 0) {
        if (mkdir(VOICE_NOTE_ROOT_DIR) != 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static bool note_matches_tab(const voice_note_note_t *note, voice_note_tab_t tab)
{
    if (note == NULL) {
        return false;
    }
    switch (tab) {
        case VOICE_NOTE_TAB_PENDING:
            return note->status == VOICE_NOTE_STATUS_PENDING;
        case VOICE_NOTE_TAB_DONE:
            return note->status == VOICE_NOTE_STATUS_DONE;
        case VOICE_NOTE_TAB_ALL:
        default:
            return true;
    }
}

static cJSON *note_to_json(const voice_note_note_t *note)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "id", note->id);
    cJSON_AddNumberToObject(root, "created_at_epoch_s", note->created_at_epoch_s);
    cJSON_AddNumberToObject(root, "updated_at_epoch_s", note->updated_at_epoch_s);
    cJSON_AddNumberToObject(root, "status", note->status);
    cJSON_AddNumberToObject(root, "transcript_state", note->transcript_state);
    cJSON_AddStringToObject(root, "title", note->title);
    cJSON_AddStringToObject(root, "text", note->text);
    cJSON_AddStringToObject(root, "wav_path", note->wav_path);
    cJSON_AddNumberToObject(root, "duration_ms", note->duration_ms);
    cJSON_AddNumberToObject(root, "sample_rate", note->sample_rate);
    cJSON_AddNumberToObject(root, "channels", note->channels);
    cJSON_AddNumberToObject(root, "bits_per_sample", note->bits_per_sample);
    cJSON_AddStringToObject(root, "last_error", note->last_error);
    return root;
}

static bool note_from_json(const cJSON *root, voice_note_note_t *note)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *title = cJSON_GetObjectItemCaseSensitive(root, "title");
    const cJSON *wav_path = cJSON_GetObjectItemCaseSensitive(root, "wav_path");

    if (!cJSON_IsString(id) || !cJSON_IsString(title) || !cJSON_IsString(wav_path) || note == NULL) {
        return false;
    }
    memset(note, 0, sizeof(*note));
    snprintf(note->id, sizeof(note->id), "%s", id->valuestring);
    snprintf(note->title, sizeof(note->title), "%s", title->valuestring);
    snprintf(note->wav_path, sizeof(note->wav_path), "%s", wav_path->valuestring);
    {
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
        const cJSON *last_error = cJSON_GetObjectItemCaseSensitive(root, "last_error");
        note->created_at_epoch_s = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "created_at_epoch_s"));
        note->updated_at_epoch_s = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "updated_at_epoch_s"));
        note->status = (voice_note_status_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "status"));
        note->transcript_state = (voice_note_transcript_state_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "transcript_state"));
        note->duration_ms = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "duration_ms"));
        note->sample_rate = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "sample_rate"));
        note->channels = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "channels"));
        note->bits_per_sample = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "bits_per_sample"));
        if (cJSON_IsString(text)) {
            snprintf(note->text, sizeof(note->text), "%s", text->valuestring);
        }
        if (cJSON_IsString(last_error)) {
            snprintf(note->last_error, sizeof(note->last_error), "%s", last_error->valuestring);
        }
    }
    return true;
}

static esp_err_t write_note_file(const char *path, const voice_note_note_t *note)
{
    FILE *fp = NULL;
    cJSON *root = NULL;
    char *text = NULL;

    root = note_to_json(note);
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    text = cJSON_PrintUnformatted(root);
    if (text == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    fp = fopen(path, "wb");
    if (fp == NULL) {
        cJSON_free(text);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    fwrite(text, 1U, strlen(text), fp);
    fclose(fp);
    cJSON_free(text);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t voice_note_store_init(void)
{
    s_note_count = 0U;
    memset(s_notes, 0, sizeof(s_notes));
    return ensure_voice_note_dir();
}

esp_err_t voice_note_store_reload(void)
{
    return voice_note_store_init();
}

size_t voice_note_store_count(void)
{
    return s_note_count;
}

bool voice_note_store_copy_summaries(
    voice_note_tab_t tab,
    voice_note_note_t *notes,
    size_t capacity,
    size_t *count_out)
{
    size_t count = 0U;
    size_t i = 0U;

    if (count_out != NULL) {
        *count_out = 0U;
    }
    if (notes == NULL) {
        return false;
    }
    for (i = 0U; i < s_note_count && count < capacity; ++i) {
        if (!note_matches_tab(&s_notes[i], tab)) {
            continue;
        }
        notes[count++] = s_notes[i];
    }
    if (count_out != NULL) {
        *count_out = count;
    }
    return true;
}

bool voice_note_store_find_note(const char *note_id, voice_note_note_t *out_note)
{
    size_t i = 0U;
    if (note_id == NULL || out_note == NULL) {
        return false;
    }
    for (i = 0U; i < s_note_count; ++i) {
        if (strcmp(s_notes[i].id, note_id) == 0) {
            *out_note = s_notes[i];
            return true;
        }
    }
    return false;
}

esp_err_t voice_note_store_create_processing_note(const voice_note_note_t *note)
{
    char path[VOICE_NOTE_PATH_LENGTH];
    if (note == NULL || s_note_count >= VOICE_NOTE_MAX_NOTES) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(path, sizeof(path), "%s/%s.json", VOICE_NOTE_ROOT_DIR, note->id);
    s_notes[s_note_count++] = *note;
    return write_note_file(path, note);
}

esp_err_t voice_note_store_update_note(const voice_note_note_t *note)
{
    size_t i = 0U;
    char path[VOICE_NOTE_PATH_LENGTH];

    if (note == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (i = 0U; i < s_note_count; ++i) {
        if (strcmp(s_notes[i].id, note->id) == 0) {
            s_notes[i] = *note;
            snprintf(path, sizeof(path), "%s/%s.json", VOICE_NOTE_ROOT_DIR, note->id);
            return write_note_file(path, note);
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t voice_note_store_delete_note(const char *note_id)
{
    size_t i = 0U;
    char json_path[VOICE_NOTE_PATH_LENGTH];

    if (note_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (i = 0U; i < s_note_count; ++i) {
        if (strcmp(s_notes[i].id, note_id) != 0) {
            continue;
        }
        snprintf(json_path, sizeof(json_path), "%s/%s.json", VOICE_NOTE_ROOT_DIR, note_id);
        remove(json_path);
        remove(s_notes[i].wav_path);
        memmove(&s_notes[i], &s_notes[i + 1U], (s_note_count - i - 1U) * sizeof(s_notes[0]));
        s_note_count--;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

bool voice_note_store_self_test(void)
{
    voice_note_note_t note;
    voice_note_note_t copy;

    memset(&note, 0, sizeof(note));
    snprintf(note.id, sizeof(note.id), "%s", "note_test");
    snprintf(note.title, sizeof(note.title), "%s", "测试便签");
    snprintf(note.wav_path, sizeof(note.wav_path), "%s", VOICE_NOTE_ROOT_DIR "/note_test.wav");
    note.status = VOICE_NOTE_STATUS_PENDING;
    note.transcript_state = VOICE_NOTE_TRANSCRIPT_PROCESSING;
    if (voice_note_store_init() != ESP_OK) {
        return false;
    }
    if (voice_note_store_create_processing_note(&note) != ESP_OK) {
        return false;
    }
    if (!voice_note_store_find_note("note_test", &copy)) {
        return false;
    }
    if (strcmp(copy.title, "测试便签") != 0) {
        return false;
    }
    return voice_note_store_delete_note("note_test") == ESP_OK;
}
```

- [ ] **Step 5: Export the store self-test from the service header for later boot integration**

Modify `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.h`:

```c
bool voice_note_store_self_test(void);
bool voice_note_model_self_test(void);
```

- [ ] **Step 6: Re-run the build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- `json` component resolves `#include "cJSON.h"`

- [ ] **Step 7: Commit the model and store layer**

Run:

```powershell
git add main/CMakeLists.txt main/voice_note/voice_note_model.h main/voice_note/voice_note_model.c main/voice_note/voice_note_store.h main/voice_note/voice_note_store.c main/voice_note/voice_note_service.h
git commit -m "feat: add voice note model and store helpers"
```

Expected:
- commit succeeds

### Task 3: Replace the in-memory store stub with index.json persistence and interrupted-note recovery

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.c`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.h`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_model.c`

- [ ] **Step 1: Add a failing store self-test for interrupted note normalization**

Extend `voice_note_store_self_test()` in `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.c`:

```c
    note.transcript_state = VOICE_NOTE_TRANSCRIPT_PROCESSING;
    snprintf(note.last_error, sizeof(note.last_error), "%s", "");
    if (voice_note_store_update_note(&note) != ESP_OK) {
        return false;
    }
    if (voice_note_store_reload() != ESP_OK) {
        return false;
    }
    if (!voice_note_store_find_note("note_test", &copy)) {
        return false;
    }
    if (copy.transcript_state != VOICE_NOTE_TRANSCRIPT_FAILED) {
        return false;
    }
    if (strcmp(copy.title, "这是一条语音标签") != 0) {
        return false;
    }
```

- [ ] **Step 2: Re-run the build to verify the store still compiles but the new self-test logic is not implemented**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- the new self-test logic is in place but the behavior is not yet guaranteed on real data reload

- [ ] **Step 3: Add `index.json` writing and per-note rescan on reload**

Add helpers in `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.c`:

```c
static esp_err_t write_index_file(void)
{
    FILE *fp = NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON *notes = cJSON_CreateArray();
    size_t i = 0U;
    char *text = NULL;

    if (root == NULL || notes == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(root, "notes", notes);
    for (i = 0U; i < s_note_count; ++i) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", s_notes[i].id);
        cJSON_AddNumberToObject(item, "updated_at_epoch_s", s_notes[i].updated_at_epoch_s);
        cJSON_AddItemToArray(notes, item);
    }
    text = cJSON_PrintUnformatted(root);
    if (text == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    fp = fopen(VOICE_NOTE_INDEX_PATH, "wb");
    if (fp == NULL) {
        cJSON_free(text);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    fwrite(text, 1U, strlen(text), fp);
    fclose(fp);
    cJSON_free(text);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t load_note_file(const char *path, voice_note_note_t *note)
{
    FILE *fp = NULL;
    long size = 0L;
    char *buffer = NULL;
    cJSON *root = NULL;
    esp_err_t result = ESP_FAIL;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return ESP_FAIL;
    }
    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    if (size <= 0L) {
        fclose(fp);
        return ESP_FAIL;
    }
    buffer = malloc((size_t)size + 1U);
    if (buffer == NULL) {
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }
    fread(buffer, 1U, (size_t)size, fp);
    buffer[size] = '\0';
    fclose(fp);
    root = cJSON_Parse(buffer);
    if (root != NULL && note_from_json(root, note)) {
        result = ESP_OK;
    }
    cJSON_Delete(root);
    free(buffer);
    return result;
}
```

- [ ] **Step 4: Normalize interrupted notes during reload**

Replace `voice_note_store_reload()` in `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.c`:

```c
esp_err_t voice_note_store_reload(void)
{
    DIR *dir = NULL;
    struct dirent *entry = NULL;

    if (voice_note_store_init() != ESP_OK) {
        return ESP_FAIL;
    }
    dir = opendir(VOICE_NOTE_ROOT_DIR);
    if (dir == NULL) {
        return ESP_FAIL;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[VOICE_NOTE_PATH_LENGTH];
        voice_note_note_t note;

        if (strstr(entry->d_name, ".json") == NULL || strcmp(entry->d_name, "index.json") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", VOICE_NOTE_ROOT_DIR, entry->d_name);
        if (load_note_file(path, &note) != ESP_OK || s_note_count >= VOICE_NOTE_MAX_NOTES) {
            continue;
        }
        if (note.transcript_state == VOICE_NOTE_TRANSCRIPT_PROCESSING) {
            note.transcript_state = VOICE_NOTE_TRANSCRIPT_FAILED;
            snprintf(note.last_error, sizeof(note.last_error), "%s", "interrupted");
            voice_note_model_build_title("", note.transcript_state, note.title, sizeof(note.title));
            (void)write_note_file(path, &note);
        }
        s_notes[s_note_count++] = note;
    }
    closedir(dir);
    return write_index_file();
}
```

and call `write_index_file()` at the end of:

```c
esp_err_t voice_note_store_create_processing_note(const voice_note_note_t *note)
esp_err_t voice_note_store_update_note(const voice_note_note_t *note)
esp_err_t voice_note_store_delete_note(const char *note_id)
```

- [ ] **Step 5: Re-run the build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- the store now persists both note files and index metadata

- [ ] **Step 6: Commit the durable store behavior**

Run:

```powershell
git add main/voice_note/voice_note_store.c main/voice_note/voice_note_store.h main/voice_note/voice_note_model.c
git commit -m "feat: persist voice note index and recovery states"
```

Expected:
- commit succeeds

### Task 4: Add ASR configuration and request helpers for mainline reuse

**Files:**
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\local_voice_note_asr_config.example.h`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\local_voice_note_asr_config.h`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_asr.h`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_asr.c`
- Modify: `D:\FUCKIDF\ink-reader\main\CMakeLists.txt`
- Modify: `D:\FUCKIDF\ink-reader\.gitignore`

- [ ] **Step 1: Add the new ASR files to the build**

Modify `D:\FUCKIDF\ink-reader\main\CMakeLists.txt`:

```cmake
        "voice_note/voice_note_asr.c"
```

- [ ] **Step 2: Add the tracked example config and local include wrapper**

Create `D:\FUCKIDF\ink-reader\main\voice_note\local_voice_note_asr_config.example.h`:

```c
#pragma once

#define VOICE_NOTE_ASR_API_KEY "replace-with-your-dashscope-api-key"
#define VOICE_NOTE_ASR_BASE_URL "https://your-workspace.cn-beijing.maas.aliyuncs.com/compatible-mode/v1"
#define VOICE_NOTE_ASR_MODEL "qwen3-asr-flash"
```

Create `D:\FUCKIDF\ink-reader\main\voice_note\local_voice_note_asr_config.h`:

```c
#pragma once

#include "local_voice_note_asr_config.example.h"

#if __has_include("local_voice_note_asr_config_override.h")
#ifdef VOICE_NOTE_ASR_API_KEY
#undef VOICE_NOTE_ASR_API_KEY
#endif
#ifdef VOICE_NOTE_ASR_BASE_URL
#undef VOICE_NOTE_ASR_BASE_URL
#endif
#ifdef VOICE_NOTE_ASR_MODEL
#undef VOICE_NOTE_ASR_MODEL
#endif
#include "local_voice_note_asr_config_override.h"
#endif
```

- [ ] **Step 3: Ignore the local override file**

Modify `D:\FUCKIDF\ink-reader\.gitignore`:

```gitignore
main/voice_note/local_voice_note_asr_config_override.h
```

- [ ] **Step 4: Add the ASR helper module using the probe-tested request shape**

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_asr.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    const char *api_key;
    const char *base_url;
    const char *model;
} voice_note_asr_config_t;

typedef enum {
    VOICE_NOTE_ASR_OK = 0,
    VOICE_NOTE_ASR_ERROR_INVALID_ARG,
    VOICE_NOTE_ASR_ERROR_CONFIG,
    VOICE_NOTE_ASR_ERROR_HTTP,
    VOICE_NOTE_ASR_ERROR_STATUS,
    VOICE_NOTE_ASR_ERROR_JSON,
    VOICE_NOTE_ASR_ERROR_RESPONSE_TOO_LARGE,
} voice_note_asr_result_t;

const voice_note_asr_config_t *voice_note_asr_get_config(void);
bool voice_note_asr_config_valid(const voice_note_asr_config_t *config);
size_t voice_note_asr_estimate_body_size(const voice_note_asr_config_t *config, size_t wav_bytes);
esp_err_t voice_note_asr_build_body(
    const voice_note_asr_config_t *config,
    const uint8_t *wav_data,
    size_t wav_size,
    char *buffer,
    size_t buffer_size,
    size_t *written_out);
voice_note_asr_result_t voice_note_asr_post_wav(
    const uint8_t *wav_data,
    size_t wav_size,
    char *transcript,
    size_t transcript_size);
bool voice_note_asr_self_test(void);
```

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_asr.c`:

```c
#include "voice_note/voice_note_asr.h"

#include "local_voice_note_asr_config.h"

#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "mbedtls/base64.h"

static const voice_note_asr_config_t s_config = {
    .api_key = VOICE_NOTE_ASR_API_KEY,
    .base_url = VOICE_NOTE_ASR_BASE_URL,
    .model = VOICE_NOTE_ASR_MODEL,
};

static const char *const kBodyPrefix = "{\"model\":\"";
static const char *const kBodyMid = "\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"data:audio/wav;base64,";
static const char *const kBodySuffix = "\"}}]}],\"stream\":false,\"asr_options\":{\"enable_itn\":false}}";

typedef struct {
    char *buffer;
    size_t capacity;
    size_t used;
    bool overflow;
} voice_note_http_accumulator_t;

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    voice_note_http_accumulator_t *acc = event != NULL ? (voice_note_http_accumulator_t *)event->user_data : NULL;
    if (event == NULL || acc == NULL || event->event_id != HTTP_EVENT_ON_DATA || event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }
    if (acc->used + (size_t)event->data_len + 1U > acc->capacity) {
        acc->overflow = true;
        return ESP_OK;
    }
    memcpy(acc->buffer + acc->used, event->data, (size_t)event->data_len);
    acc->used += (size_t)event->data_len;
    acc->buffer[acc->used] = '\0';
    return ESP_OK;
}

const voice_note_asr_config_t *voice_note_asr_get_config(void)
{
    return &s_config;
}

bool voice_note_asr_config_valid(const voice_note_asr_config_t *config)
{
    return config != NULL
        && config->api_key != NULL
        && config->base_url != NULL
        && config->model != NULL
        && config->api_key[0] != '\0'
        && strcmp(config->api_key, "replace-with-your-dashscope-api-key") != 0;
}

size_t voice_note_asr_estimate_body_size(const voice_note_asr_config_t *config, size_t wav_bytes)
{
    size_t base64_bytes = 4U * ((wav_bytes + 2U) / 3U);
    if (!voice_note_asr_config_valid(config) || wav_bytes == 0U) {
        return 0U;
    }
    return strlen(kBodyPrefix) + strlen(config->model) + strlen(kBodyMid) + base64_bytes + strlen(kBodySuffix);
}

esp_err_t voice_note_asr_build_body(
    const voice_note_asr_config_t *config,
    const uint8_t *wav_data,
    size_t wav_size,
    char *buffer,
    size_t buffer_size,
    size_t *written_out)
{
    char *cursor = buffer;
    size_t base64_written = 0U;

    ESP_RETURN_ON_FALSE(voice_note_asr_config_valid(config), ESP_ERR_INVALID_ARG, "voice_note_asr", "invalid config");
    ESP_RETURN_ON_FALSE(wav_data != NULL && wav_size > 0U, ESP_ERR_INVALID_ARG, "voice_note_asr", "invalid wav");
    ESP_RETURN_ON_FALSE(buffer != NULL && buffer_size > 0U, ESP_ERR_INVALID_ARG, "voice_note_asr", "invalid buffer");
    memcpy(cursor, kBodyPrefix, strlen(kBodyPrefix));
    cursor += strlen(kBodyPrefix);
    memcpy(cursor, config->model, strlen(config->model));
    cursor += strlen(config->model);
    memcpy(cursor, kBodyMid, strlen(kBodyMid));
    cursor += strlen(kBodyMid);
    if (mbedtls_base64_encode((unsigned char *)cursor, buffer_size - (size_t)(cursor - buffer), &base64_written, wav_data, wav_size) != 0) {
        return ESP_FAIL;
    }
    cursor += base64_written;
    memcpy(cursor, kBodySuffix, strlen(kBodySuffix));
    cursor += strlen(kBodySuffix);
    *cursor = '\0';
    if (written_out != NULL) {
        *written_out = (size_t)(cursor - buffer);
    }
    return ESP_OK;
}

voice_note_asr_result_t voice_note_asr_post_wav(
    const uint8_t *wav_data,
    size_t wav_size,
    char *transcript,
    size_t transcript_size)
{
    char url[192];
    char auth[256];
    char *body = NULL;
    char response[1024];
    size_t body_size = 0U;
    voice_note_http_accumulator_t accumulator = {
        .buffer = response,
        .capacity = sizeof(response),
        .used = 0U,
        .overflow = false,
    };
    esp_http_client_handle_t client = NULL;
    esp_http_client_config_t cfg;
    cJSON *root = NULL;
    cJSON *choices = NULL;
    cJSON *message = NULL;
    cJSON *content = NULL;
    voice_note_asr_result_t result = VOICE_NOTE_ASR_ERROR_HTTP;

    if (!voice_note_asr_config_valid(&s_config) || transcript == NULL || transcript_size == 0U) {
        return VOICE_NOTE_ASR_ERROR_CONFIG;
    }
    if (snprintf(url, sizeof(url), "%s/chat/completions", s_config.base_url) <= 0) {
        return VOICE_NOTE_ASR_ERROR_CONFIG;
    }
    if (snprintf(auth, sizeof(auth), "Bearer %s", s_config.api_key) <= 0) {
        return VOICE_NOTE_ASR_ERROR_CONFIG;
    }
    body = malloc(voice_note_asr_estimate_body_size(&s_config, wav_size) + 1U);
    if (body == NULL) {
        return VOICE_NOTE_ASR_ERROR_INVALID_ARG;
    }
    if (voice_note_asr_build_body(&s_config, wav_data, wav_size, body, voice_note_asr_estimate_body_size(&s_config, wav_size) + 1U, &body_size) != ESP_OK) {
        free(body);
        return VOICE_NOTE_ASR_ERROR_INVALID_ARG;
    }
    memset(&cfg, 0, sizeof(cfg));
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 45000;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 4096;
    cfg.event_handler = http_event_handler;
    cfg.user_data = &accumulator;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif
    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        free(body);
        return VOICE_NOTE_ASR_ERROR_HTTP;
    }
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)body_size);
    if (esp_http_client_perform(client) != ESP_OK) {
        goto cleanup;
    }
    if (esp_http_client_get_status_code(client) < 200 || esp_http_client_get_status_code(client) >= 300) {
        result = VOICE_NOTE_ASR_ERROR_STATUS;
        goto cleanup;
    }
    if (accumulator.overflow) {
        result = VOICE_NOTE_ASR_ERROR_RESPONSE_TOO_LARGE;
        goto cleanup;
    }
    root = cJSON_Parse(response);
    if (root == NULL) {
        result = VOICE_NOTE_ASR_ERROR_JSON;
        goto cleanup;
    }
    choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    message = cJSON_GetArrayItem(choices, 0);
    message = cJSON_GetObjectItemCaseSensitive(message, "message");
    content = cJSON_GetObjectItemCaseSensitive(message, "content");
    if (!cJSON_IsString(content)) {
        result = VOICE_NOTE_ASR_ERROR_JSON;
        goto cleanup;
    }
    snprintf(transcript, transcript_size, "%s", content->valuestring);
    result = VOICE_NOTE_ASR_OK;

cleanup:
    cJSON_Delete(root);
    if (client != NULL) {
        esp_http_client_cleanup(client);
    }
    free(body);
    return result;
}

bool voice_note_asr_self_test(void)
{
    uint8_t wav_data[1] = {0x66};
    char body[256];
    size_t written = 0U;
    return voice_note_asr_estimate_body_size(&(voice_note_asr_config_t){
            .api_key = "k",
            .base_url = "https://example.com/compatible-mode/v1",
            .model = "qwen3-asr-flash",
        }, sizeof(wav_data)) > 0U
        && voice_note_asr_build_body(&(voice_note_asr_config_t){
                .api_key = "k",
                .base_url = "https://example.com/compatible-mode/v1",
                .model = "qwen3-asr-flash",
            }, wav_data, sizeof(wav_data), body, sizeof(body), &written) == ESP_OK
        && written == strlen(body)
        && strstr(body, "\"type\":\"input_audio\"") != NULL;
}
```

- [ ] **Step 5: Re-run the build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- the new mainline ASR helper compiles independently of `tests/audio_asr_probe`

- [ ] **Step 6: Commit the ASR helper layer**

Run:

```powershell
git add .gitignore main/CMakeLists.txt main/voice_note/local_voice_note_asr_config.example.h main/voice_note/local_voice_note_asr_config.h main/voice_note/voice_note_asr.h main/voice_note/voice_note_asr.c
git commit -m "feat: add voice note ASR helper"
```

Expected:
- commit succeeds

### Task 5: Add WAV packaging and capture-session helpers from the validated probe flow

**Files:**
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_audio.h`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_audio.c`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_capture_logic.h`
- Create: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_capture_logic.c`
- Modify: `D:\FUCKIDF\ink-reader\main\CMakeLists.txt`

- [ ] **Step 1: Add the capture helper files to the build**

Modify `D:\FUCKIDF\ink-reader\main\CMakeLists.txt`:

```cmake
        "voice_note/voice_note_audio.c"
        "voice_note/voice_note_capture_logic.c"
```

- [ ] **Step 2: Add the WAV helper interface**

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_audio.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

enum {
    VOICE_NOTE_SAMPLE_RATE = 16000,
    VOICE_NOTE_BITS_PER_SAMPLE = 16,
    VOICE_NOTE_CHANNELS = 1,
    VOICE_NOTE_MAX_CAPTURE_MS = 10000,
    VOICE_NOTE_MIN_CAPTURE_MS = 2000,
};

size_t voice_note_audio_wav_size(size_t pcm_bytes);
esp_err_t voice_note_audio_build_wav(
    const uint8_t *pcm_data,
    size_t pcm_size,
    uint8_t *wav_buffer,
    size_t wav_buffer_size,
    size_t *wav_bytes_out);
uint32_t voice_note_audio_pcm_duration_ms(size_t pcm_bytes);
bool voice_note_audio_self_test(void);
```

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_audio.c`:

```c
#include "voice_note/voice_note_audio.h"

#include <string.h>

static void write_u16le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void write_u32le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

size_t voice_note_audio_wav_size(size_t pcm_bytes)
{
    return 44U + pcm_bytes;
}

esp_err_t voice_note_audio_build_wav(
    const uint8_t *pcm_data,
    size_t pcm_size,
    uint8_t *wav_buffer,
    size_t wav_buffer_size,
    size_t *wav_bytes_out)
{
    uint32_t byte_rate = VOICE_NOTE_SAMPLE_RATE * VOICE_NOTE_CHANNELS * (VOICE_NOTE_BITS_PER_SAMPLE / 8U);
    uint16_t block_align = VOICE_NOTE_CHANNELS * (VOICE_NOTE_BITS_PER_SAMPLE / 8U);

    if (pcm_data == NULL || wav_buffer == NULL || wav_buffer_size < voice_note_audio_wav_size(pcm_size)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(wav_buffer + 0, "RIFF", 4U);
    write_u32le(wav_buffer + 4, (uint32_t)(36U + pcm_size));
    memcpy(wav_buffer + 8, "WAVEfmt ", 8U);
    write_u32le(wav_buffer + 16, 16U);
    write_u16le(wav_buffer + 20, 1U);
    write_u16le(wav_buffer + 22, VOICE_NOTE_CHANNELS);
    write_u32le(wav_buffer + 24, VOICE_NOTE_SAMPLE_RATE);
    write_u32le(wav_buffer + 28, byte_rate);
    write_u16le(wav_buffer + 32, block_align);
    write_u16le(wav_buffer + 34, VOICE_NOTE_BITS_PER_SAMPLE);
    memcpy(wav_buffer + 36, "data", 4U);
    write_u32le(wav_buffer + 40, (uint32_t)pcm_size);
    memcpy(wav_buffer + 44, pcm_data, pcm_size);
    if (wav_bytes_out != NULL) {
        *wav_bytes_out = 44U + pcm_size;
    }
    return ESP_OK;
}

uint32_t voice_note_audio_pcm_duration_ms(size_t pcm_bytes)
{
    size_t bytes_per_ms = (VOICE_NOTE_SAMPLE_RATE * VOICE_NOTE_CHANNELS * (VOICE_NOTE_BITS_PER_SAMPLE / 8U)) / 1000U;
    return bytes_per_ms == 0U ? 0U : (uint32_t)(pcm_bytes / bytes_per_ms);
}

bool voice_note_audio_self_test(void)
{
    uint8_t pcm[4] = {1, 2, 3, 4};
    uint8_t wav[48];
    size_t written = 0U;

    return voice_note_audio_wav_size(sizeof(pcm)) == 48U
        && voice_note_audio_build_wav(pcm, sizeof(pcm), wav, sizeof(wav), &written) == ESP_OK
        && written == sizeof(wav)
        && memcmp(wav, "RIFF", 4U) == 0
        && memcmp(wav + 8, "WAVE", 4U) == 0;
}
```

- [ ] **Step 3: Add the capture-session logic helper**

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_capture_logic.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    VOICE_NOTE_CAPTURE_IDLE = 0,
    VOICE_NOTE_CAPTURE_RECORDING,
    VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE,
} voice_note_capture_state_t;

typedef struct {
    voice_note_capture_state_t state;
    uint32_t sequence;
    uint32_t started_ms;
    uint32_t capture_ms;
    uint32_t max_capture_ms;
    size_t captured_bytes;
    size_t max_bytes;
    bool stop_due_to_limit;
} voice_note_capture_session_t;

void voice_note_capture_session_init(
    voice_note_capture_session_t *session,
    uint32_t max_capture_ms,
    size_t max_bytes);
bool voice_note_capture_try_start(voice_note_capture_session_t *session, uint32_t now_ms);
void voice_note_capture_append_bytes(
    voice_note_capture_session_t *session,
    size_t bytes_captured,
    bool button_pressed,
    uint32_t now_ms);
void voice_note_capture_mark_completed(
    voice_note_capture_session_t *session,
    bool button_still_pressed);
void voice_note_capture_poll_release(
    voice_note_capture_session_t *session,
    bool button_pressed);
bool voice_note_capture_logic_self_test(void);
```

Create `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_capture_logic.c`:

```c
#include "voice_note/voice_note_capture_logic.h"

#include <string.h>

void voice_note_capture_session_init(
    voice_note_capture_session_t *session,
    uint32_t max_capture_ms,
    size_t max_bytes)
{
    memset(session, 0, sizeof(*session));
    session->max_capture_ms = max_capture_ms;
    session->max_bytes = max_bytes;
}

bool voice_note_capture_try_start(voice_note_capture_session_t *session, uint32_t now_ms)
{
    if (session == NULL || session->state != VOICE_NOTE_CAPTURE_IDLE) {
        return false;
    }
    session->state = VOICE_NOTE_CAPTURE_RECORDING;
    session->sequence++;
    session->started_ms = now_ms;
    session->capture_ms = 0U;
    session->captured_bytes = 0U;
    session->stop_due_to_limit = false;
    return true;
}

void voice_note_capture_append_bytes(
    voice_note_capture_session_t *session,
    size_t bytes_captured,
    bool button_pressed,
    uint32_t now_ms)
{
    if (session == NULL || session->state != VOICE_NOTE_CAPTURE_RECORDING) {
        return;
    }
    session->captured_bytes += bytes_captured;
    session->capture_ms = now_ms - session->started_ms;
    if (!button_pressed || session->capture_ms >= session->max_capture_ms || session->captured_bytes >= session->max_bytes) {
        session->stop_due_to_limit = button_pressed && session->capture_ms >= session->max_capture_ms;
        session->state = VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE;
    }
}

void voice_note_capture_mark_completed(
    voice_note_capture_session_t *session,
    bool button_still_pressed)
{
    if (session == NULL) {
        return;
    }
    session->state = button_still_pressed ? VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE : VOICE_NOTE_CAPTURE_IDLE;
}

void voice_note_capture_poll_release(
    voice_note_capture_session_t *session,
    bool button_pressed)
{
    if (session == NULL || session->state != VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE) {
        return;
    }
    if (!button_pressed) {
        session->state = VOICE_NOTE_CAPTURE_IDLE;
    }
}

bool voice_note_capture_logic_self_test(void)
{
    voice_note_capture_session_t session;

    voice_note_capture_session_init(&session, 10000U, 320000U);
    if (!voice_note_capture_try_start(&session, 100U)) {
        return false;
    }
    voice_note_capture_append_bytes(&session, 2048U, true, 150U);
    if (session.state != VOICE_NOTE_CAPTURE_RECORDING) {
        return false;
    }
    voice_note_capture_append_bytes(&session, 2048U, false, 250U);
    if (session.state != VOICE_NOTE_CAPTURE_WAIT_FOR_RELEASE) {
        return false;
    }
    voice_note_capture_poll_release(&session, false);
    return session.state == VOICE_NOTE_CAPTURE_IDLE;
}
```

- [ ] **Step 4: Re-run the build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- the audio and capture helpers compile and self-test cleanly

- [ ] **Step 5: Commit the capture helper layer**

Run:

```powershell
git add main/CMakeLists.txt main/voice_note/voice_note_audio.h main/voice_note/voice_note_audio.c main/voice_note/voice_note_capture_logic.h main/voice_note/voice_note_capture_logic.c
git commit -m "feat: add voice note capture helpers"
```

Expected:
- commit succeeds

### Task 6: Implement the service state machine, invalid-short-recording timeout, and note persistence pipeline

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.h`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.c`

- [ ] **Step 1: Add a failing service self-test for invalid short-recording reset**

Extend `voice_note_service_self_test()` in `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`:

```c
    if (!voice_note_service_start_capture(100U)) {
        return false;
    }
    if (!voice_note_service_stop_capture(1500U)) {
        return false;
    }
    if (!voice_note_service_get_snapshot(&snapshot)) {
        return false;
    }
    if (snapshot.state != VOICE_NOTE_JOB_INVALID_SHORT_RECORDING) {
        return false;
    }
    if (strcmp(snapshot.status_text, "无效标签，请重新录入") != 0) {
        return false;
    }
    (void)voice_note_service_tick(7000U);
    if (!voice_note_service_get_snapshot(&snapshot)) {
        return false;
    }
    return snapshot.state == VOICE_NOTE_JOB_IDLE
        && strcmp(snapshot.status_text, "按住 Confirm 开始录音") == 0;
```

- [ ] **Step 2: Add persistent service state and scratch buffers**

At the top of `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`, replace the single snapshot with:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "voice_note/voice_note_asr.h"
#include "voice_note/voice_note_audio.h"
#include "voice_note/voice_note_capture_logic.h"
#include "voice_note/voice_note_model.h"
#include "voice_note/voice_note_store.h"

static voice_note_service_snapshot_t s_snapshot;
static voice_note_capture_session_t s_capture;
static voice_note_note_t s_active_note;
static uint8_t *s_pcm_buffer;
static uint8_t *s_wav_buffer;
static size_t s_pcm_capacity;
static size_t s_pcm_used;
static size_t s_wav_capacity;
```

- [ ] **Step 3: Initialize store and audio buffers in the service**

Replace `voice_note_service_init()`:

```c
esp_err_t voice_note_service_init(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(&s_active_note, 0, sizeof(s_active_note));
    voice_note_capture_session_init(&s_capture, VOICE_NOTE_MAX_CAPTURE_MS, (VOICE_NOTE_SAMPLE_RATE * 2U * VOICE_NOTE_MAX_CAPTURE_MS) / 1000U);
    s_snapshot.state = VOICE_NOTE_JOB_IDLE;
    voice_note_model_status_copy_for_job(s_snapshot.state, s_snapshot.status_text, sizeof(s_snapshot.status_text));
    if (s_pcm_buffer == NULL) {
        s_pcm_capacity = (VOICE_NOTE_SAMPLE_RATE * VOICE_NOTE_CHANNELS * (VOICE_NOTE_BITS_PER_SAMPLE / 8U) * VOICE_NOTE_MAX_CAPTURE_MS) / 1000U;
        s_wav_capacity = voice_note_audio_wav_size(s_pcm_capacity);
        s_pcm_buffer = heap_caps_malloc(s_pcm_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_wav_buffer = heap_caps_malloc(s_wav_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_pcm_buffer == NULL || s_wav_buffer == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(voice_note_store_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(voice_note_store_reload());
    s_snapshot.note_count = voice_note_store_count();
    return ESP_OK;
}
```

- [ ] **Step 4: Implement short-recording handling and idle reset timer**

Replace `voice_note_service_stop_capture()` and `voice_note_service_tick()`:

```c
bool voice_note_service_stop_capture(uint32_t now_ms)
{
    uint32_t duration_ms = 0U;

    if (!s_snapshot.busy || s_snapshot.state != VOICE_NOTE_JOB_RECORDING) {
        return false;
    }
    duration_ms = now_ms - s_snapshot.started_ms;
    s_snapshot.capture_duration_ms = duration_ms;
    s_snapshot.pcm_bytes = s_pcm_used;
    if (voice_note_model_is_short_recording(duration_ms, VOICE_NOTE_MIN_CAPTURE_MS)) {
        s_snapshot.busy = false;
        s_snapshot.state = VOICE_NOTE_JOB_INVALID_SHORT_RECORDING;
        s_snapshot.transient_until_ms = now_ms + 5000U;
        voice_note_model_status_copy_for_job(s_snapshot.state, s_snapshot.status_text, sizeof(s_snapshot.status_text));
        return true;
    }
    s_snapshot.state = VOICE_NOTE_JOB_PACKAGING;
    voice_note_model_status_copy_for_job(s_snapshot.state, s_snapshot.status_text, sizeof(s_snapshot.status_text));
    return true;
}

bool voice_note_service_tick(uint32_t now_ms)
{
    if (s_snapshot.state == VOICE_NOTE_JOB_INVALID_SHORT_RECORDING
        && now_ms >= s_snapshot.transient_until_ms) {
        s_snapshot.state = VOICE_NOTE_JOB_IDLE;
        s_snapshot.transient_until_ms = 0U;
        voice_note_model_status_copy_for_job(s_snapshot.state, s_snapshot.status_text, sizeof(s_snapshot.status_text));
        return true;
    }
    return false;
}
```

- [ ] **Step 5: Persist the saved WAV and initial `processing` note**

Add helper functions in `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`:

```c
static esp_err_t write_wav_file(const char *path, const uint8_t *data, size_t length)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return ESP_FAIL;
    }
    fwrite(data, 1U, length, fp);
    fclose(fp);
    return ESP_OK;
}

static esp_err_t create_processing_note(uint32_t now_ms)
{
    voice_note_note_t note;
    char json_path[VOICE_NOTE_PATH_LENGTH];

    memset(&note, 0, sizeof(note));
    if (!voice_note_model_build_note_paths(now_ms / 1000U, s_snapshot.sequence, note.id, sizeof(note.id), json_path, sizeof(json_path), note.wav_path, sizeof(note.wav_path))) {
        return ESP_ERR_INVALID_SIZE;
    }
    note.created_at_epoch_s = now_ms / 1000U;
    note.updated_at_epoch_s = note.created_at_epoch_s;
    note.status = VOICE_NOTE_STATUS_PENDING;
    note.transcript_state = VOICE_NOTE_TRANSCRIPT_PROCESSING;
    note.duration_ms = s_snapshot.capture_duration_ms;
    note.sample_rate = VOICE_NOTE_SAMPLE_RATE;
    note.channels = VOICE_NOTE_CHANNELS;
    note.bits_per_sample = VOICE_NOTE_BITS_PER_SAMPLE;
    voice_note_model_build_title("", note.transcript_state, note.title, sizeof(note.title));
    s_active_note = note;
    return voice_note_store_create_processing_note(&s_active_note);
}
```

and inside `voice_note_service_tick()` add:

```c
    if (s_snapshot.state == VOICE_NOTE_JOB_PACKAGING) {
        s_snapshot.state = VOICE_NOTE_JOB_PERSISTING_WAV;
        voice_note_model_status_copy_for_job(s_snapshot.state, s_snapshot.status_text, sizeof(s_snapshot.status_text));
        if (voice_note_audio_build_wav(s_pcm_buffer, s_pcm_used, s_wav_buffer, s_wav_capacity, &s_snapshot.wav_bytes) != ESP_OK) {
            s_snapshot.state = VOICE_NOTE_JOB_FAILED;
            snprintf(s_snapshot.status_text, sizeof(s_snapshot.status_text), "%s", "存储失败");
            s_snapshot.busy = false;
            return true;
        }
        if (create_processing_note(now_ms) != ESP_OK || write_wav_file(s_active_note.wav_path, s_wav_buffer, s_snapshot.wav_bytes) != ESP_OK) {
            s_snapshot.state = VOICE_NOTE_JOB_FAILED;
            snprintf(s_snapshot.status_text, sizeof(s_snapshot.status_text), "%s", "存储失败");
            s_snapshot.busy = false;
            return true;
        }
        s_snapshot.note_count = voice_note_store_count();
        s_snapshot.state = VOICE_NOTE_JOB_COMPLETED;
        s_snapshot.busy = false;
        voice_note_model_status_copy_for_job(s_snapshot.state, s_snapshot.status_text, sizeof(s_snapshot.status_text));
        s_snapshot.transient_until_ms = now_ms + 1500U;
        return true;
    }
```

- [ ] **Step 6: Re-run the build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- the service now handles invalid short recordings and local persistence

- [ ] **Step 7: Commit the service state machine base**

Run:

```powershell
git add main/voice_note/voice_note_service.c main/voice_note/voice_note_service.h main/voice_note/voice_note_store.c
git commit -m "feat: implement voice note local capture pipeline"
```

Expected:
- commit succeeds

### Task 7: Add Wi-Fi coordinator + ASR integration, including failed-note persistence and retry

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_coordinator.h`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_asr.c`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.c`

- [ ] **Step 1: Add the voice note lease owner to the coordinator**

Modify `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_coordinator.h`:

```c
typedef enum {
    INK_WIFI_COORDINATOR_OWNER_NONE = 0,
    INK_WIFI_COORDINATOR_OWNER_BOOT_AUTO_CONNECT,
    INK_WIFI_COORDINATOR_OWNER_TIME_SYNC,
    INK_WIFI_COORDINATOR_OWNER_WIFI_SETUP,
    INK_WIFI_COORDINATOR_OWNER_VOICE_TAG_ASR,
    INK_WIFI_COORDINATOR_OWNER_VOICE_NOTE_ASR,
} ink_wifi_coordinator_owner_t;
```

- [ ] **Step 2: Add a failing retry-path self-test**

Extend `voice_note_service_self_test()` in `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`:

```c
    snprintf(s_active_note.id, sizeof(s_active_note.id), "%s", "retry_target");
    if (voice_note_service_retry_note("retry_target", 8000U)) {
        return false;
    }
```

This should fail until the store-backed retry path exists.

- [ ] **Step 3: Add helpers that move a saved note through Wi-Fi + ASR**

Add to `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`:

```c
#include "ink_wifi_coordinator.h"

static bool ensure_wifi_for_asr(void)
{
    ink_wifi_coordinator_request_t request = {
        .type = INK_WIFI_COORDINATOR_REQUEST_ENSURE_CONNECTED,
        .owner = INK_WIFI_COORDINATOR_OWNER_VOICE_NOTE_ASR,
        .keep_alive = true,
        .best_effort_saved = true,
        .timeout_ms = 12000,
    };
    ink_wifi_coordinator_result_t result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    return ink_wifi_coordinator_request(&request, &result) == ESP_OK
        && result == INK_WIFI_COORDINATOR_RESULT_OK;
}

static void release_wifi_for_asr(void)
{
    (void)ink_wifi_coordinator_release_owner(
        INK_WIFI_COORDINATOR_OWNER_VOICE_NOTE_ASR,
        3000U);
}

static void mark_failed_note(const char *error_text)
{
    s_active_note.transcript_state = VOICE_NOTE_TRANSCRIPT_FAILED;
    s_active_note.updated_at_epoch_s++;
    snprintf(s_active_note.last_error, sizeof(s_active_note.last_error), "%s", error_text);
    s_active_note.text[0] = '\0';
    voice_note_model_build_title("", s_active_note.transcript_state, s_active_note.title, sizeof(s_active_note.title));
    (void)voice_note_store_update_note(&s_active_note);
}
```

- [ ] **Step 4: Replace the “local success” stub with real ASR flow**

Replace the packaging branch inside `voice_note_service_tick()`:

```c
        s_snapshot.state = VOICE_NOTE_JOB_WIFI_CONNECTING;
        voice_note_model_status_copy_for_job(s_snapshot.state, s_snapshot.status_text, sizeof(s_snapshot.status_text));
        if (!ensure_wifi_for_asr()) {
            mark_failed_note("wifi_failed");
            s_snapshot.state = VOICE_NOTE_JOB_FAILED;
            snprintf(s_snapshot.status_text, sizeof(s_snapshot.status_text), "%s", "WiFi 连接失败");
            s_snapshot.busy = false;
            s_snapshot.transient_until_ms = now_ms + 1500U;
            return true;
        }
        s_snapshot.state = VOICE_NOTE_JOB_UPLOADING;
        voice_note_model_status_copy_for_job(s_snapshot.state, s_snapshot.status_text, sizeof(s_snapshot.status_text));
        {
            char transcript[VOICE_NOTE_TEXT_LENGTH];
            voice_note_asr_result_t asr_result = voice_note_asr_post_wav(
                s_wav_buffer,
                s_snapshot.wav_bytes,
                transcript,
                sizeof(transcript));
            release_wifi_for_asr();
            if (asr_result != VOICE_NOTE_ASR_OK) {
                mark_failed_note("asr_failed");
                s_snapshot.state = VOICE_NOTE_JOB_FAILED;
                snprintf(s_snapshot.status_text, sizeof(s_snapshot.status_text), "%s", "识别服务失败");
                s_snapshot.busy = false;
                s_snapshot.transient_until_ms = now_ms + 1500U;
                return true;
            }
            s_snapshot.state = VOICE_NOTE_JOB_PERSISTING_RESULT;
            s_active_note.transcript_state = VOICE_NOTE_TRANSCRIPT_READY;
            s_active_note.updated_at_epoch_s = now_ms / 1000U;
            snprintf(s_active_note.text, sizeof(s_active_note.text), "%s", transcript);
            voice_note_model_build_title(
                s_active_note.text,
                s_active_note.transcript_state,
                s_active_note.title,
                sizeof(s_active_note.title));
            (void)voice_note_store_update_note(&s_active_note);
            s_snapshot.state = VOICE_NOTE_JOB_COMPLETED;
            s_snapshot.busy = false;
            voice_note_model_status_copy_for_job(s_snapshot.state, s_snapshot.status_text, sizeof(s_snapshot.status_text));
            s_snapshot.transient_until_ms = now_ms + 1500U;
            return true;
        }
```

- [ ] **Step 5: Implement store-backed retry**

Replace `voice_note_service_retry_note()`:

```c
bool voice_note_service_retry_note(const char *note_id, uint32_t now_ms)
{
    FILE *fp = NULL;
    long wav_size = 0L;

    if (note_id == NULL || s_snapshot.busy || !voice_note_store_find_note(note_id, &s_active_note)) {
        return false;
    }
    fp = fopen(s_active_note.wav_path, "rb");
    if (fp == NULL) {
        return false;
    }
    fseek(fp, 0L, SEEK_END);
    wav_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    if (wav_size <= 0L || (size_t)wav_size > s_wav_capacity) {
        fclose(fp);
        return false;
    }
    fread(s_wav_buffer, 1U, (size_t)wav_size, fp);
    fclose(fp);
    s_snapshot.busy = true;
    s_snapshot.sequence++;
    s_snapshot.started_ms = now_ms;
    s_snapshot.wav_bytes = (size_t)wav_size;
    s_snapshot.state = VOICE_NOTE_JOB_WIFI_CONNECTING;
    snprintf(s_snapshot.active_note_id, sizeof(s_snapshot.active_note_id), "%s", note_id);
    voice_note_model_status_copy_for_job(s_snapshot.state, s_snapshot.status_text, sizeof(s_snapshot.status_text));
    return true;
}
```

- [ ] **Step 6: Re-run the build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- service and coordinator compile with the new `VOICE_NOTE_ASR` lease owner

- [ ] **Step 7: Commit the Wi-Fi + ASR path**

Run:

```powershell
git add components/ink_net/ink_wifi_coordinator.h main/voice_note/voice_note_service.c main/voice_note/voice_note_asr.c main/voice_note/voice_note_store.c
git commit -m "feat: integrate voice note ASR through wifi coordinator"
```

Expected:
- commit succeeds

### Task 8: Implement app interaction rules, tabs, popup actions, and service polling

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.h`
- Modify: `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.c`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.h`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`

- [ ] **Step 1: Add a failing app self-test for default tab and popup behavior**

Extend `ink_voice_note_app_self_test()` in `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.c`:

```c
    ink_voice_note_app_state_t *state = &s_voice_note_state;
    ink_app_event_t event = {.kind = INK_APP_EVENT_NAV_NEXT, .event_ms = 100U};
    voice_note_enter(&runtime, app);
    if (state->active_tab != VOICE_NOTE_TAB_PENDING) {
        return false;
    }
    if (!app->input(&runtime, app, &event)) {
        return false;
    }
    if (state->active_tab != VOICE_NOTE_TAB_DONE) {
        return false;
    }
```

- [ ] **Step 2: Expand the app state to hold visible notes and selection**

Modify `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.h`:

```c
typedef struct {
    voice_note_tab_t active_tab;
    size_t selected_index;
    bool popup_open;
    uint8_t popup_action_index;
    bool full_text_open;
    size_t visible_note_count;
    voice_note_note_t visible_notes[VOICE_NOTE_MAX_NOTES];
    voice_note_service_snapshot_t snapshot;
} ink_voice_note_app_state_t;
```

- [ ] **Step 3: Add refresh helpers and service polling**

Add to `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.c`:

```c
static void refresh_visible_notes(ink_voice_note_app_state_t *state)
{
    if (state == NULL) {
        return;
    }
    (void)voice_note_service_copy_note_summaries(
        state->active_tab,
        state->visible_notes,
        VOICE_NOTE_MAX_NOTES,
        &state->visible_note_count);
    if (state->selected_index > state->visible_note_count) {
        state->selected_index = state->visible_note_count;
    }
}

static bool current_selection_is_new_card(const ink_voice_note_app_state_t *state)
{
    return state != NULL
        && (state->active_tab == VOICE_NOTE_TAB_ALL || state->active_tab == VOICE_NOTE_TAB_PENDING)
        && state->selected_index == 0U;
}
```

Update `voice_note_enter()`:

```c
    refresh_visible_notes(state);
```

Update `voice_note_tick()`:

```c
    voice_note_service_snapshot_t snapshot;
    bool dirty = voice_note_service_tick(now_ms);
    if (voice_note_service_get_snapshot(&snapshot) && memcmp(&snapshot, &state->snapshot, sizeof(snapshot)) != 0) {
        state->snapshot = snapshot;
        refresh_visible_notes(state);
        dirty = true;
    }
    return dirty;
```

- [ ] **Step 4: Implement button handling for tabs, recording, popup, retry, delete, and done toggle**

Replace `voice_note_input()` in `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.c`:

```c
static bool voice_note_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    ink_voice_note_app_state_t *state = (ink_voice_note_app_state_t *)app->state;
    const ink_app_descriptor_t *launcher = ink_system_runtime_find_app_by_id(runtime, "launcher");

    if (state == NULL || event == NULL) {
        return false;
    }
    if (state->full_text_open) {
        if (event->kind == INK_APP_EVENT_BUTTON_BACK) {
            state->full_text_open = false;
            return true;
        }
        return false;
    }
    if (state->popup_open) {
        switch (event->kind) {
            case INK_APP_EVENT_NAV_PREVIOUS:
                state->popup_action_index = (uint8_t)((state->popup_action_index + 3U) % 4U);
                return true;
            case INK_APP_EVENT_NAV_NEXT:
                state->popup_action_index = (uint8_t)((state->popup_action_index + 1U) % 4U);
                return true;
            case INK_APP_EVENT_BUTTON_BACK:
                state->popup_open = false;
                return true;
            case INK_APP_EVENT_BUTTON_CONFIRM:
                if (state->selected_index == 0U || state->selected_index - 1U >= state->visible_note_count) {
                    return false;
                }
                switch (state->popup_action_index) {
                    case 0:
                        state->full_text_open = true;
                        state->popup_open = false;
                        return true;
                    case 1:
                        state->popup_open = false;
                        return voice_note_service_retry_note(state->visible_notes[state->selected_index - 1U].id, event->event_ms);
                    case 2:
                        state->popup_open = false;
                        return voice_note_service_set_note_status(
                            state->visible_notes[state->selected_index - 1U].id,
                            state->visible_notes[state->selected_index - 1U].status == VOICE_NOTE_STATUS_PENDING
                                ? VOICE_NOTE_STATUS_DONE
                                : VOICE_NOTE_STATUS_PENDING);
                    case 3:
                        state->popup_open = false;
                        return voice_note_service_delete_note(state->visible_notes[state->selected_index - 1U].id);
                    default:
                        return false;
                }
            default:
                return false;
        }
    }
    switch (event->kind) {
        case INK_APP_EVENT_BUTTON_BACK:
            return launcher != NULL && ink_system_runtime_request_switch(runtime, launcher);
        case INK_APP_EVENT_NAV_PREVIOUS:
            if (state->active_tab == VOICE_NOTE_TAB_ALL) {
                state->active_tab = VOICE_NOTE_TAB_DONE;
            } else {
                state->active_tab = (voice_note_tab_t)(state->active_tab - 1);
            }
            refresh_visible_notes(state);
            state->selected_index = 0U;
            return true;
        case INK_APP_EVENT_NAV_NEXT:
            state->active_tab = (voice_note_tab_t)((state->active_tab + 1U) % VOICE_NOTE_TAB_COUNT);
            refresh_visible_notes(state);
            state->selected_index = 0U;
            return true;
        case INK_APP_EVENT_BUTTON_CONFIRM:
            if (current_selection_is_new_card(state)) {
                return voice_note_service_start_capture(event->event_ms);
            }
            if (state->selected_index > 0U && state->selected_index - 1U < state->visible_note_count) {
                state->popup_open = true;
                state->popup_action_index = 0U;
                return true;
            }
            return false;
        case INK_APP_EVENT_BUTTON_SNAPSHOT:
            {
                const ink_button_snapshot_t *snapshot = (const ink_button_snapshot_t *)event->payload;
                const uint8_t confirm_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_CONFIRM);
                const bool confirm_released = snapshot != NULL && (snapshot->released_mask & confirm_mask) != 0U;
                if (current_selection_is_new_card(state) && confirm_released) {
                    return voice_note_service_stop_capture(event->event_ms);
                }
            }
            return false;
        default:
            return false;
    }
}
```

- [ ] **Step 5: Re-run the build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- the voice note app now drives the service through normal app events

- [ ] **Step 6: Commit the app interaction rules**

Run:

```powershell
git add main/apps/ink_voice_note_app.h main/apps/ink_voice_note_app.c main/voice_note/voice_note_service.h main/voice_note/voice_note_service.c
git commit -m "feat: implement voice note app interactions"
```

Expected:
- commit succeeds

### Task 9: Add render-mode support and draw the voice note page in the main render pipeline

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\ink_app_render.c`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_app_render.h`
- Modify: `D:\FUCKIDF\ink-reader\main\apps\ink_launcher_app.c`

- [ ] **Step 1: Add a failing render self-test for the new render mode**

In `D:\FUCKIDF\ink-reader\main\ink_app_render.c`, declare:

```c
static bool app_render_model_voice_note_self_test(void);
```

and add it to `ink_app_render_self_test()` before implementing the drawing code:

```c
    if (!app_render_model_voice_note_self_test()) {
        printf("FAIL render model_voice_note\n");
        return false;
    }
```

- [ ] **Step 2: Add the new app include and fill function declaration**

At the top of `D:\FUCKIDF\ink-reader\main\ink_app_render.c`, add:

```c
#include "apps/ink_voice_note_app.h"
```

and declare:

```c
static void fill_voice_note_page(
    uint8_t *buffer,
    size_t length,
    const ink_voice_note_app_render_state_t *state);
```

- [ ] **Step 3: Add a simple first-pass voice note renderer**

Add to `D:\FUCKIDF\ink-reader\main\ink_app_render.c`:

```c
static void fill_voice_note_page(
    uint8_t *buffer,
    size_t length,
    const ink_voice_note_app_render_state_t *state)
{
    char line[128];
    const ink_cpfont_t *title_font = state != NULL ? state->reader_font : NULL;
    const ink_cpfont_t *meta_font = state != NULL ? state->footer_font : NULL;
    const ink_cpfont_t *body_font = state != NULL ? state->menu_font : NULL;

    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }
    memset(buffer, 0xFF, length);
    launcher_draw_text(buffer, title_font, 20, 16, "语音便签", 1U, false);
    if (state != NULL) {
        launcher_draw_text(buffer, meta_font, 360, 16, state->header_meta, 2U, false);
        snprintf(line, sizeof(line), "状态: %s", state->state->snapshot.status_text);
        launcher_draw_text(buffer, body_font, 20, 56, line, 2U, false);
        snprintf(line, sizeof(line), "页签: %s", state->state->active_tab == VOICE_NOTE_TAB_ALL ? "全部" : (state->state->active_tab == VOICE_NOTE_TAB_PENDING ? "未完成" : "已完成"));
        launcher_draw_text(buffer, body_font, 20, 84, line, 2U, false);
        launcher_draw_text(buffer, body_font, 20, 124, "新建语音标签", 1U, false);
        launcher_draw_text(buffer, body_font, 20, 152, state->state->snapshot.status_text, 2U, false);
    }
}
```

- [ ] **Step 4: Route the new render mode through `render_model_to_buffer()` and cover it with a self-test**

Inside the render-mode switch in `D:\FUCKIDF\ink-reader\main\ink_app_render.c`, add:

```c
        case INK_APP_RENDER_MODE_VOICE_NOTE:
            fill_voice_note_page(buffer, length, (const ink_voice_note_app_render_state_t *)model->state);
            return true;
```

Add the self-test body:

```c
static bool app_render_model_voice_note_self_test(void)
{
    uint8_t *buffer = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    ink_app_render_model_t model;
    ink_voice_note_app_state_t state;
    ink_voice_note_app_render_state_t render_state;
    bool ok = false;

    if (buffer == NULL) {
        return false;
    }
    memset(buffer, 0xAA, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(&model, 0, sizeof(model));
    memset(&state, 0, sizeof(state));
    memset(&render_state, 0, sizeof(render_state));
    state.active_tab = VOICE_NOTE_TAB_PENDING;
    snprintf(state.snapshot.status_text, sizeof(state.snapshot.status_text), "%s", "按住 Confirm 开始录音");
    snprintf(render_state.header_meta, sizeof(render_state.header_meta), "%s", "12:34");
    render_state.state = &state;
    model.mode = INK_APP_RENDER_MODE_VOICE_NOTE;
    model.state = &render_state;
    ok = render_model_to_buffer(buffer, EPD_GDEY0426T82_BUFFER_SIZE, &model)
        && render_buffer_has_ink(buffer, EPD_GDEY0426T82_BUFFER_SIZE)
        && render_pixel_is_black(buffer, 24, 20)
        && render_pixel_is_black(buffer, 360, 16);
    free(buffer);
    return ok;
}
```

- [ ] **Step 5: Add a launcher icon branch for the new app row**

In `D:\FUCKIDF\ink-reader\main\ink_app_render.c`, extend `launcher_draw_row_icon()`:

```c
    switch (index) {
        case 0:
            launcher_draw_book_icon(buffer, x, y, inverted);
            break;
        case 1:
            launcher_draw_wifi_icon(buffer, x, y, inverted);
            break;
        case 2:
            launcher_draw_wifi_icon(buffer, x, y, inverted);
            break;
        case 3:
            launcher_draw_photo_icon(buffer, x, y, inverted);
            break;
        case 4:
            launcher_draw_cal_icon(buffer, x, y, inverted);
            break;
        case 5:
            launcher_draw_usb_icon(buffer, x, y, inverted);
            break;
        default:
            launcher_draw_book_icon(buffer, x, y, inverted);
            break;
    }
```

For the first pass, reuse the Wi-Fi icon slot for voice notes so the app is visible without inventing a whole new icon family mid-task.

- [ ] **Step 6: Re-run the build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- render self-tests include the new voice note render mode

- [ ] **Step 7: Commit the voice note renderer**

Run:

```powershell
git add main/ink_app_render.c main/ink_app_render.h main/apps/ink_launcher_app.c
git commit -m "feat: render voice note app page"
```

Expected:
- commit succeeds

### Task 10: Integrate boot self-tests, storage readiness checks, and hardware validation

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`
- Modify: `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.c`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_service.c`
- Modify: `D:\FUCKIDF\ink-reader\main\voice_note\voice_note_store.c`

- [ ] **Step 1: Add store/model/audio self-tests to boot coverage**

In `D:\FUCKIDF\ink-reader\main\app_main.c`, inside `run_boot_self_tests()` add:

```c
    esp_rom_printf("ST voice_note\n");
    ESP_ERROR_CHECK(voice_note_model_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_store_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_audio_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_capture_logic_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_asr_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(voice_note_service_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_voice_note_app_self_test() ? ESP_OK : ESP_FAIL);
    esp_rom_printf("OK voice_note\n");
```

- [ ] **Step 2: Gate the app against missing TF storage**

In `D:\FUCKIDF\ink-reader\main\apps\ink_voice_note_app.c`, update `voice_note_enter()`:

```c
    if (services == NULL || !services->tf_ready) {
        memset(state, 0, sizeof(*state));
        state->active_tab = VOICE_NOTE_TAB_PENDING;
        state->snapshot.state = VOICE_NOTE_JOB_FAILED;
        snprintf(state->snapshot.status_text, sizeof(state->snapshot.status_text), "%s", "存储失败");
        return;
    }
```

- [ ] **Step 3: Add a quick service sanity self-test for TF-less initialization failure handling**

Extend `voice_note_service_self_test()`:

```c
    if (!voice_note_service_get_snapshot(&snapshot)) {
        return false;
    }
```

and make sure initialization returns `ESP_ERR_NO_MEM` only on real allocation failure, not on empty storage directories.

- [ ] **Step 4: Run the full build**

Run:

```powershell
idf.py build
```

Expected:
- full mainline build succeeds

- [ ] **Step 5: Flash and manually validate the full voice note loop**

Run:

```powershell
idf.py -p COM9 flash monitor
```

Manual validation on hardware:
- Launcher shows the new `voice_note` app
- entering the app lands on `未完成`
- holding `Confirm` shows `正在录音`
- releasing after less than 2 seconds shows `无效标签，请重新录入`
- after 5 seconds of no input the new-note card returns to `按住 Confirm 开始录音`
- releasing after more than 2 seconds persists WAV and then connects Wi-Fi
- successful ASR creates a note
- failed Wi-Fi or ASR still creates a retryable failed note with title `这是一条语音标签`
- reboot preserves notes
- popup actions can mark done, retry, and delete

- [ ] **Step 6: Commit verification-driven fixes if needed**

Run:

```powershell
git add main/app_main.c main/apps/ink_voice_note_app.c main/voice_note/voice_note_service.c main/voice_note/voice_note_store.c
git commit -m "test: finalize voice note app verification fixes"
```

Expected:
- commit succeeds only if manual validation required small follow-up fixes

## Spec Coverage Check

- New mainline launcher app: covered by Tasks 1, 8, and 9.
- `全部 / 未完成 / 已完成` tabs: covered by Task 8.
- New-note card status copy inside the card: covered by Tasks 2, 6, and 9.
- Hold-to-record with short-recording invalidation: covered by Tasks 5, 6, and 8.
- TF-card persistence for JSON + WAV: covered by Tasks 2, 3, and 6.
- Failed notes preserved with retryable WAV: covered by Tasks 3, 6, and 7.
- Retry recognition from saved WAV: covered by Tasks 7 and 8.
- Wi-Fi only through coordinator: covered by Task 7.
- Reboot durability and interrupted-note recovery: covered by Task 3 and Task 10.
- Playback deferred to the next version: preserved by not adding any NS4168 output task.

## Placeholder Scan

- No `TODO`, `TBD`, or “implement later” placeholders remain in the plan.
- Wi-Fi credentials are intentionally excluded from mainline config because the app uses saved credentials through the coordinator; only ASR API config is added.
- The first-pass launcher icon reuses an existing icon branch on purpose to keep this slice small.

## Type Consistency Check

- All app-visible note data uses `voice_note_note_t`.
- All service-visible transient status uses `voice_note_service_snapshot_t`.
- All job-state copy uses `voice_note_job_state_t` plus `voice_note_model_status_copy_for_job()`.
- All storage path generation uses `voice_note_model_build_note_paths()`.
- All Wi-Fi access uses `INK_WIFI_COORDINATOR_OWNER_VOICE_NOTE_ASR`.

Plan complete and saved to `docs/superpowers/plans/2026-06-29-voice-note-app.md`. Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
