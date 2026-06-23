# EPUB Chapter Overrides Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add editable persistent chapter overrides to the EPUB web converter so users can rename chapters, change start pages, delete chapters, and have those edits drive preview, progress calculations, and exported XTC metadata.

**Architecture:** Extend the local web-serving flow with a tiny writable JSON API for per-book chapter overrides, then add a focused browser-side override layer that resolves the final TOC from automatic chapter detection plus saved user edits. Keep the UI lightweight by editing chapters inline in the existing sidebar instead of adding a new screen or modal.

**Tech Stack:** Windows batch launcher, Python standard library HTTP server replacement or wrapper, vanilla JavaScript, existing `web/app.js` flow, Node `--test`

---

### Task 1: Add a local writable chapter-override API

**Files:**
- Create: `tools/epub-to-xtc-converter/server.py`
- Create: `tools/epub-to-xtc-converter/data/chapter-overrides/.gitkeep`
- Modify: `tools/epub-to-xtc-converter/start_web.bat`
- Test: `tools/epub-to-xtc-converter/web/local_server.test.js`

- [ ] **Step 1: Write the failing server API expectations**

Add these tests to `tools/epub-to-xtc-converter/web/local_server.test.js`:

```js
test('local web server starts the writable Python companion server instead of bare http.server', function() {
    assert.match(startScript, /py -3 server\.py %PORT%/);
});

test('local server stores chapter overrides under the project data directory', function() {
    const serverSource = fs.readFileSync(
        path.join(__dirname, '..', 'server.py'),
        'utf8'
    );

    assert.match(serverSource, /chapter-overrides/);
    assert.match(serverSource, /do_GET/);
    assert.match(serverSource, /do_PUT/);
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
node --test tools/epub-to-xtc-converter/web/local_server.test.js
```

Expected: FAIL because `server.py` does not exist and `start_web.bat` still uses `http.server`.

- [ ] **Step 3: Create the writable server**

Create `tools/epub-to-xtc-converter/server.py`:

```python
import json
import os
import re
import sys
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OVERRIDES_DIR = ROOT / "data" / "chapter-overrides"
OVERRIDES_DIR.mkdir(parents=True, exist_ok=True)

BOOK_HASH_RE = re.compile(r"^sha256:[a-f0-9]{64}$")


def get_override_path(book_hash: str) -> Path:
    return OVERRIDES_DIR / (book_hash.replace(":", "_") + ".json")


class Handler(SimpleHTTPRequestHandler):
    def translate_path(self, path):
        return str(ROOT / path.lstrip("/"))

    def _send_json(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _extract_hash(self):
        prefix = "/api/chapter-overrides/"
        if not self.path.startswith(prefix):
            return None
        return self.path[len(prefix):].split("?", 1)[0]

    def do_GET(self):
        book_hash = self._extract_hash()
        if not book_hash:
            return super().do_GET()
        if not BOOK_HASH_RE.match(book_hash):
            return self._send_json(HTTPStatus.BAD_REQUEST, {"error": "invalid_book_hash"})

        override_path = get_override_path(book_hash)
        if not override_path.exists():
            return self._send_json(HTTPStatus.NOT_FOUND, {"error": "not_found"})

        payload = json.loads(override_path.read_text(encoding="utf-8"))
        return self._send_json(HTTPStatus.OK, payload)

    def do_PUT(self):
        book_hash = self._extract_hash()
        if not book_hash or not BOOK_HASH_RE.match(book_hash):
            return self._send_json(HTTPStatus.BAD_REQUEST, {"error": "invalid_book_hash"})

        content_length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(content_length)
        payload = json.loads(raw.decode("utf-8"))

        override_path = get_override_path(book_hash)
        override_path.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8"
        )
        return self._send_json(HTTPStatus.OK, {"ok": True})


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"[INFO] Serving on http://127.0.0.1:{port}/")
    server.serve_forever()
```

Create `tools/epub-to-xtc-converter/data/chapter-overrides/.gitkeep` as an empty file.

- [ ] **Step 4: Update the launcher to use the writable server**

Update `tools/epub-to-xtc-converter/start_web.bat`:

```bat
:use_py
start "" "%URL%"
py -3 server.py %PORT%
exit /b %errorlevel%

:use_python
start "" "%URL%"
python server.py %PORT%
exit /b %errorlevel%
```

- [ ] **Step 5: Run the server test to verify it passes**

Run:

```powershell
node --test tools/epub-to-xtc-converter/web/local_server.test.js
```

Expected: PASS with the new writable-server expectations.

- [ ] **Step 6: Commit**

```bash
git add tools/epub-to-xtc-converter/server.py tools/epub-to-xtc-converter/start_web.bat tools/epub-to-xtc-converter/data/chapter-overrides/.gitkeep tools/epub-to-xtc-converter/web/local_server.test.js
git commit -m "feat: add writable chapter override server"
```

### Task 2: Add a focused chapter-override data layer

**Files:**
- Create: `tools/epub-to-xtc-converter/web/chapter_overrides.js`
- Create: `tools/epub-to-xtc-converter/web/chapter_overrides.test.js`
- Create: `tools/epub-to-xtc-converter/web/chapter_overrides_bootstrap.test.js`
- Modify: `tools/epub-to-xtc-converter/web/index.html`
- Modify: `tools/epub-to-xtc-converter/web/app.js`

- [ ] **Step 1: Write the failing data-layer tests**

Create `tools/epub-to-xtc-converter/web/chapter_overrides.test.js`:

```js
const test = require('node:test');
const assert = require('node:assert/strict');
const overrides = require('./chapter_overrides');

test('converts runtime chapters to persisted 1-based page payload', function() {
    const payload = overrides.buildOverridePayload({
        bookHash: 'sha256:' + 'a'.repeat(64),
        source: 'headings',
        chapters: [
            { title: '第一章', page: 0 },
            { title: '第二章', page: 4 }
        ]
    });

    assert.equal(payload.chapters[0].page, 1);
    assert.equal(payload.chapters[1].page, 5);
});

test('hydrates persisted override payload back into runtime chapters', function() {
    const resolved = overrides.hydrateOverridePayload({
        chapters: [
            { title: '第一章', page: 1 },
            { title: '第二章', page: 5 }
        ]
    });

    assert.deepEqual(resolved.map(function(ch) { return ch.page; }), [0, 4]);
});

test('validates editable chapter rows and blocks descending pages', function() {
    const result = overrides.validateEditableChapters([
        { title: '第一章', page: 5 },
        { title: '第二章', page: 4 }
    ], 100);

    assert.equal(result.ok, false);
    assert.match(result.message, /起始页/);
});

test('allows duplicate pages but returns a warning', function() {
    const result = overrides.validateEditableChapters([
        { title: '第一章', page: 5 },
        { title: '第二章', page: 5 }
    ], 100);

    assert.equal(result.ok, true);
    assert.equal(result.warning, '存在多个章节落在同一页，将按当前顺序保存。');
});
```

Create `tools/epub-to-xtc-converter/web/chapter_overrides_bootstrap.test.js`:

```js
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');

const indexHtml = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');

test('loads chapter overrides helper before app bootstrap', function() {
    assert.match(
        indexHtml,
        /<script src="chapter_overrides\.js"><\/script>[\s\S]*<script src="app\.js"><\/script>/m
    );
});
```

- [ ] **Step 2: Run the new tests to verify they fail**

Run:

```powershell
node --test tools/epub-to-xtc-converter/web/chapter_overrides.test.js tools/epub-to-xtc-converter/web/chapter_overrides_bootstrap.test.js
```

Expected: FAIL because the helper file is not present and `index.html` does not load it.

- [ ] **Step 3: Write the minimal chapter override helper**

Create `tools/epub-to-xtc-converter/web/chapter_overrides.js`:

```js
(function(root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory();
    } else {
        root.ChapterOverrides = factory();
    }
})(typeof globalThis !== 'undefined' ? globalThis : this, function() {
    'use strict';

    function normalizeChapter(row, index) {
        return {
            title: String(row && row.title || '').trim() || ('章节 ' + (index + 1)),
            page: Number(row && row.page)
        };
    }

    function buildOverridePayload(input) {
        return {
            version: 1,
            bookHash: input.bookHash,
            source: input.source || 'toc',
            updatedAt: new Date().toISOString(),
            chapters: (input.chapters || []).map(function(chapter, index) {
                var row = normalizeChapter(chapter, index);
                return {
                    title: row.title,
                    page: row.page + 1
                };
            })
        };
    }

    function hydrateOverridePayload(payload) {
        return (payload && payload.chapters || []).map(function(chapter, index) {
            var row = normalizeChapter({
                title: chapter.title,
                page: Number(chapter.page) - 1
            }, index);
            return {
                title: row.title,
                name: row.title,
                page: Math.max(0, row.page),
                startPage: Math.max(0, row.page),
                depth: 0
            };
        });
    }

    function validateEditableChapters(rows, totalPages) {
        var warning = '';
        var i;

        for (i = 0; i < rows.length; i++) {
            var row = rows[i];
            var page = Number(row.page);
            var title = String(row.title || '').trim();

            if (!title) {
                return { ok: false, message: '章节名称不能为空。' };
            }
            if (!Number.isInteger(page)) {
                return { ok: false, message: '章节起始页必须是整数。' };
            }
            if (page < 1 || page > totalPages) {
                return { ok: false, message: '章节起始页必须在有效页码范围内。' };
            }
            if (i > 0 && page < Number(rows[i - 1].page)) {
                return { ok: false, message: '章节起始页不能倒退，请按顺序调整。' };
            }
            if (i > 0 && page === Number(rows[i - 1].page)) {
                warning = '存在多个章节落在同一页，将按当前顺序保存。';
            }
        }

        return { ok: true, warning: warning };
    }

    return {
        buildOverridePayload: buildOverridePayload,
        hydrateOverridePayload: hydrateOverridePayload,
        validateEditableChapters: validateEditableChapters
    };
});
```

- [ ] **Step 4: Load the helper into the web page**

Insert this before `app.js` in `tools/epub-to-xtc-converter/web/index.html`:

```html
<script src="chapter_overrides.js"></script>
```

- [ ] **Step 5: Add the runtime override state slots**

Near the top of `tools/epub-to-xtc-converter/web/app.js`, add:

```js
let currentBaseToc = [];
let currentResolvedToc = [];
let currentBookHash = '';
let currentChapterSource = 'toc';
```

Then update TOC assignment flow so the app can distinguish base TOC from resolved TOC:

```js
currentBaseToc = renderer.getToc() || [];
currentToc = currentBaseToc;
currentResolvedToc = currentToc;
```

This is just the minimum staging hook for later tasks.

- [ ] **Step 6: Run the helper tests to verify they pass**

Run:

```powershell
node --test tools/epub-to-xtc-converter/web/chapter_overrides.test.js tools/epub-to-xtc-converter/web/chapter_overrides_bootstrap.test.js
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add tools/epub-to-xtc-converter/web/chapter_overrides.js tools/epub-to-xtc-converter/web/chapter_overrides.test.js tools/epub-to-xtc-converter/web/chapter_overrides_bootstrap.test.js tools/epub-to-xtc-converter/web/index.html tools/epub-to-xtc-converter/web/app.js
git commit -m "feat: add chapter override data layer"
```

### Task 3: Resolve automatic chapters plus saved overrides on book load

**Files:**
- Create: `tools/epub-to-xtc-converter/web/book_identity.js`
- Create: `tools/epub-to-xtc-converter/web/book_identity.test.js`
- Create: `tools/epub-to-xtc-converter/web/book_identity_bootstrap.test.js`
- Modify: `tools/epub-to-xtc-converter/web/index.html`
- Modify: `tools/epub-to-xtc-converter/web/app.js`
- Modify: `tools/epub-to-xtc-converter/web/epub_navigation.js`

- [ ] **Step 1: Write the failing book-identity tests**

Create `tools/epub-to-xtc-converter/web/book_identity.test.js`:

```js
const test = require('node:test');
const assert = require('node:assert/strict');
const identity = require('./book_identity');

test('builds a stable sha256-prefixed hash string from epub bytes', async function() {
    const hash = await identity.hashArrayBuffer(new TextEncoder().encode('demo').buffer);
    assert.match(hash, /^sha256:[a-f0-9]{64}$/);
});
```

Create `tools/epub-to-xtc-converter/web/book_identity_bootstrap.test.js`:

```js
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');

const indexHtml = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');

test('loads book identity helper before app bootstrap', function() {
    assert.match(
        indexHtml,
        /<script src="book_identity\.js"><\/script>[\s\S]*<script src="app\.js"><\/script>/m
    );
});
```

- [ ] **Step 2: Run the book-identity tests to verify they fail**

Run:

```powershell
node --test tools/epub-to-xtc-converter/web/book_identity.test.js tools/epub-to-xtc-converter/web/book_identity_bootstrap.test.js
```

Expected: FAIL because the new helper is not present.

- [ ] **Step 3: Add the book hashing helper**

Create `tools/epub-to-xtc-converter/web/book_identity.js`:

```js
(function(root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory(require('node:crypto').webcrypto);
    } else {
        root.BookIdentity = factory(globalThis.crypto);
    }
})(typeof globalThis !== 'undefined' ? globalThis : this, function(cryptoImpl) {
    'use strict';

    async function hashArrayBuffer(buffer) {
        var digest = await cryptoImpl.subtle.digest('SHA-256', buffer);
        var bytes = Array.from(new Uint8Array(digest));
        return 'sha256:' + bytes.map(function(byte) {
            return byte.toString(16).padStart(2, '0');
        }).join('');
    }

    return {
        hashArrayBuffer: hashArrayBuffer
    };
});
```

Load it before `app.js` in `tools/epub-to-xtc-converter/web/index.html`:

```html
<script src="book_identity.js"></script>
```

- [ ] **Step 4: Add override loading during `switchToFile()`**

In `tools/epub-to-xtc-converter/web/app.js`, add:

```js
async function loadChapterOverride(bookHash) {
    var response = await fetch('/api/chapter-overrides/' + encodeURIComponent(bookHash), {
        cache: 'no-store'
    });
    if (response.status === 404) {
        return null;
    }
    if (!response.ok) {
        throw new Error('Failed to load chapter override');
    }
    return response.json();
}
```

Then inside `switchToFile(index)` after TOC resolution:

```js
currentBookHash = typeof BookIdentity !== 'undefined'
    ? await BookIdentity.hashArrayBuffer(fileBuffer)
    : '';
currentChapterSource = currentTocIsEstimated ? 'headings' : 'toc';

var overridePayload = null;
if (currentBookHash && typeof ChapterOverrides !== 'undefined') {
    try {
        overridePayload = await loadChapterOverride(currentBookHash);
    } catch (overrideErr) {
        console.warn('Failed to load chapter override:', overrideErr);
    }
}

currentBaseToc = currentToc.slice();
currentResolvedToc = overridePayload
    ? ChapterOverrides.hydrateOverridePayload(overridePayload)
    : currentBaseToc.slice();
currentToc = currentResolvedToc.slice();
```

- [ ] **Step 5: Keep chapter export and progress calculations consuming resolved chapters**

In `tools/epub-to-xtc-converter/web/app.js`, update all chapter-dependent helpers to use `currentToc` as the resolved list and do not re-read from `renderer.getToc()` after load.

The concrete rule for this step:

```js
// No code path after switchToFile should overwrite currentToc from renderer again.
```

This is a verification step against the actual file, not a placeholder to invent extra logic.

- [ ] **Step 6: Run the identity tests to verify they pass**

Run:

```powershell
node --test tools/epub-to-xtc-converter/web/book_identity.test.js tools/epub-to-xtc-converter/web/book_identity_bootstrap.test.js
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add tools/epub-to-xtc-converter/web/book_identity.js tools/epub-to-xtc-converter/web/book_identity.test.js tools/epub-to-xtc-converter/web/book_identity_bootstrap.test.js tools/epub-to-xtc-converter/web/index.html tools/epub-to-xtc-converter/web/app.js tools/epub-to-xtc-converter/web/epub_navigation.js
git commit -m "feat: load saved chapter overrides on book open"
```

### Task 4: Add inline chapter editing UI and save flow

**Files:**
- Create: `tools/epub-to-xtc-converter/web/chapter_editor.test.js`
- Modify: `tools/epub-to-xtc-converter/web/index.html`
- Modify: `tools/epub-to-xtc-converter/web/style.css`
- Modify: `tools/epub-to-xtc-converter/web/app.js`

- [ ] **Step 1: Write the failing UI bootstrap expectations**

Create `tools/epub-to-xtc-converter/web/chapter_editor.test.js`:

```js
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');

const indexHtml = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
const appSource = fs.readFileSync(path.join(__dirname, 'app.js'), 'utf8');

test('chapter panel exposes edit, save, and cancel controls', function() {
    assert.match(indexHtml, /id="editChaptersBtn"/);
    assert.match(indexHtml, /id="saveChaptersBtn"/);
    assert.match(indexHtml, /id="cancelChaptersBtn"/);
});

test('app source renders editable chapter rows with title and page inputs', function() {
    assert.match(appSource, /input\.type = 'text'/);
    assert.match(appSource, /input\.type = 'number'/);
    assert.match(appSource, /删除/);
});
```

- [ ] **Step 2: Run the UI test to verify it fails**

Run:

```powershell
node --test tools/epub-to-xtc-converter/web/chapter_editor.test.js
```

Expected: FAIL because the controls and rendering logic do not exist yet.

- [ ] **Step 3: Add the chapter editor controls**

In `tools/epub-to-xtc-converter/web/index.html`, add this block above the chapter list:

```html
<div class="chapter-toolbar">
    <button id="editChaptersBtn" type="button">编辑目录</button>
    <button id="saveChaptersBtn" type="button" style="display: none;">保存</button>
    <button id="cancelChaptersBtn" type="button" style="display: none;">取消</button>
</div>
```

In `tools/epub-to-xtc-converter/web/app.js`, bind:

```js
const editChaptersBtn = document.getElementById('editChaptersBtn');
const saveChaptersBtn = document.getElementById('saveChaptersBtn');
const cancelChaptersBtn = document.getElementById('cancelChaptersBtn');

let chapterEditMode = false;
let draftChapterRows = [];
```

- [ ] **Step 4: Add edit-mode rendering and validation**

In `tools/epub-to-xtc-converter/web/app.js`, add:

```js
function enterChapterEditMode() {
    chapterEditMode = true;
    draftChapterRows = currentToc.map(function(chapter) {
        return {
            title: chapter.title || chapter.name || '',
            page: (chapter.page || chapter.startPage || 0) + 1
        };
    });
    editChaptersBtn.style.display = 'none';
    saveChaptersBtn.style.display = 'inline-block';
    cancelChaptersBtn.style.display = 'inline-block';
    renderChapterEditor();
}

function exitChapterEditMode() {
    chapterEditMode = false;
    draftChapterRows = [];
    editChaptersBtn.style.display = 'inline-block';
    saveChaptersBtn.style.display = 'none';
    cancelChaptersBtn.style.display = 'none';
    updateChapterList();
}

function renderChapterEditor() {
    while (chapterList.firstChild) {
        chapterList.removeChild(chapterList.firstChild);
    }

    draftChapterRows.forEach(function(row, index) {
        var wrapper = document.createElement('div');
        wrapper.className = 'chapter-edit-row';

        var titleInput = document.createElement('input');
        titleInput.type = 'text';
        titleInput.value = row.title;
        titleInput.addEventListener('input', function() {
            draftChapterRows[index].title = titleInput.value;
        });

        var pageInput = document.createElement('input');
        pageInput.type = 'number';
        pageInput.min = '1';
        pageInput.max = String(totalPages);
        pageInput.value = row.page;
        pageInput.addEventListener('input', function() {
            draftChapterRows[index].page = Number(pageInput.value);
        });

        var deleteBtn = document.createElement('button');
        deleteBtn.type = 'button';
        deleteBtn.textContent = '删除';
        deleteBtn.addEventListener('click', function() {
            draftChapterRows.splice(index, 1);
            renderChapterEditor();
        });

        wrapper.appendChild(titleInput);
        wrapper.appendChild(pageInput);
        wrapper.appendChild(deleteBtn);
        chapterList.appendChild(wrapper);
    });
}
```

Then wire:

```js
editChaptersBtn.addEventListener('click', enterChapterEditMode);
cancelChaptersBtn.addEventListener('click', exitChapterEditMode);
```

- [ ] **Step 5: Add save-to-API flow**

In `tools/epub-to-xtc-converter/web/app.js`, add:

```js
async function saveChapterOverride(bookHash, payload) {
    var response = await fetch('/api/chapter-overrides/' + encodeURIComponent(bookHash), {
        method: 'PUT',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(payload)
    });

    if (!response.ok) {
        throw new Error('Failed to save chapter override');
    }
}

saveChaptersBtn.addEventListener('click', async function() {
    var result = ChapterOverrides.validateEditableChapters(draftChapterRows, totalPages);
    if (!result.ok) {
        alert(result.message);
        return;
    }

    if (result.warning) {
        progressContainer.style.display = 'block';
        progressFill.style.width = '100%';
        progressText.textContent = result.warning;
    }

    var runtimeChapters = draftChapterRows.map(function(row) {
        return {
            title: String(row.title).trim(),
            name: String(row.title).trim(),
            page: Number(row.page) - 1,
            startPage: Number(row.page) - 1,
            depth: 0
        };
    });

    var payload = ChapterOverrides.buildOverridePayload({
        bookHash: currentBookHash,
        source: currentChapterSource,
        chapters: runtimeChapters
    });

    try {
        await saveChapterOverride(currentBookHash, payload);
        currentResolvedToc = runtimeChapters.slice();
        currentToc = currentResolvedToc.slice();
        exitChapterEditMode();
        renderCurrentPage();
    } catch (err) {
        alert('保存目录修改失败，请稍后重试。');
    }
});
```

- [ ] **Step 6: Add minimal styling for edit rows**

In `tools/epub-to-xtc-converter/web/style.css`, add:

```css
.chapter-toolbar {
    display: flex;
    gap: 8px;
    margin-bottom: 8px;
}

.chapter-edit-row {
    display: grid;
    grid-template-columns: 1fr 72px 56px;
    gap: 6px;
    padding: 8px;
    border-bottom: 1px solid #3d3d3d;
}

.chapter-edit-row input,
.chapter-edit-row button {
    min-width: 0;
}
```

- [ ] **Step 7: Run the UI test to verify it passes**

Run:

```powershell
node --test tools/epub-to-xtc-converter/web/chapter_editor.test.js
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add tools/epub-to-xtc-converter/web/chapter_editor.test.js tools/epub-to-xtc-converter/web/index.html tools/epub-to-xtc-converter/web/style.css tools/epub-to-xtc-converter/web/app.js
git commit -m "feat: add inline chapter editor"
```

### Task 5: Verify chapter overrides affect preview and export end-to-end

**Files:**
- Create: `tools/epub-to-xtc-converter/web/chapter_override_export.test.js`
- Modify: `tools/epub-to-xtc-converter/web/app.js`
- Modify: `tools/epub-to-xtc-converter/web/epub_navigation_bootstrap.test.js`

- [ ] **Step 1: Write the failing regression expectations**

Create `tools/epub-to-xtc-converter/web/chapter_override_export.test.js`:

```js
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');

const appSource = fs.readFileSync(path.join(__dirname, 'app.js'), 'utf8');

test('save flow replaces the resolved toc and re-renders the current page', function() {
    assert.match(appSource, /currentResolvedToc = runtimeChapters\.slice\(\);/);
    assert.match(appSource, /currentToc = currentResolvedToc\.slice\(\);/);
    assert.match(appSource, /renderCurrentPage\(\);/);
});

test('xtc export continues to serialize the currently resolved toc', function() {
    assert.match(appSource, /bytes\[11\] = currentToc\.length > 0 \? 1 : 0;/);
    assert.match(appSource, /for \(var i = 0; i < currentToc\.length; i\+\+\)/);
});
```

Add this test to `tools/epub-to-xtc-converter/web/epub_navigation_bootstrap.test.js`:

```js
test('book load keeps chapter calculations on the resolved toc after override application', function() {
    assert.doesNotMatch(appSource, /currentToc = renderer\.getToc\(\) \|\| \[\];[\s\S]*currentToc = renderer\.getToc\(\) \|\| \[\];/m);
});
```

- [ ] **Step 2: Run the regression tests to verify the new expectations**

Run:

```powershell
node --test tools/epub-to-xtc-converter/web/chapter_override_export.test.js tools/epub-to-xtc-converter/web/epub_navigation_bootstrap.test.js
```

Expected: FAIL until the save flow and resolved-TOC invariants are fully in place.

- [ ] **Step 3: Tighten any remaining `app.js` TOC references so preview and export are consistent**

In `tools/epub-to-xtc-converter/web/app.js`, confirm and, if needed, adjust these invariants:

```js
currentToc = currentResolvedToc.slice();
```

must be the source used by:

```js
updateChapterList();
updateCurrentChapter();
getCurrentChapterInfo();
getChapterInfoForPage();
buildXTCContainer(...);
```

If any of those code paths are reading stale automatic TOC state, replace them with the resolved TOC list already held in `currentToc`.

- [ ] **Step 4: Run the full web test suite**

Run:

```powershell
node --test tools/epub-to-xtc-converter/web/auto_font_application.test.js tools/epub-to-xtc-converter/web/auto_font.test.js tools/epub-to-xtc-converter/web/book_identity.test.js tools/epub-to-xtc-converter/web/book_identity_bootstrap.test.js tools/epub-to-xtc-converter/web/chapter_editor.test.js tools/epub-to-xtc-converter/web/chapter_override_export.test.js tools/epub-to-xtc-converter/web/chapter_overrides.test.js tools/epub-to-xtc-converter/web/chapter_overrides_bootstrap.test.js tools/epub-to-xtc-converter/web/epub_navigation.test.js tools/epub-to-xtc-converter/web/epub_navigation_bootstrap.test.js tools/epub-to-xtc-converter/web/font_bootstrap.test.js tools/epub-to-xtc-converter/web/font_compat.test.js tools/epub-to-xtc-converter/web/font_compat_bootstrap.test.js tools/epub-to-xtc-converter/web/font_dropdown_init.test.js tools/epub-to-xtc-converter/web/font_feedback.test.js tools/epub-to-xtc-converter/web/font_feedback_bootstrap.test.js tools/epub-to-xtc-converter/web/local_fonts.test.js tools/epub-to-xtc-converter/web/local_server.test.js tools/epub-to-xtc-converter/web/render_settings.test.js tools/epub-to-xtc-converter/web/ui_font_stack.test.js
```

Expected: PASS.

- [ ] **Step 5: Start the local server and smoke-test the web UI**

Run:

```powershell
Start-Process -FilePath .\tools\epub-to-xtc-converter\start_web.bat -WindowStyle Hidden
Start-Sleep -Seconds 3
(Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8000/web/).StatusCode
```

Expected: `200`

Then manually verify:

- Load an EPUB with a coarse TOC
- Click `编辑目录`
- Change a chapter name
- Change a chapter start page
- Save
- Refresh the page and reload the same EPUB
- Confirm the edited chapter list is restored
- Export and confirm no runtime errors occur

- [ ] **Step 6: Commit**

```bash
git add tools/epub-to-xtc-converter/web/chapter_override_export.test.js tools/epub-to-xtc-converter/web/epub_navigation_bootstrap.test.js tools/epub-to-xtc-converter/web/app.js
git commit -m "feat: apply chapter overrides to preview and export"
```

## Self-Review

Spec coverage check:

- Editable existing chapters: Task 4
- Rename chapter and change start page: Task 4
- Delete chapters: Task 4
- Persist across sessions: Tasks 1 and 3
- Sidecar JSON stored in project: Tasks 1 and 2
- Affect preview, progress, and export: Tasks 3 and 5
- No manual chapter insertion in v1: preserved by scope in Task 4

Placeholder scan:

- No `TBD`, `TODO`, or "implement later" markers remain
- Validation, persistence, and verification commands are explicit

Type consistency:

- Persisted pages are always 1-based in `buildOverridePayload`
- Runtime pages are always 0-based in `hydrateOverridePayload` and `app.js`
- `currentToc` remains the resolved chapter list consumed by export and progress helpers

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-21-epub-chapter-overrides-implementation.md`. Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?
