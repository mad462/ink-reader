# Reader Footer And Fast Browse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make fast-browse release land on the last visible preview, slow early overshoot, move chapter title to the left footer lane, and trim noisy logs.

**Architecture:** Keep the existing mailbox-driven EPD pipeline, but add explicit visible-preview tracking in the reader fast-browse state and use that state when committing on release. Footer formatting remains centralized in the display request builder, and log cleanup stays local to the current reader/EPD flow.

**Tech Stack:** ESP-IDF 5.5.4, FreeRTOS tasks, existing XTC reader and SSD1677/GDEY0426T82 driver stack

---

### Task 1: Fast Browse State

**Files:**
- Modify: `main/ink_app_priv.h`
- Modify: `main/ink_app_fast_browse.c`

- [ ] Add state for the last visible footer preview page.
- [ ] Update self-tests for the new step policy and release semantics.

### Task 2: Footer Commit And Layout

**Files:**
- Modify: `main/app_main.c`
- Modify: `main/ink_app_display_request.c`

- [ ] Feed successful preview completion back into fast-browse state.
- [ ] Commit release to visible preview page when available.
- [ ] Reformat footer text to `chapter title` on the left and `percent + page/total` on the right.

### Task 3: Log Cleanup

**Files:**
- Modify: `main/app_main.c`
- Modify: `components/ink_book_xtc/ink_reader_session.c`

- [ ] Remove noisy recurring logs that are no longer useful during normal testing.
- [ ] Keep key flow and error logs intact.

### Task 4: Build And Flash

**Files:**
- Modify only if verification reveals a defect.

- [ ] Run `idf.py build`
- [ ] Run `idf.py -p COM9 flash`
- [ ] Capture startup confirmation log
