# Fast Browse Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a reader-only fast browse mode where long-hold updates a footer preview immediately and a single final page jump happens only on release or confirm.

**Architecture:** Implement the interaction entirely in the app/UI layer. Reuse existing hold-duration input, XTC random page jump, display mailbox, and partial refresh pipeline. Keep the document body stable during browsing and only update a footer strip until final commit.

**Tech Stack:** ESP-IDF 5.5.4, C, FreeRTOS tasks/queues, existing SSD1677 EPD driver, existing XTC reader/session code.

---

## File Map

- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`
  - Add fast browse state to UI model
  - Detect long-hold ownership in reader page
  - Build footer-only display updates during browse
  - Commit/cancel final jump
  - Remove/disable the current maintenance-full-refresh experiment
  - Add self-tests for browse policy helpers
- Modify: `D:\FUCKIDF\ink-reader\main\ink_reader_session.h`
  - Add lightweight query helpers for chapter count / current chapter when available
- Modify: `D:\FUCKIDF\ink-reader\main\ink_reader_session.c`
  - Implement chapter metadata query helpers from current XTC session state
  - Add self-test coverage if chapter metadata is exposed through helpers
- Modify: `D:\FUCKIDF\ink-reader\main\ink_xtc_book.h`
  - Expose minimal chapter/page metadata query hooks if needed by session layer
- Modify: `D:\FUCKIDF\ink-reader\main\ink_xtc_book.c`
  - Implement chapter/page metadata lookup helpers if current structures support it cleanly
- Modify: `D:\FUCKIDF\ink-reader\main\epd_test_pattern.c`
  - Add footer rendering helper or compact footer-only text render helper if existing text-page helpers are too coarse
- Modify: `D:\FUCKIDF\ink-reader\main\epd_test_pattern.h`
  - Declare footer helper

## Task 1: Remove the Current Full-Refresh Carryover Experiment

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`
- Test: existing self-tests in `app_main()`

- [ ] **Step 1: Write the failing self-test expectation**

Add or update a self-test in `app_main.c` so it asserts that ordinary repeated `NAV_NEXT` requests in reader mode do not automatically flip `full_refresh` just because a streak counter exists.

Target helper behavior:
```c
static bool app_main_reader_refresh_policy_self_test(void)
{
    ink_display_request_t request;

    memset(&request, 0, sizeof(request));
    request.page = INK_RUNTIME_SHELL_PAGE_TXT_READER;
    request.command = INK_RUNTIME_SHELL_COMMAND_NAV_NEXT;
    request.use_native_page = true;

    return !should_force_reader_maintenance_refresh(&request, 0U)
        && !should_force_reader_maintenance_refresh(&request, 5U);
}
```

- [ ] **Step 2: Run build to verify the old behavior is wrong**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected before fix: either self-test failure or logic mismatch because the helper still enables maintenance full refresh.

- [ ] **Step 3: Replace maintenance refresh helper with inert behavior**

In `app_main.c`, remove the streak-driven maintenance full refresh path by making the helper return `false` for now and by removing the state transitions that force `full_refresh_requested = true` purely for ghost cleanup.

Target shape:
```c
static bool should_force_reader_maintenance_refresh(
    const ink_display_request_t *request,
    uint8_t partial_streak)
{
    (void)request;
    (void)partial_streak;
    return false;
}
```

Also remove any code that sets reader navigation into repeated `full_native` mode because of `reader_native_partial_streak`.

- [ ] **Step 4: Run build to verify it passes**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: build passes and self-tests succeed.

- [ ] **Step 5: Commit**

```bash
git add D:/FUCKIDF/ink-reader/main/app_main.c
git commit -m "fix: remove reader full refresh carryover"
```

## Task 2: Add Fast Browse State to the Reader UI Model

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`
- Test: `D:\FUCKIDF\ink-reader\main\app_main.c`

- [ ] **Step 1: Write the failing self-test for state transitions**

Add a new self-test that models fast browse state transitions without touching hardware:
```c
static bool app_main_fast_browse_state_self_test(void)
{
    ink_ui_model_t model;
    memset(&model, 0, sizeof(model));

    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 99;
    model.reader_session.total_pages = 1000;

    fast_browse_begin(&model, INK_FAST_BROWSE_DIR_FORWARD, 1200);
    if (!model.fast_browse.active) return false;
    if (model.fast_browse.origin_page != 99) return false;
    if (model.fast_browse.target_page != 99) return false;

    fast_browse_cancel(&model);
    return !model.fast_browse.active;
}
```

- [ ] **Step 2: Run build to verify the test fails**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: missing symbols such as `fast_browse_begin` / `fast_browse_cancel`.

- [ ] **Step 3: Add fast browse state structures and helpers**

In `app_main.c`, add a focused state block inside the UI model, for example:
```c
typedef enum {
    INK_FAST_BROWSE_DIR_BACKWARD = -1,
    INK_FAST_BROWSE_DIR_FORWARD = 1,
} ink_fast_browse_dir_t;

typedef struct {
    bool active;
    bool dirty;
    size_t origin_page;
    size_t target_page;
    size_t total_pages;
    size_t target_chapter_index;
    size_t total_chapters;
    ink_fast_browse_dir_t direction;
    uint32_t hold_start_ms;
    uint32_t last_step_ms;
} ink_fast_browse_state_t;
```

Add helpers:
- `fast_browse_begin(...)`
- `fast_browse_cancel(...)`
- `fast_browse_commit(...)`
- `fast_browse_apply_step(...)`
- `fast_browse_step_size_from_hold_ms(...)`

Keep these helpers private to `app_main.c`.

- [ ] **Step 4: Run build to verify it passes**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: build passes.

- [ ] **Step 5: Commit**

```bash
git add D:/FUCKIDF/ink-reader/main/app_main.c
git commit -m "feat: add fast browse reader state"
```

## Task 3: Expose Chapter Progress Metadata for Footer Preview

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\ink_reader_session.h`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_reader_session.c`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_xtc_book.h`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_xtc_book.c`
- Test: `D:\FUCKIDF\ink-reader\main\ink_reader_session.c`

- [ ] **Step 1: Write the failing reader-session self-test**

Add a small self-test that verifies chapter query helpers are safe when metadata is unavailable and return bounded values when available.

Example shape:
```c
if (!ink_reader_session_get_chapter_progress(&session, 0, &chapter_index, &chapter_total)) {
    if (chapter_index != 0 || chapter_total != 0) {
        return false;
    }
}
```

- [ ] **Step 2: Run build to verify it fails**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: missing declarations for chapter progress helper APIs.

- [ ] **Step 3: Add minimal metadata query APIs**

Preferred public API at session layer:
```c
bool ink_reader_session_get_chapter_progress(
    const ink_reader_session_t *session,
    size_t page_index,
    size_t *chapter_index,
    size_t *chapter_total);
```

Behavior:
- Returns `true` if chapter metadata is available
- Returns `false` if unavailable
- Uses 1-based chapter index for UI-facing values only when rendering
- Keeps storage/indexing internal as 0-based

If `ink_xtc_book_t` already has enough information in page entries, implement the lookup there and keep the session helper thin.

- [ ] **Step 4: Run build to verify it passes**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: build passes and reader-session self-tests succeed.

- [ ] **Step 5: Commit**

```bash
git add D:/FUCKIDF/ink-reader/main/ink_reader_session.h D:/FUCKIDF/ink-reader/main/ink_reader_session.c D:/FUCKIDF/ink-reader/main/ink_xtc_book.h D:/FUCKIDF/ink-reader/main/ink_xtc_book.c
git commit -m "feat: expose chapter progress metadata"
```

## Task 4: Render a Footer-Only Fast Browse Overlay

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\epd_test_pattern.h`
- Modify: `D:\FUCKIDF\ink-reader\main\epd_test_pattern.c`
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`
- Test: `D:\FUCKIDF\ink-reader\main\epd_test_pattern.c`

- [ ] **Step 1: Write the failing footer-render self-test**

Add a compact self-test that renders a footer strip into a blank page buffer and verifies only the footer region changes.

Example shape:
```c
bool epd_test_pattern_footer_overlay_self_test(void)
{
    static uint8_t page[EPD_GDEY0426T82_BUFFER_SIZE];
    static uint8_t before[EPD_GDEY0426T82_BUFFER_SIZE];
    uint16_t x, y, w, h;

    memset(page, 0xFF, sizeof(page));
    memcpy(before, page, sizeof(page));

    epd_test_pattern_draw_footer_overlay(page, sizeof(page), NULL, "12/1558 1%", "8/19");
    if (!find_changed_region(before, page, sizeof(page), &x, &y, &w, &h)) return false;
    return y >= 780 && h <= 20;
}
```

- [ ] **Step 2: Run build to verify the test fails**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: missing footer overlay helper.

- [ ] **Step 3: Implement footer overlay drawing helper**

Add a helper like:
```c
void epd_test_pattern_draw_footer_overlay(
    uint8_t *framebuffer,
    size_t framebuffer_length,
    ink_cpfont_t *font,
    const char *left_text,
    const char *right_text);
```

Requirements:
- Reserve the bottom 20 px area
- Clear only that footer band to white before drawing
- Draw left and right status strings into the footer band
- Keep body area untouched
- Use existing font path when available; ASCII fallback acceptable for first pass

- [ ] **Step 4: Run build to verify it passes**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: build passes and footer self-test succeeds.

- [ ] **Step 5: Commit**

```bash
git add D:/FUCKIDF/ink-reader/main/epd_test_pattern.h D:/FUCKIDF/ink-reader/main/epd_test_pattern.c D:/FUCKIDF/ink-reader/main/app_main.c
git commit -m "feat: add fast browse footer overlay"
```

## Task 5: Hook Long-Hold Input Into Fast Browse Mode

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`
- Test: `D:\FUCKIDF\ink-reader\main\app_main.c`

- [ ] **Step 1: Write the failing self-test for hold ownership**

Add a self-test covering these cases:
- short press in reader still yields normal `NAV_NEXT`
- long hold enters fast browse instead of normal repeated page-turn commits
- release triggers commit request
- back cancels browse

Sketch:
```c
static bool app_main_fast_browse_input_self_test(void)
{
    ink_ui_model_t model;
    ink_button_snapshot_t snapshot;

    memset(&model, 0, sizeof(model));
    model.shell.page = INK_RUNTIME_SHELL_PAGE_TXT_READER;
    model.reader_session.active = true;
    model.reader_session.xtc_active = true;
    model.reader_session.current_page = 10;
    model.reader_session.total_pages = 100;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.stable_mask = 0x08;
    snapshot.held_duration_ms[INK_RAW_BUTTON_RIGHT] = 900;

    return fast_browse_should_take_over(&model, &snapshot);
}
```

- [ ] **Step 2: Run build to verify it fails**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: missing helper logic.

- [ ] **Step 3: Implement fast browse input takeover in `ui_task()` / reader command path**

Behavior:
- If reader page + XTC active + stable hold on left/right beyond threshold, enter fast browse
- Once active, repeat hold events update `target_page` only
- Release commits jump
- `Confirm` commits immediately
- `Back` cancels
- Suppress ordinary `NAV_NEXT` / `NAV_PREVIOUS` page-turn code while browsing is active

Keep short-press path unchanged.

- [ ] **Step 4: Run build to verify it passes**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: build passes and browse input self-tests succeed.

- [ ] **Step 5: Commit**

```bash
git add D:/FUCKIDF/ink-reader/main/app_main.c
git commit -m "feat: enter fast browse on long hold"
```

## Task 6: Update Display Request / Render Path for Footer-Only Browse Updates

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`
- Test: `D:\FUCKIDF\ink-reader\main\app_main.c`

- [ ] **Step 1: Write the failing self-test for browse display requests**

Add a self-test that verifies an active fast browse request does not ask for native full-page rendering and instead produces footer-only dirty content.

Sketch:
```c
static bool app_main_fast_browse_display_request_self_test(void)
{
    ink_ui_model_t model;
    ink_display_request_t request;

    memset(&model, 0, sizeof(model));
    model.shell.page = INK_RUNTIME_SHELL_PAGE_TXT_READER;
    model.fast_browse.active = true;
    model.fast_browse.dirty = true;

    if (!build_display_request(&model, 0, 0, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) return false;
    return !request.use_native_page && !request.full_refresh;
}
```

- [ ] **Step 2: Run build to verify it fails**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: request path still chooses ordinary reader/native page render.

- [ ] **Step 3: Modify display request generation and rendering**

Implementation notes:
- Add a request mode or flags indicating `fast_browse_overlay`
- During overlay mode, render current page framebuffer plus footer updates only
- Ensure the resulting dirty region is the footer band, not full screen
- Do not call `ink_reader_session_jump_to_page()` while only browsing
- Only final commit should switch session page state

- [ ] **Step 4: Run build to verify it passes**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: build passes and browse display self-tests succeed.

- [ ] **Step 5: Commit**

```bash
git add D:/FUCKIDF/ink-reader/main/app_main.c
git commit -m "feat: render footer-only fast browse updates"
```

## Task 7: Commit Final Jump and Persist Progress Exactly Once

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_reader_session.c`
- Test: `D:\FUCKIDF\ink-reader\main\app_main.c`

- [ ] **Step 1: Write the failing self-test for commit semantics**

Add a self-test that verifies:
- `origin_page` remains unchanged during browse
- `target_page` changes while browsing
- commit calls the jump path once
- cancel leaves session page unchanged

The test can be helper-level rather than full IO-level.

- [ ] **Step 2: Run build to verify it fails**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: commit/cancel semantics are not fully wired.

- [ ] **Step 3: Implement final jump and cleanup**

Behavior:
- On release or confirm, call `ink_reader_session_jump_to_page()` with `target_page`
- If jump succeeds, exit browse mode and submit one normal page display request
- If jump fails, cancel browse mode and stay on origin page
- Update persisted progress only through the successful jump path

- [ ] **Step 4: Run build to verify it passes**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: build passes and commit/cancel self-tests succeed.

- [ ] **Step 5: Commit**

```bash
git add D:/FUCKIDF/ink-reader/main/app_main.c D:/FUCKIDF/ink-reader/main/ink_reader_session.c
git commit -m "feat: commit fast browse jumps once"
```

## Task 8: Flash and Device Verification

**Files:**
- No source changes required unless bugs are found during validation

- [ ] **Step 1: Build final firmware**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py build
```

Expected: `Project build complete`

- [ ] **Step 2: Flash to the connected board**

Run:
```powershell
. 'C:\esp\v5.5.4\esp-idf\export.ps1'; idf.py -p COM9 flash
```

Expected: flash completes and board resets.

- [ ] **Step 3: Verify on device**

Manual checks:
- Short press left/right still flips one page
- Long hold right enters browse quickly without changing body page
- Footer updates page/percent and chapter counts while holding
- Longer hold clearly accelerates target movement
- Releasing performs one jump only
- Confirm commits immediately from browse
- Back cancels browse
- No repeated `full_native` loop during browse

Target serial patterns:
- browse overlay submissions should not spam `full_native`
- final commit should show one real page render only

- [ ] **Step 4: If needed, tune acceleration thresholds and rebuild**

Tune constants only if manual verification shows movement too slow or too jumpy.

- [ ] **Step 5: Commit final tuning**

```bash
git add D:/FUCKIDF/ink-reader/main/app_main.c D:/FUCKIDF/ink-reader/main/ink_reader_session.c D:/FUCKIDF/ink-reader/main/ink_reader_session.h D:/FUCKIDF/ink-reader/main/ink_xtc_book.c D:/FUCKIDF/ink-reader/main/ink_xtc_book.h D:/FUCKIDF/ink-reader/main/epd_test_pattern.c D:/FUCKIDF/ink-reader/main/epd_test_pattern.h
git commit -m "feat: add reader fast browse mode"
```
