# EPUB Chapter Overrides Design

Date: 2026-06-21
Project: ink-reader
Scope: `tools/epub-to-xtc-converter` web UI chapter editing for EPUB-derived XTC export

## Problem

The EPUB converter currently derives chapter navigation from one of two sources:

1. The EPUB's built-in TOC
2. A fallback heading-based synthetic TOC when the built-in TOC is missing or too coarse

This improves many books, but it still leaves an important gap: some EPUB files have valid text rendering while their structural navigation is incomplete, misaligned, or simply not to the user's liking. In those cases, the user needs a practical way to correct chapter names and chapter starting pages without modifying the original EPUB by hand.

The key user requirement for the first version is:

- Users can edit existing chapters
- Users can change chapter name
- Users can change chapter start page
- Users can delete chapters
- Edits persist for the same book across sessions
- The edited result affects preview, navigation, progress bar segmentation, and exported XTC chapter metadata

The first version does not need manual chapter creation yet.

## Decision

Add a lightweight editable chapter overlay to the converter UI and persist the user's edits as a sidecar override file stored in the project, not in the source EPUB.

The override layer becomes the highest-priority chapter source:

1. User chapter overrides
2. Auto-generated heading fallback
3. EPUB built-in TOC

The final resolved chapter list is the only list used by:

- Left sidebar chapter list
- Chapter highlight during preview navigation
- Progress bar chapter marks and chapter progress calculations
- Exported XTC chapter metadata

This keeps behavior consistent across preview and export.

## Why Sidecar Overrides

The chosen persistence model is a per-book sidecar configuration file instead of rewriting the original EPUB.

Benefits:

- Safe: original EPUB stays untouched
- Repeatable: reopening the same book restores the same chapter corrections
- Simple: no need to rebuild EPUB internals or repack archives
- Reversible: deleting the override file restores automatic behavior

This matches the current tool's workflow better than editing package metadata inside the EPUB itself.

## UX

### Entry

Add an `编辑目录` button near the chapter list in the web UI.

Clicking it switches the chapter panel into edit mode.

### Edit Mode Layout

Each chapter row shows:

- Chapter name text input
- Start page numeric input
- Delete button

Panel footer actions:

- `保存`
- `取消`

The chapter list remains linear and compact. The first version intentionally avoids a separate modal or full table editor.

### Save Behavior

On save:

- Validate all rows
- If valid, write the override file
- Rebuild the final resolved chapter list
- Refresh preview-dependent chapter state immediately

Affected UI after save:

- Sidebar chapter names and order
- Current chapter highlight
- Chapter jump targets
- Progress bar chapter marks
- Chapter page counters used in export

### Cancel Behavior

On cancel:

- Discard unsaved edits
- Return to normal chapter list view

### Delete Behavior

Deleting a row removes that chapter from the final resolved list after save.

The first version does not expose "undo delete" inside the row. Users can cancel the edit session before saving if they changed their mind.

## Validation Rules

Validation must block save and show a clear error message when a rule fails.

Rules:

- Chapter name cannot be empty after trimming
- Start page must be an integer
- Start page must be within `1..totalPages`
- Chapter start pages must be non-decreasing in row order

Additional behavior:

- Equal page numbers are allowed
- Equal page numbers should show a warning or explanatory note, but should not block save

Reasoning:

- Some books legitimately have multiple headings that start on the same rendered page
- Strictly forcing ascending unique pages would create unnecessary friction

## Data Model

### Runtime Chapter Sources

At runtime the converter should keep these concepts separate:

- `baseToc`: the flattened usable TOC from EPUB metadata
- `fallbackToc`: the heading-derived synthetic TOC when needed
- `overrideToc`: the user-edited chapter list loaded from disk
- `resolvedToc`: the final list actually used everywhere else

The UI and export pipeline must only consume `resolvedToc`.

### Override File Placement

Store override files under a project-managed directory inside the converter tool, for example:

- `tools/epub-to-xtc-converter/data/chapter-overrides/`

This keeps the data local to the tool and easy to inspect.

### Override File Key

The override file must be tied to a stable per-book identifier.

Recommended identifier:

- SHA-1 or SHA-256 hash of the EPUB file bytes

Why:

- File name alone is unreliable
- Title/author metadata may be duplicated or missing
- File hash survives reopening and avoids accidental cross-book collisions

### Override File Shape

Use JSON.

Recommended shape:

```json
{
  "version": 1,
  "bookHash": "sha256:...",
  "source": "toc|headings",
  "updatedAt": "2026-06-21T12:34:56.000Z",
  "chapters": [
    {
      "title": "第一章",
      "page": 1
    },
    {
      "title": "第二章",
      "page": 5
    }
  ]
}
```

Notes:

- Page numbers in persisted JSON should be 1-based because that matches user mental model
- Runtime can convert them to 0-based internally
- Deletions do not need tombstones in version 1 because the saved list is the full authoritative edited list

## Resolution Flow

When a book is loaded:

1. Load EPUB into renderer
2. Build base TOC from renderer metadata
3. If TOC page data is unusable, build fallback TOC from headings
4. Compute book hash
5. Look for saved override JSON by book hash
6. If override exists and is valid, use it as `resolvedToc`
7. Otherwise use automatic TOC result as `resolvedToc`

This means the user's edited chapter list always wins over auto-generated structure.

## Persistence Strategy

The current web tool is served by a local Python static server, so browser-only storage is not sufficient for project-level persistence. The override files should therefore be stored through a small local endpoint rather than `localStorage`.

Recommended implementation:

- Extend the local startup path with a minimal companion HTTP server endpoint
- Add endpoints:
  - `GET /api/chapter-overrides/<bookHash>`
  - `PUT /api/chapter-overrides/<bookHash>`

The static web app remains unchanged in deployment model: still local-only, but now with a writable metadata sidecar path.

Why this is preferred over browser-only storage:

- Survives browser cache/session resets
- Files are visible in the project
- Easier to back up and debug
- Avoids per-browser isolation problems

## Error Handling

### Override Load Failure

If reading an override file fails:

- Log the failure
- Fall back to automatic TOC
- Show a non-fatal status message if practical

The book must remain usable.

### Override Save Failure

If writing the override file fails:

- Keep the edit UI open
- Show a blocking save failure message
- Do not switch the resolved TOC to the unsaved edits

### Invalid Override JSON

If an override file exists but is malformed:

- Ignore it
- Fall back to automatic TOC
- Surface a warning if practical

## Testing

### Unit Tests

Add tests for:

- Chapter row validation
- Runtime 0-based / persisted 1-based page conversion
- Resolved TOC priority order
- Override JSON serialization and parsing
- Duplicate-page warning behavior

### Integration Tests

Add tests for:

- Loading a saved override for the same book hash
- Applying edited chapters to progress bar chapter calculations
- Export using edited chapters instead of automatic TOC

## Non-Goals

Version 1 does not include:

- Adding brand-new chapters
- Drag-and-drop reordering
- Editing original EPUB files
- Cross-book shared chapter templates
- Multi-user collaboration or cloud sync

## Rollout Plan

Phase 1:

- Add chapter override data model
- Add project-local persistence endpoint
- Add lightweight edit mode in sidebar
- Apply edited chapters to preview and export

Phase 2, if needed later:

- Add manual chapter insertion
- Add row reordering
- Add reset-to-auto button
- Add diff view between auto TOC and edited TOC
