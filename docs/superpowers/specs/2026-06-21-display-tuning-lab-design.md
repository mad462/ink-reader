# Display Tuning Lab Design

Date: 2026-06-21
Project: ink-reader
Scope: Replace the current ebook-oriented runtime flow with a minimal fixed-page display tuning lab while preserving the existing refresh pipeline, mailbox, timing logs, and button-driven interaction model.

## Problem

The current firmware is optimized around ebook reading flows, including reader session state, file browsing, XTC/TXT page generation, and navigation behaviors. That is useful for product work, but it makes display refresh tuning slower and noisier because:

1. Refresh timing is mixed with document loading and reader state transitions.
2. Dirty-region behavior depends on real book page content instead of controlled test patterns.
3. Fast iteration on partial refresh, busy timing, and LUT experiments is harder when the app has many unrelated moving parts.

For display tuning, the firmware needs a smaller runtime model that can reproduce a few stable page-to-page transitions and report refresh behavior clearly.

## Decision

Create a minimal "Display Tuning Lab" runtime on a dedicated `codex/display-tuning-lab` branch.

The lab keeps the current low-level display workflow:

- button polling task
- UI queue
- display mailbox submit / merge / cancel behavior
- render timing and dirty-rect logging
- `full`, `fast`, and `partial` EPD code paths

The lab removes ebook-driven runtime behavior from the active build path:

- no file browser flow
- no TXT/XTC reader session flow
- no fast browse mode
- no book-derived page generation

Instead, the app cycles through a small fixed set of display test pages so refresh behavior can be tuned in isolation.

## Goals

- Keep the existing EPD task / mailbox / cancellation architecture intact.
- Replace dynamic reader content with deterministic fixed test pages.
- Allow quick switching between refresh strategies from hardware buttons.
- Preserve and improve timing logs for side-by-side comparison of refresh modes.
- Keep the scope minimal enough to support fast code/test/build cycles.

## Non-Goals

- Do not delete existing ebook code from the repository.
- Do not redesign the display driver architecture.
- Do not introduce a large new UI framework.
- Do not attempt LUT reverse engineering in this phase beyond providing hooks for experiments.

## Approaches Considered

### 1. Minimal replacement inside the existing shell flow

Keep the current app skeleton and replace content generation with fixed pages.

Pros:

- smallest change set
- best reuse of mailbox, timing, and button flow
- easiest to compare with the current firmware behavior

Cons:

- leaves some naming and shell concepts that no longer reflect ebook behavior

### 2. Separate display tuning mode inside the current app

Add a mode flag and keep both ebook and tuning paths active in the same firmware.

Pros:

- easier to switch back later
- preserves product flow in one binary

Cons:

- more branching in UI/model logic
- more room for accidental coupling between lab mode and reader mode

### 3. Standalone minimal demo program

Bypass most current app code and write a narrow one-off test harness.

Pros:

- smallest runtime surface
- maximum experimental focus

Cons:

- loses the existing mailbox, cancellation, and timing infrastructure
- poor long-term value because improvements would need to be re-integrated later

## Chosen Approach

Use approach 1.

The lab will keep the existing application skeleton and low-level refresh flow, but the active model will become a fixed-page test harness.

This gives the fastest path to meaningful tuning while preserving the pieces that already matter most for measurement:

- request coalescing
- render cancellation
- dirty-region detection
- partial-area refresh
- per-refresh timing logs

## User Interaction

The runtime exposes a small number of fixed pages and a small number of refresh profiles.

### Buttons

- Left: previous test page
- Right: next test page
- Confirm: cycle refresh profile
- Back: force full refresh of the current page

Power button behavior is unchanged unless existing firmware already maps it elsewhere.

### Visible Status

Each page includes a bottom status/footer strip that shows:

- current page index and total pages
- current refresh profile name
- current render sequence or update counter
- optional compact dirty-rect summary when useful

This makes it possible to correlate visual behavior with serial logs during tuning.

## Fixed Test Pages

The first implementation should include 4 deterministic pages:

1. Text-heavy page
   Large black-on-white text blocks for realistic reader-like transitions.

2. Footer-change page
   Mostly stable body content with a small changing footer/status region.

3. Fine-detail page
   Thin lines, borders, checkerboard patches, and small text for artifact detection.

4. High-delta page
   A visually different page with strong black/white changes to stress partial refresh behavior.

These pages should be generated entirely in firmware from deterministic drawing helpers rather than loaded from storage.

## Refresh Profiles

The lab should support profile switching without reflashing.

Initial profiles:

1. `FULL`
   Always use full refresh.

2. `FAST_FULL`
   Always use the current fast full-screen refresh path.

3. `PARTIAL_AUTO_DIRTY`
   Use dirty-region detection and partial-area refresh when possible.

4. `PARTIAL_FIXED_FOOTER`
   Refresh only a known footer/status region to isolate small-area behavior.

5. `CUSTOM_LUT_A`
   Reserved experimental profile that initially aliases an existing path until custom LUT experiments are added.

Profiles should be rendered in both the footer and the serial logs.

## Runtime Model

The active application model becomes a compact display-lab state:

- current page index
- selected refresh profile
- force-full-refresh latch
- render/update counter
- consecutive partial refresh counter

No reader session, chapter metadata, page cache, file browser state, or book-derived navigation state should be required by the active render path.

## Architecture

### Keep

- `input_task`
- `ui_task`
- `epd_task`
- display mailbox
- existing EPD driver APIs
- timing log path in `ink_app_render`

### Replace or bypass

- ebook state initialization in boot/runtime setup
- command handling that depends on reader/browser state
- render request generation based on live document state
- page draw path that reads from TXT/XTC sessions

### New focused modules

The lab may introduce small helper modules if needed:

- fixed page generator
- refresh profile state helper
- minimal app state helper

These helpers should be narrowly scoped and avoid reintroducing product-level complexity.

## Render Flow

The render flow remains structurally the same:

1. Button event enters UI queue.
2. UI updates the minimal lab state.
3. UI builds a display request.
4. Mailbox coalesces requests if needed.
5. EPD task claims the latest request.
6. Render code draws the selected fixed page into the framebuffer.
7. Previous/current buffer diff determines the dirty region when the profile needs it.
8. The selected refresh path runs.
9. Timing and dirty-rect data are logged.

The main change is that step 6 becomes deterministic and no longer depends on ebook/session state.

## Logging

Serial logs are a core output of the lab.

Every refresh should report:

- page identifier
- refresh profile
- full vs partial mode
- dirty rectangle
- draw/convert/diff/EPD/total timing
- EPD driver timing breakdown including transmit and busy time
- consecutive partial refresh count

The logs should make it easy to answer:

- Is busy time or transmit time dominant?
- How much does page pattern affect dirty area?
- Which profile gives the best tradeoff between speed and artifacts?

## Error Handling

- If a render request cannot be built, the UI should log and ignore the event.
- If the selected profile cannot perform partial refresh for a given frame, it should fall back to full refresh and log that fallback explicitly.
- If the EPD driver returns an aborted/stale result, the existing mailbox cancellation behavior remains unchanged.
- If a fixed page generator fails validation in tests, boot should fail fast in self-test mode.

## Testing

### Unit / self-test targets

- fixed page generator returns deterministic, non-uniform buffers
- page navigation wraps or clamps exactly as designed
- refresh profile cycling order is stable
- footer/status text changes when page/profile changes
- dirty-region detection remains correct for known page transitions

### Manual verification

Hardware checks should cover:

- page-to-page switching in every profile
- repeated footer-only updates
- forced full refresh after several partials
- visual ghosting comparison across profiles
- timing log capture for each profile and page pair

## Rollout

Phase 1:

- create dedicated branch
- add design doc
- replace active ebook runtime path with fixed pages
- add refresh profile switching
- preserve current timing logs

Phase 2:

- simplify naming where it meaningfully improves readability
- add profile-specific counters and fallback logs
- add optional custom LUT experiment hooks

Phase 3:

- compare timing and artifact results
- decide which refresh strategy should inform later product firmware work

## Acceptance Criteria

- Firmware boots without depending on ebook content or SD card files for the active display path.
- Left/right switches between fixed test pages.
- Confirm cycles refresh profiles.
- Back forces a full refresh.
- Partial-area and full-refresh behavior can be compared on the same fixed pages.
- Serial logs clearly identify page, profile, dirty rect, and timing breakdown.
- Existing display mailbox and EPD task architecture remain in use.
