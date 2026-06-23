# Minimal Reader Flip Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the synthetic tuning-page startup path with a fixed sample-book reader flip path while preserving the current display tuning instrumentation.

**Architecture:** Reuse `ink_reader_session` and the existing mailbox/render pipeline. Boot straight into one fixed XTC book, route left/right into real page turns, and let `ink_app_render` consume live bitmap/native page buffers when available while keeping the current overlay and profile logging.

**Tech Stack:** ESP-IDF 5.5.4, ESP32-S3, FreeRTOS, `ink_book_xtc`, `ink_app_core`, `ink_hw`

---

### Task 1: Document and wire minimal sample-book boot

**Files:**
- Modify: `main/app_main.c`
- Modify: `main/ink_app_boot.c`
- Modify: `main/ink_app_boot.h`

- [ ] Define a fixed-book boot helper that mounts TF, loads fonts, opens `/sdcard/books/sample.xtc` with a fallback to `/sdcard/sample.xtc`, and places `shell.page` in `READER`.
- [ ] Keep failure non-fatal so the branch can still boot into the synthetic fallback if the sample book is missing.
- [ ] Log the chosen sample path and start page clearly.

### Task 2: Route UI commands to real page flips

**Files:**
- Modify: `main/ink_app_ui.c`
- Modify: `main/app_main.c`

- [ ] Update command handling so left/right page turns call `ink_reader_session_previous_page()` / `ink_reader_session_next_page()` when the sample reader is active.
- [ ] Preserve confirm/back behavior for refresh profile cycling and force-full refresh.
- [ ] Leave the current tuning-page navigation available only as the fallback path when no sample book is active.

### Task 3: Build display requests from reader-session content

**Files:**
- Modify: `main/ink_app_display_request.c`

- [ ] Populate `use_bitmap_page`, `bitmap_page_buffer`, `use_native_page`, and `native_page_buffer` from `reader_session` when the fixed book is active.
- [ ] Keep footer overlay text, but change the left side to real page context and the right side to `PROFILE #render_counter`.
- [ ] Retain the old tuning-page request fields for the fallback synthetic path.

### Task 4: Render real page buffers with existing tuning instrumentation

**Files:**
- Modify: `main/ink_app_render.c`

- [ ] When a request contains a real bitmap page buffer, copy or compose from that buffer instead of calling `fill_tuning_page(...)`.
- [ ] Apply the existing footer overlay on top of the real page.
- [ ] Preserve dirty-rect detection, partial-area refresh, fixed-footer probe behavior, and custom LUT selection exactly as today.
- [ ] Log real reader page numbers so captures are easier to interpret.

### Task 5: Verify on hardware

**Files:**
- Output logs under: `tmp/`

- [ ] Build with `C:\esp\v5.5.4\esp-idf` and `C:\Espressif`.
- [ ] Flash to `COM9`.
- [ ] Capture boot logs to confirm sample-book open success.
- [ ] Capture several real next-page flips under baseline partial auto and `CUSTOM_LUT_A`.
- [ ] Compare dirty-region and total/busy timing against the existing synthetic lab measurements to decide whether more tuning is worth it.
