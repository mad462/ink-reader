# Fast Browse Mode Design

Date: 2026-06-20
Project: ink-reader
Scope: XTC/TXT reader navigation interaction for large-page-count books on ESP32-S3 + SSD1677 e-paper panel

## Problem

The current reader navigation uses direct page-turn rendering on every input event. On XTC books this gives acceptable single-step latency when using native partial refresh, but it creates two user-facing problems during rapid navigation:

1. Rapid multi-page movement causes repeated screen updates and accumulates visible ghosting.
2. Any fallback strategy that periodically inserts a maintenance full refresh can degrade into repeated full-screen refreshes when new requests arrive before the maintenance refresh has been fully consumed by the UI state.

The result is that "fast page turn" is the wrong interaction primitive for this display. The e-paper panel is good at rendering a final target page, but poor at repeatedly rendering every intermediate page under rapid input.

## Decision

Replace long-hold rapid page turning with a dedicated Fast Browse Mode.

Short press behavior stays unchanged:

- Left: previous page
- Right: next page

Long press behavior changes:

- Long-hold Left or Right in reader page enters Fast Browse Mode.
- While in Fast Browse Mode, the reader does not render intermediate pages.
- Only a lightweight footer preview is updated while the target page moves.
- When the hold ends, or when the user explicitly confirms, the reader performs a single jump to the final target page.

This avoids continuous page rendering and turns "many intermediate refreshes" into "one lightweight overlay update loop plus one final page jump".

## UX

### Entry

Fast Browse Mode can only be entered from `TXT_READER`.

Trigger:

- Long hold `Left`: start fast browse backward
- Long hold `Right`: start fast browse forward

Initial state:

- `origin_page` = current reader page
- `target_page` = current reader page
- `direction` = backward or forward based on hold key
- browsing overlay becomes active immediately

### Display

A footer-only preview bar is shown in the bottom 20 px of the page.

Layout:

- Left side: `currentTargetPage/totalPages percent`, for example `12/1558 1%`
- Right side: `currentChapter/totalChapters`, for example `8/19`
- Font size target: 16 px numeric/status font

Behavior:

- The main text page remains visually stable while the user is holding.
- Only the footer preview region changes during browsing.
- If the current book format cannot provide chapter count metadata, the right side should gracefully fall back to an empty field or `-/ -` style placeholder without breaking layout.

### Commit / Cancel

While Fast Browse Mode is active:

- Releasing the hold key commits the jump to `target_page`
- `Confirm` commits immediately
- `Back` cancels and returns to `origin_page`

Cancel must not change reading progress.
Commit updates reading progress exactly once after the final jump succeeds.

## Acceleration Model

The target page changes faster the longer the button is held.

Recommended step schedule for the first implementation:

- 0 ms to < 600 ms: 1 page per tick
- 600 ms to < 1500 ms: 5 pages per tick
- 1500 ms to < 3000 ms: 20 pages per tick
- > = 3000 ms: 100 pages per tick

Tick cadence should reuse the existing input repeat rhythm as much as possible, but the page delta is multiplied by the active acceleration tier.

Constraints:

- Clamp `target_page` to valid range `[0, total_pages - 1]`
- If the user keeps holding at the first/last page boundary, target remains pinned there
- Chapter indicator must update from `target_page`, not from `origin_page`

## Rendering Strategy

### Normal Reader Mode

No change for short press navigation:

- Single page previous/next continues to use the existing reader path
- Existing native XTC partial path remains available for single-step turns

### Fast Browse Mode Overlay

Overlay updates should avoid full-page rendering.

Preferred rendering plan:

- Keep current body content unchanged during browsing
- Render only the bottom footer strip in portrait coordinates
- Use partial-area refresh for that 20 px footer region
- Reuse current framebuffer and diff logic where practical, but confine changes to the footer area

This is intentionally lighter than rendering the destination page preview. The browsing interaction is informational, not a live document preview.

### Final Jump

When browsing commits:

- Exit overlay state
- Resolve `target_page`
- Perform a single real page jump via reader session / TXT reader jump APIs
- Refresh the resulting page once

For XTC:

- Use `ink_reader_session_jump_to_page(...)`
- Allow one real page render only after commit

For TXT fallback:

- First implementation may keep Fast Browse Mode disabled, or support only XTC if direct arbitrary-page jump is not yet available in TXT path

## Chapter Metadata

The footer requires chapter progress information:

- `target_chapter_index`
- `total_chapter_count`

Implementation preference:

- Reuse XTC metadata if available from current imported format
- If the active book has no chapter metadata, render placeholder values and keep fast browse functional

The browsing interaction must not block on chapter title extraction. Numeric chapter progress is sufficient for first release.

## State Model

Add a reader-local browsing state, conceptually:

- `active`
- `origin_page`
- `target_page`
- `direction`
- `hold_start_ms`
- `last_step_ms`
- `current_step_size`
- `target_chapter_index`
- `total_chapter_count`

This state should live at the UI/model layer rather than inside the EPD driver.

## Input Handling

Current input stack already tracks hold duration per button. Fast Browse Mode should build on that instead of inventing a parallel timing system.

Expected behavior:

- Detect hold on Left/Right while on reader page
- Convert repeat events into target-page adjustments instead of immediate page rendering
- On release, commit the jump
- Ignore ordinary short-press page turn logic once browsing mode has taken ownership of that hold session

## Performance Expectations

During browsing:

- No repeated full-page page-turn rendering
- No repeated chapter/page content loading for intermediate pages
- Only lightweight footer updates should occur

At commit:

- One final page load/jump
- One final screen refresh

This should dramatically reduce perceived lag, ghosting accumulation, and unnecessary BUSY wait cycles during long navigation runs.

## Error Handling

If final jump fails:

- Exit Fast Browse Mode
- Stay on origin page
- Preserve previous reading position
- Show a simple footer/status failure message if practical, but do not leave the UI stuck in browsing mode

If chapter metadata is unavailable:

- Continue with page progress only

If reader format does not support arbitrary page jump yet:

- Fast Browse Mode should be feature-gated off for that format until jump support is available

## Rollout Plan

Phase 1:

- Remove the current maintenance-full-refresh fast-turn experiment
- Add Fast Browse Mode state and footer overlay
- Support XTC page jump commit
- Show page count + percentage
- Show chapter counts when metadata is available

Phase 2:

- Tune acceleration schedule from real device feedback
- Improve footer typography/layout
- Add optional chapter title text if metadata lookup is cheap enough

## Tradeoffs

Chosen approach benefits:

- Eliminates repeated full-page updates during long navigation
- Reduces ghosting pressure
- Feels more immediate because the UI responds instantly in the footer
- Better aligned with e-paper strengths

Chosen approach costs:

- User no longer sees every intermediate page while long-holding
- Requires a new browsing state machine and footer-only rendering path
- Needs chapter-count metadata plumbing to fully realize the right-side indicator

Rejected approach:

- Keep rapid direct page-turning and attempt to balance ghosting with periodic full refresh

Reason rejected:

- It is fundamentally fighting the panel behavior instead of designing around it
- State handoff between partial and maintenance full refresh is fragile under queued inputs
- User experience remains inconsistent under long rapid navigation

## Testing

Required verification:

- Short press still flips exactly one page
- Long hold enters browsing without changing body page content
- Footer updates immediately while holding
- Footer acceleration increases with hold duration
- Release commits exactly one jump
- Confirm commits exactly one jump
- Back cancels and returns to original page
- No repeated full refresh loop occurs during browsing
- XTC progress persistence updates only after committed jump
- Browsing at first/last page clamps correctly

## Open Constraints

The first implementation should prefer correctness and interaction clarity over visual polish. The footer can remain text-only if that keeps the code path small and stable.
