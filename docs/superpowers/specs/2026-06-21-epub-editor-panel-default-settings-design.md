# EPUB Editor Panel And Default Settings Design

Date: 2026-06-21
Project: ink-reader
Scope: `tools/epub-to-xtc-converter` chapter editor panel UX and reusable default page settings

## Problem

The converter already supports chapter overrides and rich page/render settings, but two workflow gaps remain:

1. Chapter editing is still rendered inline through the same list container, which makes it hard to inspect and adjust long TOCs efficiently.
2. Users can tune a book's page/render parameters, but there is no one-click way to reuse that setup for the next EPUB.

The user wants:

- A dedicated chapter editing panel in the sidebar
- Each editable row to include a jump action, chapter name, and page number
- Edits to remain draft-only until saved
- A one-click "save current page parameters as default" flow
- New EPUB files to automatically inherit those saved defaults

## Decision

Add a sidebar-local chapter editor panel that replaces the read-only chapter list while edit mode is active, and persist reusable render/page settings in browser `localStorage`.

The chapter override persistence model remains per-book sidecar JSON through the local API.
The new default page settings are global to this browser on this machine and are independent of chapter overrides.

## UX

### Chapter Editor Panel

When the user clicks `编辑章节`, the chapter area switches from read-only list mode into a dedicated editor panel.

Each row shows:

- `跳转` button
- chapter name input
- page number input

The jump button updates preview immediately so the user can inspect the referenced page before saving.

The panel remains draft-only until the user clicks `保存`. `取消` discards the draft and restores the normal chapter list.

### Default Settings Button

Add a `保存为默认参数` button in the converter settings area near the reading/render controls.

Clicking it stores the current effective page/render configuration to `localStorage`.

When a new EPUB is opened:

- the converter loads the saved default settings first
- applies them before or immediately after the renderer is ready for the new book
- then refreshes preview with that configuration

The default settings button does not save:

- chapter overrides
- currently loaded files
- current page position

## Data Scope

### Saved Default Settings

Persist the current values for:

- device preset
- custom width / custom height
- orientation
- monitor DPI
- font selection
- font size
- font weight
- line height
- margin
- text alignment
- hyphenation mode
- hyphenation language
- quality mode
- dithering toggle / strength
- negative mode
- progress-bar toggles
- progress-bar position / width mode
- status font size
- status edge / side margins

### Excluded From Defaults

Do not persist:

- chapter overrides
- active book hash
- file list
- current chapter source
- current page number

## Error Handling

- If `localStorage` is unavailable or corrupted, ignore saved defaults and keep current built-in defaults.
- Saving defaults should show a short success/failure status in the existing progress area.
- Chapter editor jump should clamp to page range and never crash on malformed draft values.

## Testing

Add tests for:

- presence of dedicated editor panel container
- jump button rendering in chapter editor rows
- default settings button presence
- save/load `localStorage` code path wiring
- applying saved defaults during book switch

