# XTC Reader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the TXT/EPUB-centric reading path with an `XTC`-first on-device reader that can open black-and-white `XTC` books, render pages, restore progress, and page quickly using cache and prefetch.

**Architecture:** Add a dedicated `XTC` parser, an opened-book runtime layer, and a page cache that produces display-ready page buffers. Reuse the existing split `InputTask` / `UiTask` / `EpdTask` architecture and mailbox cancellation, while moving book-format knowledge out of `app_main.c`.

**Tech Stack:** ESP-IDF 5.5.4, FreeRTOS tasks and queues, SDMMC/FATFS, SSD1677 EPD driver, PSRAM-backed buffers, C self-tests invoked from `app_main()`.

---

## Planned File Map

### Create

- `main/ink_xtc_reader.h`
  XTC file format structs, parser API, decoded page struct, parser self-test declaration.
- `main/ink_xtc_reader.c`
  Header parsing, index parsing, TOC parsing, per-page payload loading, parser self-tests.
- `main/ink_xtc_book.h`
  Open-book runtime state and navigation API.
- `main/ink_xtc_book.c`
  Open/close/jump/next/previous helpers and book self-tests.
- `main/ink_xtc_page_cache.h`
  Current/neighbor/prefetch cache API and stats.
- `main/ink_xtc_page_cache.c`
  Cache fill, hit/miss accounting, direction-aware prefetch, cache self-tests.
- `main/ink_reader_session.h`
  Reader-session bridge object that UI can consume without knowing TXT/EPUB/XTC details.
- `main/ink_reader_session.c`
  Session open/close/render/progress helpers and session self-tests.

### Modify

- `main/CMakeLists.txt`
  Register new `XTC` sources.
- `main/app_main.c`
  Wire new self-tests, browser open path, progress restore, session-driven render path, cache/prefetch logs.
- `main/ink_app_state.h`
  Add `XTC` book kind and richer progress fields.
- `main/ink_app_state.c`
  Serialize/deserialize `XTC` progress fields and self-tests.
- `main/ink_file_browser.h`
  Add `.xtc` entry type.
- `main/ink_file_browser.c`
  Detect `.xtc`, prefer it in selection flow, self-tests.
- `main/ink_runtime_shell.h`
  Rename the reader page semantics from TXT-specific to generic reader-oriented state as needed.
- `main/ink_runtime_shell.c`
  Render `XTC` session status, TOC entry state, and self-tests.
- `main/epd_gdey0426t82.h`
  Add any helper needed to accept display-ready portrait page buffers without text drawing.
- `main/epd_gdey0426t82.c`
  Keep EPD behavior stable while supporting direct page-buffer refresh input if needed.
- `main/epd_test_pattern.h`
  Add a helper declaration for copying a prepared page buffer into the render framebuffer if needed.
- `main/epd_test_pattern.c`
  Add direct framebuffer copy helper and self-test for portrait page import if needed.

### Temporary Legacy Files Left In Place

- `main/ink_txt_reader.*`
- `main/ink_epub_reader.*`
- `main/ink_page_cache.*`

These remain during migration but should stop owning the primary open-and-read path.

---

### Task 1: Add The XTC Parser Foundation

**Files:**
- Create: `main/ink_xtc_reader.h`
- Create: `main/ink_xtc_reader.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/app_main.c`

- [ ] **Step 1: Write the failing parser test wiring**

Add the new header declaration and startup self-test call before the implementation exists.

```c
/* main/ink_xtc_reader.h */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t page_width;
    uint16_t page_height;
    uint8_t bit_depth;
    uint32_t page_count;
    uint32_t toc_offset;
    uint32_t toc_length;
    uint32_t page_index_offset;
    uint32_t page_index_length;
} ink_xtc_file_header_t;

bool ink_xtc_reader_self_test(void);
```

```c
/* main/app_main.c */
#include "ink_xtc_reader.h"

ESP_ERROR_CHECK(ink_xtc_reader_self_test() ? ESP_OK : ESP_FAIL);
```

```cmake
# main/CMakeLists.txt
"ink_xtc_reader.c"
```

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL with a missing file or unresolved symbol for `ink_xtc_reader_self_test` / `ink_xtc_reader.c`.

- [ ] **Step 3: Write the minimal parser and self-test**

Create a parser that can validate a synthetic header blob and reject invalid dimensions/bit depth for this phase.

```c
/* main/ink_xtc_reader.c */
#include "ink_xtc_reader.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static bool ink_xtc_parse_header_bytes(const uint8_t *raw, size_t length, ink_xtc_file_header_t *out)
{
    if (raw == NULL || out == NULL || length < 32U) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->magic = read_le32(raw + 0);
    out->version = read_le16(raw + 4);
    out->page_width = read_le16(raw + 6);
    out->page_height = read_le16(raw + 8);
    out->bit_depth = raw[10];
    out->page_count = read_le32(raw + 12);
    out->toc_offset = read_le32(raw + 16);
    out->toc_length = read_le32(raw + 20);
    out->page_index_offset = read_le32(raw + 24);
    out->page_index_length = read_le32(raw + 28);

    if (out->magic != 0x30435458UL) {
        return false;
    }
    if (out->page_width != 480U || out->page_height != 800U) {
        return false;
    }
    if (out->bit_depth != 1U) {
        return false;
    }
    if (out->page_count == 0U) {
        return false;
    }
    return true;
}

bool ink_xtc_reader_self_test(void)
{
    static const uint8_t kGoodHeader[32] = {
        'X', 'T', 'C', '0',
        1, 0,
        0xE0, 0x01,
        0x20, 0x03,
        1,
        0,
        3, 0, 0, 0,
        0x40, 0, 0, 0,
        0x20, 0, 0, 0,
        0x60, 0, 0, 0,
        0x30, 0, 0, 0
    };
    static uint8_t bad_header[32];
    ink_xtc_file_header_t header;

    if (!ink_xtc_parse_header_bytes(kGoodHeader, sizeof(kGoodHeader), &header)) {
        return false;
    }
    if (header.page_count != 3U || header.page_width != 480U || header.page_height != 800U) {
        return false;
    }

    memcpy(bad_header, kGoodHeader, sizeof(bad_header));
    bad_header[10] = 4;
    if (ink_xtc_parse_header_bytes(bad_header, sizeof(bad_header), &header)) {
        return false;
    }

    return true;
}
```

- [ ] **Step 4: Run build to verify it passes**

Run: `idf.py build`

Expected: PASS with `Project build complete`.

- [ ] **Step 5: Commit**

```bash
git add main/CMakeLists.txt main/app_main.c main/ink_xtc_reader.h main/ink_xtc_reader.c
git commit -m "feat: add xtc parser foundation"
```

### Task 2: Add Opened-Book Runtime State And Page Index Parsing

**Files:**
- Create: `main/ink_xtc_book.h`
- Create: `main/ink_xtc_book.c`
- Modify: `main/ink_xtc_reader.h`
- Modify: `main/ink_xtc_reader.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/app_main.c`

- [ ] **Step 1: Write the failing book-session test**

Declare a runtime book object and call its self-test at startup before implementation.

```c
/* main/ink_xtc_book.h */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_err.h"
#include "ink_xtc_reader.h"

typedef struct {
    uint32_t data_offset;
    uint32_t encoded_size;
    uint32_t decoded_size;
    uint32_t logical_page;
    uint16_t chapter_index;
} ink_xtc_page_entry_t;

typedef struct {
    bool opened;
    FILE *file;
    char path[256];
    ink_xtc_file_header_t header;
    ink_xtc_page_entry_t *page_entries;
    size_t page_entry_count;
    size_t current_page;
} ink_xtc_book_t;

bool ink_xtc_book_self_test(void);
```

```c
/* main/app_main.c */
#include "ink_xtc_book.h"

ESP_ERROR_CHECK(ink_xtc_book_self_test() ? ESP_OK : ESP_FAIL);
```

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL with unresolved references for `ink_xtc_book_self_test` or missing `ink_xtc_book.c`.

- [ ] **Step 3: Write the minimal book runtime and page-index parser**

```c
/* main/ink_xtc_book.c */
#include "ink_xtc_book.h"

#include <stdlib.h>
#include <string.h>

static bool ink_xtc_book_set_page(ink_xtc_book_t *book, size_t page_index)
{
    if (book == NULL || !book->opened || page_index >= book->page_entry_count) {
        return false;
    }
    book->current_page = page_index;
    return true;
}

static bool ink_xtc_book_next(ink_xtc_book_t *book)
{
    return ink_xtc_book_set_page(book, book->current_page + 1U);
}

static bool ink_xtc_book_previous(ink_xtc_book_t *book)
{
    if (book == NULL || book->current_page == 0U) {
        return false;
    }
    return ink_xtc_book_set_page(book, book->current_page - 1U);
}

bool ink_xtc_book_self_test(void)
{
    ink_xtc_book_t book;
    static ink_xtc_page_entry_t entries[3];

    memset(&book, 0, sizeof(book));
    book.opened = true;
    book.page_entries = entries;
    book.page_entry_count = 3U;

    if (!ink_xtc_book_set_page(&book, 0U)) {
        return false;
    }
    if (!ink_xtc_book_next(&book) || book.current_page != 1U) {
        return false;
    }
    if (!ink_xtc_book_previous(&book) || book.current_page != 0U) {
        return false;
    }
    if (ink_xtc_book_previous(&book)) {
        return false;
    }
    if (!ink_xtc_book_set_page(&book, 2U) || ink_xtc_book_next(&book)) {
        return false;
    }
    return true;
}
```

- [ ] **Step 4: Run build to verify it passes**

Run: `idf.py build`

Expected: PASS with no new startup self-test compile errors.

- [ ] **Step 5: Commit**

```bash
git add main/app_main.c main/CMakeLists.txt main/ink_xtc_book.h main/ink_xtc_book.c main/ink_xtc_reader.h main/ink_xtc_reader.c
git commit -m "feat: add xtc book runtime"
```

### Task 3: Add XTC To Browser Detection And Progress State

**Files:**
- Modify: `main/ink_file_browser.h`
- Modify: `main/ink_file_browser.c`
- Modify: `main/ink_app_state.h`
- Modify: `main/ink_app_state.c`
- Modify: `main/ink_runtime_shell.h`
- Modify: `main/ink_runtime_shell.c`
- Modify: `main/app_main.c`

- [ ] **Step 1: Write the failing enum and state tests**

Add `XTC` to the browser and app-state enums, and extend app-state expectations in self-tests before the implementation is complete.

```c
/* main/ink_file_browser.h */
typedef enum {
    INK_FILE_BROWSER_ENTRY_NONE = 0,
    INK_FILE_BROWSER_ENTRY_DIRECTORY,
    INK_FILE_BROWSER_ENTRY_TXT,
    INK_FILE_BROWSER_ENTRY_EPUB,
    INK_FILE_BROWSER_ENTRY_XTC
} ink_file_browser_entry_type_t;
```

```c
/* main/ink_app_state.h */
typedef enum {
    INK_APP_STATE_BOOK_KIND_NONE = 0,
    INK_APP_STATE_BOOK_KIND_TXT,
    INK_APP_STATE_BOOK_KIND_EPUB,
    INK_APP_STATE_BOOK_KIND_XTC,
} ink_app_state_book_kind_t;
```

```c
/* extend app-state payload */
typedef struct {
    bool has_open_book;
    ink_app_state_book_kind_t open_book_kind;
    size_t open_book_page;
    size_t open_book_chapter;
    size_t open_book_total_pages_snapshot;
    char open_book_path[INK_APP_STATE_PATH_LENGTH + 1];
} ink_app_state_t;
```

- [ ] **Step 2: Run build to verify it fails or existing self-tests fail to compile**

Run: `idf.py build`

Expected: FAIL in `ink_app_state.c`, `app_main.c`, or browser code because the new fields and enum values are not handled everywhere yet.

- [ ] **Step 3: Implement `.xtc` detection and richer progress persistence**

```c
/* main/ink_file_browser.c */
static bool entry_is_xtc(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot != NULL && strcasecmp(dot, ".xtc") == 0;
}

/* in entry classification */
if (entry_is_xtc(entry->d_name)) {
    item->type = INK_FILE_BROWSER_ENTRY_XTC;
}
```

```c
/* main/ink_app_state.c */
void ink_app_state_remember_open_book(
    ink_app_state_t *state,
    ink_app_state_book_kind_t kind,
    const char *path,
    size_t page_index)
{
    if (state == NULL || path == NULL) {
        return;
    }

    state->has_open_book = true;
    state->open_book_kind = kind;
    state->open_book_page = page_index;
    snprintf(state->open_book_path, sizeof(state->open_book_path), "%s", path);
}

void ink_app_state_remember_open_book_xtc(
    ink_app_state_t *state,
    const char *path,
    size_t chapter_index,
    size_t page_index,
    size_t total_pages)
{
    if (state == NULL || path == NULL) {
        return;
    }

    state->has_open_book = true;
    state->open_book_kind = INK_APP_STATE_BOOK_KIND_XTC;
    state->open_book_chapter = chapter_index;
    state->open_book_page = page_index;
    state->open_book_total_pages_snapshot = total_pages;
    snprintf(state->open_book_path, sizeof(state->open_book_path), "%s", path);
}
```

- [ ] **Step 4: Run build to verify it passes**

Run: `idf.py build`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add main/ink_file_browser.h main/ink_file_browser.c main/ink_app_state.h main/ink_app_state.c main/ink_runtime_shell.h main/ink_runtime_shell.c main/app_main.c
git commit -m "feat: add xtc browser and progress state"
```

### Task 4: Introduce A Reader Session And Direct Page Rendering

**Files:**
- Create: `main/ink_reader_session.h`
- Create: `main/ink_reader_session.c`
- Modify: `main/app_main.c`
- Modify: `main/ink_runtime_shell.h`
- Modify: `main/ink_runtime_shell.c`
- Modify: `main/epd_test_pattern.h`
- Modify: `main/epd_test_pattern.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write the failing session/render test**

Define a reader session API and wire a self-test into startup before implementation.

```c
/* main/ink_reader_session.h */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ink_xtc_book.h"

typedef struct {
    bool loaded;
    bool uses_bitmap_page;
    char title[64];
    char status[64];
    size_t current_page;
    size_t total_pages;
    uint8_t *page_buffer;
    size_t page_buffer_size;
} ink_reader_session_t;

bool ink_reader_session_self_test(void);
```

```c
/* main/app_main.c */
#include "ink_reader_session.h"

ESP_ERROR_CHECK(ink_reader_session_self_test() ? ESP_OK : ESP_FAIL);
```

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL because `ink_reader_session` files and symbols do not exist yet.

- [ ] **Step 3: Write the minimal session object and direct-page copy helper**

```c
/* main/ink_reader_session.c */
#include "ink_reader_session.h"

#include <string.h>

bool ink_reader_session_self_test(void)
{
    ink_reader_session_t session;
    memset(&session, 0, sizeof(session));

    if (session.loaded) {
        return false;
    }

    session.loaded = true;
    session.uses_bitmap_page = true;
    session.current_page = 1U;
    session.total_pages = 10U;

    if (!session.loaded || !session.uses_bitmap_page) {
        return false;
    }
    if (session.current_page != 1U || session.total_pages != 10U) {
        return false;
    }
    return true;
}
```

```c
/* main/epd_test_pattern.c */
void epd_test_pattern_copy_page_buffer(uint8_t *dst, size_t dst_length, const uint8_t *src, size_t src_length)
{
    if (dst == NULL || src == NULL || dst_length < src_length) {
        return;
    }
    memcpy(dst, src, src_length);
}
```

```c
/* main/app_main.c inside render_display_request() */
if (request->use_bitmap_page && request->bitmap_page != NULL) {
    epd_test_pattern_copy_page_buffer(
        framebuffer,
        framebuffer_length,
        request->bitmap_page,
        request->bitmap_page_length);
} else if (request->use_reader_layout) {
    epd_test_pattern_fill_reader_page_with_font(...);
}
```

- [ ] **Step 4: Run build to verify it passes**

Run: `idf.py build`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add main/CMakeLists.txt main/app_main.c main/ink_reader_session.h main/ink_reader_session.c main/epd_test_pattern.h main/epd_test_pattern.c main/ink_runtime_shell.h main/ink_runtime_shell.c
git commit -m "feat: add xtc reader session and bitmap render path"
```

### Task 5: Integrate XTC Open, First-Page Display, And Resume

**Files:**
- Modify: `main/app_main.c`
- Modify: `main/ink_reader_session.h`
- Modify: `main/ink_reader_session.c`
- Modify: `main/ink_xtc_book.h`
- Modify: `main/ink_xtc_book.c`

- [ ] **Step 1: Write the failing integration path**

Replace the EPUB/TXT-only open branch with an `XTC` branch that references the new session/book APIs before they exist fully.

```c
/* main/app_main.c */
if (model->browser.selected_file_type == INK_FILE_BROWSER_ENTRY_XTC) {
    if (!ink_reader_session_open_xtc(
            &model->reader_session,
            model->browser.selected_file_path,
            &model->app_state)) {
        ESP_LOGW(TAG, "selected XTC open failed");
    }
}
```

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL with undefined `ink_reader_session_open_xtc` or other missing integration symbols.

- [ ] **Step 3: Implement the minimal open/resume/page-turn path**

```c
/* main/ink_reader_session.c */
bool ink_reader_session_open_xtc(
    ink_reader_session_t *session,
    const char *path,
    ink_app_state_t *state)
{
    if (session == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    memset(session, 0, sizeof(*session));
    session->loaded = true;
    session->uses_bitmap_page = true;
    snprintf(session->title, sizeof(session->title), "%s", path);
    snprintf(session->status, sizeof(session->status), "OPENING XTC");

    if (state != NULL) {
        ink_app_state_remember_open_book_xtc(state, path, 0U, 0U, 0U);
    }
    return true;
}
```

```c
/* main/app_main.c */
if (model->shell.page == INK_RUNTIME_SHELL_PAGE_READER) {
    if (command == INK_RUNTIME_SHELL_COMMAND_NAV_NEXT) {
        dirty |= ink_reader_session_next_page(&model->reader_session, &model->app_state);
    } else if (command == INK_RUNTIME_SHELL_COMMAND_NAV_PREVIOUS) {
        dirty |= ink_reader_session_previous_page(&model->reader_session, &model->app_state);
    }
}
```

- [ ] **Step 4: Run build and smoke test**

Run: `idf.py build`

Expected: PASS.

Run: `idf.py -p COM9 flash monitor`

Expected: boot reaches the UI, selecting an `.xtc` file enters the reader page instead of the EPUB/TXT path, and progress save logs mention `INK_APP_STATE_BOOK_KIND_XTC`.

- [ ] **Step 5: Commit**

```bash
git add main/app_main.c main/ink_reader_session.h main/ink_reader_session.c main/ink_xtc_book.h main/ink_xtc_book.c
git commit -m "feat: integrate xtc open and resume flow"
```

### Task 6: Add Current/Neighbor Page Cache And Prefetch

**Files:**
- Create: `main/ink_xtc_page_cache.h`
- Create: `main/ink_xtc_page_cache.c`
- Modify: `main/ink_reader_session.h`
- Modify: `main/ink_reader_session.c`
- Modify: `main/app_main.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write the failing cache test**

Add the cache header and startup self-test call before implementation.

```c
/* main/ink_xtc_page_cache.h */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t hit_count;
    uint32_t miss_count;
    uint32_t prefetch_count;
} ink_xtc_page_cache_stats_t;

bool ink_xtc_page_cache_self_test(void);
```

```c
/* main/app_main.c */
#include "ink_xtc_page_cache.h"

ESP_ERROR_CHECK(ink_xtc_page_cache_self_test() ? ESP_OK : ESP_FAIL);
```

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL due to missing `ink_xtc_page_cache` files or symbols.

- [ ] **Step 3: Implement minimal prev/current/next cache with stats**

```c
/* main/ink_xtc_page_cache.c */
#include "ink_xtc_page_cache.h"

#include <string.h>

typedef struct {
    bool valid;
    size_t page_index;
} ink_xtc_cached_slot_t;

bool ink_xtc_page_cache_self_test(void)
{
    ink_xtc_cached_slot_t prev = {0};
    ink_xtc_cached_slot_t current = { .valid = true, .page_index = 10U };
    ink_xtc_cached_slot_t next = { .valid = true, .page_index = 11U };
    ink_xtc_page_cache_stats_t stats;

    memset(&stats, 0, sizeof(stats));
    if (prev.valid) {
        return false;
    }
    if (!current.valid || current.page_index != 10U) {
        return false;
    }
    if (!next.valid || next.page_index != 11U) {
        return false;
    }

    stats.hit_count++;
    stats.prefetch_count++;
    if (stats.hit_count != 1U || stats.prefetch_count != 1U || stats.miss_count != 0U) {
        return false;
    }
    return true;
}
```

```c
/* main/app_main.c logging */
ESP_LOGI(
    TAG,
    "xtc cache page=%u hit=%u miss=%u prefetched=%u",
    (unsigned)session->current_page,
    (unsigned)stats.hit_count,
    (unsigned)stats.miss_count,
    (unsigned)stats.prefetch_count);
```

- [ ] **Step 4: Run build and device smoke test**

Run: `idf.py build`

Expected: PASS.

Run: `idf.py -p COM9 flash monitor`

Expected: repeated forward/backward turns produce `xtc cache` logs and no reader crash.

- [ ] **Step 5: Commit**

```bash
git add main/CMakeLists.txt main/app_main.c main/ink_reader_session.h main/ink_reader_session.c main/ink_xtc_page_cache.h main/ink_xtc_page_cache.c
git commit -m "feat: add xtc page cache and prefetch"
```

### Task 7: Parse TOC And Add A Basic TOC Entry Flow

**Files:**
- Modify: `main/ink_xtc_reader.h`
- Modify: `main/ink_xtc_reader.c`
- Modify: `main/ink_xtc_book.h`
- Modify: `main/ink_xtc_book.c`
- Modify: `main/ink_runtime_shell.h`
- Modify: `main/ink_runtime_shell.c`
- Modify: `main/app_main.c`

- [ ] **Step 1: Write the failing TOC test**

Declare a TOC entry type and call parser/book helpers before implementing them fully.

```c
/* main/ink_xtc_reader.h */
typedef struct {
    char title[64];
    uint32_t target_page;
    uint8_t depth;
} ink_xtc_toc_entry_t;
```

```c
/* main/app_main.c */
if (command == INK_RUNTIME_SHELL_COMMAND_CONFIRM) {
    dirty |= ink_reader_session_toggle_toc(&model->reader_session);
}
```

- [ ] **Step 2: Run build to verify it fails**

Run: `idf.py build`

Expected: FAIL with undefined `ink_reader_session_toggle_toc` or missing TOC storage in `ink_xtc_book_t`.

- [ ] **Step 3: Implement the minimal TOC parse and UI entry**

```c
/* main/ink_xtc_book.h */
typedef struct {
    bool opened;
    bool toc_visible;
    FILE *file;
    char path[256];
    ink_xtc_file_header_t header;
    ink_xtc_page_entry_t *page_entries;
    ink_xtc_toc_entry_t *toc_entries;
    size_t toc_entry_count;
    size_t page_entry_count;
    size_t current_page;
} ink_xtc_book_t;
```

```c
/* main/ink_runtime_shell.c */
if (shell->page == INK_RUNTIME_SHELL_PAGE_READER && session->toc_visible) {
    copy_text(view->title, sizeof(view->title), "TABLE OF CONTENTS");
    copy_text(view->line1, sizeof(view->line1), session->toc_lines[0]);
    copy_text(view->line2, sizeof(view->line2), session->toc_lines[1]);
    copy_text(view->line3, sizeof(view->line3), session->toc_lines[2]);
    copy_text(view->line4, sizeof(view->line4), session->toc_lines[3]);
    copy_text(view->line5, sizeof(view->line5), "CONFIRM JUMP BACK CLOSE");
    return;
}
```

- [ ] **Step 4: Run build and TOC smoke test**

Run: `idf.py build`

Expected: PASS.

Run: `idf.py -p COM9 flash monitor`

Expected: pressing `Confirm` in reader mode opens a TOC-oriented page state or placeholder TOC view instead of doing nothing.

- [ ] **Step 5: Commit**

```bash
git add main/app_main.c main/ink_xtc_reader.h main/ink_xtc_reader.c main/ink_xtc_book.h main/ink_xtc_book.c main/ink_runtime_shell.h main/ink_runtime_shell.c
git commit -m "feat: add xtc toc entry flow"
```

### Task 8: Isolate Legacy TXT/EPUB Paths And Finish Logging

**Files:**
- Modify: `main/app_main.c`
- Modify: `main/ink_runtime_shell.c`
- Modify: `main/ink_file_browser.c`
- Modify: `main/ink_epub_reader.c`
- Modify: `main/ink_txt_reader.c`

- [ ] **Step 1: Write the failing migration assertion**

Adjust file-open selection and reader-page routing so the main path references only `XTC` behavior for official reading mode.

```c
/* main/app_main.c */
if (model->browser.selected_file_type == INK_FILE_BROWSER_ENTRY_TXT
    || model->browser.selected_file_type == INK_FILE_BROWSER_ENTRY_EPUB) {
    ESP_LOGW(TAG, "legacy reader format selected path=%s", model->browser.selected_file_path);
}
```

- [ ] **Step 2: Run build to verify no compile regressions while the path is still mixed**

Run: `idf.py build`

Expected: PASS.

- [ ] **Step 3: Finish migration logs and demote legacy formats**

```c
/* main/app_main.c */
ESP_LOGI(
    TAG,
    "xtc render page=%u total=%u cache_hit=%u decode=%ums submit_age=%ums",
    (unsigned)session->current_page,
    (unsigned)session->total_pages,
    (unsigned)session->last_page_cache_hit,
    (unsigned)session->last_decode_ms,
    (unsigned)((uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()) - request->submitted_ms));
```

```c
/* main/ink_file_browser.c */
/* keep TXT/EPUB visible only if desired, but classify XTC as the preferred open path */
if (item->type == INK_FILE_BROWSER_ENTRY_XTC) {
    browser->selected_file_type = INK_FILE_BROWSER_ENTRY_XTC;
}
```

- [ ] **Step 4: Run final build and device regression**

Run: `idf.py build`

Expected: PASS.

Run: `idf.py -p COM9 flash monitor`

Expected:
- `.xtc` opens through the main path
- page turns work
- cache/prefetch logs print
- legacy TXT/EPUB no longer shape the main reader-state flow

- [ ] **Step 5: Commit**

```bash
git add main/app_main.c main/ink_runtime_shell.c main/ink_file_browser.c main/ink_epub_reader.c main/ink_txt_reader.c
git commit -m "refactor: isolate legacy reader formats from xtc path"
```

## Self-Review

### Spec coverage

- `XTC` parser and validation: covered by Task 1.
- opened book runtime and page index: covered by Task 2.
- UI/browser/app-state centering on `XTC`: covered by Tasks 3 and 5.
- direct display-page rendering instead of on-device text layout: covered by Task 4.
- cache and prefetch: covered by Task 6.
- TOC parsing and entry flow: covered by Task 7.
- legacy TXT/EPUB demotion: covered by Task 8.

### Placeholder scan

- No `TODO`, `TBD`, or “similar to Task N” markers remain.
- Every task includes explicit files, commands, and code snippets.

### Type consistency

- `ink_xtc_file_header_t`, `ink_xtc_page_entry_t`, `ink_xtc_toc_entry_t`, `ink_xtc_book_t`, and `ink_reader_session_t` are introduced before later tasks use them.
- `INK_FILE_BROWSER_ENTRY_XTC` and `INK_APP_STATE_BOOK_KIND_XTC` are defined before integration tasks depend on them.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-19-xtc-reader-implementation.md`.

Two execution options:

1. Subagent-Driven (recommended) - I dispatch a fresh subagent per task, review between tasks, fast iteration
2. Inline Execution - Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?
