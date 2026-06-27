# Reader Long Press Page Turn Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore old-style reader long press so holding left/right continuously performs real page turns and releasing stops immediately.

**Architecture:** Reuse the existing reader button snapshot pipeline and idle tick loop, but change the current fast-browse helper so it becomes a repeated real-navigation helper instead of a footer-preview/commit system. Keep the behavior scoped to the reader app and UI helpers.

**Tech Stack:** ESP-IDF 5.5.4, C, FreeRTOS, existing reader app/runtime/render pipeline.

---

## File Map

- Modify: `D:\FUCKIDF\ink-reader\main\ink_app_fast_browse.c`
  - Replace footer-preview fast browse behavior with repeated real page-turn behavior
  - Add self-tests for hold-start, repeat cadence, and release-stop behavior
- Modify: `D:\FUCKIDF\ink-reader\main\ink_app_priv.h`
  - Rename or repurpose timing constants only if needed for clearer semantics
- Modify: `D:\FUCKIDF\ink-reader\main\apps\ink_reader_app.c`
  - Update reader self-tests to cover long-press repeat through the app boundary
- Modify: `D:\FUCKIDF\ink-reader\main\ink_app_render.c`
  - Adjust render self-tests so reader long press no longer expects footer preview overlay requests
