# EPUB Page Editing Project Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add persistent page-level editing to `tools/epub-to-xtc-converter`, including per-page dithering selection, single-page fine editing, BMP page replacement, and a reusable `*.xtcproj` project file that never mutates the source EPUB.

**Architecture:** Introduce a dedicated in-memory page-editing state module that `web/app.js` reads for preview and export decisions. Add a small project-file persistence layer built on top of the existing browser-side `JSZip`, and keep preview/export parity by routing both through the same replacement-aware, page-level dithering pipeline.

**Tech Stack:** Browser JavaScript, existing `JSZip` runtime, existing preview/export pipeline in `web/app.js`, Node source-level tests with `node --test`

---

## File Structure

### Existing files to modify

- Modify: `tools/epub-to-xtc-converter/web/app.js`
  - Integrate page-editing state with preview, navigation, export, and project save/load flows.
- Modify: `tools/epub-to-xtc-converter/web/index.html`
  - Add multi-page card controls, project file buttons/inputs, and single-page fine-edit controls.
- Modify: `tools/epub-to-xtc-converter/web/style.css`
  - Update multi-page grid to 5 columns, add card checkbox/badge styling, add single-page editor styling.
- Modify: `tools/epub-to-xtc-converter/web/multi_page_preview.test.js`
  - Extend source-level assertions for 5-column layout, per-page toggles, and click-through to single-page editing.
- Modify: `tools/epub-to-xtc-converter/web/render_settings.js`
  - Expose any render-setting snapshot helpers needed by pagination fingerprinting if they are already centralized here.

### New files to create

- Create: `tools/epub-to-xtc-converter/web/page_editing.js`
  - Own the canonical page-editing state and helper functions.
- Create: `tools/epub-to-xtc-converter/web/page_editing.test.js`
  - Test global/per-page dithering resolution and replacement bookkeeping.
- Create: `tools/epub-to-xtc-converter/web/page_replacement_io.js`
  - Validate BMP imports, normalize dimensions, and produce replacement descriptors for preview/export.
- Create: `tools/epub-to-xtc-converter/web/page_replacement_io.test.js`
  - Test BMP acceptance and replacement metadata flow.
- Create: `tools/epub-to-xtc-converter/web/project_file.js`
  - Save/load `*.xtcproj` archives, including manifest, EPUB, and BMP replacements.
- Create: `tools/epub-to-xtc-converter/web/project_file.test.js`
  - Test project archive serialization and deserialization.
- Create: `tools/epub-to-xtc-converter/web/pagination_fingerprint.js`
  - Compute stable pagination fingerprints from book identity plus layout-affecting settings.
- Create: `tools/epub-to-xtc-converter/web/pagination_fingerprint.test.js`
  - Test fingerprint stability and change detection.
- Create: `tools/epub-to-xtc-converter/web/page_editing_ui.test.js`
  - Test single-page editor controls and project buttons are present in source.

## Task 1: Add Canonical Page-Editing State

**Files:**
- Create: `tools/epub-to-xtc-converter/web/page_editing.js`
- Test: `tools/epub-to-xtc-converter/web/page_editing.test.js`
- Modify: `tools/epub-to-xtc-converter/web/index.html`

- [ ] **Step 1: Write the failing test for global select-all and per-page overrides**

```js
const test = require('node:test');
const assert = require('node:assert/strict');
const PageEditing = require('./page_editing.js');

test('resolves effective page dithering from global state plus overrides', function() {
    const state = PageEditing.createPageEditingState(20);

    PageEditing.setGlobalDitherSelected(state, true);
    assert.equal(PageEditing.isPageDitherSelected(state, 0), true);
    assert.equal(PageEditing.isPageDitherSelected(state, 19), true);

    PageEditing.setPageDitherSelected(state, 5, false);
    assert.equal(PageEditing.isPageDitherSelected(state, 5), false);
    assert.equal(PageEditing.isPageDitherSelected(state, 6), true);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node --test tools/epub-to-xtc-converter/web/page_editing.test.js`
Expected: FAIL with `Cannot find module './page_editing.js'` or missing export errors.

- [ ] **Step 3: Write minimal page-editing state implementation**

```js
(function(root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory();
    } else {
        root.PageEditing = factory();
    }
})(typeof self !== 'undefined' ? self : this, function() {
    'use strict';

    function createPageEditingState(totalPages) {
        return {
            totalPages: totalPages || 0,
            globalDitherSelected: false,
            ditherOverrides: {},
            bmpReplacements: {}
        };
    }

    function setGlobalDitherSelected(state, nextValue) {
        state.globalDitherSelected = !!nextValue;
        state.ditherOverrides = {};
    }

    function setPageDitherSelected(state, pageIndex, nextValue) {
        if (!!nextValue === !!state.globalDitherSelected) {
            delete state.ditherOverrides[String(pageIndex)];
            return;
        }
        state.ditherOverrides[String(pageIndex)] = !!nextValue;
    }

    function isPageDitherSelected(state, pageIndex) {
        var key = String(pageIndex);
        if (Object.prototype.hasOwnProperty.call(state.ditherOverrides, key)) {
            return !!state.ditherOverrides[key];
        }
        return !!state.globalDitherSelected;
    }

    return {
        createPageEditingState: createPageEditingState,
        setGlobalDitherSelected: setGlobalDitherSelected,
        setPageDitherSelected: setPageDitherSelected,
        isPageDitherSelected: isPageDitherSelected
    };
});
```

- [ ] **Step 4: Extend the test to cover replacement bookkeeping**

```js
test('tracks replacement file references per page', function() {
    const state = PageEditing.createPageEditingState(8);

    PageEditing.setPageReplacement(state, 2, 'replacements/page-000002.bmp');
    assert.equal(PageEditing.getPageReplacementPath(state, 2), 'replacements/page-000002.bmp');

    PageEditing.clearPageReplacement(state, 2);
    assert.equal(PageEditing.getPageReplacementPath(state, 2), null);
});
```

- [ ] **Step 5: Implement replacement helpers and export snapshot helpers**

```js
function setPageReplacement(state, pageIndex, relativePath) {
    state.bmpReplacements[String(pageIndex)] = relativePath;
}

function clearPageReplacement(state, pageIndex) {
    delete state.bmpReplacements[String(pageIndex)];
}

function getPageReplacementPath(state, pageIndex) {
    return state.bmpReplacements[String(pageIndex)] || null;
}

function exportPageEditingSnapshot(state) {
    return {
        globalDitherSelected: !!state.globalDitherSelected,
        ditherOverrides: Object.assign({}, state.ditherOverrides),
        bmpReplacements: Object.assign({}, state.bmpReplacements)
    };
}
```

- [ ] **Step 6: Run the focused test to verify it passes**

Run: `node --test tools/epub-to-xtc-converter/web/page_editing.test.js`
Expected: PASS with `2/2` tests passing.

- [ ] **Step 7: Wire the browser script into the page**

```html
<script src="page_editing.js"></script>
<script src="page_replacement_io.js"></script>
<script src="pagination_fingerprint.js"></script>
<script src="project_file.js"></script>
```

- [ ] **Step 8: Commit**

```bash
git -C tools/epub-to-xtc-converter add web/index.html web/page_editing.js web/page_editing.test.js
git -C tools/epub-to-xtc-converter commit -m "feat: add page editing state model"
```

## Task 2: Upgrade Multi-Page Preview for Page-Level Control

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/index.html`
- Modify: `tools/epub-to-xtc-converter/web/style.css`
- Modify: `tools/epub-to-xtc-converter/web/app.js`
- Modify: `tools/epub-to-xtc-converter/web/multi_page_preview.test.js`
- Create: `tools/epub-to-xtc-converter/web/page_editing_ui.test.js`

- [ ] **Step 1: Write the failing UI/source tests**

```js
test('multi-page preview button is renamed and page cards expose per-page controls', function() {
    assert.match(indexHtml, /多页预览模式/);
    assert.match(appSource, /className = 'preview-page-dither-toggle'/);
    assert.match(appSource, /className = 'preview-page-replacement-badge'/);
});

test('multi-page grid targets five columns on desktop', function() {
    assert.match(styleSource, /grid-template-columns:\s*repeat\(5,\s*minmax\(0,\s*1fr\)\)/);
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `node --test tools/epub-to-xtc-converter/web/multi_page_preview.test.js tools/epub-to-xtc-converter/web/page_editing_ui.test.js`
Expected: FAIL because the renamed button text and new card classes do not exist yet.

- [ ] **Step 3: Add the new controls to the HTML**

```html
<button id="multiPagePreviewBtn">多页预览模式</button>
<button id="toggleAllDitherBtn">全选抖动</button>
<input type="file" id="projectFileInput" accept=".xtcproj" style="display: none;">
<button id="saveProjectBtn">保存工程</button>
<button id="openProjectBtn">打开工程</button>
```

- [ ] **Step 4: Update the multi-page layout CSS to a fixed five-column grid**

```css
.multi-page-preview {
    --multi-page-preview-zoom: 1;
    display: grid;
    grid-template-columns: repeat(5, minmax(0, 1fr));
    gap: 12px;
}

.preview-page-dither-toggle {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-top: 8px;
}

.preview-page-replacement-badge {
    display: inline-flex;
    padding: 2px 8px;
    border-radius: 999px;
    background: rgba(125, 211, 252, 0.2);
    color: #d7f3ff;
    font-size: 11px;
}
```

- [ ] **Step 5: Render card-level dithering checkboxes and replacement badges**

```js
var ditherToggle = document.createElement('label');
var ditherInput = document.createElement('input');
var replacementBadge = document.createElement('div');

ditherToggle.className = 'preview-page-dither-toggle';
ditherInput.type = 'checkbox';
ditherInput.checked = PageEditing.isPageDitherSelected(pageEditingState, pageNum);
ditherInput.addEventListener('change', function(e) {
    e.stopPropagation();
    PageEditing.setPageDitherSelected(pageEditingState, pageNumForCard, e.target.checked);
});

replacementBadge.className = 'preview-page-replacement-badge';
replacementBadge.textContent = '已替换';
replacementBadge.style.display = PageEditing.getPageReplacementPath(pageEditingState, pageNum) ? 'inline-flex' : 'none';
```

- [ ] **Step 6: Add global select-all / clear-all behavior in `app.js`**

```js
toggleAllDitherBtn.addEventListener('click', function() {
    var nextValue = !pageEditingState.globalDitherSelected;
    PageEditing.setGlobalDitherSelected(pageEditingState, nextValue);
    toggleAllDitherBtn.textContent = nextValue ? '全部关闭抖动' : '全部开启抖动';
    renderCurrentPage();
});
```

- [ ] **Step 7: Run UI/source tests to verify they pass**

Run: `node --test tools/epub-to-xtc-converter/web/multi_page_preview.test.js tools/epub-to-xtc-converter/web/page_editing_ui.test.js`
Expected: PASS with the new button text, 5-column layout, and page-card controls detected.

- [ ] **Step 8: Commit**

```bash
git -C tools/epub-to-xtc-converter add web/app.js web/index.html web/style.css web/multi_page_preview.test.js web/page_editing_ui.test.js
git -C tools/epub-to-xtc-converter commit -m "feat: add page-level controls to multi-page preview"
```

## Task 3: Add Single-Page Fine Editing and Click-Through

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/index.html`
- Modify: `tools/epub-to-xtc-converter/web/style.css`
- Modify: `tools/epub-to-xtc-converter/web/app.js`
- Create: `tools/epub-to-xtc-converter/web/page_editing_ui.test.js`

- [ ] **Step 1: Write the failing test for click-through to single-page editing**

```js
test('clicking a multi-page card switches back to single-page editing', function() {
    assert.match(appSource, /setCurrentPageFromMultiPreview\(pageNumForCard, true\);\s*multiPagePreviewMode = false;\s*renderCurrentPage\(\);/);
    assert.match(indexHtml, /id="singlePageEditingPanel"/);
    assert.match(indexHtml, /id="currentPageDitherCheckbox"/);
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `node --test tools/epub-to-xtc-converter/web/page_editing_ui.test.js`
Expected: FAIL because the single-page editing panel and click-through logic do not exist yet.

- [ ] **Step 3: Add the single-page editing panel markup**

```html
<div class="single-page-editing-panel" id="singlePageEditingPanel">
    <label class="checkbox-group">
        <input type="checkbox" id="currentPageDitherCheckbox">
        <span>本页启用抖动</span>
    </label>
    <button id="replaceCurrentPageBtn" type="button">载入 BMP 替换</button>
    <button id="clearCurrentPageReplacementBtn" type="button">清除本页替换</button>
    <button id="returnToMultiPageBtn" type="button">返回多页预览模式</button>
    <div id="currentPageReplacementStatus">当前页未替换</div>
</div>
<input type="file" id="pageReplacementInput" accept=".bmp" style="display: none;">
```

- [ ] **Step 4: Style the panel so it fits beside or below the preview without breaking the existing layout**

```css
.single-page-editing-panel {
    display: grid;
    gap: 10px;
    padding: 12px;
    background: #1f2937;
    border-top: 1px solid #334155;
}
```

- [ ] **Step 5: Implement the click-through and current-page editor sync**

```js
function openSinglePageEditor(pageIndex) {
    multiPagePreviewMode = false;
    currentPage = pageIndex;
    syncCurrentPageEditingUi();
    renderCurrentPage();
}

function syncCurrentPageEditingUi() {
    currentPageDitherCheckbox.checked = PageEditing.isPageDitherSelected(pageEditingState, currentPage);
    currentPageReplacementStatus.textContent = PageEditing.getPageReplacementPath(pageEditingState, currentPage)
        ? '当前页正在使用外部 BMP'
        : '当前页未替换';
}
```

- [ ] **Step 6: Hook page-card clicks and single-page checkbox changes**

```js
card.addEventListener('click', function() {
    openSinglePageEditor(pageNumForCard);
});

currentPageDitherCheckbox.addEventListener('change', function(e) {
    PageEditing.setPageDitherSelected(pageEditingState, currentPage, e.target.checked);
    syncCurrentPageEditingUi();
    renderCurrentPage();
});
```

- [ ] **Step 7: Run the focused UI test to verify it passes**

Run: `node --test tools/epub-to-xtc-converter/web/page_editing_ui.test.js`
Expected: PASS with single-page editor controls and click-through source detected.

- [ ] **Step 8: Commit**

```bash
git -C tools/epub-to-xtc-converter add web/app.js web/index.html web/style.css web/page_editing_ui.test.js
git -C tools/epub-to-xtc-converter commit -m "feat: add single-page fine editing controls"
```

## Task 4: Route Preview and Export Through Replacement-Aware Per-Page Dithering

**Files:**
- Create: `tools/epub-to-xtc-converter/web/page_replacement_io.js`
- Create: `tools/epub-to-xtc-converter/web/page_replacement_io.test.js`
- Modify: `tools/epub-to-xtc-converter/web/app.js`

- [ ] **Step 1: Write the failing tests for BMP replacement metadata and validation**

```js
const ReplacementIo = require('./page_replacement_io.js');

test('accepts bmp files and normalizes replacement metadata', async function() {
    const result = await ReplacementIo.buildReplacementRecord({
        filename: 'cover.bmp',
        arrayBuffer: new ArrayBuffer(8),
        pageIndex: 0,
        width: 480,
        height: 800
    });

    assert.equal(result.relativePath, 'replacements/page-000000.bmp');
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `node --test tools/epub-to-xtc-converter/web/page_replacement_io.test.js`
Expected: FAIL because the module does not exist yet.

- [ ] **Step 3: Create the replacement helper module**

```js
async function buildReplacementRecord(input) {
    if (!/\.bmp$/i.test(input.filename)) {
        throw new Error('Only BMP replacements are supported.');
    }

    return {
        pageIndex: input.pageIndex,
        relativePath: 'replacements/page-' + String(input.pageIndex).padStart(6, '0') + '.bmp',
        bytes: input.arrayBuffer
    };
}
```

- [ ] **Step 4: Add failing tests for preview/export precedence**

```js
test('app source prefers replacement images before normal page rendering', function() {
    assert.match(appSource, /var replacement = getCurrentPageReplacementRecord\(currentPage\);/);
    assert.match(appSource, /if \(replacement\) \{\s*return renderReplacementPreviewImage\(replacement\);\s*\}/);
    assert.match(appSource, /if \(PageEditing\.isPageDitherSelected\(pageEditingState, pageNum\)\)/);
});
```

- [ ] **Step 5: Implement replacement-aware rendering in `app.js`**

```js
async function renderPageImageData(pageNum) {
    var replacement = getCurrentPageReplacementRecord(pageNum);
    if (replacement) {
        return renderReplacementPreviewImage(replacement);
    }

    renderer.goToPage(pageNum);
    renderer.renderCurrentPage();

    var imageData = new ImageData(
        new Uint8ClampedArray(renderer.getFrameBuffer()),
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    if (PageEditing.isPageDitherSelected(pageEditingState, pageNum)) {
        return processPreviewImageData(imageData);
    }

    return imageData;
}
```

- [ ] **Step 6: Use the same precedence rule in export code**

```js
async function buildExportPageImageData(pageNum) {
    var replacement = getCurrentPageReplacementRecord(pageNum);
    if (replacement) {
        return loadReplacementImageDataForExport(replacement);
    }

    var rendered = await renderPageImageDataWithoutDither(pageNum);
    if (PageEditing.isPageDitherSelected(pageEditingState, pageNum)) {
        return applyDithering(rendered, bits, strength, true);
    }
    return rendered;
}
```

- [ ] **Step 7: Run focused tests to verify they pass**

Run: `node --test tools/epub-to-xtc-converter/web/page_replacement_io.test.js tools/epub-to-xtc-converter/web/page_editing.test.js tools/epub-to-xtc-converter/web/multi_page_preview.test.js`
Expected: PASS with replacement helpers and preview/export precedence covered.

- [ ] **Step 8: Commit**

```bash
git -C tools/epub-to-xtc-converter add web/app.js web/page_replacement_io.js web/page_replacement_io.test.js web/multi_page_preview.test.js
git -C tools/epub-to-xtc-converter commit -m "feat: add bmp replacement pipeline"
```

## Task 5: Save and Reopen `*.xtcproj` Project Files

**Files:**
- Create: `tools/epub-to-xtc-converter/web/project_file.js`
- Create: `tools/epub-to-xtc-converter/web/project_file.test.js`
- Modify: `tools/epub-to-xtc-converter/web/index.html`
- Modify: `tools/epub-to-xtc-converter/web/app.js`

- [ ] **Step 1: Write the failing archive round-trip test**

```js
const ProjectFile = require('./project_file.js');

test('serializes manifest, epub, and replacements into an xtcproj archive', async function() {
    const bytes = await ProjectFile.serializeProject({
        manifest: { version: 1, pageEditing: { globalDitherSelected: true } },
        epubBytes: new Uint8Array([1, 2, 3]),
        replacements: {
            'replacements/page-000000.bmp': new Uint8Array([4, 5, 6])
        }
    });

    assert.ok(bytes.byteLength > 0);
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `node --test tools/epub-to-xtc-converter/web/project_file.test.js`
Expected: FAIL because `project_file.js` does not exist yet.

- [ ] **Step 3: Implement archive serialization/deserialization**

```js
async function serializeProject(input) {
    var zip = new JSZip();
    zip.file('manifest.json', JSON.stringify(input.manifest, null, 2));
    zip.file('source/book.epub', input.epubBytes);

    Object.keys(input.replacements || {}).forEach(function(path) {
        zip.file(path, input.replacements[path]);
    });

    return zip.generateAsync({ type: 'uint8array' });
}

async function deserializeProject(bytes) {
    var zip = await JSZip.loadAsync(bytes);
    return {
        manifest: JSON.parse(await zip.file('manifest.json').async('string')),
        epubBytes: new Uint8Array(await zip.file('source/book.epub').async('uint8array')),
        zip: zip
    };
}
```

- [ ] **Step 4: Add save/open project UI integration**

```js
saveProjectBtn.addEventListener('click', async function() {
    var projectBytes = await ProjectFile.serializeProject(buildCurrentProjectPayload());
    downloadFile(new Blob([projectBytes]), buildProjectFilename());
});

openProjectBtn.addEventListener('click', function() {
    projectFileInput.click();
});
```

- [ ] **Step 5: Implement project load rehydration**

```js
projectFileInput.addEventListener('change', async function(e) {
    var file = e.target.files[0];
    if (!file) return;

    var project = await ProjectFile.deserializeProject(new Uint8Array(await file.arrayBuffer()));
    await loadBookFromProject(project);
    applyProjectManifest(project.manifest);
});
```

- [ ] **Step 6: Run the round-trip and UI tests**

Run: `node --test tools/epub-to-xtc-converter/web/project_file.test.js tools/epub-to-xtc-converter/web/page_editing_ui.test.js`
Expected: PASS with the project archive round-trip and button wiring covered.

- [ ] **Step 7: Commit**

```bash
git -C tools/epub-to-xtc-converter add web/app.js web/index.html web/project_file.js web/project_file.test.js web/page_editing_ui.test.js
git -C tools/epub-to-xtc-converter commit -m "feat: add xtc project persistence"
```

## Task 6: Detect Pagination Drift Before Keeping Page-Level Overrides

**Files:**
- Create: `tools/epub-to-xtc-converter/web/pagination_fingerprint.js`
- Create: `tools/epub-to-xtc-converter/web/pagination_fingerprint.test.js`
- Modify: `tools/epub-to-xtc-converter/web/render_settings.js`
- Modify: `tools/epub-to-xtc-converter/web/app.js`

- [ ] **Step 1: Write the failing fingerprint test**

```js
const Fingerprint = require('./pagination_fingerprint.js');

test('changes fingerprint when layout-affecting settings change', function() {
    const base = Fingerprint.computePaginationFingerprint({
        bookHash: 'sha256_a',
        fontFamily: 'Literata',
        fontSize: 34,
        margin: 16
    });

    const changed = Fingerprint.computePaginationFingerprint({
        bookHash: 'sha256_a',
        fontFamily: 'Literata',
        fontSize: 36,
        margin: 16
    });

    assert.notEqual(base, changed);
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `node --test tools/epub-to-xtc-converter/web/pagination_fingerprint.test.js`
Expected: FAIL because the module does not exist yet.

- [ ] **Step 3: Implement a stable fingerprint helper**

```js
function computePaginationFingerprint(input) {
    return JSON.stringify({
        bookHash: input.bookHash,
        devicePreset: input.devicePreset,
        customWidth: input.customWidth,
        customHeight: input.customHeight,
        orientation: input.orientation,
        fontFamily: input.fontFamily,
        fontSize: input.fontSize,
        fontWeight: input.fontWeight,
        lineHeight: input.lineHeight,
        margin: input.margin,
        textAlign: input.textAlign,
        hyphenation: input.hyphenation,
        hyphenationLang: input.hyphenationLang
    });
}
```

- [ ] **Step 4: Add failing source test for invalidation prompt logic**

```js
test('app source warns before keeping page overrides across pagination drift', function() {
    assert.match(appSource, /if \(hasPageLevelEdits\(\) && nextFingerprint !== currentPaginationFingerprint\)/);
    assert.match(appSource, /clearPageLevelEditingState\(\);/);
});
```

- [ ] **Step 5: Implement invalidation handling in settings application**

```js
function guardPaginationChange(nextSettingsSnapshot) {
    var nextFingerprint = PaginationFingerprint.computePaginationFingerprint(nextSettingsSnapshot);
    if (!hasPageLevelEdits() || nextFingerprint === currentPaginationFingerprint) {
        return true;
    }

    if (!window.confirm('修改这些参数会导致逐页抖动和页面替换失效，是否清空页级编辑并继续？')) {
        return false;
    }

    clearPageLevelEditingState();
    currentPaginationFingerprint = nextFingerprint;
    return true;
}
```

- [ ] **Step 6: Run focused tests to verify they pass**

Run: `node --test tools/epub-to-xtc-converter/web/pagination_fingerprint.test.js tools/epub-to-xtc-converter/web/page_editing.test.js tools/epub-to-xtc-converter/web/page_editing_ui.test.js`
Expected: PASS with fingerprint changes and invalidation guard coverage.

- [ ] **Step 7: Commit**

```bash
git -C tools/epub-to-xtc-converter add web/app.js web/render_settings.js web/pagination_fingerprint.js web/pagination_fingerprint.test.js web/page_editing_ui.test.js
git -C tools/epub-to-xtc-converter commit -m "feat: guard page edits against pagination drift"
```

## Task 7: Run Full Regression and Polish Error Handling

**Files:**
- Modify: `tools/epub-to-xtc-converter/web/app.js`
- Modify: `tools/epub-to-xtc-converter/web/project_file.test.js`
- Modify: `tools/epub-to-xtc-converter/web/page_replacement_io.test.js`
- Modify: `tools/epub-to-xtc-converter/web/multi_page_preview.test.js`

- [ ] **Step 1: Add failing tests for broken project / replacement error messaging**

```js
test('project deserialization rejects archives missing source/book.epub', async function() {
    await assert.rejects(
        () => ProjectFile.deserializeProject(new Uint8Array([80, 75])),
        /source\/book\.epub/
    );
});

test('replacement loader rejects non-bmp extensions with a user-safe message', async function() {
    await assert.rejects(
        () => ReplacementIo.buildReplacementRecord({ filename: 'page.png', arrayBuffer: new ArrayBuffer(1), pageIndex: 1 }),
        /Only BMP replacements are supported/
    );
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `node --test tools/epub-to-xtc-converter/web/project_file.test.js tools/epub-to-xtc-converter/web/page_replacement_io.test.js`
Expected: FAIL because the new guardrails are not fully implemented yet.

- [ ] **Step 3: Implement the missing validation and status messaging**

```js
if (!zip.file('source/book.epub')) {
    throw new Error('Project is missing source/book.epub');
}

function showPageReplacementError(message) {
    progressContainer.style.display = 'block';
    progressFill.style.width = '100%';
    progressText.textContent = message;
}
```

- [ ] **Step 4: Run the full targeted regression suite**

Run:

```bash
node --check tools/epub-to-xtc-converter/web/app.js
node --test tools/epub-to-xtc-converter/web/chapter_editor.test.js tools/epub-to-xtc-converter/web/default_settings.test.js tools/epub-to-xtc-converter/web/chapter_overrides.test.js tools/epub-to-xtc-converter/web/multi_page_preview.test.js tools/epub-to-xtc-converter/web/preview_style.test.js tools/epub-to-xtc-converter/web/chapter_override_export.test.js tools/epub-to-xtc-converter/web/page_editing.test.js tools/epub-to-xtc-converter/web/page_editing_ui.test.js tools/epub-to-xtc-converter/web/page_replacement_io.test.js tools/epub-to-xtc-converter/web/project_file.test.js tools/epub-to-xtc-converter/web/pagination_fingerprint.test.js
```

Expected: PASS with zero failures.

- [ ] **Step 5: Restart the local server and smoke-check the latest frontend assets**

Run:

```powershell
$port = 8000
$listener = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
if ($listener) { Stop-Process -Id $listener.OwningProcess -Force }
Start-Process -FilePath "C:\Users\46289\AppData\Local\Programs\Python\Python39\python.exe" -ArgumentList "server.py","8000" -WorkingDirectory "D:\FUCKIDF\ink-reader\tools\epub-to-xtc-converter" -WindowStyle Hidden
Start-Sleep -Seconds 2
(Invoke-WebRequest -Uri "http://127.0.0.1:8000/web/" -Method Head -UseBasicParsing).Headers
```

Expected: `Cache-Control: no-store` present and the new process listening on port `8000`.

- [ ] **Step 6: Commit**

```bash
git -C tools/epub-to-xtc-converter add web/app.js web/multi_page_preview.test.js web/page_replacement_io.test.js web/project_file.test.js
git -C tools/epub-to-xtc-converter commit -m "test: cover page editing project regressions"
```

## Self-Review

### Spec coverage

- Multi-page preview mode rename, 5-column target, and page-card controls are covered in Task 2.
- Single-page click-through editing is covered in Task 3.
- BMP replacement preview/export parity is covered in Task 4.
- `*.xtcproj` persistence is covered in Task 5.
- Pagination fingerprint invalidation is covered in Task 6.
- Broken asset/project handling and full verification are covered in Task 7.

### Placeholder scan

- No `TODO`, `TBD`, or “similar to above” placeholders remain.
- Every task includes exact file paths and exact verification commands.
- Code-bearing steps include concrete snippets instead of abstract instructions.

### Type consistency

- Page editing state consistently uses `globalDitherSelected`, `ditherOverrides`, and `bmpReplacements`.
- Project persistence consistently uses `manifest.json`, `source/book.epub`, and `replacements/page-XXXXXX.bmp`.
- Pagination invalidation consistently uses `currentPaginationFingerprint` and `computePaginationFingerprint(...)`.
