# Minimal Reader Flip Test Design

Date: 2026-06-21
Project: ink-reader
Scope: Add a minimal real XTC reader flip path on top of the current display tuning branch so we can measure actual page-turn refresh behavior without restoring the full ebook product flow.

## Problem

The current `codex/display-tuning-lab` branch proved that custom partial LUT tuning is the biggest win for this panel, but the active runtime still renders synthetic test pages. That is good for controlled footer and dirty-rect experiments, yet it does not answer the final product question:

- how much of the gain survives on real book pages
- what dirty region sizes look like on actual consecutive page turns
- whether the current tuning is already "good enough" to land

Bringing the whole ebook runtime back into this branch would add browser, restore-state, and multi-mode complexity that slows iteration and muddies measurements.

## Decision

Add a minimal fixed-book reader flow to this branch.

The branch will still boot into a small lab-like runtime, but the content source changes from synthetic pages to a real XTC session opened from a fixed sample book on TF card. Navigation is limited to previous/next page plus refresh-profile switching and forced full refresh. No library UI, resume flow, state restore, or book selection UI is brought back.

## Goals

- Measure refresh timing on real consecutive reader pages.
- Preserve the current mailbox, render timing, dirty-rect, and custom-LUT logging pipeline.
- Keep startup deterministic by opening one known sample XTC file.
- Keep the code small enough that it can be moved into mainline later without dragging the old ebook complexity back in.

## Non-Goals

- Do not restore the old library / browser / resume experience.
- Do not reintroduce editor, chapter browsing, or persistent app-state workflows.
- Do not broaden the scope into UI polish.

## Chosen Runtime Shape

### Startup

At boot:

1. mount TF card
2. load reader/footer fonts if present
3. open a fixed sample book, preferring `/sdcard/books/sample.xtc`
4. fall back to `/sdcard/sample.xtc` only if needed before the book migration helper runs
5. jump to a fixed start page near the body of the book so page-turn diffs are representative
6. set shell state to `READER`

If book open fails, the runtime should stay bootable and fall back to the existing tuning page path so the branch remains debuggable on devices without the sample file.

### Input Model

Buttons remain minimal:

- Left: previous reader page
- Right: next reader page
- Confirm: cycle refresh profile
- Back: force full refresh of the current page

No fast-browse hold mode is required for this experiment.

### Render Model

The display request should use real `ink_reader_session` buffers when the fixed book is active:

- `bitmap_page_buffer` for diffing and area-partial refresh
- `native_page_buffer` reserved in the request so future panel-native experiments remain possible
- footer overlay remains enabled so the active profile and render counter are visible

The existing tuning pages remain available as a fallback when no sample book is active.

## Refresh Behavior

Keep the current refresh profile set:

1. `FULL`
2. `FAST_FULL`
3. `PARTIAL_AUTO_DIRTY`
4. `PARTIAL_FIXED_FOOTER`
5. `CUSTOM_LUT_A`

For real page flips, the key comparisons are:

- baseline partial auto dirty vs `CUSTOM_LUT_A`
- full refresh fallback behavior
- dirty-rect size and stability across consecutive next-page transitions

## Logging

Logs must still show:

- page id / current book page
- refresh profile
- dirty rect
- draw / diff / epd / total timing
- EPD driver busy and transfer timing

For real flips, add enough page context to correlate measurements, such as current page number and total pages from `reader_session`.

## Acceptance Criteria

- Firmware boots and attempts to open a fixed sample XTC book automatically.
- Consecutive page flips use real `ink_reader_session` content instead of synthetic tuning pages.
- Existing custom LUT partial path remains selectable and measurable.
- Footer/status overlay still shows profile and render counter.
- If the sample book is unavailable, the device still boots into a safe fallback path for debugging.
