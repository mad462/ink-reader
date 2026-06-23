# EPUB MVP Design For ESP-IDF Ink Reader

## Goal

On the ESP32-S3 ink-reader target, replace the current EPUB placeholder page with a minimal but usable EPUB reading path that can:

- open a `.epub` file from the TF card
- parse common EPUB2/EPUB3 package metadata
- extract readable UTF-8 body text from spine XHTML documents
- reuse the existing paged text reader, Chinese font rendering, and partial-refresh UI flow
- remember reading progress
- optionally auto-open a configured EPUB after boot/flash to speed up device-side debugging

This design intentionally targets the fastest path to a stable on-device reader loop, not a full ebook layout engine.

## Non-Goals

This phase will not attempt to implement:

- CSS styling fidelity
- inline images or cover rendering
- full TOC navigation UI
- chapter list UI
- footnotes/endnotes
- advanced paragraph shaping, justification, or typography
- full CrossPoint activity stack migration

## Why This Approach

The current firmware already has the expensive parts of a usable reading stack:

- stable portrait EPD driver
- partial refresh
- Chinese `cpfont` rendering
- file browser
- paged text reader shell
- state persistence

The missing piece is EPUB ingestion. Reusing the existing text-reader surface keeps the first working version small, debuggable, and easy to verify on hardware.

Porting the full CrossPoint reader stack now would add significant UI, parser, cache, and rendering complexity before we have even proven a basic open-and-read path on this board.

## Chosen Architecture

Add a focused `ink_epub_reader` module that converts EPUB package contents into plain UTF-8 reading pages.

Data flow:

1. User selects an `.epub` in the existing file browser, or firmware auto-opens a configured debug EPUB during boot.
2. `ink_epub_reader` opens the ZIP container and reads `META-INF/container.xml`.
3. It resolves the package document (`content.opf` or equivalent).
4. It parses manifest and spine entries.
5. It reads spine XHTML items in order.
6. It strips markup, decodes basic entities, and emits UTF-8 plain text with paragraph/newline preservation.
7. The extracted text is paginated through the existing text-reader page model.
8. The runtime shell displays the result using the existing reader page and Chinese font path.
9. Current page is saved through the existing app-state mechanism.

## Module Boundaries

### `main/ink_epub_reader.h/.c`

Owns:

- EPUB container open/load
- package path discovery
- minimal OPF parsing
- manifest/spine resolution
- XHTML-to-text extraction
- pagination into the existing text-reader-compatible model
- EPUB-specific self-test coverage

Will expose a compact API similar in usage style to the current TXT reader so `app_main.c` stays simple.

### `main/ink_txt_reader.h/.c`

Will remain the display/pagination owner. Small refactoring is allowed only if needed to share pagination helpers with EPUB input, but the TXT path must keep its current behavior.

Preferred direction:

- extract shared UTF-8 page-building helpers only if this materially reduces duplication
- otherwise keep EPUB pagination local for lower risk

### `main/app_main.c`

Owns:

- choosing between TXT and EPUB open paths
- boot-time debug auto-open behavior
- progress restore
- page navigation behavior
- serial diagnostics for EPUB open timing and failure points

### `main/ink_app_state.h/.c`

Keeps the existing persisted state format and book-kind tracking. This phase only needs to ensure EPUB uses the same stored path/page mechanism as TXT.

## EPUB Parsing Scope

The parser will support a deliberately narrow but useful subset:

### ZIP layer

- read files from the `.epub` ZIP container
- support stored and deflated members
- avoid full archive extraction to SD

### Container discovery

- read `META-INF/container.xml`
- locate the package document from `<rootfile full-path="...">`

### OPF support

- parse manifest items: `id`, `href`, `media-type`, optional `properties`
- parse spine itemrefs in reading order
- detect nav/ncx references only for future compatibility logging, not UI

### XHTML content support

- read spine items with XHTML/HTML media types
- remove tags
- insert line breaks for paragraph-like/block-like elements
- ignore unsupported formatting tags safely
- decode a small useful entity set:
  - `&amp;`
  - `&lt;`
  - `&gt;`
  - `&quot;`
  - `&apos;`
  - numeric entities in decimal and hex when practical

### Failure handling

If EPUB parsing fails, the reader page should show a compact diagnostic state instead of crashing or returning to a blank page, for example:

- `EPUB OPEN FAIL`
- `NO CONTAINER`
- `NO OPF`
- `EMPTY SPINE`
- `TEXT EXTRACT FAIL`

## Rendering Strategy

The first usable version should render EPUB through the existing text reader page layout:

- same 4 body lines per page
- same line width budget
- same Chinese `cpfont` route when non-ASCII appears
- same status line format, adapted for EPUB if helpful

This keeps EPD behavior identical between TXT and EPUB during initial bring-up, which makes hardware debugging much easier.

## Boot-Time Debug Auto-Open

To speed up iteration, firmware should support an explicit debug behavior during this phase:

- after boot, if TF mount succeeds and a configured debug EPUB path exists, automatically load that EPUB and enter the reader page
- this should bypass the manual browser selection flow for development builds

Design constraints:

- keep this behavior simple and easy to disable
- path should be a local constant in firmware for now, not a settings UI
- if auto-open fails, log the failure and fall back to the normal home/browser flow

Recommended initial behavior:

- configure a single debug path such as the first known working EPUB on `/sdcard`
- gate it behind a compile-time or file-local boolean constant

## Performance Expectations

This phase optimizes for correctness and turnaround, not maximum EPUB parsing speed.

Expected behavior:

- initial EPUB open can be slower than TXT because ZIP + XML/XHTML parsing occurs once
- page turns should remain fast after pagination completes because display still uses the existing page cache model

If parse-time becomes a problem, later phases can add incremental chapter loading or a lighter page cache. That is not required for this milestone.

## Logging And Diagnostics

Serial logging should clearly show where EPUB time is spent:

- ZIP open
- container parse
- OPF parse
- spine text extraction
- pagination complete
- auto-open path selection

Failures should include the path and stage, but logs should remain compact enough for repeated device testing.

## Testing Strategy

### Self-tests in firmware

Add self-tests covering:

- container path extraction from sample XML text
- OPF manifest/spine parsing on a compact sample
- XHTML-to-text stripping with Chinese UTF-8 preserved
- entity decoding
- pagination output for a short synthetic EPUB text sample

These tests should be local C self-tests similar to the existing module tests already run at startup.

### Device verification

Hardware verification for this phase:

1. flash firmware
2. boot with TF inserted
3. auto-open configured EPUB
4. confirm reader page shows real book text instead of placeholder text
5. flip forward/backward pages
6. reboot and confirm progress restore

## Risks

### XML parsing complexity

Full XML correctness is not needed. We should use a narrow parser strategy suitable for common EPUB structures and fail safely on unsupported edge cases.

### Memory pressure

Loading entire large XHTML files blindly may waste RAM. The implementation should prefer bounded buffers and predictable limits. A whole-book in-memory DOM is explicitly out of scope.

### Formatting loss

Because we are converting XHTML to plain text, some books will lose spacing nuance, scene-break styling, or emphasis. This is acceptable for the MVP as long as the body text remains readable.

## Future Extension Path

If this MVP proves stable, the next phases can build upward without discarding the work:

1. richer XHTML block handling
2. chapter-aware incremental loading
3. TOC navigation
4. better paragraph spacing and typography
5. grayscale/image support where useful
6. selective reuse of more CrossPoint reader internals

## Acceptance Criteria

This design is considered implemented successfully when:

- selecting an EPUB no longer shows the placeholder reader message
- a real EPUB on TF opens into readable UTF-8 text
- Chinese body text remains readable on-screen
- page navigation works
- progress save/restore works for EPUB
- boot-time debug auto-open can jump straight into the configured EPUB
- failures fall back gracefully with clear logs and a readable on-screen error state
