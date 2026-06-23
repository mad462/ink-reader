# Display Tuning Lab Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the active ebook-oriented runtime with a fixed-page display tuning lab that preserves the existing refresh pipeline, mailbox, and timing logs.

**Architecture:** Keep `input_task`, `ui_task`, `epd_task`, the display mailbox, and the EPD driver unchanged at a structural level. Replace the active UI model, request-building logic, and page rendering inputs with deterministic fixed tuning pages plus selectable refresh profiles so we can compare full, fast, and partial refresh behavior on controlled transitions.

**Tech Stack:** ESP-IDF 5.5.4, ESP32-S3, FreeRTOS, existing `ink_hw`, `ink_app_core`, and self-test based verification in C

---

### Task 1: Add display tuning lab model and self-tests

**Files:**
- Create: `main/ink_tuning_lab.h`
- Create: `main/ink_tuning_lab.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/ink_app_priv.h`
- Test: `main/ink_tuning_lab.c`

- [ ] **Step 1: Write the failing self-test declarations**

Add a new module include and self-test hook in `main/ink_app_priv.h` by defining lab page/profile state that does not depend on ebook state:

```c
typedef enum {
    INK_TUNING_PAGE_TEXT = 0,
    INK_TUNING_PAGE_FOOTER,
    INK_TUNING_PAGE_DETAIL,
    INK_TUNING_PAGE_HIGH_DELTA,
    INK_TUNING_PAGE_COUNT
} ink_tuning_page_t;

typedef enum {
    INK_TUNING_REFRESH_FULL = 0,
    INK_TUNING_REFRESH_FAST_FULL,
    INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY,
    INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER,
    INK_TUNING_REFRESH_CUSTOM_LUT_A,
    INK_TUNING_REFRESH_COUNT
} ink_tuning_refresh_profile_t;

typedef struct {
    ink_tuning_page_t current_page;
    ink_tuning_refresh_profile_t refresh_profile;
    uint32_t render_counter;
    uint32_t consecutive_partial_count;
    bool force_full_refresh;
} ink_tuning_lab_state_t;
```

Expected immediate failure: references to `ink_tuning_lab_state_t` or the new header do not compile until the new module exists.

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL with missing declarations or missing `ink_tuning_lab.h` / `ink_tuning_lab.c`

- [ ] **Step 3: Write minimal implementation**

Create `main/ink_tuning_lab.h` with:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INK_TUNING_PAGE_TEXT = 0,
    INK_TUNING_PAGE_FOOTER,
    INK_TUNING_PAGE_DETAIL,
    INK_TUNING_PAGE_HIGH_DELTA,
    INK_TUNING_PAGE_COUNT
} ink_tuning_page_t;

typedef enum {
    INK_TUNING_REFRESH_FULL = 0,
    INK_TUNING_REFRESH_FAST_FULL,
    INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY,
    INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER,
    INK_TUNING_REFRESH_CUSTOM_LUT_A,
    INK_TUNING_REFRESH_COUNT
} ink_tuning_refresh_profile_t;

typedef struct {
    ink_tuning_page_t current_page;
    ink_tuning_refresh_profile_t refresh_profile;
    uint32_t render_counter;
    uint32_t consecutive_partial_count;
    bool force_full_refresh;
} ink_tuning_lab_state_t;

void ink_tuning_lab_init(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_previous_page(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_next_page(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_cycle_refresh_profile(ink_tuning_lab_state_t *lab);
bool ink_tuning_lab_request_force_full_refresh(ink_tuning_lab_state_t *lab);
const char *ink_tuning_lab_page_name(ink_tuning_page_t page);
const char *ink_tuning_lab_refresh_profile_name(ink_tuning_refresh_profile_t profile);
bool ink_tuning_lab_self_test(void);
```

Create `main/ink_tuning_lab.c` with:

```c
#include "ink_tuning_lab.h"

#include <string.h>

void ink_tuning_lab_init(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL) {
        return;
    }
    memset(lab, 0, sizeof(*lab));
    lab->current_page = INK_TUNING_PAGE_TEXT;
    lab->refresh_profile = INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY;
}

bool ink_tuning_lab_previous_page(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL || lab->current_page == INK_TUNING_PAGE_TEXT) {
        return false;
    }
    lab->current_page = (ink_tuning_page_t)(lab->current_page - 1);
    return true;
}

bool ink_tuning_lab_next_page(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL || lab->current_page >= (INK_TUNING_PAGE_COUNT - 1)) {
        return false;
    }
    lab->current_page = (ink_tuning_page_t)(lab->current_page + 1);
    return true;
}

bool ink_tuning_lab_cycle_refresh_profile(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL) {
        return false;
    }
    lab->refresh_profile = (ink_tuning_refresh_profile_t)((lab->refresh_profile + 1) % INK_TUNING_REFRESH_COUNT);
    return true;
}

bool ink_tuning_lab_request_force_full_refresh(ink_tuning_lab_state_t *lab)
{
    if (lab == NULL) {
        return false;
    }
    lab->force_full_refresh = true;
    return true;
}

const char *ink_tuning_lab_page_name(ink_tuning_page_t page)
{
    switch (page) {
        case INK_TUNING_PAGE_TEXT: return "TEXT";
        case INK_TUNING_PAGE_FOOTER: return "FOOTER";
        case INK_TUNING_PAGE_DETAIL: return "DETAIL";
        case INK_TUNING_PAGE_HIGH_DELTA: return "HIGH_DELTA";
        default: return "UNKNOWN";
    }
}

const char *ink_tuning_lab_refresh_profile_name(ink_tuning_refresh_profile_t profile)
{
    switch (profile) {
        case INK_TUNING_REFRESH_FULL: return "FULL";
        case INK_TUNING_REFRESH_FAST_FULL: return "FAST_FULL";
        case INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY: return "PARTIAL_AUTO";
        case INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER: return "PARTIAL_FOOTER";
        case INK_TUNING_REFRESH_CUSTOM_LUT_A: return "CUSTOM_LUT_A";
        default: return "UNKNOWN";
    }
}
```

Append a self-test in `main/ink_tuning_lab.c`:

```c
bool ink_tuning_lab_self_test(void)
{
    ink_tuning_lab_state_t lab;

    ink_tuning_lab_init(&lab);
    if (lab.current_page != INK_TUNING_PAGE_TEXT) {
        return false;
    }
    if (lab.refresh_profile != INK_TUNING_REFRESH_PARTIAL_AUTO_DIRTY) {
        return false;
    }
    if (ink_tuning_lab_previous_page(&lab)) {
        return false;
    }
    if (!ink_tuning_lab_next_page(&lab)) {
        return false;
    }
    if (lab.current_page != INK_TUNING_PAGE_FOOTER) {
        return false;
    }
    if (!ink_tuning_lab_cycle_refresh_profile(&lab)) {
        return false;
    }
    if (lab.refresh_profile != INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER) {
        return false;
    }
    if (!ink_tuning_lab_request_force_full_refresh(&lab)) {
        return false;
    }
    return lab.force_full_refresh;
}
```

Update `main/CMakeLists.txt` to include:

```cmake
        "ink_tuning_lab.c"
```

- [ ] **Step 4: Run build to verify it passes this slice**

Run: `idf.py build`

Expected: PASS for the new module integration or, if later tasks are still missing, fail beyond this new module with no unresolved `ink_tuning_lab` symbols

- [ ] **Step 5: Commit**

```bash
git add main/CMakeLists.txt main/ink_app_priv.h main/ink_tuning_lab.h main/ink_tuning_lab.c
git commit -m "feat: add display tuning lab state"
```

### Task 2: Replace active UI model command handling with lab navigation

**Files:**
- Modify: `main/ink_app_priv.h`
- Modify: `main/ink_app_ui.h`
- Modify: `main/ink_app_ui.c`
- Modify: `main/app_main.c`
- Test: `main/ink_app_ui.c`

- [ ] **Step 1: Write the failing self-test changes**

Update `main/ink_app_ui.c` self-test expectations to assert lab behavior instead of fast browse / reader behavior:

```c
static bool app_lab_input_self_test(void)
{
    ink_ui_model_t model;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);

    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_NAV_NEXT)) {
        return false;
    }
    if (model.lab.current_page != INK_TUNING_PAGE_FOOTER) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_CONFIRM)) {
        return false;
    }
    if (model.lab.refresh_profile != INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER) {
        return false;
    }
    if (!ink_app_handle_ui_command(&model, INK_RUNTIME_SHELL_COMMAND_BACK)) {
        return false;
    }
    return model.lab.force_full_refresh;
}
```

Expected immediate failure: current `ink_ui_model_t` does not contain `lab`, and current command handling still depends on browser/reader pages.

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL in `main/ink_app_ui.c` due to missing lab field or incompatible command flow

- [ ] **Step 3: Write minimal implementation**

In `main/ink_app_priv.h`, replace active ebook-oriented UI model fields with a compact model:

```c
typedef struct {
    ink_runtime_shell_button_state_t buttons;
    ink_tuning_lab_state_t lab;
} ink_ui_model_t;
```

In `main/ink_app_ui.h`, delete reader/fast-browse entry points and keep only:

```c
void ink_app_button_state_from_snapshot(
    const ink_button_snapshot_t *snapshot,
    ink_runtime_shell_button_state_t *buttons
);
ink_runtime_shell_command_t ink_app_command_from_snapshot(
    const ink_ui_model_t *model,
    const ink_button_snapshot_t *snapshot
);
bool ink_app_should_dispatch_button_event(
    const ink_button_snapshot_t *snapshot,
    uint32_t now_ms,
    uint32_t *last_hold_event_ms
);
bool ink_app_handle_ui_command(ink_ui_model_t *model, ink_runtime_shell_command_t command);
bool ink_app_ui_self_test(void);
```

In `main/ink_app_ui.c`, replace `ink_app_handle_ui_command(...)` with:

```c
bool ink_app_handle_ui_command(ink_ui_model_t *model, ink_runtime_shell_command_t command)
{
    if (model == NULL || command == INK_RUNTIME_SHELL_COMMAND_NONE) {
        return false;
    }

    switch (command) {
        case INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS:
            return ink_tuning_lab_previous_page(&model->lab);
        case INK_RUNTIME_SHELL_COMMAND_NAV_NEXT:
            return ink_tuning_lab_next_page(&model->lab);
        case INK_RUNTIME_SHELL_COMMAND_CONFIRM:
            return ink_tuning_lab_cycle_refresh_profile(&model->lab);
        case INK_RUNTIME_SHELL_COMMAND_BACK:
            return ink_tuning_lab_request_force_full_refresh(&model->lab);
        default:
            return false;
    }
}
```

Update `main/app_main.c`:

- initialize `ink_tuning_lab_init(&app.model.lab);`
- remove fast-browse idle handling from `ui_task`
- remove reader-session specific branches from event handling

Use this simplified dirty flow in `ui_task`:

```c
if (command != INK_RUNTIME_SHELL_COMMAND_NONE) {
    ink_app_note_command(event.event_ms, INK_RUNTIME_SHELL_PAGE_READER, command);
    dirty = ink_app_handle_ui_command(&app->model, command);
}

if (dirty) {
    (void)submit_display_request(app, command, event.event_ms);
}
```

- [ ] **Step 4: Run build to verify it passes**

Run: `idf.py build`

Expected: PASS for UI/model code or fail only in request/render paths that still expect ebook data

- [ ] **Step 5: Commit**

```bash
git add main/ink_app_priv.h main/ink_app_ui.h main/ink_app_ui.c main/app_main.c
git commit -m "refactor: route ui flow to tuning lab state"
```

### Task 3: Replace display request generation with lab request data

**Files:**
- Modify: `main/ink_app_display_request.c`
- Modify: `components/ink_app_core/ink_display_mailbox.h`
- Test: `main/ink_app_display_request.c`

- [ ] **Step 1: Write the failing self-test**

Replace the existing render request self-test with expectations that describe the lab state:

```c
static bool app_display_request_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    ink_tuning_lab_init(&model.lab);
    model.lab.current_page = INK_TUNING_PAGE_DETAIL;
    model.lab.refresh_profile = INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER;
    model.lab.render_counter = 7U;

    if (!ink_app_build_display_request(&model, 0U, 0U, INK_RUNTIME_SHELL_COMMAND_CONFIRM, &request)) {
        return false;
    }
    if (strcmp(request.overlay_left, "P3/4 DETAIL") != 0) {
        return false;
    }
    if (strcmp(request.overlay_right, "PARTIAL_FOOTER #7") != 0) {
        return false;
    }
    return !request.full_refresh;
}
```

Expected immediate failure: current request builder still depends on shell/browser/reader fields.

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL in `main/ink_app_display_request.c` self-test or request-building logic

- [ ] **Step 3: Write minimal implementation**

In `components/ink_app_core/ink_display_mailbox.h`, extend `ink_display_request_t` with lab fields:

```c
    ink_tuning_page_t tuning_page;
    ink_tuning_refresh_profile_t refresh_profile;
    bool force_fixed_footer_partial;
```

In `main/ink_app_display_request.c`, replace shell/reader-derived view building with lab metadata:

```c
static void format_lab_overlay(
    const ink_ui_model_t *model,
    ink_display_request_t *request)
{
    snprintf(
        request->overlay_left,
        sizeof(request->overlay_left),
        "P%u/%u %s",
        (unsigned)(model->lab.current_page + 1),
        (unsigned)INK_TUNING_PAGE_COUNT,
        ink_tuning_lab_page_name(model->lab.current_page));

    snprintf(
        request->overlay_right,
        sizeof(request->overlay_right),
        "%s #%u",
        ink_tuning_lab_refresh_profile_name(model->lab.refresh_profile),
        (unsigned)model->lab.render_counter);
}
```

Use this request construction:

```c
    request->page = INK_RUNTIME_SHELL_PAGE_READER;
    request->tuning_page = model->lab.current_page;
    request->refresh_profile = model->lab.refresh_profile;
    request->use_footer_overlay = true;
    request->force_fixed_footer_partial =
        model->lab.refresh_profile == INK_TUNING_REFRESH_PARTIAL_FIXED_FOOTER;

    request->full_refresh =
        model->lab.force_full_refresh
        || model->lab.refresh_profile == INK_TUNING_REFRESH_FULL;

    format_lab_overlay(model, request);
```

For `FAST_FULL`, keep `full_refresh = true` and rely on Task 4 for the distinct render path.

- [ ] **Step 4: Run build to verify it passes**

Run: `idf.py build`

Expected: PASS for request-building changes or fail only in render code that still expects shell/reader content

- [ ] **Step 5: Commit**

```bash
git add main/ink_app_display_request.c components/ink_app_core/ink_display_mailbox.h
git commit -m "feat: build display requests from tuning lab state"
```

### Task 4: Render deterministic tuning pages and selectable refresh strategies

**Files:**
- Modify: `main/ink_app_render.c`
- Modify: `main/ink_app_render.h`
- Modify: `components/ink_hw/epd_test_pattern.h`
- Modify: `components/ink_hw/epd_test_pattern.c`
- Test: `components/ink_hw/epd_test_pattern.c`
- Test: `main/ink_app_render.c`

- [ ] **Step 1: Write the failing self-tests**

Add a new framebuffer self-test in `components/ink_hw/epd_test_pattern.c`:

```c
bool epd_test_pattern_tuning_pages_self_test(void)
{
    uint8_t *a = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    uint8_t *b = malloc(EPD_GDEY0426T82_BUFFER_SIZE);
    bool ok = false;

    if (a == NULL || b == NULL) {
        free(a);
        free(b);
        return false;
    }

    epd_test_pattern_fill_tuning_page(a, EPD_GDEY0426T82_BUFFER_SIZE, INK_TUNING_PAGE_TEXT, "L", "R");
    epd_test_pattern_fill_tuning_page(b, EPD_GDEY0426T82_BUFFER_SIZE, INK_TUNING_PAGE_HIGH_DELTA, "L", "R");
    ok = memcmp(a, b, EPD_GDEY0426T82_BUFFER_SIZE) != 0;

    free(a);
    free(b);
    return ok;
}
```

Update `main/ink_app_render.c` self-test to assert `PARTIAL_FIXED_FOOTER` marks a non-full request and uses overlay strings.

Expected immediate failure: `epd_test_pattern_fill_tuning_page(...)` does not exist yet.

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL due to missing tuning-page generator

- [ ] **Step 3: Write minimal implementation**

In `components/ink_hw/epd_test_pattern.h`, add:

```c
void epd_test_pattern_fill_tuning_page(
    uint8_t *buffer,
    size_t length,
    ink_tuning_page_t page,
    const char *overlay_left,
    const char *overlay_right
);
bool epd_test_pattern_tuning_pages_self_test(void);
```

In `components/ink_hw/epd_test_pattern.c`, implement `epd_test_pattern_fill_tuning_page(...)` by composing existing helpers:

```c
void epd_test_pattern_fill_tuning_page(
    uint8_t *buffer,
    size_t length,
    ink_tuning_page_t page,
    const char *overlay_left,
    const char *overlay_right)
{
    if (buffer == NULL || length < EPD_GDEY0426T82_BUFFER_SIZE) {
        return;
    }

    memset(buffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);

    switch (page) {
        case INK_TUNING_PAGE_TEXT:
            epd_test_pattern_fill_text_page(buffer, length, "Display Tuning Lab", "Text page", "Reader-like body", "Partial tuning", "Observe ghosting", "Status");
            break;
        case INK_TUNING_PAGE_FOOTER:
            epd_test_pattern_fill_text_page(buffer, length, "Display Tuning Lab", "Stable body", "Footer isolates", "small-area", "refresh behavior", "Status");
            break;
        case INK_TUNING_PAGE_DETAIL:
            epd_test_pattern_fill_stripes(buffer, length);
            break;
        case INK_TUNING_PAGE_HIGH_DELTA:
            memset(buffer, 0x00, EPD_GDEY0426T82_BUFFER_SIZE / 2);
            memset(buffer + (EPD_GDEY0426T82_BUFFER_SIZE / 2), 0xFF, EPD_GDEY0426T82_BUFFER_SIZE / 2);
            break;
        default:
            break;
    }

    epd_test_pattern_draw_footer_overlay(buffer, length, NULL, overlay_left, overlay_right);
}
```

In `main/ink_app_render.c`, replace body-content selection with:

```c
    epd_test_pattern_fill_tuning_page(
        app->framebuffer,
        EPD_GDEY0426T82_BUFFER_SIZE,
        request->tuning_page,
        request->overlay_left,
        request->overlay_right);
```

Then select refresh behavior:

```c
        if (request->refresh_profile == INK_TUNING_REFRESH_FAST_FULL
            || request->refresh_profile == INK_TUNING_REFRESH_CUSTOM_LUT_A) {
            dirty_x = 0U;
            dirty_y = 0U;
            dirty_w = EPD_GDEY0426T82_WIDTH;
            dirty_h = EPD_GDEY0426T82_HEIGHT;
            ret = epd_gdey0426t82_gray_refresh(
                app->framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                app->framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE);
        } else if (request->full_refresh) {
            dirty_x = 0U;
            dirty_y = 0U;
            dirty_w = EPD_GDEY0426T82_WIDTH;
            dirty_h = EPD_GDEY0426T82_HEIGHT;
            ret = epd_gdey0426t82_full_refresh_ex(app->framebuffer, EPD_GDEY0426T82_BUFFER_SIZE, &control);
        } else if (request->force_fixed_footer_partial) {
            dirty_x = 0U;
            dirty_y = EPD_GDEY0426T82_HEIGHT - 32U;
            dirty_w = EPD_GDEY0426T82_WIDTH;
            dirty_h = 32U;
            ret = epd_gdey0426t82_partial_refresh_area_ex(
                app->framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                dirty_x,
                dirty_y,
                dirty_w,
                dirty_h,
                &control);
        } else {
            ret = epd_gdey0426t82_partial_refresh_area_ex(
                app->framebuffer,
                EPD_GDEY0426T82_BUFFER_SIZE,
                dirty_x,
                dirty_y,
                dirty_w,
                dirty_h,
                &control);
        }
```

After successful refresh:

```c
    if (ret == ESP_OK) {
        app->model.lab.render_counter++;
        if (request->full_refresh || request->refresh_profile == INK_TUNING_REFRESH_FAST_FULL) {
            app->model.lab.consecutive_partial_count = 0U;
        } else {
            app->model.lab.consecutive_partial_count++;
        }
        app->model.lab.force_full_refresh = false;
    }
```

- [ ] **Step 4: Run build to verify it passes**

Run: `idf.py build`

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add main/ink_app_render.c main/ink_app_render.h components/ink_hw/epd_test_pattern.h components/ink_hw/epd_test_pattern.c
git commit -m "feat: render fixed tuning pages with refresh profiles"
```

### Task 5: Remove active ebook boot dependencies and verify the lab build

**Files:**
- Modify: `main/app_main.c`
- Modify: `main/CMakeLists.txt`
- Modify: `README.md`
- Test: build output

- [ ] **Step 1: Write the failing simplification**

Trim boot self-tests and startup initialization to the pieces still needed by the lab. Remove reader/browser-specific boot expectations from `main/app_main.c`.

Expected immediate failure: build or self-test still references removed ebook-only entry points.

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL on remaining ebook-only references in active code path

- [ ] **Step 3: Write minimal implementation**

In `main/app_main.c`, reduce boot self-tests to:

```c
    ESP_ERROR_CHECK(epd_test_pattern_gray_demo_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(epd_test_pattern_reader_page_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(epd_test_pattern_copy_page_buffer_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(epd_test_pattern_tuning_pages_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_button_input_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_cpfont_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_display_mailbox_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_tuning_lab_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_app_ui_self_test() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(ink_app_render_self_test() ? ESP_OK : ESP_FAIL);
```

Keep component dependencies in `main/CMakeLists.txt` unchanged for now unless the build proves one can be removed without touching unrelated code.

Update `README.md` top sections to state:

```md
- Current runtime is a display tuning lab on branch `codex/display-tuning-lab`
- Left/right switch fixed pages
- Confirm cycles refresh profile
- Back forces full refresh
```

- [ ] **Step 4: Run verification**

Run:

```bash
idf.py build
git status --short
```

Expected:

- `idf.py build` succeeds
- `git status --short` shows only intended source/doc changes

- [ ] **Step 5: Commit**

```bash
git add main/app_main.c main/CMakeLists.txt README.md
git commit -m "feat: switch runtime to display tuning lab"
```
