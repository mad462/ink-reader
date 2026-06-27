# Photo Album App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `photo_album_app` to the single-firmware launcher runtime that preloads a TF-card photo catalog at boot, enters preview mode by default, supports preview/list switching, and renders only validated `480x800 4bpp indexed BMP` images without moving logic back into `app_main.c`.

**Architecture:** Keep the existing runtime/services/render/input boundaries intact by adding a shared `ink_photo_catalog` service-side module for `/sdcard/photos` pre-scan, a focused `ink_photo_bmp_parser` for strict BMP-to-plane conversion, and a dedicated `ink_photo_album_app` state machine that only consumes those modules. Extend the app render path with a distinct `INK_APP_RENDER_MODE_PHOTO_ALBUM` and a native grayscale request path so preview rendering stays explicit and does not reuse reader or WiFi semantics.

**Tech Stack:** ESP-IDF 5.5.4, FreeRTOS runtime tasks, FATFS/dirent file scanning, existing GDEY0426T82 grayscale panel driver, existing runtime app registry and mailbox render pipeline.

---

### Task 1: Photo Catalog Module And Shared Service State

**Files:**
- Create: `main/ink_photo_catalog.h`
- Create: `main/ink_photo_catalog.c`
- Modify: `main/ink_system_services.h`
- Modify: `main/ink_system_services.c`
- Modify: `main/ink_app_boot.h`
- Modify: `main/ink_app_boot.c`
- Modify: `main/ink_app_startup.c`

- [ ] **Step 1: Add the photo catalog API and service fields with failing self-test references**

Declare a shared catalog API in `main/ink_photo_catalog.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define INK_PHOTO_ALBUM_DIR "/sdcard/photos"
#define INK_PHOTO_CATALOG_MAX_ITEMS 128
#define INK_PHOTO_CATALOG_NAME_MAX 64
#define INK_PHOTO_CATALOG_PATH_MAX 256

typedef struct {
    char name[INK_PHOTO_CATALOG_NAME_MAX];
    char path[INK_PHOTO_CATALOG_PATH_MAX];
} ink_photo_catalog_entry_t;

typedef struct {
    bool initialized;
    bool directory_ready;
    bool directory_create_failed;
    size_t count;
    char last_error[64];
    ink_photo_catalog_entry_t entries[INK_PHOTO_CATALOG_MAX_ITEMS];
} ink_photo_catalog_t;

void ink_photo_catalog_init(ink_photo_catalog_t *catalog);
esp_err_t ink_photo_catalog_reload(ink_photo_catalog_t *catalog);
size_t ink_photo_catalog_count(const ink_photo_catalog_t *catalog);
const ink_photo_catalog_entry_t *ink_photo_catalog_entry_at(const ink_photo_catalog_t *catalog, size_t index);
bool ink_photo_catalog_copy_name(const ink_photo_catalog_t *catalog, size_t index, char *out_name, size_t out_size);
bool ink_photo_catalog_copy_path(const ink_photo_catalog_t *catalog, size_t index, char *out_path, size_t out_size);
bool ink_photo_catalog_self_test(void);
```

Extend `main/ink_system_services.h` with a stored catalog:

```c
#include "ink_photo_catalog.h"
...
    ink_photo_catalog_t photo_catalog;
```

Expected follow-up failure: `main/ink_photo_catalog.c` does not exist yet, and service code will not compile until the implementation is added.

- [ ] **Step 2: Wire the catalog source into the build and confirm failure**

Update `main/CMakeLists.txt` to include:

```cmake
        "ink_photo_catalog.c"
```

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: FAIL because `main/ink_photo_catalog.c` is missing.

- [ ] **Step 3: Implement strict directory preload with BMP file filtering**

Create `main/ink_photo_catalog.c` so it:
- calls `ink_photo_catalog_init(...)` to zero state and clear errors
- ensures `/sdcard/photos` exists via a new helper in `ink_app_boot.c`
- scans only that directory, not recursively
- only accepts names ending in `.bmp` or `.BMP`
- sorts accepted filenames case-insensitively in ascending order
- stores up to `INK_PHOTO_CATALOG_MAX_ITEMS` entries with full absolute paths
- records a short status string in `last_error` when directory creation or scanning fails
- keeps `directory_ready` false and `count` zero on error without crashing

Use helpers shaped like:

```c
static bool has_bmp_extension(const char *name);
static int compare_catalog_entries(const void *lhs, const void *rhs);
static bool catalog_store_entry(ink_photo_catalog_t *catalog, const char *name);
```

And load entries with `opendir/readdir/stat`, skipping dot files and subdirectories.

- [ ] **Step 4: Add boot helper for photo directory readiness and integrate preload into system services**

Add to `main/ink_app_boot.h`:

```c
esp_err_t ink_app_ensure_photo_directory(void);
```

Implement it in `main/ink_app_boot.c` by mirroring the existing state/books directory helpers:

```c
esp_err_t ink_app_ensure_photo_directory(void)
{
    struct stat st;
    if (stat(INK_PHOTO_ALBUM_DIR, &st) != 0) {
        if (mkdir(INK_PHOTO_ALBUM_DIR, 0777) != 0 && errno != EEXIST) {
            ESP_LOGW(TAG, "photos dir create failed path=%s errno=%d", INK_PHOTO_ALBUM_DIR, errno);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "created photos dir path=%s", INK_PHOTO_ALBUM_DIR);
    }
    return ESP_OK;
}
```

Then preload the catalog from `ink_system_services_init(...)` only after TF mount succeeds:

```c
    ink_photo_catalog_init(&services->photo_catalog);
    if (services->tf_ready) {
        (void)ink_photo_catalog_reload(&services->photo_catalog);
        ...
    }
```

Also initialize the catalog in `ink_system_services_reset(...)`.

- [ ] **Step 5: Add self-tests for empty directory, extension filtering, and sorting**

Add self-tests in `main/ink_photo_catalog.c` that verify:
- init clears all fields
- `.bmp` and `.BMP` names are accepted, `.png` and bare names are rejected
- sorting is case-insensitive and stable enough for `A.bmp`, `b.BMP`, `c.bmp`

Keep the tests pure by exercising static helper logic directly where possible.

- [ ] **Step 6: Run the build to verify the catalog/service slice passes**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: PASS with the new catalog service state compiled in.

### Task 2: Strict 4bpp BMP Parser

**Files:**
- Create: `main/ink_photo_bmp_parser.h`
- Create: `main/ink_photo_bmp_parser.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Add the parser API and failing self-test declaration**

Create `main/ink_photo_bmp_parser.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "epd_gdey0426t82.h"

typedef struct {
    uint8_t white_index;
    uint8_t light_index;
    uint8_t dark_index;
    uint8_t black_index;
} ink_photo_bmp_palette_map_t;

typedef struct {
    bool valid;
    uint32_t width;
    uint32_t height;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t colors_used;
    ink_photo_bmp_palette_map_t palette;
} ink_photo_bmp_info_t;

esp_err_t ink_photo_bmp_parse_file(
    const char *path,
    uint8_t *lsb_plane,
    size_t lsb_size,
    uint8_t *msb_plane,
    size_t msb_size,
    ink_photo_bmp_info_t *info_out);
bool ink_photo_bmp_parser_self_test(void);
```

Expected follow-up failure: implementation file missing.

- [ ] **Step 2: Wire the parser source into the build and confirm failure**

Update `main/CMakeLists.txt` to include:

```cmake
        "ink_photo_bmp_parser.c"
```

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: FAIL because `main/ink_photo_bmp_parser.c` is missing.

- [ ] **Step 3: Implement strict header and palette validation**

Create `main/ink_photo_bmp_parser.c` to:
- parse BMP file and DIB headers from disk with `fopen/fread`
- reject anything that does not satisfy:
  - `bfType == 'BM'`
  - `biCompression == 0`
  - `biBitCount == 4`
  - `width == 480`
  - `height == 800` or `-800` if top-down support is intentionally included; if not, reject negative height explicitly
  - `colorsUsed >= 4` after normalizing `0` to `16` for 4bpp
- read the palette and classify four grayscale anchors by luminance so we can map source indices to `white/light/dark/black`

Use focused helpers:

```c
static uint32_t read_u32_le(const uint8_t *bytes);
static uint16_t read_u16_le(const uint8_t *bytes);
static bool validate_bmp_headers(const uint8_t *file_header, const uint8_t *dib_header, ink_photo_bmp_info_t *info);
static bool map_palette_to_grayscale(FILE *fp, uint32_t palette_offset, uint32_t colors_used, ink_photo_bmp_info_t *info);
```

- [ ] **Step 4: Convert indexed 4bpp pixels into GDEY grayscale planes**

Continue `main/ink_photo_bmp_parser.c` so pixel decoding:
- reads each packed BMP row with 4-byte stride alignment
- handles BMP bottom-up row order for positive heights
- maps each 4-bit pixel to one of 4 grayscale levels using the palette classification
- writes the grayscale code into `lsb_plane` and `msb_plane` using the panel’s portrait coordinate system
- clears both planes to white before painting

Use a helper like:

```c
static void write_gray_pixel(uint8_t *lsb, uint8_t *msb, uint16_t x, uint16_t y, uint8_t gray2);
```

Where `gray2` is `0..3` and the planes encode the 2-bit level.

- [ ] **Step 5: Add parser self-tests for validation and pixel packing helpers**

Add self-tests that verify:
- invalid width/height/bit depth/compression are rejected by the pure validation helper
- palette classification orders luminance extremes correctly
- `write_gray_pixel(...)` sets the expected LSB/MSB bits for white/light/dark/black

Keep the self-tests file-local and deterministic without needing a real TF file.

- [ ] **Step 6: Run the build to verify the parser slice passes**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: PASS with the parser module compiled in.

### Task 3: Photo Album App, Launcher, And Render Integration

**Files:**
- Create: `main/apps/ink_photo_album_app.h`
- Create: `main/apps/ink_photo_album_app.c`
- Modify: `main/apps/ink_app_iface.h`
- Modify: `main/apps/ink_launcher_app.h`
- Modify: `main/apps/ink_launcher_app.c`
- Modify: `main/app_main.c`
- Modify: `main/ink_app_render.h`
- Modify: `main/ink_app_render.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Add the album app API, state type, and render mode with failing references**

Create `main/apps/ink_photo_album_app.h` with the required state shape:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ink_app_iface.h"

typedef enum {
    INK_PHOTO_ALBUM_VIEW_PREVIEW = 0,
    INK_PHOTO_ALBUM_VIEW_LIST,
} ink_photo_album_view_mode_t;

typedef struct {
    bool initialized;
    bool catalog_ready;
    bool image_loaded;
    bool load_failed;
    size_t current_index;
    size_t list_selected_index;
    size_t total_count;
    ink_photo_album_view_mode_t view_mode;
    char current_name[64];
    char current_path[256];
    char status_text[64];
    uint8_t *lsb_plane;
    uint8_t *msb_plane;
    size_t plane_size;
} ink_photo_album_app_state_t;

typedef struct {
    const ink_photo_album_app_state_t *state;
} ink_photo_album_render_state_t;

const ink_app_descriptor_t *ink_photo_album_app_descriptor(void);
bool ink_photo_album_app_self_test(void);
```

Extend `main/apps/ink_app_iface.h`:

```c
    INK_APP_RENDER_MODE_WIFI_SETUP,
    INK_APP_RENDER_MODE_PHOTO_ALBUM,
```

Expected follow-up failure: `main/apps/ink_photo_album_app.c` does not exist yet.

- [ ] **Step 2: Wire the new app source into the build and confirm failure**

Update `main/CMakeLists.txt` to include:

```cmake
        "apps/ink_photo_album_app.c"
```

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: FAIL because the app source is missing.

- [ ] **Step 3: Implement app-local state machine and preview/list behavior**

Create `main/apps/ink_photo_album_app.c` so it:
- allocates one static app state
- lazily allocates `lsb_plane` and `msb_plane` on first enter using `malloc`
- on enter, reads the shared `runtime->services->photo_catalog`, copies `total_count`, keeps `current_index` across exits, defaults `view_mode` to preview, and attempts to load the current image
- on empty catalog, sets `catalog_ready=true`, `image_loaded=false`, and `status_text="相册为空"` or a short equivalent
- on Confirm, toggles between preview and list, keeping `list_selected_index` synchronized with `current_index`
- on Back, always requests switch to `launcher`
- on nav previous/next and tilt previous/next, changes index with wraparound and reloads the image
- in list mode, the same navigation changes `list_selected_index` and immediately mirrors `current_index` so the next preview opens at the same image
- on load failure, updates `status_text` but does not crash

Use focused helpers:

```c
static bool request_switch_to_launcher(ink_system_runtime_t *runtime);
static bool album_sync_catalog(ink_photo_album_app_state_t *state, const ink_system_services_t *services);
static bool album_load_current_image(ink_photo_album_app_state_t *state);
static bool album_select_index(ink_photo_album_app_state_t *state, size_t index);
static void album_set_status(ink_photo_album_app_state_t *state, const char *text);
```

- [ ] **Step 4: Register the app in runtime composition and launcher selection**

Modify `main/app_main.c` to add only registration:

```c
#include "apps/ink_photo_album_app.h"
...
    ESP_ERROR_CHECK(ink_system_runtime_register_app(&app.runtime, ink_photo_album_app_descriptor()) ? ESP_OK : ESP_FAIL);
```

Modify `main/apps/ink_launcher_app.c` to:
- set `INK_LAUNCHER_APP_COUNT = 3`
- map indices in order:

```c
static const char *const kLauncherTargetIds[INK_LAUNCHER_APP_COUNT] = {
    "reader",
    "wifi_setup",
    "photo_album",
};
```

- render a third launcher row labeled `相册` or `Photo Album`
- update self-tests so nav wraps through 3 apps and confirm on index `2` targets `photo_album`

- [ ] **Step 5: Extend the render pipeline for album preview and list states**

Modify `main/ink_app_render.h` to expose a photo-aware request path if needed:

```c
esp_err_t ink_app_render_gray_planes_request(
    ink_app_context_t *app,
    const uint8_t *lsb_plane,
    size_t lsb_length,
    const uint8_t *msb_plane,
    size_t msb_length,
    epd_gdey0426t82_phase_t *phase_out);
```

Modify `main/ink_app_render.c` so:
- `render_model_to_buffer(...)` gains `INK_APP_RENDER_MODE_PHOTO_ALBUM` for non-preview text states (empty, error, list mode)
- a new `fill_photo_album_page(...)` draws:
  - preview fallback text when no image loaded
  - list mode title, current name, and a short surrounding filename list when catalog has entries
- `ink_app_render_display_request(...)` detects `request->use_app_render_model` with `INK_APP_RENDER_MODE_PHOTO_ALBUM`; if the state is in preview mode with `image_loaded=true`, call `epd_gdey0426t82_gray_refresh(...)` directly on the app planes instead of drawing text into the mono framebuffer
- list mode and empty/error mode continue through the existing mono framebuffer path

Keep semantics explicit; do not overload reader bitmap/native page fields for album planes.

- [ ] **Step 6: Add self-tests for app behavior and render mode routing**

Add self-tests that verify:
- enter with an empty catalog leaves preview mode selected but `image_loaded=false`
- Back requests switch to launcher from both preview and list modes
- Confirm toggles preview/list and preserves the selected index
- nav next/previous wraps around `total_count`
- launcher confirm on the third slot requests `photo_album`
- render model with `INK_APP_RENDER_MODE_PHOTO_ALBUM` can draw a text/list fallback page

- [ ] **Step 7: Run the build to verify full integration passes**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: PASS with the new app registered and render path compiled in.

### Task 4: Verification And Requirement Audit

**Files:**
- Modify if needed after verification: `main/apps/ink_photo_album_app.c`
- Modify if needed after verification: `main/ink_photo_catalog.c`
- Modify if needed after verification: `main/ink_photo_bmp_parser.c`
- Modify if needed after verification: `main/ink_app_render.c`

- [ ] **Step 1: Re-run the full firmware build with the required ESP-IDF version**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py set-target esp32s3
idf.py build
```

Expected: PASS for the main firmware.

- [ ] **Step 2: Audit the implemented behavior against the requested requirements**

Verify explicitly that the code now covers:
- independent `photo_album_app`
- launcher entry addition
- strict `/sdcard/photos` scanning and creation attempt
- only `.bmp` / `.BMP`
- only valid `480x800 4bpp indexed BMP`
- enter preview first
- up/down or tilt previous/next navigation via normalized nav/tilt events
- Confirm toggles preview/list
- Back always exits to launcher
- same-boot re-entry preserves `current_index`
- boot-time preload after TF mount
- no app logic pushed back into `app_main.c`

- [ ] **Step 3: Fix any verification failures and re-run build immediately**

If any build or audit item fails:
- patch only the affected files
- rerun:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
idf.py build
```

Expected: PASS before claiming completion.