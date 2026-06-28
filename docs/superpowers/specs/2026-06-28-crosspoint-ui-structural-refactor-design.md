# CrossPoint-Style UI Structural Refactor Design

## Goal

Refactor the board-side firmware UI so the overall page rhythm, hierarchy, and component discipline move noticeably closer to `crosspoint-reader`, while preserving the current ESP-IDF runtime, service, render, and input architecture.

This is a structural UI pass, not a skin pass. The focus is to unify page templates, title bars, list density, popup structure, typography hierarchy, and long-text behavior without breaking the refresh strategy boundaries that were just stabilized.

## Scope

In scope for this pass:

- Launcher
- Library shell page
- Reader overlays:
  - loading popup
  - chapter popup
  - bookmark popup
  - reader action/menu popup
- Photo Album list page
- Photo Album preview page non-image chrome only
- USB MSC status/action page
- WiFi setup page family where it visibly diverges from the system template

Out of scope for this pass:

- EPUB feature expansion
- low-level EPD driver rework
- arbitrary visual beautification
- changes that blur the existing refresh strategy semantics
- redesigning Reader正文 layout itself

## Why This Refactor Is Worth Doing

This is not cosmetic over-optimization.

The current firmware already has usable features, but the UI is split across several visual dialects:

1. Library and Reader overlays already behave like a coherent system.
2. Launcher still uses an older, simpler layout language.
3. Photo Album list uses a separate custom card rhythm.
4. USB MSC and some WiFi states behave more like ad hoc status pages than system pages.
5. Font hierarchy is partly driven by historical font file choices instead of explicit UI semantics.

That inconsistency makes later work harder:

- UI changes become page-by-page one-offs.
- list spacing and popup sizing drift over time.
- long-text handling is inconsistent.
- refresh tuning becomes harder because layouts do not share stable dirty regions.

The expected payoff is practical:

1. New pages can reuse a small number of known-safe templates.
2. Font and spacing decisions stop being rediscovered for each app.
3. Refresh behavior becomes easier to preserve because page geometry is more predictable.
4. Later EPUB support and richer reader controls can plug into a consistent overlay system instead of inventing another UI dialect.

## Current State Assessment

### Best Existing Template Seed

The current best local foundation is the Library / Reader overlay family:

- `main/ink_app_display_request.c`
- `components/ink_hw/epd_test_pattern.h`
- `components/ink_hw/epd_test_pattern.c`

These already define a reusable visual grammar:

- frameless large panel
- tab row
- compact cards
- taller bookmark cards
- action popup
- loading popup

This is already much closer to CrossPoint-style page discipline than the other app pages.

### Pages Closest To The Target

These pages already feel structurally aligned with CrossPoint and should be changed conservatively:

- Library shell page
- Reader chapter popup
- Reader bookmark popup
- Reader loading popup
- Reader action popup

### Pages Furthest From The Target

These pages need the most structural cleanup:

- Launcher
- Photo Album list page
- USB MSC page
- some WiFi setup layouts and spacing

### Pages To Leave Largely Alone

These should only receive minimal chrome cleanup:

- Reader正文
- Photo Album preview image area

The rule here is content-first. We should not add extra chrome just to make them look more templated.

## CrossPoint Ideas We Are Borrowing

Borrowed at the design level:

1. A small family of reusable page templates instead of per-page invention.
2. Strong title bar discipline with predictable spacing.
3. High-density but readable list pages with restrained selection styling.
4. Popup pages that feel like part of one system rather than per-feature modals.
5. A content-first philosophy where overlays are structured, but the primary content still dominates.

## CrossPoint Ideas We Are Not Copying

We will not mechanically copy:

1. bottom prompt layouts tied to CrossPoint hardware buttons
2. exact safety margins from different panel hardware
3. refresh effects tightly coupled to their low-level implementation
4. visual details that would enlarge dirty regions or worsen ghosting on our current device

This project remains an ESP-IDF firmware with its own runtime, mailbox, and render pipeline. We are borrowing the visual system logic, not porting their UI code.

## Chosen Approach

### Option A: Use Library / Reader Overlay As The Mother Template

Extend the existing overlay grammar outward so Launcher, Photo Album list, USB MSC, and WiFi states gradually converge toward the same system.

Pros:

- closest to current code structure
- lowest refresh regression risk
- reuses the most mature existing layout seed
- easiest to keep page geometry stable

Cons:

- requires extracting shared render helpers from current overlay code
- some pages will need adaptation rather than direct reuse

### Option B: Rebuild Each Page Independently To Look More Like CrossPoint

Pros:

- can chase exact visual similarity faster

Cons:

- high duplication
- high drift risk
- poor maintainability
- higher chance of refresh regressions

### Recommendation

Choose Option A.

We should treat the current Library / Reader overlay system as the canonical in-project template seed and expand it into a small shared UI kit. That gives us CrossPoint-like discipline without creating a parallel rendering language.

## Page Template Families

The firmware should converge toward six fixed template families.

### 1. Launcher / Home Template

Purpose:

- installed apps
- small set of top-level destinations

Structure:

- unified title bar
- content area with 2 to 4 large destination cards
- restrained footer/meta hint area only if needed

Behavior:

- selection emphasis should be strong but bounded
- card geometry must be consistent with list-page spacing

### 2. List Page Template

Purpose:

- Library lists
- Photo Album file list
- Settings lists
- WiFi AP / saved network lists where appropriate

Structure:

- unified title bar
- optional right-side meta count
- vertically stacked rows/cards
- fixed list viewport rhythm

Behavior:

- long titles truncate with ellipsis
- selection state is one consistent system-wide style
- no oversized decorative cards

### 3. Frameless Library Overlay Template

Purpose:

- full-screen shell pages that still feel like structured panels

Structure:

- full-page inset bounds
- optional tabs
- internal list/card area
- optional action popup on top

This remains the base for Library and some large structured overlays.

### 4. Popup Template

Purpose:

- chapter picker
- bookmark picker
- loading popup
- confirm/cancel popup
- USB action prompt if needed later

Subtypes:

- small popup
- large popup

Rules:

- same border/padding language
- same title positioning
- same spacing between title, body, and action area

### 5. Content-First Preview Template

Purpose:

- Photo Album preview
- any future image-first or document-preview-first page

Structure:

- content occupies nearly all available area
- title/footer chrome is suppressed or minimal
- any auxiliary text is secondary and sparse

### 6. Status Page Template

Purpose:

- USB MSC mounted / waiting / unavailable states
- some WiFi transient states
- empty/error pages outside Reader

Structure:

- unified title bar
- centered status body
- optional compact action hint block

This replaces ad hoc text-only pages.

## Page Mapping

### Launcher

Map to: `Launcher / Home Template`

Required changes:

- replace the old simple menu feel with structured destination cards
- align title/header spacing with the rest of the system
- keep dirty regions bounded for selection movement

### Library

Map to: `Frameless Library Overlay Template`

Required changes:

- mostly preserve current structure
- normalize header, tabs, card spacing, and font hierarchy against the new rules

### Reader Loading / Chapter / Bookmark / Menu

Map to: `Popup Template`

Required changes:

- keep current mechanics
- unify title scale, popup bounds, list density, and card fill ratio

### Photo Album List

Map to: `List Page Template`

Required changes:

- remove the current custom card proportions
- keep title area isolated from the file list
- increase visible row density
- use ellipsis for long file names
- share the same list rhythm as Library and Settings

### Photo Album Preview

Map to: `Content-First Preview Template`

Required changes:

- keep image full-frame
- do not reintroduce a persistent bottom info bar
- only show non-image UI if strictly necessary

### USB MSC

Map to: `Status Page Template`

Required changes:

- replace ad hoc text layout with consistent status panel layout
- use the same title bar and centered status body rules as other system pages

### WiFi Setup

Map to: mixed `List Page Template` + `Status Page Template` + `Popup Template`

Required changes:

- preserve input behavior
- tighten header/list spacing and popup style to match the system
- keep the password page functional priority over style experimentation

## Unified UI Rules

These rules are the core deliverable.

### Header Rules

- one shared title bar height across major pages
- one shared left/right horizontal padding
- title always left-aligned
- right-side meta information uses a consistent anchor and font level
- divider treatment between header and content is consistent

### Content Bounds

- one primary horizontal content gutter for major pages
- one narrower inset rule for frameless panels and popups
- no page-specific random offsets unless required by content type

### List Density Rules

- list rows share a fixed height family
- compact rows for dense navigation pages
- taller rows only for bookmark-style or two-line metadata items
- visible row count should feel intentionally full, not sparse

### Long Text Rules

- book names and image names do not show extensions in list cards unless needed
- long titles truncate with ellipsis
- no clipping into neighboring UI
- popup titles and card titles use the same truncation policy

### Selection State Rules

- one primary selected-row style across list pages
- no giant inverted slabs unless a page explicitly needs it
- selection must stay visually legible under partial refresh
- selected state should not enlarge dirty regions more than necessary

### Divider Rules

- use subtle structure lines to separate zones
- do not overuse boxes inside boxes

### Popup Rules

- one small popup size family
- one large popup size family
- same padding logic
- loading popup stays compact and centered
- large popup content area should be sized so list/cards occupy the available height instead of leaving dead space

### Empty/Error State Rules

- centered and concise
- same title/body spacing across pages
- no custom one-off compositions per app

## Typography Strategy

This is a hard design constraint, not a loose guideline.

### Font Hierarchy

Define four semantic levels:

- `UI_H1`
  - page titles
- `UI_H2`
  - popup titles
  - tabs
  - section headings
- `UI_BODY`
  - list items
  - card titles
  - main menu text
- `UI_META`
  - progress
  - counts
  - hints
  - secondary lines

### Hard Font Source Rule

- `16px` and below: use `SmallSimSun` family because it is already optimized for low-pixel rendering on this device
- above `16px`: use the existing vector-derived / larger cpfont path where it visually holds up better

This means we do not chase tiny vector fonts for dense UI text. Readability and panel behavior take priority.

### Practical Font Mapping

Initial intended mapping:

- `UI_H1`: larger cpfont family
- `UI_H2`: `16px` or larger depending on actual verified output
- `UI_BODY`: `16px` SmallSimSun by default for dense list pages
- `UI_META`: `14px` or similarly small SmallSimSun if available and verified

Because existing cpfont file names are not fully trustworthy, the implementation must verify real display size against actual page output rather than trusting file names.

### Font Asset Work

If existing assets do not fit the semantic hierarchy:

1. identify all currently used font assets in boot/service loading
2. map them to semantic roles rather than file names
3. generate only the minimum additional cpfont sizes needed
4. keep the font set small and reusable

## Refresh Compatibility Requirements

The new UI structure must obey the refresh strategy model rather than fighting it.

Rules:

1. title bars should remain stable and change infrequently
2. list selection should dirty only bounded list regions
3. popup open/close geometry should be stable and predictable
4. Photo Album preview must remain image-first and not gain high-frequency chrome
5. no new UI pattern should force routine full refreshes for ordinary list movement
6. selection styles should be chosen for partial-refresh survivability, not just appearance

## Module Boundaries

This refactor must remain inside the current architecture.

Likely modules to modify:

- `main/ink_app_render.c`
- `main/ink_app_render.h`
- `main/ink_app_display_request.c`
- `components/ink_hw/epd_test_pattern.c`
- `components/ink_hw/epd_test_pattern.h`
- `main/apps/ink_launcher_app.c`
- `main/apps/ink_usb_msc_app.c`
- `components/ink_wifi_setup/ink_wifi_setup_ui.c`
- `main/ink_app_boot.c`
- `main/ink_system_services.c`

Preferred structural direction:

1. extract or formalize shared page-template render helpers
2. make apps choose among shared template families
3. keep app state machines separate from template rendering

Do not push UI business logic into `app_main.c`.

## Implementation Order

Recommended order:

1. formalize template vocabulary in render code
2. unify title bar geometry
3. unify list page geometry
4. unify popup geometry
5. map font hierarchy to real assets
6. migrate Launcher
7. migrate Photo Album list
8. migrate USB MSC
9. tune WiFi setup visible layout
10. verify refresh compatibility and partial dirty regions

## Success Criteria

This refactor is successful if:

1. Launcher, Library, Photo Album list, USB, and Reader overlays visibly look like one system.
2. Header height, horizontal padding, row density, and selection style are consistent.
3. Long titles truncate cleanly instead of colliding or disappearing unpredictably.
4. Small text remains readable because `16px` and below stay on `SmallSimSun`.
5. Photo Album preview remains content-first and uncluttered.
6. Refresh strategy semantics are preserved and no major page starts white-flashing due to UI restructuring.

## Risks And Mitigations

### Risk: UI similarity causes refresh regressions

Mitigation:

- reuse bounded geometry from current overlays
- prefer stable, predictable dirty regions
- validate list and popup pages with existing refresh logs

### Risk: font unification accidentally worsens readability

Mitigation:

- semantic mapping first
- real-device verification second
- keep `SmallSimSun` for `16px` and below

### Risk: overfitting to CrossPoint visuals

Mitigation:

- borrow structure, not exact hardware-specific ornament
- prefer our device’s refresh and readability constraints

## Non-Goals

This pass will not:

- redesign Reader正文 content layout
- add EPUB parsing or rendering
- change the low-level EPD waveform model
- introduce decorative animation
- create a separate UI framework outside the existing render stack
