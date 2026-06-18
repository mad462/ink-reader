# SDMMC FAT32 Bring-Up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mount a FAT32 TF card over ESP32-S3 SDMMC 4-bit mode and print card + root-directory information at boot.

**Architecture:** Keep the existing e-paper demo intact and add one focused SD card helper that configures the SDMMC host, mounts the filesystem through VFS FAT, lists the root directory, and reports failures clearly over serial. Use the user's provided GPIO mapping directly and avoid adding write paths or image loading until mount/read success is confirmed.

**Tech Stack:** ESP-IDF 5.5.4, `sdmmc`, `esp_vfs_fat`, `dirent`, existing `main` component

---

### Task 1: Add SD card mount helper and root-directory probe

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`
- Modify: `D:\FUCKIDF\ink-reader\main\CMakeLists.txt`

- [ ] **Step 1: Add the required component dependency**

```cmake
idf_component_register(
    SRCS
        "app_main.c"
        "epd_gdey0426t82.c"
        "epd_test_pattern.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_driver_sdmmc
        fatfs
        vfs
)
```

- [ ] **Step 2: Write a failing build-driving integration attempt**

Add the SDMMC mount call and directory listing code to `app_main.c` before implementing any helper abstraction. The first build should fail if headers or symbols are missing, proving the new integration surface is actually exercised.

```c
ESP_LOGI(TAG, "mounting TF card over SDMMC");
ESP_ERROR_CHECK(sd_card_mount_and_list_root());
```

Expected first build failure: unresolved helper symbol or missing includes.

- [ ] **Step 3: Implement the minimal SDMMC helper in `app_main.c`**

Add includes and a focused helper using the user-provided pins:

```c
#include <dirent.h>
#include <errno.h>
#include <sys/unistd.h>

#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
```

And implement:

```c
static esp_err_t sd_card_mount_and_list_root(void)
{
    static const char *kMountPoint = "/sdcard";
    static constexpr int kSdSdioClk = 40;
    static constexpr int kSdSdioCmd = 39;
    static constexpr int kSdSdioD0 = 41;
    static constexpr int kSdSdioD1 = 42;
    static constexpr int kSdSdioD2 = 48;
    static constexpr int kSdSdioD3 = 38;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = kSdSdioClk;
    slot_config.cmd = kSdSdioCmd;
    slot_config.d0 = kSdSdioD0;
    slot_config.d1 = kSdSdioD1;
    slot_config.d2 = kSdSdioD2;
    slot_config.d3 = kSdSdioD3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TF mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_card_print_info(stdout, card);

    DIR *dir = opendir(kMountPoint);
    if (dir == NULL) {
        ESP_LOGE(TAG, "opendir(%s) failed: errno=%d", kMountPoint, errno);
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "sdcard entry: %s", entry->d_name);
    }
    closedir(dir);
    return ESP_OK;
}
```

- [ ] **Step 4: Run build to verify the integration passes**

Run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected: `Project build complete`.

### Task 2: Put the SD card probe into the boot demo flow

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`

- [ ] **Step 1: Call the helper at boot before the e-paper sequence**

```c
ESP_LOGI(TAG, "mounting TF card over SDMMC");
ESP_ERROR_CHECK(sd_card_mount_and_list_root());
```

Place it after panel init or before display updates depending on readability preference, but keep boot logs easy to scan.

- [ ] **Step 2: Preserve the existing display verification sequence**

Keep:

```c
full white -> full black -> text demo -> deep sleep
```

This preserves the known-good display bring-up while adding TF verification.

- [ ] **Step 3: Flash and capture runtime evidence**

Run:

```powershell
idf.py -p COM10 flash
```

Then capture serial logs for 10-20 seconds.

Expected success evidence:
- `mounting TF card over SDMMC`
- card identification printed by `sdmmc_card_print_info`
- at least one `sdcard entry:` log, or an empty directory without mount failure
- existing e-paper logs still appear afterwards

### Task 3: Validate error reporting and document the current bring-up state

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\README.md`

- [ ] **Step 1: Document the SDMMC wiring and expected log flow**

Add:

```markdown
## TF Card (SDMMC 4-bit)

Wiring:
- CLK -> GPIO40
- CMD -> GPIO39
- D0 -> GPIO41
- D1 -> GPIO42
- D2 -> GPIO48
- D3 -> GPIO38

Expected boot logs:
- TF mount start
- card info dump
- root directory listing
- e-paper white/black/text demo
```

- [ ] **Step 2: Re-run build after docs-adjacent code changes**

Run:

```powershell
idf.py build
```

Expected: `Project build complete`.