# CrossPoint-Style UI Structural Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify the board-side UI into a small family of CrossPoint-inspired page templates, stable font roles, and consistent list/popup/header rules without breaking the current refresh-strategy boundaries.

**Architecture:** Extend the existing `epd_test_pattern` and app render pipeline with shared page-template helpers instead of redrawing each app independently. Treat the current Library / Reader overlay family as the canonical seed, map Launcher / Photo Album list / USB / WiFi onto that system, and preserve app-local state machines while centralizing layout rhythm and typography choices in render helpers.

**Tech Stack:** ESP-IDF 5.5.4, FreeRTOS mailbox-driven UI pipeline, custom `epd_gdey0426t82` renderer, `ink_cpfont`, in-tree firmware self-tests, Unity host-side tests

---

## File Structure

### Shared UI Template Layer

- Modify: `D:/FUCKIDF/ink-reader/components/ink_hw/epd_test_pattern.h`
  - Add reusable template structs, long-text truncation helpers, header/list/popup/status drawing entry points.
- Modify: `D:/FUCKIDF/ink-reader/components/ink_hw/epd_test_pattern.c`
  - Implement the shared CrossPoint-style geometry and render helpers.

### App Render Integration

- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`
  - Replace page-specific ad hoc layout code with shared template calls for Launcher, Photo Album list, USB, and overlay tuning.
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_render.h`
  - Add any helper declarations needed by render self-tests if they must be exposed.

### Overlay / Request Builder Integration

- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_display_request.c`
  - Normalize Library / Reader overlay payloads to better fill the shared popup/list templates.

### Font Role Mapping

- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_boot.c`
  - Enforce the font search order and semantic role mapping so `16px` and below prefer `SmallSimSun`.
- Modify: `D:/FUCKIDF/ink-reader/main/ink_system_services.h`
  - Keep semantic font slots stable; only extend if a new slot is strictly required.

### App-Specific Behavior / Tests

- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_launcher_app.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_usb_msc_app.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_photo_album_app.c`
- Modify: `D:/FUCKIDF/ink-reader/components/ink_wifi_setup/ink_wifi_setup_ui.c`
- Modify: `D:/FUCKIDF/ink-reader/components/ink_wifi_setup/ink_wifi_setup_ui.h`

### Validation

- Modify: `D:/FUCKIDF/ink-reader/main/test/test_epd_test_pattern.c`
  - Add host-side coverage for template-level helpers that do not require full firmware boot.

---

### Task 1: Add Shared UI Template Primitives

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/components/ink_hw/epd_test_pattern.h`
- Modify: `D:/FUCKIDF/ink-reader/components/ink_hw/epd_test_pattern.c`
- Test: `D:/FUCKIDF/ink-reader/main/test/test_epd_test_pattern.c`

- [ ] **Step 1: Write the failing template helper tests**

Add host-side tests for title/header bounds and ellipsis truncation in [main/test/test_epd_test_pattern.c](/D:/FUCKIDF/ink-reader/main/test/test_epd_test_pattern.c):

```c
#include "epd_test_pattern.h"
#include "unity.h"

void test_epd_ui_truncate_keeps_short_text(void)
{
    char out[32];
    epd_test_pattern_truncate_text_middle("Photos", out, sizeof(out), 12);
    TEST_ASSERT_EQUAL_STRING("Photos", out);
}

void test_epd_ui_truncate_adds_ellipsis_for_long_text(void)
{
    char out[24];
    epd_test_pattern_truncate_text_middle(
        "crop_480x800_Eink-4Gray_atkinson_serpentine_indexed4_1782481618665",
        out,
        sizeof(out),
        18);
    TEST_ASSERT_NOT_EQUAL(0, strstr(out, "...") != NULL);
}

void test_epd_ui_list_geometry_has_dense_rows(void)
{
    epd_test_pattern_list_layout_t layout = epd_test_pattern_crosspoint_list_layout();
    TEST_ASSERT_GREATER_THAN_INT(6, layout.visible_rows);
    TEST_ASSERT_LESS_THAN_INT(64, layout.row_h);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
idf.py build
```

Expected:

- Build fails because `epd_test_pattern_truncate_text_middle`, `epd_test_pattern_list_layout_t`, or `epd_test_pattern_crosspoint_list_layout()` do not exist yet.

- [ ] **Step 3: Add the shared template contract to the header**

Extend [components/ink_hw/epd_test_pattern.h](/D:/FUCKIDF/ink-reader/components/ink_hw/epd_test_pattern.h) with reusable layout and page-template structs:

```c
typedef struct {
    int x;
    int y;
    int w;
    int h;
} epd_test_pattern_rect_t;

typedef struct {
    int header_h;
    int gutter_x;
    int title_y;
    int meta_y;
    int divider_y;
} epd_test_pattern_header_layout_t;

typedef struct {
    int list_x;
    int list_y;
    int row_w;
    int row_h;
    int row_gap;
    int visible_rows;
} epd_test_pattern_list_layout_t;

typedef struct {
    const char *title;
    const char *meta;
    const ink_cpfont_t *title_font;
    const ink_cpfont_t *meta_font;
} epd_test_pattern_header_spec_t;

typedef struct {
    const char *title;
    const char *line1;
    const char *line2;
    bool selected;
    bool emphasized;
} epd_test_pattern_list_row_t;

epd_test_pattern_header_layout_t epd_test_pattern_crosspoint_header_layout(void);
epd_test_pattern_list_layout_t epd_test_pattern_crosspoint_list_layout(void);
void epd_test_pattern_truncate_text_middle(
    const char *src,
    char *dst,
    size_t dst_size,
    size_t max_chars);
```

- [ ] **Step 4: Implement the shared template helpers**

In [components/ink_hw/epd_test_pattern.c](/D:/FUCKIDF/ink-reader/components/ink_hw/epd_test_pattern.c), add geometry and truncation helpers with a single system-wide rhythm:

```c
epd_test_pattern_header_layout_t epd_test_pattern_crosspoint_header_layout(void)
{
    epd_test_pattern_header_layout_t layout = {
        .header_h = 68,
        .gutter_x = 24,
        .title_y = 24,
        .meta_y = 28,
        .divider_y = 66,
    };
    return layout;
}

epd_test_pattern_list_layout_t epd_test_pattern_crosspoint_list_layout(void)
{
    epd_test_pattern_list_layout_t layout = {
        .list_x = 24,
        .list_y = 84,
        .row_w = EPD_GDEY0426T82_WIDTH - 48,
        .row_h = 48,
        .row_gap = 6,
        .visible_rows = 10,
    };
    return layout;
}

void epd_test_pattern_truncate_text_middle(
    const char *src,
    char *dst,
    size_t dst_size,
    size_t max_chars)
{
    size_t src_len = src != NULL ? strlen(src) : 0U;

    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src_len == 0U || max_chars == 0U) {
        dst[0] = '\0';
        return;
    }
    if (src_len <= max_chars) {
        snprintf(dst, dst_size, "%s", src);
        return;
    }

    {
        const size_t head = max_chars > 3U ? max_chars - 3U : max_chars;
        snprintf(dst, dst_size, "%.*s...", (int)head, src);
    }
}
```

- [ ] **Step 5: Add shared header/list/page draw helpers**

In [components/ink_hw/epd_test_pattern.c](/D:/FUCKIDF/ink-reader/components/ink_hw/epd_test_pattern.c), add reusable drawing entry points instead of repeating raw rectangles inside each app:

```c
void epd_test_pattern_draw_crosspoint_header(
    uint8_t *buffer,
    const epd_test_pattern_header_spec_t *spec)
{
    epd_test_pattern_header_layout_t layout = epd_test_pattern_crosspoint_header_layout();
    draw_ui_text(buffer, spec->title_font, layout.gutter_x, layout.title_y, spec->title, 2, 1U, true);
    if (spec->meta != NULL && spec->meta[0] != '\0') {
        draw_ui_text(buffer, spec->meta_font, EPD_GDEY0426T82_WIDTH - 96, layout.meta_y, spec->meta, 2, 1U, true);
    }
    fill_rect(buffer, layout.gutter_x, layout.divider_y, EPD_GDEY0426T82_WIDTH - layout.gutter_x * 2, 1, true);
}
```

- [ ] **Step 6: Run the test and build to verify it passes**

Run:

```powershell
idf.py build
```

Expected:

- Build succeeds.
- `test_epd_test_pattern.c` compiles with the new helper names.

- [ ] **Step 7: Commit**

```bash
git add components/ink_hw/epd_test_pattern.h components/ink_hw/epd_test_pattern.c main/test/test_epd_test_pattern.c
git commit -m "refactor: add shared crosspoint ui template primitives"
```

### Task 2: Lock Font Roles And SmallSimSun Preference

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_boot.c`
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`
- Test: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`

- [ ] **Step 1: Write the failing font-role self-test**

Add a self-test in [main/ink_app_render.c](/D:/FUCKIDF/ink-reader/main/ink_app_render.c) asserting small-text pages prefer footer/body fonts that can safely be backed by `SmallSimSun`:

```c
static bool app_overlay_menu_font_selection_prefers_small_font_roles_self_test(void)
{
    ink_system_services_t services;
    ink_system_services_reset(&services);

    return services.menu_font.advance_y >= services.footer_font.advance_y;
}
```

- [ ] **Step 2: Run build to verify the test is not wired or fails**

Run:

```powershell
idf.py build
```

Expected:

- The test either fails once wired into `ink_app_render_self_test()` or highlights that the font-role ordering is not explicit enough.

- [ ] **Step 3: Reorder font candidates by semantic role**

Update [main/ink_app_boot.c](/D:/FUCKIDF/ink-reader/main/ink_app_boot.c) so `footer_font` and dense list/body roles prefer `SmallSimSun` candidates before larger vector fonts, while `menu_font` keeps larger title-friendly candidates first:

```c
static const char *kMenuFontPaths[] = {
    "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_24.cpfont",
    "/sdcard/fonts/LXGWWenKai_24.cpfont",
    "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_20.cpfont",
    "/sdcard/fonts/LXGWWenKai_20.cpfont",
    "/sdcard/fonts/SmallSimSunBitmap_16.cpfont",
};

static const char *kFooterFontPaths[] = {
    "/sdcard/fonts/SmallSimSunEmbedded_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSunEmbedded_16.cpfont",
    "/sdcard/fonts/SmallSimSunBitmap_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSunBitmap_16.cpfont",
    "/sdcard/fonts/SmallSimSun_16.cpfont",
    "/sdcard/.fonts/SmallSimSun/SmallSimSun_16.cpfont",
    "/sdcard/.fonts/LXGWWenKai/LXGWWenKai_16.cpfont",
};
```

- [ ] **Step 4: Make render helpers consume semantic font roles**

In [main/ink_app_render.c](/D:/FUCKIDF/ink-reader/main/ink_app_render.c), stop using raw page-local assumptions like “menu font for everything.” Use:

```c
const ink_cpfont_t *title_font = &app->services.menu_font;
const ink_cpfont_t *body_font = &app->services.footer_font;
const ink_cpfont_t *meta_font = &app->services.footer_font;
```

for list/status pages, and keep larger fonts only for `UI_H1` and some popup headers.

- [ ] **Step 5: Wire the self-test into render boot coverage**

Add the new test to `ink_app_render_self_test()` in [main/ink_app_render.c](/D:/FUCKIDF/ink-reader/main/ink_app_render.c):

```c
if (!app_overlay_menu_font_selection_prefers_small_font_roles_self_test()) {
    printf("FAIL render overlay_menu_font_selection_prefers_small_font_roles\n");
    return false;
}
```

- [ ] **Step 6: Run build to verify the font-role pass**

Run:

```powershell
idf.py build
```

Expected:

- Build succeeds.
- Existing render self-tests still pass through compilation.

- [ ] **Step 7: Commit**

```bash
git add main/ink_app_boot.c main/ink_app_render.c
git commit -m "refactor: lock semantic ui font roles"
```

### Task 3: Migrate Launcher And USB To Shared Page Templates

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_launcher_app.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_usb_msc_app.c`
- Test: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`

- [ ] **Step 1: Add failing render self-tests for template-based Launcher and USB pages**

In [main/ink_app_render.c](/D:/FUCKIDF/ink-reader/main/ink_app_render.c), add checks that the Launcher and USB render paths now go through shared header/list/status helpers:

```c
static bool app_launcher_template_layout_self_test(void)
{
    ink_display_request_t request = {0};
    ink_app_render_model_t model = {0};
    ink_launcher_app_state_t state = {.selected_app_index = 2U};

    model.mode = INK_APP_RENDER_MODE_LAUNCHER;
    model.state = &state;

    return ink_app_render_model_fill_request(&model, &request)
        && request.refresh_strategy == INK_REFRESH_STRATEGY_BW_UI_PAGE_FAST;
}

static bool app_usb_status_template_layout_self_test(void)
{
    ink_usb_msc_app_state_t state = {.view = INK_USB_MSC_APP_VIEW_ACTIVE};
    return state.view == INK_USB_MSC_APP_VIEW_ACTIVE;
}
```

- [ ] **Step 2: Run build to verify the tests are failing or incomplete**

Run:

```powershell
idf.py build
```

Expected:

- Tests are either not yet wired or page rendering still relies on old text-page helpers.

- [ ] **Step 3: Replace the old Launcher text page with a card-based home template**

Refactor `fill_launcher_page()` in [main/ink_app_render.c](/D:/FUCKIDF/ink-reader/main/ink_app_render.c) from `epd_test_pattern_fill_text_page()` to shared header + card/list rows:

```c
static void fill_launcher_page(
    uint8_t *buffer,
    size_t length,
    const ink_launcher_app_state_t *state)
{
    static const char *const kAppNames[] = {
        "Reader",
        "WiFi Setup",
        "Photo Album",
        "Gray Cal",
        "USB Disk",
    };
    epd_test_pattern_header_spec_t header = {
        .title = "Launcher",
        .meta = "5 Apps",
    };
    epd_test_pattern_home_card_spec_t cards[5] = {0};

    memset(buffer, 0xFF, length);
    epd_test_pattern_draw_crosspoint_header(buffer, &header);

    for (size_t i = 0; i < 5; ++i) {
        cards[i].title = kAppNames[i];
        cards[i].selected = state != NULL && state->selected_app_index == i;
    }
    epd_test_pattern_draw_home_cards(buffer, cards, 5);
}
```

- [ ] **Step 4: Convert USB MSC to the shared status-page template**

Replace the text-page rendering in `fill_usb_msc_page()` with a shared status-body helper:

```c
static void fill_usb_msc_page(
    uint8_t *buffer,
    size_t length,
    const ink_usb_msc_app_render_state_t *render_state)
{
    const ink_usb_msc_app_state_t *state = render_state != NULL ? render_state->state : NULL;
    epd_test_pattern_status_page_spec_t spec = {
        .title = state != NULL ? state->title : "USB Disk",
        .line1 = state != NULL ? state->line1 : "No state",
        .line2 = state != NULL ? state->line2 : "",
        .line3 = state != NULL ? state->line3 : "",
        .hint_left = state != NULL && state->view == INK_USB_MSC_APP_VIEW_ACTIVE ? "Back Exit" : "",
        .hint_right = "USB MSC",
    };

    memset(buffer, 0xFF, length);
    epd_test_pattern_draw_status_page(buffer, &spec);
}
```

- [ ] **Step 5: Keep app-state behavior stable while removing page-local decoration assumptions**

Only touch render semantics in [main/apps/ink_launcher_app.c](/D:/FUCKIDF/ink-reader/main/apps/ink_launcher_app.c) and [main/apps/ink_usb_msc_app.c](/D:/FUCKIDF/ink-reader/main/apps/ink_usb_msc_app.c) if a count/meta string or template-specific data must be surfaced. Do not move app logic into render helpers.

- [ ] **Step 6: Wire new render self-tests**

Add to `ink_app_render_self_test()`:

```c
if (!app_launcher_template_layout_self_test()) {
    printf("FAIL render launcher_template_layout\n");
    return false;
}
if (!app_usb_status_template_layout_self_test()) {
    printf("FAIL render usb_status_template_layout\n");
    return false;
}
```

- [ ] **Step 7: Run build to verify it passes**

Run:

```powershell
idf.py build
```

Expected:

- Build succeeds.
- Existing launcher navigation and USB prompt self-tests still compile and pass at boot.

- [ ] **Step 8: Commit**

```bash
git add main/ink_app_render.c main/apps/ink_launcher_app.c main/apps/ink_usb_msc_app.c
git commit -m "refactor: migrate launcher and usb to shared ui templates"
```

### Task 4: Converge Photo Album List On The Shared List Template

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_photo_album_app.c`
- Modify: `D:/FUCKIDF/ink-reader/main/apps/ink_photo_album_app.h`
- Test: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`

- [ ] **Step 1: Add failing self-tests for photo list density and ellipsis**

Replace the current custom geometry assumptions in [main/ink_app_render.c](/D:/FUCKIDF/ink-reader/main/ink_app_render.c) with explicit assertions:

```c
static bool photo_album_list_uses_shared_layout_self_test(void)
{
    epd_test_pattern_list_layout_t layout = epd_test_pattern_crosspoint_list_layout();
    return layout.visible_rows >= 8 && layout.row_h <= 52;
}

static bool photo_album_list_truncates_long_names_self_test(void)
{
    char out[32];
    epd_test_pattern_truncate_text_middle(
        "crop_480x800_Eink-4Gray_atkinson_serpentine_indexed4_1782481618665",
        out,
        sizeof(out),
        18);
    return strstr(out, "...") != NULL;
}
```

- [ ] **Step 2: Run build to verify the new checks are not satisfied by the old code**

Run:

```powershell
idf.py build
```

Expected:

- The new tests are missing or the current geometry remains out of sync with the shared list layout.

- [ ] **Step 3: Refactor `draw_photo_album_list_page()` to use the shared list template**

In [main/ink_app_render.c](/D:/FUCKIDF/ink-reader/main/ink_app_render.c), replace the local `panel_x`, `title_y`, `row_h = 56`, `visible_rows = 10` block with shared header/list specs:

```c
epd_test_pattern_header_spec_t header = {
    .title = "Photos",
    .meta = count_text,
    .title_font = menu_font,
    .meta_font = footer_font,
};
epd_test_pattern_list_layout_t layout = epd_test_pattern_crosspoint_list_layout();

epd_test_pattern_draw_crosspoint_header(buffer, &header);

for (int row = 0; row < layout.visible_rows && start + (size_t)row < state->total_count; ++row) {
    epd_test_pattern_list_row_t row_spec = {0};
    char title[40];

    epd_test_pattern_truncate_text_middle(entry->name, title, sizeof(title), 20);
    row_spec.title = title;
    row_spec.selected = item_index == state->list_selected_index;
    row_spec.emphasized = item_index == state->current_index;
    epd_test_pattern_draw_list_row(buffer, &layout, row, &row_spec, footer_font);
}
```

- [ ] **Step 4: Keep preview page content-first**

In `fill_photo_album_page()` in [main/ink_app_render.c](/D:/FUCKIDF/ink-reader/main/ink_app_render.c), keep preview mode image-first and do not reintroduce bottom info bars:

```c
if (state->view_mode == INK_PHOTO_ALBUM_VIEW_LIST) {
    draw_photo_album_list_page(buffer, length, render_state, menu_font, footer_font);
    return;
}

epd_test_pattern_fill_text_page_with_font(
    buffer,
    length,
    menu_font,
    NULL,
    NULL,
    "Photos",
    state->status_text[0] != '\0' ? state->status_text : "图片读取失败",
    "",
    "",
    "",
    "");
```

Only use this status fallback when preview has no valid image. Do not add always-on preview chrome.

- [ ] **Step 5: Update album render self-tests**

Wire the new checks into `ink_app_render_self_test()`:

```c
if (!photo_album_list_uses_shared_layout_self_test()) {
    printf("FAIL render photo_album_list_shared_layout\n");
    return false;
}
if (!photo_album_list_truncates_long_names_self_test()) {
    printf("FAIL render photo_album_list_truncates_long_names\n");
    return false;
}
```

- [ ] **Step 6: Run build to verify the album render path passes**

Run:

```powershell
idf.py build
```

Expected:

- Build succeeds.
- Existing album app self-tests still pass through compilation.

- [ ] **Step 7: Commit**

```bash
git add main/ink_app_render.c main/apps/ink_photo_album_app.c main/apps/ink_photo_album_app.h
git commit -m "refactor: migrate photo album list to shared ui template"
```

### Task 5: Normalize Library And Reader Popup Geometry

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/main/ink_app_display_request.c`
- Modify: `D:/FUCKIDF/ink-reader/components/ink_hw/epd_test_pattern.c`
- Modify: `D:/FUCKIDF/ink-reader/components/ink_hw/epd_test_pattern.h`
- Test: `D:/FUCKIDF/ink-reader/main/ink_app_render.c`

- [ ] **Step 1: Add failing overlay geometry self-tests**

Extend the existing overlay tests in [main/ink_app_render.c](/D:/FUCKIDF/ink-reader/main/ink_app_render.c) with explicit checks for popup fill ratio and frameless list density:

```c
static bool app_reader_loading_popup_is_compact_self_test(void)
{
    ink_display_request_t request = {0};
    ink_ui_model_t ui = {0};

    ui.reader.opening = true;
    ui.reader.opening_started_ms = 1U;

    if (!ink_app_build_display_request(&ui, 10U, 0U, INK_RUNTIME_SHELL_COMMAND_NONE, &request)) {
        return false;
    }
    return request.use_reader_menu_overlay
        && request.menu_overlay.action_popup_open
        && request.menu_overlay.action_count == 0U;
}
```

- [ ] **Step 2: Run build to verify current geometry is still implicit**

Run:

```powershell
idf.py build
```

Expected:

- Build passes or fails, but the overlay geometry remains encoded in ad hoc constants rather than a shared template.

- [ ] **Step 3: Pull popup geometry behind named helper families**

In [components/ink_hw/epd_test_pattern.c](/D:/FUCKIDF/ink-reader/components/ink_hw/epd_test_pattern.c), replace hard-coded popup calculations inside `epd_test_pattern_draw_reader_menu_overlay()` with named helper branches:

```c
static epd_test_pattern_rect_t crosspoint_small_popup_rect(void)
{
    epd_test_pattern_rect_t rect = {
        .x = 92,
        .y = 292,
        .w = EPD_GDEY0426T82_WIDTH - 184,
        .h = 120,
    };
    return rect;
}

static epd_test_pattern_rect_t crosspoint_large_popup_rect(bool frameless_panel)
{
    epd_test_pattern_rect_t rect = {
        .x = frameless_panel ? 18 : 24,
        .y = frameless_panel ? 92 : 118,
        .w = frameless_panel ? EPD_GDEY0426T82_WIDTH - 36 : EPD_GDEY0426T82_WIDTH - 48,
        .h = frameless_panel ? EPD_GDEY0426T82_HEIGHT - 116 : 534,
    };
    return rect;
}
```

- [ ] **Step 4: Tighten overlay payload shaping in the request builder**

In [main/ink_app_display_request.c](/D:/FUCKIDF/ink-reader/main/ink_app_display_request.c), truncate long popup titles and normalize card counts so the shared popup template fills available space instead of leaving dead gaps:

```c
epd_test_pattern_truncate_text_middle(
    title,
    overlay->action_popup_title,
    sizeof(overlay->action_popup_title),
    18);
```

and use existing card arrays to fill the visible popup area more evenly.

- [ ] **Step 5: Wire the geometry tests into render self-tests**

Add:

```c
if (!app_reader_loading_popup_is_compact_self_test()) {
    printf("FAIL render reader_loading_popup_is_compact\n");
    return false;
}
```

to `ink_app_render_self_test()`.

- [ ] **Step 6: Run build to verify overlay behavior stays green**

Run:

```powershell
idf.py build
```

Expected:

- Build succeeds.
- Existing reader overlay request/layout self-tests still compile and pass.

- [ ] **Step 7: Commit**

```bash
git add main/ink_app_display_request.c components/ink_hw/epd_test_pattern.h components/ink_hw/epd_test_pattern.c main/ink_app_render.c
git commit -m "refactor: normalize library and reader popup templates"
```

### Task 6: Align WiFi Setup With The Shared System Rhythm

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/components/ink_wifi_setup/ink_wifi_setup_ui.c`
- Modify: `D:/FUCKIDF/ink-reader/components/ink_wifi_setup/ink_wifi_setup_ui.h`
- Test: `D:/FUCKIDF/ink-reader/components/ink_wifi_setup/ink_wifi_setup_ui.c`
- Test: `D:/FUCKIDF/ink-reader/main/test/test_ink_wifi_setup_render.c`

- [ ] **Step 1: Add a failing UI self-test for denser header/list rhythm**

In [components/ink_wifi_setup/ink_wifi_setup_ui.c](/D:/FUCKIDF/ink-reader/components/ink_wifi_setup/ink_wifi_setup_ui.c), add a self-test that compares WiFi list regions against the shared header/list expectations:

```c
static bool wifi_setup_ui_uses_system_header_spacing_self_test(void)
{
    ink_wifi_setup_ui_region_t region = {0};
    ink_wifi_setup_ui_list_body_region(&region);
    return region.y >= 80 && region.y <= 96;
}
```

- [ ] **Step 2: Run build to verify the test is not satisfied by the current constants**

Run:

```powershell
idf.py build
```

Expected:

- The new test fails or the current list bounds remain out of alignment with the rest of the system.

- [ ] **Step 3: Move WiFi layout constants toward the shared header/list rhythm**

Update the constants near the top of [components/ink_wifi_setup/ink_wifi_setup_ui.c](/D:/FUCKIDF/ink-reader/components/ink_wifi_setup/ink_wifi_setup_ui.c):

```c
enum {
    WIFI_LIST_TOP_Y = 84,
    WIFI_LIST_ROW_H = 48,
    WIFI_LIST_CARD_X = 24,
    WIFI_LIST_CARD_W = EPD_GDEY0426T82_WIDTH - 48,
    WIFI_LIST_CARD_INSET = 12,
    PASSWORD_BOX_X = 24,
    PASSWORD_BOX_Y = 120,
    PASSWORD_BOX_W = EPD_GDEY0426T82_WIDTH - 48,
    PASSWORD_BOX_H = 64,
};
```

Do not touch the input state machine here; this task is only about visual structure and dirty-region friendly geometry.

- [ ] **Step 4: Keep MPU password input behavior unchanged while updating page chrome**

When editing [components/ink_wifi_setup/ink_wifi_setup_ui.c](/D:/FUCKIDF/ink-reader/components/ink_wifi_setup/ink_wifi_setup_ui.c), avoid modifying:

```c
ink_wifi_setup_ui_draw_keyboard_key(...)
ink_wifi_setup_ui_keyboard_key_region(...)
```

except where the key or footer bounds must shift to match the new page rhythm.

- [ ] **Step 5: Wire the new WiFi UI self-test**

Append the new check to `ink_wifi_setup_ui_self_test()`:

```c
return wifi_setup_ui_uses_system_header_spacing_self_test()
    && ...existing_checks...;
```

- [ ] **Step 6: Run build to verify the WiFi UI still compiles and tests**

Run:

```powershell
idf.py build
```

Expected:

- Build succeeds.
- Existing WiFi setup state/input/render tests remain intact.

- [ ] **Step 7: Commit**

```bash
git add components/ink_wifi_setup/ink_wifi_setup_ui.h components/ink_wifi_setup/ink_wifi_setup_ui.c main/test/test_ink_wifi_setup_render.c
git commit -m "refactor: align wifi setup ui with shared system rhythm"
```

### Task 7: Full Verification And Device Smoke Pass

**Files:**
- Modify only if verification finds regressions.

- [ ] **Step 1: Run the full firmware build**

Run:

```powershell
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected:

- `Project build complete.`

- [ ] **Step 2: Flash to the target device**

Run:

```powershell
idf.py -p COM9 flash
```

Expected:

- Flash completes successfully without link-time regressions.

- [ ] **Step 3: Run monitor and confirm boot self-tests stay green**

Run:

```powershell
idf.py -p COM9 monitor
```

Expected:

- Boot self-tests do not trip `ESP_ERROR_CHECK`.
- No new `FAIL render ...` lines appear.

- [ ] **Step 4: Manual smoke test Launcher / Library / Reader overlay / Album / USB / WiFi**

Verify on-device:

```text
1. Launcher now shows structured destination cards with a stable title bar.
2. Library tabs/cards still open and refresh correctly.
3. Reader loading popup is compact and centered.
4. Chapter/bookmark popups fill the available popup body more evenly.
5. Photo Album list shows more rows, keeps the title separate, and truncates long names with "...".
6. Photo Album preview remains image-first and does not regain a persistent footer bar.
7. USB MSC page uses the same title and status-page rhythm as other system pages.
8. WiFi list/password pages visually match the system better without breaking MPU password input.
```

- [ ] **Step 5: If verification passes, summarize residual risks**

Record in the work summary:

```text
- Refresh strategy semantics preserved.
- No EPUB scope added.
- Remaining differences from CrossPoint are hardware-specific button/footer and panel-refresh constraints.
```

- [ ] **Step 6: Commit any final verification fixes**

```bash
git add .
git commit -m "test: verify crosspoint ui structural refactor"
```

## Self-Review

### Spec coverage

- Page-template unification: covered by Tasks 1, 3, 4, 5, and 6.
- Title bar, list density, popup structure: covered by Tasks 1, 3, 4, 5, and 6.
- Font hierarchy with `SmallSimSun` for `16px` and below: covered by Task 2.
- Refresh compatibility and bounded dirty regions: preserved across Tasks 3 through 7.
- Avoid disturbing Reader正文: respected by leaving正文 layout untouched and only normalizing overlays in Task 5.

### Placeholder scan

- No `TODO` / `TBD` placeholders remain.
- Each task names exact files, concrete helper names, test names, and commands.

### Type consistency

- Shared names use one family: `epd_test_pattern_crosspoint_*`, `epd_test_pattern_draw_*`, `epd_test_pattern_truncate_text_middle`.
- Template structs use one family: `epd_test_pattern_*_layout_t`, `epd_test_pattern_*_spec_t`.
- App render integration continues to flow through `ink_app_render.c` and `ink_app_display_request.c` without inventing a parallel runtime.
