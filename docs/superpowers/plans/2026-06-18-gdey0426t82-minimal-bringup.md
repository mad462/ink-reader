# GDEY0426T82 Minimal Bring-Up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a minimal ESP-IDF 5.5.4 demo for ESP32-S3 that drives the 4.26-inch GDEY0426T82 e-paper panel through full-screen refresh only and proves the panel works with the provided wiring.

**Architecture:** Create a small ESP-IDF app with one focused driver module for panel transport and register sequencing, one framebuffer helper for deterministic test patterns, and a tiny `app_main` demo that performs white, black, and pattern refreshes before entering deep sleep.

**Tech Stack:** ESP-IDF 5.5.4, ESP32-S3, SPI master driver, GPIO driver, FreeRTOS, Unity host test for pure framebuffer helpers

---

### Task 1: Create the ESP-IDF project skeleton

**Files:**
- Create: `D:/FUCKIDF/ink-reader/CMakeLists.txt`
- Create: `D:/FUCKIDF/ink-reader/sdkconfig.defaults`
- Create: `D:/FUCKIDF/ink-reader/main/CMakeLists.txt`

- [ ] **Step 1: Write the failing build precondition**

Confirm the workspace is not yet an ESP-IDF project.

Run: `Test-Path D:/FUCKIDF/ink-reader/CMakeLists.txt`
Expected: `False`

- [ ] **Step 2: Create the top-level CMake file**

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ink_reader)
```

- [ ] **Step 3: Create the default configuration file**

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_PARTITION_TABLE_SINGLE_APP=y
CONFIG_COMPILER_OPTIMIZATION_DEFAULT=y
```

- [ ] **Step 4: Create the `main` component CMake file**

```cmake
idf_component_register(
    SRCS
        "app_main.c"
        "epd_gdey0426t82.c"
        "epd_test_pattern.c"
    INCLUDE_DIRS
        "."
)
```

- [ ] **Step 5: Verify the new project files exist**

Run: `Get-ChildItem D:/FUCKIDF/ink-reader, D:/FUCKIDF/ink-reader/main`
Expected: top-level `CMakeLists.txt`, `sdkconfig.defaults`, and `main/CMakeLists.txt` appear

### Task 2: Add a pure helper test first for the test-pattern generator

**Files:**
- Create: `D:/FUCKIDF/ink-reader/main/epd_test_pattern.h`
- Create: `D:/FUCKIDF/ink-reader/main/test/test_epd_test_pattern.c`
- Test: `D:/FUCKIDF/ink-reader/main/test/test_epd_test_pattern.c`

- [ ] **Step 1: Write the failing Unity test**

```c
#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "epd_test_pattern.h"

void test_epd_pattern_generates_non_uniform_buffer(void)
{
    uint8_t buffer[32];

    memset(buffer, 0x00, sizeof(buffer));
    epd_test_pattern_fill_stripes(buffer, sizeof(buffer));

    TEST_ASSERT_NOT_EQUAL_HEX8(0x00, buffer[0]);
    TEST_ASSERT_NOT_EQUAL_HEX8(buffer[0], buffer[1]);
}
```

- [ ] **Step 2: Run the test target to verify it fails**

Run: `rg -n "epd_test_pattern_fill_stripes" D:/FUCKIDF/ink-reader`
Expected: only the test references the symbol, so the implementation is still missing

- [ ] **Step 3: Create the helper interface**

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

void epd_test_pattern_fill_stripes(uint8_t *buffer, size_t length);
```

- [ ] **Step 4: Implement the minimal helper to satisfy the test**

```c
#include "epd_test_pattern.h"

void epd_test_pattern_fill_stripes(uint8_t *buffer, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = (i % 2 == 0) ? 0xAA : 0x55;
    }
}
```

- [ ] **Step 5: Verify the symbol now exists**

Run: `rg -n "epd_test_pattern_fill_stripes" D:/FUCKIDF/ink-reader`
Expected: the header, source, and test now all reference the helper

### Task 3: Implement the minimal panel driver

**Files:**
- Create: `D:/FUCKIDF/ink-reader/main/epd_gdey0426t82.h`
- Create: `D:/FUCKIDF/ink-reader/main/epd_gdey0426t82.c`

- [ ] **Step 1: Declare the driver API**

```c
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define EPD_GDEY0426T82_WIDTH 480
#define EPD_GDEY0426T82_HEIGHT 800
#define EPD_GDEY0426T82_BUFFER_SIZE (EPD_GDEY0426T82_WIDTH * EPD_GDEY0426T82_HEIGHT / 8)

typedef struct {
    int gpio_mosi;
    int gpio_sclk;
    int gpio_cs;
    int gpio_dc;
    int gpio_rst;
    int gpio_busy;
    int spi_host;
    int spi_clock_hz;
} epd_gdey0426t82_config_t;

esp_err_t epd_gdey0426t82_init(const epd_gdey0426t82_config_t *config);
esp_err_t epd_gdey0426t82_full_refresh(const uint8_t *buffer, size_t length);
esp_err_t epd_gdey0426t82_sleep(void);
```

- [ ] **Step 2: Implement the transport and busy-wait path**

Implement SPI bus init, device add, GPIO direction setup, reset pulse, and a timeout-based busy wait matching the Arduino busy polarity.

- [ ] **Step 3: Implement the full-refresh register sequence**

Port the Arduino sequence for `EPD_HW_Init`, `EPD_Update`, `EPD_WhiteScreen_ALL`, and `EPD_DeepSleep`, keeping only the full-refresh path.

- [ ] **Step 4: Add argument validation**

Reject null config, null buffers, or buffers shorter than `EPD_GDEY0426T82_BUFFER_SIZE` with `ESP_ERR_INVALID_ARG`.

- [ ] **Step 5: Verify the driver API is referenced by the app**

Run: `rg -n "epd_gdey0426t82_(init|full_refresh|sleep)" D:/FUCKIDF/ink-reader`
Expected: references in the header, source, and app entry file

### Task 4: Add the demo app entrypoint

**Files:**
- Create: `D:/FUCKIDF/ink-reader/main/app_main.c`

- [ ] **Step 1: Write the app skeleton**

Create an `app_main` that logs boot, allocates a 48 KB framebuffer, and loads the fixed pin mapping:

```c
static const epd_gdey0426t82_config_t panel = {
    .gpio_mosi = 4,
    .gpio_sclk = 5,
    .gpio_cs = 6,
    .gpio_dc = 7,
    .gpio_rst = 15,
    .gpio_busy = 16,
    .spi_host = SPI2_HOST,
    .spi_clock_hz = 10 * 1000 * 1000,
};
```

- [ ] **Step 2: Add the display sequence**

Call the driver in this order:

```c
memset(buffer, 0xFF, buffer_size);
epd_gdey0426t82_full_refresh(buffer, buffer_size);
vTaskDelay(pdMS_TO_TICKS(2000));

memset(buffer, 0x00, buffer_size);
epd_gdey0426t82_full_refresh(buffer, buffer_size);
vTaskDelay(pdMS_TO_TICKS(2000));

epd_test_pattern_fill_stripes(buffer, buffer_size);
epd_gdey0426t82_full_refresh(buffer, buffer_size);

epd_gdey0426t82_sleep();
```

- [ ] **Step 3: Add clear logging and hard-fail checks**

Use `ESP_LOGI` around each phase and `ESP_ERROR_CHECK` for every driver call.

- [ ] **Step 4: Verify the app references all expected pieces**

Run: `rg -n "EPD_GDEY0426T82_BUFFER_SIZE|epd_test_pattern_fill_stripes|ESP_ERROR_CHECK" D:/FUCKIDF/ink-reader/main`
Expected: the app ties together the helper, buffer size, and driver calls

### Task 5: Build and verify with ESP-IDF 5.5.4

**Files:**
- Modify: `D:/FUCKIDF/ink-reader/sdkconfig.defaults` if build feedback requires config corrections

- [ ] **Step 1: Export the canonical ESP-IDF 5.5.4 environment**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
```

Expected: the shell reports the 5.5.4 environment is active

- [ ] **Step 2: Set the target and run the first build**

Run:

```powershell
idf.py set-target esp32s3
idf.py build
```

Expected: either a successful build or concrete compiler/configuration errors

- [ ] **Step 3: Fix any compile errors with minimal edits**

Adjust includes, enum names, or `sdkconfig.defaults` entries only as required by the build output.

- [ ] **Step 4: Re-run the full build**

Run: `idf.py build`
Expected: `Project build complete.` and exit code `0`

### Task 6: Document the on-device bring-up expectations

**Files:**
- Create: `D:/FUCKIDF/ink-reader/README.md`

- [ ] **Step 1: Document the wiring and environment**

List the exact GPIO mapping, required ESP-IDF version, and flash/PSRAM assumptions.

- [ ] **Step 2: Document the flash and monitor commands**

```powershell
idf.py -p COMx flash monitor
```

- [ ] **Step 3: Document the expected panel behavior**

Describe the visible sequence: full white, full black, striped test pattern, then deep sleep.

- [ ] **Step 4: Verify the README mentions the critical paths**

Run: `rg -n "GDEY0426T82|GPIO|flash monitor|deep sleep" D:/FUCKIDF/ink-reader/README.md`
Expected: all operational notes are present
