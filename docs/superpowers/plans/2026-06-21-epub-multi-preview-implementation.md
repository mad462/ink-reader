# EPUB Multi Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 10-page preview mode that stays active while changing current page, and make descending chapter-page save errors specific enough to fix quickly.

**Architecture:** Extend the existing preview area with a second rendering branch for continuous page cards while preserving the current single-canvas path. Improve chapter validation centrally in `chapter_overrides.js` so both UI and tests share the more detailed message.

**Tech Stack:** Vanilla HTML/CSS/JavaScript, existing preview rendering helpers, Node `--test`

---

### Task 1: Add failing tests for clearer chapter-order errors and 10-page preview

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/chapter_overrides.test.js`
- Create: `tools/epub-to-xtc-converter/web/multi_page_preview.test.js`

- [ ] Add expectations that descending-page validation reports the conflicting page values.
- [ ] Add expectations for the new preview toggle, container, render loop, click behavior, and CSS.
- [ ] Run the focused tests and confirm they fail first.

### Task 2: Add preview-mode markup and styles

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/index.html`
- Modify: `tools/epub-to-xtc-converter/web/style.css`

- [ ] Add `10页预览` toggle button to the preview header.
- [ ] Add a dedicated multi-page preview container inside the preview area.
- [ ] Add styles for the grid, page cards, active state, and page-number captions.

### Task 3: Implement 10-page preview rendering and interaction

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/app.js`

- [ ] Add preview-mode state and DOM bindings.
- [ ] Branch `renderCurrentPage()` into single-page and multi-page paths.
- [ ] Render up to 10 consecutive pages with captions and active highlighting.
- [ ] Clicking a page card updates `currentPage` and keeps multi-preview mode active.
- [ ] Keep chapter highlighting and page-info text synchronized.

### Task 4: Improve chapter validation messaging

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/chapter_overrides.js`

- [ ] Include the conflicting previous/current page values in descending-page validation errors.
- [ ] Keep duplicate-page handling unchanged.

### Task 5: Verify focused regressions

**Files:**
- Verify only

- [ ] Run: `node --test tools/epub-to-xtc-converter/web/chapter_overrides.test.js tools/epub-to-xtc-converter/web/multi_page_preview.test.js`
- [ ] Then run the surrounding regression set for chapter editing and preview behavior.

