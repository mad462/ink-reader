# Single-Firmware App Launcher Stage 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Stage 1 single-firmware app runtime that boots into Launcher, supports switching to a placeholder Reader app and a real WiFi Setup app, and keeps display/input flow system-owned and non-blocking.

**Architecture:** Keep the proven `InputTask -> UiTask -> EpdTask -> ink_display_mailbox` execution model, but move page ownership into a new runtime with registered apps and shared services. Launcher, Reader placeholder, and WiFi Setup become explicit apps; display, input, storage, fonts, and WiFi remain runtime-owned services.

**Tech Stack:** ESP-IDF 5.5.4, FreeRTOS tasks/queues, existing `ink_display_mailbox`, existing `ink_wifi_manager`, existing GDEY0426T82 driver, existing button input path, reused `tests/tilt_grid` WiFi setup logic.

---

### Task 1: App Interface And Runtime Skeleton

**Files:**
- Create: `main/apps/ink_app_iface.h`
- Create: `main/ink_system_runtime.h`
- Create: `main/ink_system_runtime.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write the failing runtime self-test declarations**

Add a runtime self-test entry point in `main/ink_system_runtime.h`:

```c
#pragma once

#include <stdbool.h>

bool ink_system_runtime_self_test(void);
```

Expected follow-up failure: `main/ink_system_runtime.c` does not exist yet, so the project cannot build with the new header/source added to `main/CMakeLists.txt`.

- [ ] **Step 2: Wire the new runtime source into the build and confirm failure**

Update `main/CMakeLists.txt` to include:

```cmake
        "ink_system_runtime.c"
```

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: FAIL because `main/ink_system_runtime.c` and `main/apps/ink_app_iface.h` are missing.

- [ ] **Step 3: Add the minimal app interface and runtime types**

Create `main/apps/ink_app_iface.h` with the Stage 1 contract:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INK_APP_EVENT_NONE = 0,
    INK_APP_EVENT_BUTTON_BACK,
    INK_APP_EVENT_BUTTON_CONFIRM,
    INK_APP_EVENT_NAV_PREVIOUS,
    INK_APP_EVENT_NAV_NEXT,
    INK_APP_EVENT_TILT_PREVIOUS,
    INK_APP_EVENT_TILT_NEXT,
    INK_APP_EVENT_DISPLAY_DONE,
    INK_APP_EVENT_TICK,
    INK_APP_EVENT_WIFI_WORK_DONE,
} ink_app_event_kind_t;

typedef struct {
    ink_app_event_kind_t kind;
    uint32_t event_ms;
    int32_t arg0;
    int32_t arg1;
    void *payload;
} ink_app_event_t;

typedef enum {
    INK_APP_RENDER_MODE_NONE = 0,
    INK_APP_RENDER_MODE_LAUNCHER,
    INK_APP_RENDER_MODE_READER_PLACEHOLDER,
    INK_APP_RENDER_MODE_WIFI_SETUP,
} ink_app_render_mode_t;

typedef struct {
    ink_app_render_mode_t mode;
    bool request_full_refresh;
    bool request_partial_refresh;
    int partial_x;
    int partial_y;
    int partial_w;
    int partial_h;
    void *state;
} ink_app_render_model_t;

struct ink_system_services;
struct ink_system_runtime;

typedef struct ink_app_descriptor {
    const char *id;
    const char *name;
    const void *icon;
    void (*enter)(struct ink_system_runtime *runtime, const struct ink_app_descriptor *app);
    void (*exit)(struct ink_system_runtime *runtime, const struct ink_app_descriptor *app);
    bool (*input)(struct ink_system_runtime *runtime, const struct ink_app_descriptor *app, const ink_app_event_t *event);
    bool (*tick)(struct ink_system_runtime *runtime, const struct ink_app_descriptor *app, uint32_t now_ms);
    bool (*render)(struct ink_system_runtime *runtime, const struct ink_app_descriptor *app, ink_app_render_model_t *out_model);
    void *state;
} ink_app_descriptor_t;
```

Create `main/ink_system_runtime.c` and `main/ink_system_runtime.h` with a minimal runtime model and a self-test that verifies:

```c
typedef struct ink_system_runtime {
    const ink_app_descriptor_t *apps[4];
    size_t app_count;
    const ink_app_descriptor_t *active_app;
    const ink_app_descriptor_t *pending_app;
    bool force_full_refresh_on_next_render;
} ink_system_runtime_t;
```

Self-test requirements:
- register two fake apps
- set the first one active
- request switch to the second one
- assert `pending_app` is updated and `force_full_refresh_on_next_render` is set

- [ ] **Step 4: Run the build to confirm the runtime skeleton passes**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: PASS for the new runtime skeleton compile path.

- [ ] **Step 5: Commit**

```powershell
git add main/CMakeLists.txt main/apps/ink_app_iface.h main/ink_system_runtime.h main/ink_system_runtime.c
git commit -m "feat: add app runtime skeleton"
```

### Task 2: Shared Services Extraction

**Files:**
- Create: `main/ink_system_services.h`
- Create: `main/ink_system_services.c`
- Modify: `main/ink_app_boot.h`
- Modify: `main/ink_app_startup.c`
- Modify: `main/ink_app_priv.h`

- [ ] **Step 1: Write the failing shared-services self-test**

Declare the new service bundle API in `main/ink_system_services.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ink_display_mailbox.h"
#include "ink_cpfont.h"
#include "ink_wifi_manager.h"

typedef struct ink_system_services {
    QueueHandle_t ui_queue;
    ink_display_mailbox_t mailbox;
    uint8_t *framebuffer;
    uint8_t *previous_framebuffer;
    uint8_t *bitmap_snapshot_a;
    uint8_t *bitmap_snapshot_b;
    uint8_t *native_snapshot_a;
    uint8_t *native_snapshot_b;
    ink_cpfont_t menu_font;
    ink_cpfont_t footer_font;
    ink_cpfont_t reader_font;
    bool tf_ready;
} ink_system_services_t;

bool ink_system_services_self_test(void);
```

Expected follow-up failure: implementation file not present yet.

- [ ] **Step 2: Add the source and verify the build fails before implementation**

Update `main/CMakeLists.txt` to include:

```cmake
        "ink_system_services.c"
```

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: FAIL because `main/ink_system_services.c` is missing.

- [ ] **Step 3: Implement the service bundle with minimal extraction**

Create `main/ink_system_services.c` so it:
- allocates display buffers by calling `ink_app_alloc_display_buffer(...)`
- initializes `ink_display_mailbox`
- loads fonts via `ink_app_load_reader_font`, `ink_app_load_footer_font`, `ink_app_load_menu_font`
- mounts SD card via `ink_app_mount_tf_card`
- creates the UI queue

Add a self-test that:
- zeros a local `ink_system_services_t`
- verifies `ink_system_services_reset(...)` sets queue and pointers to `NULL`
- verifies `ink_system_services_init_mailbox_only(...)` stores the snapshot buffers in the mailbox

Add helper declarations to `main/ink_app_boot.h` only if needed; do not move reader or UI logic into the service layer.

- [ ] **Step 4: Update old context structs to reference services cleanly**

Modify `main/ink_app_priv.h` so the old monolithic app context no longer owns duplicate mailbox or font fields that the runtime will move under `ink_system_services_t`.

Minimum target:
- keep reader and tuning state where still needed
- remove or de-emphasize fields that are now system-owned services

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: PASS with the shared service bundle compiled in.

- [ ] **Step 5: Commit**

```powershell
git add main/CMakeLists.txt main/ink_system_services.h main/ink_system_services.c main/ink_app_boot.h main/ink_app_startup.c main/ink_app_priv.h
git commit -m "refactor: extract shared system services"
```

### Task 3: Launcher And Reader Placeholder Apps

**Files:**
- Create: `main/apps/ink_launcher_app.h`
- Create: `main/apps/ink_launcher_app.c`
- Create: `main/apps/ink_reader_app.h`
- Create: `main/apps/ink_reader_app.c`
- Modify: `main/ink_system_runtime.c`
- Modify: `main/ink_app_render.c`

- [ ] **Step 1: Write failing self-tests for Launcher selection and Reader back navigation**

Add self-tests inside the new app source files:

Launcher expected tests:
- boot selection starts at `Reader`
- nav next selects `WiFi Setup`
- nav previous wraps back to `Reader`
- confirm requests switch to the selected app

Reader expected tests:
- entering the app requests a full refresh on first render
- Back requests switch to `launcher`

Expected initial failure: app source files do not exist yet.

- [ ] **Step 2: Run the build to confirm it fails before implementation**

Update `main/CMakeLists.txt` to include:

```cmake
        "apps/ink_launcher_app.c"
        "apps/ink_reader_app.c"
```

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: FAIL because the new app files are missing.

- [ ] **Step 3: Implement Launcher app state and behavior**

Create `main/apps/ink_launcher_app.c` with:
- a state struct holding selected app index
- `enter()` that resets selection to index `0`
- `input()` that handles:
  - `INK_APP_EVENT_NAV_PREVIOUS`
  - `INK_APP_EVENT_NAV_NEXT`
  - `INK_APP_EVENT_TILT_PREVIOUS`
  - `INK_APP_EVENT_TILT_NEXT`
  - `INK_APP_EVENT_BUTTON_CONFIRM`
- `render()` that emits:

```c
out_model->mode = INK_APP_RENDER_MODE_LAUNCHER;
out_model->request_full_refresh = runtime->force_full_refresh_on_next_render;
out_model->state = state;
```

Use app ordering:
- index `0` -> `reader`
- index `1` -> `wifi_setup`

- [ ] **Step 4: Implement Reader placeholder app**

Create `main/apps/ink_reader_app.c` with:
- `enter()` that marks the first frame as full refresh
- `input()` that switches to `launcher` on `INK_APP_EVENT_BUTTON_BACK`
- `render()` that emits:

```c
out_model->mode = INK_APP_RENDER_MODE_READER_PLACEHOLDER;
out_model->request_full_refresh = runtime->force_full_refresh_on_next_render;
out_model->state = app->state;
```

- [ ] **Step 5: Extend render backend to draw Launcher and Reader placeholder**

Modify `main/ink_app_render.c` to add simple draw helpers:
- `fill_launcher_page(...)`
- `fill_reader_placeholder_page(...)`

The Launcher render should show:
- title `Launcher`
- `Reader`
- `WiFi Setup`
- a visible selection marker

The Reader placeholder should show:
- `Reader App`
- `Phase 1`
- `Back to Launcher`

For this task, full refresh on enter is acceptable; no WiFi logic yet.

- [ ] **Step 6: Build and confirm Launcher/Reader app render path works**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: PASS with Launcher and Reader placeholder compile paths wired in.

- [ ] **Step 7: Commit**

```powershell
git add main/CMakeLists.txt main/apps/ink_launcher_app.h main/apps/ink_launcher_app.c main/apps/ink_reader_app.h main/apps/ink_reader_app.c main/ink_system_runtime.c main/ink_app_render.c
git commit -m "feat: add launcher and reader placeholder apps"
```

### Task 4: Reusable WiFi Setup Logic Extraction

**Files:**
- Create: `main/wifi_setup/ink_wifi_setup_state.h`
- Create: `main/wifi_setup/ink_wifi_setup_state.c`
- Create: `main/wifi_setup/ink_wifi_setup_render.h`
- Create: `main/wifi_setup/ink_wifi_setup_render.c`
- Create: `main/wifi_setup/ink_wifi_setup_input.h`
- Create: `main/wifi_setup/ink_wifi_setup_input.c`
- Modify: `main/CMakeLists.txt`
- Modify: `tests/tilt_grid/main/CMakeLists.txt`

- [ ] **Step 1: Copy the existing WiFi setup unit tests into the main firmware path and make them fail first**

Create `main/test/test_ink_wifi_setup_state.c` by porting the assertions from `tests/tilt_grid/main/test/test_tilt_wifi_setup.c`.

Adjust includes to:

```c
#include "ink_wifi_setup_state.h"
```

Add the new sources to `main/CMakeLists.txt` before implementation:

```cmake
        "wifi_setup/ink_wifi_setup_state.c"
        "wifi_setup/ink_wifi_setup_input.c"
        "wifi_setup/ink_wifi_setup_render.c"
```

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: FAIL because the new WiFi setup files are not created yet.

- [ ] **Step 2: Extract the pure WiFi setup state machine**

Create `main/wifi_setup/ink_wifi_setup_state.[hc]` by moving the pure logic from:
- `tests/tilt_grid/main/tilt_wifi_setup.h`
- `tests/tilt_grid/main/tilt_wifi_setup.c`

Rename types from `wifi_setup_state_t` only if needed for consistency, but keep the behavior identical to preserve the tested flow.

Required exported behaviors:
- selectable count
- strongest selection
- selection cycling
- saved-menu open
- password open
- popup cancel
- connecting begin
- connecting finish
- result confirm

- [ ] **Step 3: Extract the input helper path used by WiFi setup**

Create `main/wifi_setup/ink_wifi_setup_input.[hc]` by moving or wrapping the keyboard and tilt-selection helpers that are not panel-specific.

This module should own:
- keyboard text state
- label activation logic
- selection normalization for the 12x5 grid
- request preparation for scan/connect/delete

It should not own:
- panel refresh
- hardware initialization
- FreeRTOS task creation

- [ ] **Step 4: Extract the render decision helper for partial fallback**

Create `main/wifi_setup/ink_wifi_setup_render.[hc]` and move the pure helper from `tests/tilt_grid/main/tilt_display_refresh.c`:

```c
bool ink_wifi_setup_partial_error_should_full_refresh(esp_err_t error);
```

Port the `tests/tilt_grid/main/test/test_tilt_display_refresh.c` assertions into `main/test/test_ink_wifi_setup_render.c`.

- [ ] **Step 5: Repoint the tilt-grid test app to the extracted shared WiFi setup modules**

Modify `tests/tilt_grid/main/CMakeLists.txt` so it uses the extracted shared WiFi setup sources instead of keeping a second diverging implementation.

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py -C tests/tilt_grid build
```

Expected: PASS, proving the extraction did not break the existing test app build.

- [ ] **Step 6: Commit**

```powershell
git add main/CMakeLists.txt main/test/test_ink_wifi_setup_state.c main/test/test_ink_wifi_setup_render.c main/wifi_setup/ink_wifi_setup_state.h main/wifi_setup/ink_wifi_setup_state.c main/wifi_setup/ink_wifi_setup_input.h main/wifi_setup/ink_wifi_setup_input.c main/wifi_setup/ink_wifi_setup_render.h main/wifi_setup/ink_wifi_setup_render.c tests/tilt_grid/main/CMakeLists.txt
git commit -m "refactor: extract reusable wifi setup modules"
```

### Task 5: WiFi Setup App Integration

**Files:**
- Create: `main/apps/ink_wifi_setup_app.h`
- Create: `main/apps/ink_wifi_setup_app.c`
- Modify: `main/ink_system_runtime.h`
- Modify: `main/ink_system_runtime.c`
- Modify: `main/ink_app_render.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write failing WiFi app self-tests**

Add self-tests for:
- entering WiFi setup requests an initial full refresh
- Back from list view returns to Launcher
- scan completion updates the app state without blocking the UI task
- inactive-app completion events are ignored after app exit

Expected failure: app source missing.

- [ ] **Step 2: Add the app source and verify the build fails before implementation**

Update `main/CMakeLists.txt` to include:

```cmake
        "apps/ink_wifi_setup_app.c"
```

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: FAIL because `main/apps/ink_wifi_setup_app.c` does not exist yet.

- [ ] **Step 3: Implement app-local WiFi setup state and worker queue**

Create `main/apps/ink_wifi_setup_app.c` with app-local state containing:
- extracted WiFi setup state
- keyboard text state
- tilt grid input state
- a worker queue handle
- a generation counter or active-token used to ignore stale worker completions
- selection and partial-region bookkeeping needed for bounded refresh

Provide:
- `enter()` that resets transient popup state, kicks off a scan request, and requests full refresh
- `exit()` that increments the active token and clears transient UI state
- `tick()` that consumes worker completions non-blockingly
- `input()` that maps Back, Confirm, nav, and tilt events into WiFi setup state transitions

- [ ] **Step 4: Hook WiFi app rendering into the shared render backend**

Modify `main/ink_app_render.c` so `INK_APP_RENDER_MODE_WIFI_SETUP` uses:
- the extracted WiFi setup render helpers
- the existing fonts from `ink_system_services_t`
- partial refresh requests for:
  - list body
  - list selection
  - keyboard selection
  - password box

Use the existing fallback rule:

```c
if (ink_wifi_setup_partial_error_should_full_refresh(ret)) {
    /* fallback to full refresh */
}
```

- [ ] **Step 5: Register WiFi Setup app in the runtime**

Modify `main/ink_system_runtime.c` so built-in registration order is:
- `launcher`
- `reader`
- `wifi_setup`

The Launcher selection map must target those registered apps, not hard-coded page enums.

- [ ] **Step 6: Build and confirm full app trio compiles**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: PASS with Launcher, Reader, and WiFi Setup apps registered.

- [ ] **Step 7: Commit**

```powershell
git add main/CMakeLists.txt main/apps/ink_wifi_setup_app.h main/apps/ink_wifi_setup_app.c main/ink_system_runtime.h main/ink_system_runtime.c main/ink_app_render.c
git commit -m "feat: integrate wifi setup app into runtime"
```

### Task 6: Switch Semantics, Queue Draining, And app_main Shrink

**Files:**
- Modify: `main/app_main.c`
- Modify: `main/ink_system_runtime.h`
- Modify: `main/ink_system_runtime.c`
- Modify: `components/ink_app_core/ink_display_mailbox.h`
- Modify: `components/ink_app_core/ink_display_mailbox.c`

- [ ] **Step 1: Write failing self-tests for queue drain and display invalidation**

Add tests that prove:
- switching apps drains pending input events
- switching apps invalidates pending display requests
- first render after a switch is forced full refresh

Expected failure: no mailbox invalidation API exists yet.

- [ ] **Step 2: Run the build to verify the new API is missing**

Add declarations to `components/ink_app_core/ink_display_mailbox.h`:

```c
void ink_display_mailbox_invalidate_pending(ink_display_mailbox_t *mailbox);
```

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: FAIL because the new mailbox function is declared but not implemented.

- [ ] **Step 3: Implement mailbox invalidation**

In `components/ink_app_core/ink_display_mailbox.c`, add:

```c
void ink_display_mailbox_invalidate_pending(ink_display_mailbox_t *mailbox)
{
    if (mailbox == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_mailbox_lock);
    mailbox->latest_seq = mailbox->completed_seq;
    mailbox->active_seq = 0U;
    taskEXIT_CRITICAL(&s_mailbox_lock);
}
```

Update the existing self-test to verify that a pending request is no longer claimable after invalidation.

- [ ] **Step 4: Implement runtime queue drain helpers and shrink app_main**

Modify `main/ink_system_runtime.c` to add:
- `ink_system_runtime_drain_ui_queue(...)`
- `ink_system_runtime_switch_now(...)`
- `ink_system_runtime_dispatch_input(...)`
- `ink_system_runtime_handle_display_done(...)`

Modify `main/app_main.c` so it:
- initializes services
- initializes runtime
- creates `InputTask`, `UiTask`, and `EpdTask`
- delegates all app behavior to runtime calls

The resulting `app_main.c` should no longer contain library, reader, or WiFi UI logic branches.

- [ ] **Step 5: Build and verify runtime-centric app_main**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: PASS, with app switching and queue-drain APIs in place.

- [ ] **Step 6: Commit**

```powershell
git add main/app_main.c main/ink_system_runtime.h main/ink_system_runtime.c components/ink_app_core/ink_display_mailbox.h components/ink_app_core/ink_display_mailbox.c
git commit -m "refactor: move app switching into runtime"
```

### Task 7: Hardware Verification On Target

**Files:**
- Modify if needed after validation: `sdkconfig`, `sdkconfig.defaults`, `main/app_main.c`, `main/ink_app_render.c`, `main/apps/ink_wifi_setup_app.c`

- [ ] **Step 1: Build for the target board with the required ESP-IDF version**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py set-target esp32s3
idf.py build
```

Expected: PASS for the main firmware.

- [ ] **Step 2: Flash to the target board on COM9**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py -p COM9 flash
```

Expected: PASS and successful boot image write to the ESP32-S3 with 16 MB flash / 8 MB PSRAM.

- [ ] **Step 3: Capture serial output and verify boot-to-Launcher flow**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py -p COM9 monitor
```

Expected serial evidence:
- system init succeeds
- SD and font init either succeed or log a controlled fallback
- Launcher becomes the active app at boot
- first render is a full refresh

- [ ] **Step 4: Verify app switching on hardware**

Manual hardware checks:
- use keys to select `Reader` and `WiFi Setup`
- Confirm enters the selected app
- Back from Reader returns to Launcher
- Back from WiFi Setup returns to Launcher where supported
- observe that app switches trigger a full refresh
- observe that routine WiFi list and keyboard updates remain partial where already tuned

Expected serial evidence:
- input events logged once per action
- app switch logs show queue drain and mailbox invalidation
- no stale display completion triggers after a switch

- [ ] **Step 5: Fix any hardware-only regressions and re-run build/flash/monitor**

If behavior differs on target:
- patch the minimal runtime/app/render code involved
- rebuild
- reflashing to `COM9`
- repeat monitor verification until the acceptance checks pass

- [ ] **Step 6: Commit**

```powershell
git add sdkconfig sdkconfig.defaults main/app_main.c main/ink_app_render.c main/apps/ink_wifi_setup_app.c
git commit -m "test: verify launcher runtime on hardware"
```
