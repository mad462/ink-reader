# Refresh Strategy Convergence Design

## Goal

Stabilize and clarify the firmware refresh system by separating page semantics from low-level panel routes, reducing accidental full refreshes and repeated redraws, and establishing a clear contract between runtime, app render models, and the EPD driver.

## Problem Summary

The current firmware has multiple working refresh paths, but their semantics are distributed across request flags, reader-specific conditions, tuning-lab experiments, and ad hoc route promotion rules. This makes the system hard to reason about and easy to regress when adding pages or changing UI behavior.

The main structural problems are:

1. Refresh behavior is selected through many booleans instead of a small set of explicit strategy classes.
2. Reader text pages, reader overlays, library cards, grayscale album preview, and tuning-lab experiments share routing machinery without a stable policy boundary.
3. Grayscale image rendering is physically separated from normal black/white rendering, but that split is not represented as a first-class strategy with its own lifecycle.
4. Logs show low-level routes and timing, but not the semantic reason a route was chosen.

## Design Principles

1. Keep the existing runtime/services/render/input architecture.
2. Do not move refresh business logic into `app_main.c`.
3. Keep Reader and Photo Album on shared mechanisms, not shared special cases.
4. Separate semantic refresh strategy from physical panel route.
5. Preserve current working behavior first, then simplify.
6. Treat tuning-lab flows as explicit lab strategies so they stop leaking into normal product semantics.

## Refresh Strategy Layer

Introduce a semantic refresh strategy enum carried by the display request.

Recommended initial strategy set:

- `INK_REFRESH_STRATEGY_PAGE_TRANSITION_FULL`
- `INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST`
- `INK_REFRESH_STRATEGY_BW_UI_LIST_LOCAL`
- `INK_REFRESH_STRATEGY_OVERLAY_LOCAL_UPDATE`
- `INK_REFRESH_STRATEGY_READER_TEXT_TURN`
- `INK_REFRESH_STRATEGY_READER_HOLD_PREVIEW`
- `INK_REFRESH_STRATEGY_READER_TEXT_CLEANUP`
- `INK_REFRESH_STRATEGY_GRAY_IMAGE_PREVIEW`
- `INK_REFRESH_STRATEGY_GRAY_IMAGE_INTERRUPTIBLE`
- `INK_REFRESH_STRATEGY_GRAY_IMAGE_SETTLE`
- `INK_REFRESH_STRATEGY_LAB_EXPLICIT_MODE`

These names describe product intent. They do not commit the renderer to a particular waveform or panel operation by themselves.

## Responsibility Split

### Apps

Each app chooses a semantic strategy based on its current scene:

- Launcher: `BW_UI_PAGE_FAST`
- Library: `BW_UI_LIST_LOCAL`
- Reader正文: `READER_TEXT_TURN`
- Reader长按预览: `READER_HOLD_PREVIEW`
- Reader章节/书签/loading/menu: `OVERLAY_LOCAL_UPDATE`
- Photo Album LIST: `BW_UI_LIST_LOCAL`
- Photo Album PREVIEW: `GRAY_IMAGE_PREVIEW` or `GRAY_IMAGE_INTERRUPTIBLE`
- USB MSC / WiFi setup standard pages: `BW_UI_PAGE_FAST` or `BW_UI_LIST_LOCAL`

Apps may still provide partial rect hints, but the rect now refines a strategy instead of implicitly defining it.

### Display Request Builder

The request builder converts app/runtime state into:

- page identity
- selected semantic strategy
- optional partial region
- grayscale/interrupt permissions
- full refresh requirement

This layer is where page-transition full refresh policy should be centralized.

### Renderer

The renderer maps strategy to a concrete route:

- `full_refresh`
- `partial_area`
- `partial_full_window`
- `gray_refresh`

The renderer also owns:

- route promotion decisions
- diff vs fixed-region selection
- strategy-level logging

### Driver

The EPD driver remains responsible only for:

- executing the requested physical operation
- honoring cancellation policy
- reporting timing and phase

The driver should not know about Reader, Photo Album, Library, or overlays.

## Strategy Matrix

### Launcher

- Default: `BW_UI_PAGE_FAST`
- App enter/leave: `PAGE_TRANSITION_FULL`
- Partial allowed: yes
- Interrupt allowed: no
- Cleanup cadence: no

### Library

- Default: `BW_UI_LIST_LOCAL`
- Large content shift may still promote to full-window partial
- Partial allowed: yes
- Interrupt allowed: no
- Cleanup cadence: optional low-frequency cleanup later

### Reader Text

- Default page turn: `READER_TEXT_TURN`
- Hold navigation preview: `READER_HOLD_PREVIEW`
- Stop/cleanup: `READER_TEXT_CLEANUP`
- Partial allowed: renderer decides between dirty-area and full-window partial
- Interrupt allowed: only for hold-preview flow

### Reader Overlay

- Chapter/bookmark/menu/loading: `OVERLAY_LOCAL_UPDATE`
- Partial allowed: yes
- Interrupt allowed: no
- Cleanup cadence: no, returns to surrounding Reader strategy

### Photo Album List

- Default: `BW_UI_LIST_LOCAL`
- Partial allowed: yes
- Interrupt allowed: no
- Cleanup cadence: no

### Photo Album Preview

- Normal image flip: `GRAY_IMAGE_PREVIEW`
- Continuous browse / queued next image: `GRAY_IMAGE_INTERRUPTIBLE`
- Optional stop-settle pass: `GRAY_IMAGE_SETTLE`
- Partial allowed: no
- Interrupt allowed: yes for interruptible strategy only

### USB MSC / WiFi Setup

- Default: `BW_UI_PAGE_FAST`
- Fine-grained lists/forms: `BW_UI_LIST_LOCAL`
- App enter/leave: `PAGE_TRANSITION_FULL`

## Album-Specific Rules

Photo Album PREVIEW and LIST are formally different strategy domains.

Preview rules:

1. Preview uses the grayscale plane route only.
2. Preview must never inherit list-local partial semantics.
3. Interruptibility is opt-in and strategy-bound.
4. If a settle/cleanup pass is later enabled, it must be explicit and logged.

List rules:

1. List uses normal BW UI rendering.
2. List must never forward preview interrupt flags.
3. List partial regions are fixed UI regions, not framebuffer-diff guesses only.

## Reader-Specific Rules

Reader currently works reasonably well and must be disturbed as little as possible.

The refactor should:

1. Keep current full-window partial promotion logic, but attach it to `READER_TEXT_TURN` semantics.
2. Keep hold-preview cancellation isolated to `READER_HOLD_PREVIEW`.
3. Keep chapter/bookmark/loading/menu under `OVERLAY_LOCAL_UPDATE`.
4. Preserve page-transition full refresh behavior.

## Logging

Add durable logs that identify semantic policy before or alongside low-level route logs.

Suggested fields:

- `strategy`
- `route`
- `page`
- `full`
- `gray`
- `partial`
- `area`
- `interrupt`
- `cleanup`
- `seq`

The driver timing log is already valuable and should remain:

- `convert`
- `prepare`
- `update`
- `tx`
- `busy`
- `total`

## Borrowed Ideas

Borrowed from `crosspoint-reader` and `CrossInk` at the design level:

1. A small, explicit refresh mode taxonomy.
2. Cleanup as cadence rather than ad hoc full refresh.
3. Grayscale rendering treated as a separate discipline from ordinary BW text/UI.
4. Distinguishing interaction frames from cleanup frames.

## Deliberate Differences

We will not copy their implementation details because this project is built around:

- ESP-IDF tasks and queues
- the existing mailbox display pipeline
- current app/runtime/render boundaries
- the existing GDEY0426T82 driver

Instead of porting their HAL classes, we will translate those ideas into a request strategy layer inside our current architecture.

## Success Criteria

1. Major scenes can be explained by one semantic strategy each.
2. Album preview and album list no longer share ambiguous refresh semantics.
3. Reader text, Reader overlays, and Library stop sharing accidental policy through unrelated flags.
4. Logs show both semantic strategy and physical route.
5. Existing working full-refresh transitions are preserved.
