# EPUB Page Editing Project Design

- Date: 2026-06-21
- Project: `tools/epub-to-xtc-converter`
- Status: Draft for review

## Summary

Add a persistent editing workflow on top of the existing EPUB-to-XTC preview/export tool without modifying the original EPUB resources.

The new workflow adds:

- multi-page preview mode optimized for page triage
- per-page dithering selection with a global select-all toggle
- click-through from multi-page preview into single-page fine editing
- per-page BMP replacement that only affects preview/export resources
- a portable project file format that stores the source EPUB plus all editing state for later re-editing

## Goals

1. Let users decide dithering page by page so image-heavy pages can keep dithering while text-heavy pages stay clean.
2. Preserve the original EPUB untouched at all times.
3. Let users replace individual rendered pages with BMP files for export-only overrides.
4. Support reopening the same book later with all settings and page edits restored.
5. Keep preview behavior and export behavior identical.

## Non-Goals

1. Do not write modifications back into the source EPUB.
2. Do not bind page edits to EPUB chapter structure or DOM resources.
3. Do not attempt partial smart migration of page overrides after re-pagination.
4. Do not introduce a separate image editor inside the app.

## User Experience

### Multi-Page Preview Mode

- Rename the existing `10页预览` button to `多页预览模式`.
- Keep continuous loading behavior.
- Change the grid so one row targets 5 page cards instead of the current denser layout that effectively tops out lower on screen.
- Each page card shows:
  - page number
  - per-page dithering checkbox
  - replacement badge when the page has a BMP override
- Clicking a page card exits multi-page preview and switches to single-page preview for that page.

### Global Dithering Control

- The current dithering control becomes a global select-all toggle.
- First click selects dithering for all pages.
- Second click clears dithering for all pages.
- Page-level checkboxes stay editable after the global toggle so users can make exceptions.
- The effective rule is: a page is dithered only when that page is selected.

### Single-Page Fine Editing

When a user clicks a page in multi-page preview, the app switches back to single-page preview and opens fine editing controls for the current page:

- current page dithering checkbox
- `载入 BMP 替换` button
- `清除本页替换` button
- `返回多页预览模式` button
- status text showing whether the page is using a replacement image

### BMP Replacement

- BMP replacement only affects the app’s preview/export pipeline.
- It never edits the EPUB file.
- A replacement BMP fully overrides the rendered page image for that page.
- When a page is replaced, preview and export must both use the replacement image.

## Data Model

### Project File

Introduce a project file format named `*.xtcproj`.

The file is a zip container with at least:

- `manifest.json`
- `source/book.epub`
- `replacements/page-000000.bmp` style files for per-page overrides
- optional `meta/` directory for future thumbnails, export records, or notes

### Manifest Structure

`manifest.json` stores:

- project format version
- source book metadata
- global render settings already supported by the app
- chapter override state
- per-page editing state
- pagination fingerprint

Representative shape:

```json
{
  "version": 1,
  "source": {
    "filename": "book.epub",
    "bookHash": "sha256_..."
  },
  "renderProfile": {
    "devicePreset": "xteink-x4",
    "fontFamily": "Noto Serif CJK SC",
    "fontSize": 34,
    "fontWeight": 400,
    "lineHeight": 120,
    "margin": 16,
    "textAlign": "justify",
    "qualityMode": "hq"
  },
  "chapters": {
    "source": "override",
    "payload": {}
  },
  "pageEditing": {
    "globalDitherSelected": true,
    "ditherOverrides": {
      "5": false,
      "6": false,
      "18": false
    },
    "bmpReplacements": {
      "0": "replacements/page-000000.bmp",
      "27": "replacements/page-000027.bmp"
    }
  },
  "paginationFingerprint": "sha256_..."
}
```

### Effective Per-Page Dithering Rule

- `globalDitherSelected` stores the default state for all pages.
- `ditherOverrides` only stores exceptions relative to the global state.
- Effective page state:
  - if no override exists, use `globalDitherSelected`
  - if override exists, use the override value

This keeps the project compact even for large books.

## Pagination Fingerprint and Invalidation

Per-page edits are bound to rendered page numbers, not semantic chapter anchors.

That means page edits can become invalid if pagination changes after edits already exist.

The app must compute a `paginationFingerprint` from the source book identity plus all layout-affecting settings, including at least:

- device size / preset
- orientation
- font family
- font size
- font weight
- line height
- margin
- alignment / hyphenation options that can alter flow

If the fingerprint changes after page edits exist:

- warn the user that per-page edits may no longer match the same content
- offer:
  - keep the old layout and cancel the new setting change, or
  - apply the new layout and clear page-level overrides

Do not silently remap page edits.

## Rendering and Export Pipeline

Preview and export must use the same page-resolution pipeline.

For each page:

1. Check whether the page has a BMP replacement.
2. If a replacement exists, load that image as the page image.
3. Otherwise render the page normally from EPUB.
4. Resolve whether dithering is enabled for that page.
5. Apply dithering only if that page is selected for dithering.
6. Continue with existing preview/export encoding steps.

This guarantees that preview output matches exported XTC output.

## UI and Interaction Details

### Multi-Page Layout

- Keep the existing multi-page mode as the entry point for bulk page inspection.
- Target 5 cards per row on desktop-width layouts.
- Preserve current zoom support in multi-page mode.
- Preserve infinite/continuous loading behavior.
- Keep batch navigation in multi-page mode aligned with the visible workflow.

### Selection Behavior

- Global select-all toggle updates the default page selection state immediately.
- Individual page toggles update only the clicked page’s effective dithering state.
- Replacement badge is visible in multi-page mode so manually replaced pages are easy to find.

### Single-Page Transition

- Clicking a multi-page card sets that page as current.
- The app exits multi-page mode and returns to single-page mode.
- The page editing controls open already scoped to that page.

## BMP Import Rules

- Accepted replacement input: `.bmp`
- Replacement target: current rendered page only
- If the BMP dimensions do not match the device resolution, the app should offer automatic fit-to-page handling rather than failing silently.
- The imported BMP is stored in the project container and referenced from `manifest.json`.

## Persistence Workflow

### Save Project

Saving a project writes:

- current source EPUB
- current app settings
- chapter override state
- global/page dithering state
- BMP replacement assets
- pagination fingerprint

### Open Project

Opening a project restores:

- source book
- render settings
- chapter edits
- multi-page and single-page page editing state
- replacement assets

If replacement assets are missing or unreadable, the app must show a specific per-page error instead of failing generically.

## Error Handling

The app should provide explicit messages for:

- invalid or unreadable project files
- unsupported or broken BMP replacements
- dimension mismatch during BMP import
- pagination invalidation caused by layout-setting changes
- missing replacement assets inside an existing project

## Testing

Add or extend tests for:

1. global select-all / clear-all dithering behavior
2. per-page dithering override resolution
3. multi-page mode page-card checkbox and badge rendering
4. 5-column multi-page layout structure
5. click-through from multi-page mode to single-page editing
6. BMP replacement preview behavior
7. BMP replacement export behavior
8. project save and reopen round-trip
9. pagination fingerprint invalidation warnings
10. failure handling for broken project or replacement assets

## Recommended Implementation Order

1. Introduce the page-editing state model in memory.
2. Update multi-page preview UI for 5-column cards, page checkboxes, and replacement badges.
3. Add click-through back to single-page fine editing.
4. Add per-page BMP replacement in preview/export.
5. Add project file save/open support.
6. Add pagination fingerprint invalidation handling.
7. Add regression tests across preview, export, and persistence.
