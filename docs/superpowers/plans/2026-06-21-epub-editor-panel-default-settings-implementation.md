# EPUB Editor Panel And Default Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a dedicated sidebar chapter editor panel with per-row jump actions, and add a one-click way to persist current page/render settings as browser-local defaults for future EPUB files.

**Architecture:** Keep chapter overrides book-specific through the existing local sidecar API, but separate edit-mode rendering into its own panel container and treat the chapter draft as isolated UI state. Persist reusable reading/render defaults in `localStorage`, then hydrate and apply them during app startup and each book switch before preview rendering.

**Tech Stack:** Vanilla HTML/CSS/JavaScript, existing `web/app.js`, browser `localStorage`, Node `--test`

---

### Task 1: Add failing tests for the new editor panel and default settings persistence

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/chapter_editor.test.js`
- Create: `tools/epub-to-xtc-converter/web/default_settings.test.js`

- [ ] Add tests for `chapterEditorPanel`, per-row jump button wiring, default settings button, and `localStorage` persistence hooks.
- [ ] Run: `node --test tools/epub-to-xtc-converter/web/chapter_editor.test.js tools/epub-to-xtc-converter/web/default_settings.test.js`
- [ ] Confirm the new expectations fail before implementation.

### Task 2: Add the sidebar editor panel and default-settings button markup

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/index.html`
- Modify: `tools/epub-to-xtc-converter/web/style.css`

- [ ] Add a `保存为默认参数` button to the converter settings area.
- [ ] Add a dedicated `chapterEditorPanel` container separate from the read-only `chapterList`.
- [ ] Add styles for the new panel and row layout, including jump button, row inputs, and empty-state treatment.
- [ ] Run the relevant markup-oriented tests after the HTML/CSS changes.

### Task 3: Implement default settings persistence and hydration

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/app.js`
- Test: `tools/epub-to-xtc-converter/web/default_settings.test.js`

- [ ] Write helpers to collect current setting values into a serializable object.
- [ ] Write helpers to apply saved values back onto the form controls and runtime state.
- [ ] Persist defaults to `localStorage` from the new button.
- [ ] Load/apply saved defaults during startup and at the start of `switchToFile()`.
- [ ] Show success/failure feedback through the existing progress area.
- [ ] Run the focused default-settings test and verify it passes.

### Task 4: Refactor chapter editing into a dedicated panel with per-row jump

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/app.js`
- Modify: `tools/epub-to-xtc-converter/web/style.css`
- Test: `tools/epub-to-xtc-converter/web/chapter_editor.test.js`

- [ ] Route edit-mode rendering to `chapterEditorPanel` while keeping read-only rendering in `chapterList`.
- [ ] Add per-row `跳转` actions that clamp page numbers and refresh preview immediately.
- [ ] Keep edits draft-only until save and preserve current validation/save flow.
- [ ] Keep cancel behavior restoring list mode and current chapter highlight.
- [ ] Run the focused chapter-editor test and verify it passes.

### Task 5: Run focused and regression verification

**Files:**
- Verify only

- [ ] Run: `node --test tools/epub-to-xtc-converter/web/chapter_editor.test.js tools/epub-to-xtc-converter/web/default_settings.test.js tools/epub-to-xtc-converter/web/chapter_override_loading.test.js tools/epub-to-xtc-converter/web/chapter_overrides.test.js tools/epub-to-xtc-converter/web/local_server_behavior.test.js`
- [ ] If those pass, run the full web suite.
- [ ] Report any residual risk if there is still no browser-level interaction test for `localStorage`.

