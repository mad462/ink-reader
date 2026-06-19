# XTC Reader Design For ESP-IDF Ink Reader

## Goal

Turn the ESP32-S3 firmware into a high-quality `XTC` reading terminal for the 480x800 SSD1677-based panel.

The device should stop behaving like an on-device EPUB layout engine and instead focus on:

- opening preconverted `XTC` black-and-white books
- rendering pages with low-latency page turns
- caching and prefetching adjacent pages
- preserving reading progress
- exposing the right metadata hooks for TOC and chapter navigation
- reusing the existing split-task input/UI/EPD pipeline

This phase targets `XTC` black-and-white text books only. `XTCH` grayscale support is explicitly deferred.

## Non-Goals

This phase will not attempt to implement:

- on-device EPUB layout as a primary path
- TXT as a first-class long-term reading format
- `XTCH` grayscale page decoding
- cover/image-heavy rendering features
- wireless sync or external library management
- a custom replacement for the existing desktop/mobile `XTC` conversion toolchain

## Why XTC First

The current firmware has already proven three things:

1. the panel, partial refresh, and request coalescing pipeline are working
2. live EPUB parsing on-device is functional but not the right final architecture
3. the real bottleneck is display-side page preparation plus EPD refresh timing, not button dispatch

The broader Xteink ecosystem also points in the same direction:

- community tools already convert EPUB into `XTC` or related device-oriented containers
- those tools target 480x800 Xteink-class devices directly
- the common model is "layout once off-device, display efficiently on-device"

Matching that model gives this firmware the best chance of feeling fast and stable on ESP32-S3 hardware.

## Product Decision

The device will become `XTC first`.

That means:

- `.xtc` is the primary and preferred book format in the UI
- on-device TXT and ad-hoc EPUB parsing are no longer the strategic reading path
- legacy TXT/EPUB support may remain temporarily as debug or fallback code, but should not shape the architecture

## Chosen Architecture

The firmware will be split into four functional layers for reading:

1. file/library selection
2. `XTC` format parsing and book session state
3. page cache and prefetch
4. existing UI + EPD presentation pipeline

High-level flow:

1. user selects an `.xtc` file in the browser
2. `ink_xtc_reader` validates the file and loads header/index metadata
3. `ink_xtc_book` opens a session around that file
4. `ink_xtc_page_cache` prepares the current page and adjacent pages
5. UI consumes already-prepared display pages rather than raw text source
6. EPD task renders the target page using the existing request mailbox and timing-aware refresh path
7. progress is persisted in app state

This keeps the device focused on display quality, caching, and responsiveness instead of layout complexity.

## Module Boundaries

### `main/ink_xtc_reader.h/.c`

Owns:

- `XTC` file header validation
- version and capability checks
- metadata loading
- TOC/index parsing
- page index parsing
- reading raw page payloads from SD
- decoding file-native page payload into a device-consumable page object

Does not own:

- navigation state
- cache policy
- UI decisions

### `main/ink_xtc_book.h/.c`

Owns:

- one opened book session
- current page and total page count
- TOC snapshot
- current file handle or reopen policy
- navigation helpers such as next/previous/jump

This layer shields the UI from raw file-format details.

### `main/ink_xtc_page_cache.h/.c`

Owns:

- current page cache
- adjacent page cache
- direction-aware prefetch
- cache invalidation when jumping across the book

The goal is to optimize for actual reading behavior instead of general-purpose LRU complexity.

### `main/ink_reader_session.h/.c`

Owns:

- the active reading-mode state exposed to UI
- bridging app state, current book, cache, and rendered page data
- migration point away from TXT/EPUB-centric assumptions in `app_main.c`

### Existing modules that stay in place

- `main/app_main.c`
  Keeps task wiring, button dispatch, shell command flow, and display request submission.
- `main/ink_display_mailbox.*`
  Keeps latest-request coalescing and stale-request cancellation.
- `main/epd_gdey0426t82.*`
  Keeps panel control and timing-aware refresh.
- `main/ink_app_state.*`
  Keeps persistence, but its payload will expand for `XTC` reading progress.

## XTC Data Model

The parser should normalize `XTC` into a small set of explicit runtime structures.

### `ink_xtc_file_header_t`

Fields should include at least:

- magic
- format version
- page width
- page height
- bit depth
- total page count
- TOC offset and length
- page index offset and length
- reserved fields for future compatibility

This lets the device reject unsupported variants early.

### `ink_xtc_page_entry_t`

One entry per page, containing at minimum:

- page data offset
- compressed size
- decoded size
- logical page number
- optional chapter association

This index is the backbone for fast random access and jump operations.

### `ink_xtc_toc_entry_t`

Contains:

- title
- target page
- depth or level
- optional parent linkage if available in source format

The first phase only needs to read and retain this cleanly; a richer TOC UI can follow.

### `ink_xtc_page_t`

Represents one decoded, display-ready page.

First-phase requirement:

- normalized to black-and-white framebuffer content usable by the existing portrait-oriented rendering path

This is important because the cache should hold ready-to-display pages, not re-layout instructions.

### `ink_xtc_book_t`

Runtime state for one open book:

- parsed header
- current page
- total pages
- TOC summary
- page index cache
- opened file path
- open file state

## Cache And Prefetch Strategy

The cache should optimize for real reading behavior, not generic desktop-style resource management.

### L0: Current page

- always resident
- the page currently being shown or about to be shown

### L1: Neighbor pages

- keep `prev/current/next`
- this covers the majority of short-press page turns

### L2: Direction-aware prefetch

When reading forward:

- prefetch `next + 1`

When reading backward:

- prefetch `prev - 1`

When jumping via TOC or direct page target:

- drop stale neighbor assumptions
- rebuild the cache window around the target page

This keeps implementation simple while still matching user behavior well on MCU hardware.

## Display Strategy

The cache should store decoded display pages, not raw text or partially decoded payload.

That means:

- if `XTC` stores black-and-white page bitmaps directly, cache the normalized 1bpp page
- if `XTC` uses a lightweight encoded page payload, decode it when filling cache, not during each UI repaint

This is the architectural shift that moves work out of the page-turn hot path.

The existing EPD pipeline remains valuable:

- request coalescing still matters during rapid flipping
- partial refresh path still matters for menus and overlays
- timing logs still matter for measuring `prepare`, `tx`, and `busy`

## UI And Interaction Model

The reader flow should collapse around `XTC`.

### Reader states

- `Library`
- `Opening`
- `Reading`
- `Jumping`
- `Error`

### Button mapping in reading mode

- `Left(GPIO12)`: previous page
- `Right(GPIO11)`: next page
- `Confirm(GPIO10)`: open TOC or confirm target
- `Back(GPIO9)`: return to library or close TOC
- `Power(GPIO46)`: reserved for system-level handling

### Page-turn behavior

- short press turns one page
- long press can keep advancing target pages
- display queue should continue dropping stale intermediate pages and settle on the latest requested page

This aligns well with the mailbox and cancellation work already completed.

## Persistence

App-state persistence should be extended beyond the current path-plus-page model.

Recommended saved fields:

- `book_path`
- `book_format = xtc`
- `current_page`
- `chapter_index` if available
- `total_pages_snapshot`
- optional last-opened timestamp

This will make TOC navigation and progress restore straightforward later.

## Legacy TXT And EPUB Paths

TXT and on-device EPUB should not drive the new architecture.

Recommended treatment:

- keep them only as temporary fallback or debug code during migration
- do not invest further in optimizing their reading path
- avoid letting their data structures constrain the new `XTC` runtime model

After `XTC` is stable, removing or isolating the legacy path is acceptable.

## Implementation Order

To avoid a big-bang rewrite, implementation should happen in this order:

1. define `XTC` file structures and a validating parser
2. add `ink_xtc_book` runtime session and basic open/close/page navigation
3. support open-first-page and sequential page turns
4. integrate app-state save/restore for `XTC`
5. add `prev/current/next` cache
6. add direction-aware background prefetch
7. expose TOC parsing and basic TOC UI entry
8. trim or isolate legacy TXT/EPUB paths

This sequence gets a working reader early while preserving room to optimize.

## Testing Strategy

### Firmware self-tests

Add tests for:

- header validation
- malformed file rejection
- page index parsing
- TOC parsing
- page decode into normalized display pages
- cache window transitions on next/prev/jump

### Device verification

Hardware verification should include:

1. mount TF and discover `.xtc`
2. open a valid `XTC` book
3. render the first page
4. flip forward and backward repeatedly
5. confirm rapid keypresses settle on the latest target page
6. reboot and confirm resume position
7. jump via TOC once that layer is exposed

### Performance logging

Keep and extend timing logs around:

- file open
- header parse
- index load
- page decode
- cache hit or miss
- prefetch complete
- display submit
- EPD refresh timing

## Risks

### Unknown `XTC` edge cases

The exact format variants used by community tools may differ. The reader should fail safely and report unsupported versions rather than guessing.

### SD read latency

Even with preconverted content, SD access can still hurt page turns if every request becomes synchronous. That is why early page-cache work is necessary.

### Memory pressure

Caching ready-to-display pages costs RAM. The cache must be sized deliberately for PSRAM use and bounded behavior.

### Legacy coupling

If new `XTC` runtime logic is forced into the current TXT-centric structures, future work will slow down. Clear module boundaries are important.

## Acceptance Criteria

This design is successful when:

- `.xtc` files can be discovered and opened on-device
- the device can render black-and-white `XTC` pages without on-device EPUB layout
- page turns are driven by cached or prefetched display pages
- reading progress restores correctly after reboot
- rapid page-turn input settles on the latest intended page
- the architecture clearly centers on `XTC`, not legacy TXT/EPUB constraints
