# UI Shell Unification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify launcher, library, WiFi, photo album, and overlay list pages onto one shared shell rhythm based on the current launcher header/list style.

**Architecture:** Treat the launcher shell as the visual source of truth, then pull shared header/list spacing into the common render helpers so pages stop approximating the same UI with separate constants. Keep only two list densities: detail rows and compact title-only rows.

**Tech Stack:** ESP-IDF C firmware, shared render helpers in `components/ink_hw`, app render assembly in `main/ink_app_render.c`, display request shaping in `main/ink_app_display_request.c`, Unity self-tests.

---

### Task 1: Align shared header/list geometry with launcher shell

**Files:**
- Modify: `components/ink_hw/epd_test_pattern.c`
- Modify: `components/ink_hw/epd_test_pattern.h`
- Test: `main/test/test_epd_test_pattern.c`

- [ ] Set the shared header layout to match launcher shell metrics and expose only the two row densities we actually use.
- [ ] Update shared list-row rendering so detail rows and compact rows use non-overlapping title/meta baselines.
- [ ] Extend tests to lock the common geometry and compact/detail separation.

### Task 2: Route library/photo/USB/wifi list pages through the same shell

**Files:**
- Modify: `main/ink_app_render.c`
- Modify: `components/ink_wifi_setup/ink_wifi_setup_ui.c`
- Test: `main/ink_app_render.c`

- [ ] Replace remaining per-page header approximations with the shared header helper.
- [ ] Make library/photo/USB/WiFi rows share the same detail-row or compact-row spacing family.
- [ ] Add/adjust self-tests that prove header meta and list body ink appear in the expected shared shell regions.

### Task 3: Fix library/reader overlay row overlap while keeping the same shell

**Files:**
- Modify: `main/ink_app_display_request.c`
- Modify: `components/ink_hw/epd_test_pattern.c`
- Test: `main/ink_app_render.c`

- [ ] Ensure library overlay titles/subtitles fit the same detail-row rhythm as the outer pages.
- [ ] Keep reader/library frameless overlays on the same shell, while using the correct page title and row density.
- [ ] Verify no regression in existing overlay strategy tests.
