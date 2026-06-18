# CrossPoint Core S3 Port Plan

## Goal

Port the core CrossPoint reader stack into the current `ink-reader` ESP-IDF project on `ESP32-S3`, using the already validated:

- `GDEY0426T82` display bring-up
- TF card mount path
- portrait display mapping
- partial refresh path
- initial grayscale path

The first target is not full feature parity. The first target is a stable reading firmware core that can:

1. boot into a CrossPoint-style runtime
2. read button input from the custom S3 board
3. browse files on TF
4. open TXT first, then EPUB
5. keep the code structured so more CrossPoint modules can be added incrementally

## Working Branch

Intended branch name:

`feature/crosspoint-core-s3-port`

Note:

- the current workspace `D:\FUCKIDF\ink-reader` is not a git repository yet
- branch creation is blocked until the user either initializes git here or points the work at an existing repository root

## Hardware Mapping For This Port

### Active buttons

- `Left` -> `GPIO12`
- `Right` -> `GPIO11`
- `Confirm` -> `GPIO10`
- `Back` -> `GPIO9`
- `Power` -> `GPIO46`

### Disabled for phase 1

- `Volume Up`
- `Volume Down`
- all side-button based page navigation
- power + volume combo shortcuts
- tilt-related controls

## Port Strategy

Recommended strategy: keep the current ESP-IDF project as the host firmware and port CrossPoint in layers.

Reasoning:

- the display and TF board support are already proven locally
- the current project targets `ESP32-S3`, while upstream CrossPoint targets `ESP32-C3` with Arduino/PlatformIO
- a layered port limits breakage and makes it easier to validate each milestone on hardware

## What Must Be Ported

### Layer 1: board and HAL adaptation

Purpose:

- expose the local S3 hardware through interfaces that are close to the CrossPoint expectations

Needed work:

- wrap current display driver behind a CrossPoint-compatible display abstraction
- wrap TF storage behind a CrossPoint-compatible storage abstraction
- implement button scanning for the 5-button S3 board
- add basic power/sleep hooks
- add minimal clock/system hooks

Primary references:

- `D:\FUCKIDF\ink-reader\main\epd_gdey0426t82.c`
- `D:\FUCKIDF\refs\crosspoint-reader\lib\hal\HalDisplay.*`
- `D:\FUCKIDF\refs\crosspoint-reader\lib\hal\HalStorage.*`
- `D:\FUCKIDF\refs\crosspoint-reader\lib\hal\HalGPIO.*`
- `D:\FUCKIDF\refs\crosspoint-reader\lib\hal\HalPowerManager.*`

### Layer 2: input mapping layer

Purpose:

- map physical GPIO buttons into stable logical actions used by activities

Needed work:

- port `MappedInputManager`
- remove dependency on upstream `BTN_UP` / `BTN_DOWN`
- preserve logical actions `Back`, `Confirm`, `Left`, `Right`, `Power`
- decide temporary behavior for `NavNext` and `NavPrevious` without side buttons

Primary references:

- `D:\FUCKIDF\refs\crosspoint-reader\src\MappedInputManager.*`

### Layer 3: runtime and activity framework

Purpose:

- boot into a CrossPoint-like application loop instead of a fixed demo sequence

Needed work:

- port `Activity`
- port `ActivityManager`
- port the minimum activity boot flow
- replace current demo-only `app_main()` flow with runtime init + activity loop

Primary references:

- `D:\FUCKIDF\refs\crosspoint-reader\src\main.cpp`
- `D:\FUCKIDF\refs\crosspoint-reader\src\activities\Activity*`
- `D:\FUCKIDF\refs\crosspoint-reader\docs\contributing\architecture.md`
- `D:\FUCKIDF\refs\crosspoint-reader\docs\activity-manager.md`

### Layer 4: settings and state persistence

Purpose:

- store user settings, current reading context, and recent books

Needed work:

- port minimal `CrossPointSettings`
- port minimal `CrossPointState`
- port recent-books storage only as needed
- defer advanced network/server settings

Primary references:

- `D:\FUCKIDF\refs\crosspoint-reader\src\CrossPointSettings.*`
- `D:\FUCKIDF\refs\crosspoint-reader\src\CrossPointState.*`
- `D:\FUCKIDF\refs\crosspoint-reader\src\RecentBooksStore.*`

### Layer 5: renderer and UI building blocks

Purpose:

- render CrossPoint UI and reader screens instead of test patterns

Needed work:

- port the required renderer interfaces
- decide whether to adapt upstream `GfxRenderer` or write a thinner compatibility layer first
- port only the UI components required by home/browser/reader

Primary references:

- `D:\FUCKIDF\refs\crosspoint-reader\lib\GfxRenderer`
- `D:\FUCKIDF\refs\crosspoint-reader\src\components`

### Layer 6: reader entry path

Purpose:

- support opening a book from file browser into a reader activity

Needed work:

- port `ReaderActivity`
- port `TxtReaderActivity` first
- port `EpubReaderActivity` second
- keep TXT as the first functional checkpoint before EPUB

Primary references:

- `D:\FUCKIDF\refs\crosspoint-reader\src\activities\reader\ReaderActivity.*`
- `D:\FUCKIDF\refs\crosspoint-reader\src\activities\reader\TxtReaderActivity.*`
- `D:\FUCKIDF\refs\crosspoint-reader\src\activities\reader\EpubReaderActivity.*`

### Layer 7: EPUB engine and caches

Purpose:

- enable real EPUB layout, pagination, and resume behavior

Needed work:

- port the subset of `lib/Epub` required for opening and paginating an EPUB
- preserve SD-card-first caching strategy
- verify S3 RAM/PSRAM layout is safe for the chosen cache behavior

Primary references:

- `D:\FUCKIDF\refs\crosspoint-reader\lib\Epub`
- `D:\FUCKIDF\refs\crosspoint-reader\docs\file-formats.md`

## Explicitly Deferred

These are not phase-1 goals:

- Wi-Fi flows
- OTA
- OPDS
- WebDAV
- browser upload UI
- screenshot combo shortcuts
- tilt sensor support
- full localization surface
- full font package parity
- exact upstream settings menu parity

## Phase Plan

### Phase 0: repo and directory preparation

Deliverables:

- decide git handling for this workspace
- add a `crosspoint_port/` or similar staging area for imported code
- decide which upstream files are copied, adapted, or referenced

Exit criteria:

- repository state is settled
- code import layout is chosen

### Phase 1: HAL and input foundation

Deliverables:

- S3 button driver for GPIO `12/11/10/9/46`
- input event logging
- minimal HAL display/storage/system/power shims
- volume keys fully disabled

Exit criteria:

- hardware buttons can be polled reliably
- current screen and TF stack still work

### Phase 2: runtime skeleton

Deliverables:

- minimal application runtime based on CrossPoint activity flow
- one simple activity rendered on screen
- `Back/Confirm/Left/Right` handled through mapped logical buttons

Exit criteria:

- device boots into an activity page instead of the current diagnostics

### Phase 3: file browser shell

Deliverables:

- simple file browser over TF
- selectable entries
- directory enter/exit flow

Exit criteria:

- user can browse TF content on-device

### Phase 4: TXT reader checkpoint

Deliverables:

- open a TXT file from browser
- paginate or scroll in a stable way
- next/previous navigation on available buttons
- back returns to browser

Exit criteria:

- TXT reading loop is complete

### Phase 5: EPUB reader checkpoint

Deliverables:

- open EPUB
- render first page
- turn pages
- keep resume/progress in storage

Exit criteria:

- EPUB reading loop is complete

### Phase 6: polish and upstream convergence

Deliverables:

- reduce divergence in settings, renderer behavior, and activity structure
- re-enable selected advanced features only when core reading path is stable

Exit criteria:

- core firmware is stable enough to evaluate broader CrossPoint feature imports

## Risks

### Build model mismatch

Upstream CrossPoint assumes Arduino/PlatformIO and `open-x4-sdk`. This project is ESP-IDF-native and already has a custom panel driver.

Mitigation:

- adapt interfaces instead of trying to preserve upstream build assumptions

### Input model mismatch

Upstream expects four front buttons plus volume side buttons. This S3 board only has five mapped keys and no side-key flow in phase 1.

Mitigation:

- treat side-key features as optional and stub or disable them first

### Renderer integration cost

Upstream UI expects `GfxRenderer` semantics and its surrounding font/layout ecosystem.

Mitigation:

- start with the minimal renderer surface needed by one activity at a time

### UTF-8 and CJK rendering

Current local demo only sanitizes UTF-8 to ASCII placeholders. Real ebook usage needs proper glyph pipeline and text shaping behavior.

Mitigation:

- do not claim CJK support until the font/render path is genuinely wired
- TXT reader can ship first with honest limitations if necessary

## Recommended Immediate Next Action

Start with:

1. settle git handling for `D:\FUCKIDF\ink-reader`
2. implement the phase-1 button/HAL foundation
3. replace the demo-centric boot flow with a tiny activity-based shell
