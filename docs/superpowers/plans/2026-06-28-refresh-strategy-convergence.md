# Refresh Strategy Convergence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce a semantic refresh strategy layer, re-map current page flows onto it, preserve existing runtime boundaries, and improve logs/tests without destabilizing Reader or Photo Album behavior.

**Architecture:** Add a small refresh-strategy enum to the display request contract, have apps/request builders choose strategies based on scene semantics, keep the renderer responsible for mapping strategy to full/partial/gray routes, and preserve the EPD driver as a low-level executor plus timing source.

**Tech Stack:** ESP-IDF 5.5.4, FreeRTOS tasks, mailbox-driven display pipeline, custom GDEY0426T82 driver, in-tree self-tests

---

### Task 1: Add Strategy Contract

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_render.h`
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_display_request.c`
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`

- [ ] Add a semantic refresh strategy enum and helpers.
- [ ] Thread strategy through the display request and app render model bridge.
- [ ] Add strategy-name formatting for logs and tests.

### Task 2: Add Failing Strategy Tests

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_photo_album_app.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_reader_app.c`

- [ ] Add failing self-tests for Photo Album preview/list strategy separation.
- [ ] Add failing self-tests for Reader正文 vs overlay strategy separation.
- [ ] Add failing self-tests for full-refresh transition semantics remaining intact.

### Task 3: Map App Scenes To Strategies

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_launcher_app.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_reader_app.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_photo_album_app.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_usb_msc_app.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_wifi_setup_app.c`

- [ ] Make each app emit a stable semantic strategy for each major scene.
- [ ] Keep existing partial-region hints where they still make sense.
- [ ] Keep app-switch full refresh behavior untouched.

### Task 4: Rework Render Routing Around Strategy

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`

- [ ] Centralize semantic-strategy-to-route mapping.
- [ ] Keep grayscale preview on the dedicated gray path.
- [ ] Keep Reader promotion logic, but attach it to Reader strategies instead of generic flag soup.
- [ ] Keep lab-only logic under explicit lab strategy handling.

### Task 5: Strategy Logging

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`
- Modify: `D:/FUCKIDF/ink-reader/main/app_main.c`

- [ ] Add strategy-aware logs at request claim/render route points.
- [ ] Preserve existing timing logs.
- [ ] Ensure logs distinguish semantic strategy from physical route.

### Task 6: Regression Sweep

**Files:**
- Modify only if verification reveals defects.

- [ ] Run the relevant self-test path through firmware startup self-tests.
- [ ] Run `idf.py build`.
- [ ] Summarize preserved behavior and remaining limits, especially grayscale speed limits tied to driver/panel behavior.
