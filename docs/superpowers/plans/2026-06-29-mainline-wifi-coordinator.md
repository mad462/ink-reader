# Mainline Wi-Fi Coordinator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a serialized Wi-Fi coordinator to mainline firmware and migrate existing Wi-Fi consumers onto it before integrating voice-tag ASR behavior.

**Architecture:** Keep `ink_wifi_manager` as the low-level ESP-IDF Wi-Fi wrapper, and introduce a new `ink_wifi_coordinator` service that owns one task, one queue, one compact lease table, and one centralized idle disconnect policy. Roll migration in stages: coordinator module first, then system auto-connect and time sync, then Wi-Fi setup app.

**Tech Stack:** ESP-IDF 5.5.4, FreeRTOS tasks/queues/event groups, existing `ink_net` component, mainline self-test pattern in `app_main.c`, existing Wi-Fi setup app worker flow.

---

### Task 1: Add the coordinator module skeleton and self-tests

**Files:**
- Create: `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_coordinator.h`
- Create: `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_coordinator.c`
- Modify: `D:\FUCKIDF\ink-reader\components\ink_net\CMakeLists.txt`

- [ ] **Step 1: Write the failing build change**

Modify `D:\FUCKIDF\ink-reader\components\ink_net\CMakeLists.txt` so it references the new source before that file exists:

```cmake
idf_component_register(
    SRCS
        "ink_wifi_manager.c"
        "ink_wifi_coordinator.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
)
```

- [ ] **Step 2: Run build to verify missing-module failure**

Run:

```powershell
cd D:\FUCKIDF\ink-reader
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
```

Expected:
- build fails because `components/ink_net/ink_wifi_coordinator.c` does not exist yet

- [ ] **Step 3: Add the public coordinator header**

Create `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_coordinator.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ink_wifi_manager.h"

typedef enum {
    INK_WIFI_COORDINATOR_OWNER_NONE = 0,
    INK_WIFI_COORDINATOR_OWNER_BOOT_AUTO_CONNECT,
    INK_WIFI_COORDINATOR_OWNER_TIME_SYNC,
    INK_WIFI_COORDINATOR_OWNER_WIFI_SETUP,
    INK_WIFI_COORDINATOR_OWNER_VOICE_TAG_ASR,
} ink_wifi_coordinator_owner_t;

typedef enum {
    INK_WIFI_COORDINATOR_STATE_OFF = 0,
    INK_WIFI_COORDINATOR_STATE_STARTING,
    INK_WIFI_COORDINATOR_STATE_DISCONNECTED,
    INK_WIFI_COORDINATOR_STATE_CONNECTING,
    INK_WIFI_COORDINATOR_STATE_ONLINE,
    INK_WIFI_COORDINATOR_STATE_ERROR,
} ink_wifi_coordinator_state_t;

typedef enum {
    INK_WIFI_COORDINATOR_RESULT_OK = 0,
    INK_WIFI_COORDINATOR_RESULT_TIMEOUT,
    INK_WIFI_COORDINATOR_RESULT_NO_CREDENTIAL,
    INK_WIFI_COORDINATOR_RESULT_CONNECT_FAILED,
    INK_WIFI_COORDINATOR_RESULT_SCAN_FAILED,
    INK_WIFI_COORDINATOR_RESULT_BUSY_RETRYABLE,
    INK_WIFI_COORDINATOR_RESULT_INVALID_STATE,
} ink_wifi_coordinator_result_t;

typedef enum {
    INK_WIFI_COORDINATOR_REQUEST_ENSURE_CONNECTED = 0,
    INK_WIFI_COORDINATOR_REQUEST_SCAN,
    INK_WIFI_COORDINATOR_REQUEST_CONNECT_SAVED,
    INK_WIFI_COORDINATOR_REQUEST_CONNECT_PASSWORD,
    INK_WIFI_COORDINATOR_REQUEST_RELEASE_LEASE,
    INK_WIFI_COORDINATOR_REQUEST_DISCONNECT_IF_IDLE,
} ink_wifi_coordinator_request_type_t;

typedef struct {
    ink_wifi_coordinator_state_t state;
    ink_wifi_coordinator_result_t last_result;
    ink_wifi_status_t wifi_status;
    uint32_t active_leases;
    uint32_t idle_timeout_ms;
    bool request_in_progress;
} ink_wifi_coordinator_status_t;

typedef struct {
    ink_wifi_coordinator_request_type_t type;
    ink_wifi_coordinator_owner_t owner;
    bool keep_alive;
    bool best_effort_saved;
    uint32_t timeout_ms;
    char ssid[INK_WIFI_SSID_MAX_LEN + 1];
    char password[INK_WIFI_PASSWORD_MAX_LEN + 1];
    ink_wifi_scan_list_t *scan_out;
    ink_wifi_status_t *status_out;
} ink_wifi_coordinator_request_t;

esp_err_t ink_wifi_coordinator_init(void);
esp_err_t ink_wifi_coordinator_start(void);
esp_err_t ink_wifi_coordinator_request(
    const ink_wifi_coordinator_request_t *request,
    ink_wifi_coordinator_result_t *out_result);
esp_err_t ink_wifi_coordinator_release_owner(
    ink_wifi_coordinator_owner_t owner,
    uint32_t timeout_ms);
esp_err_t ink_wifi_coordinator_disconnect_if_idle(uint32_t timeout_ms);
esp_err_t ink_wifi_coordinator_get_status(ink_wifi_coordinator_status_t *out_status);
bool ink_wifi_coordinator_self_test(void);
```

- [ ] **Step 4: Add the minimal coordinator implementation with local self-test helpers**

Create `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_coordinator.c` with:

```c
#include "ink_wifi_coordinator.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "ink_wifi_coord";

enum {
    INK_WIFI_COORDINATOR_QUEUE_LEN = 6,
    INK_WIFI_COORDINATOR_TASK_STACK = 6144,
    INK_WIFI_COORDINATOR_DEFAULT_IDLE_MS = 30000,
    INK_WIFI_COORDINATOR_MAX_LEASES = 6,
};

typedef struct {
    bool in_use;
    ink_wifi_coordinator_owner_t owner;
} ink_wifi_coordinator_lease_slot_t;

typedef struct {
    ink_wifi_coordinator_request_t request;
    TaskHandle_t reply_task;
    ink_wifi_coordinator_result_t result;
    esp_err_t error;
} ink_wifi_coordinator_work_item_t;

static QueueHandle_t s_request_queue;
static TaskHandle_t s_worker_task;
static ink_wifi_coordinator_status_t s_status;
static ink_wifi_coordinator_lease_slot_t s_leases[INK_WIFI_COORDINATOR_MAX_LEASES];

static uint32_t coordinator_active_leases(void);
static bool coordinator_add_lease(ink_wifi_coordinator_owner_t owner);
static void coordinator_remove_lease(ink_wifi_coordinator_owner_t owner);
static void coordinator_refresh_status_counts(void);
static void coordinator_worker_task(void *arg);
```

In the first pass:
- make `init()` set default status values
- make `start()` create queue and worker
- make `request()` validate args and post to queue
- make the worker support `RELEASE_LEASE` and `DISCONNECT_IF_IDLE`
- return `INK_WIFI_COORDINATOR_RESULT_INVALID_STATE` for unimplemented request types for now

- [ ] **Step 5: Add the first self-test body**

In `ink_wifi_coordinator.c`, add:

```c
bool ink_wifi_coordinator_self_test(void)
{
    ink_wifi_coordinator_status_t status;

    memset(&s_status, 0, sizeof(s_status));
    memset(s_leases, 0, sizeof(s_leases));
    s_status.state = INK_WIFI_COORDINATOR_STATE_OFF;
    s_status.idle_timeout_ms = INK_WIFI_COORDINATOR_DEFAULT_IDLE_MS;
    if (!coordinator_add_lease(INK_WIFI_COORDINATOR_OWNER_TIME_SYNC)) {
        return false;
    }
    if (!coordinator_add_lease(INK_WIFI_COORDINATOR_OWNER_WIFI_SETUP)) {
        return false;
    }
    coordinator_remove_lease(INK_WIFI_COORDINATOR_OWNER_TIME_SYNC);
    coordinator_refresh_status_counts();
    if (s_status.active_leases != 1U) {
        return false;
    }
    if (ink_wifi_coordinator_get_status(&status) != ESP_OK) {
        return false;
    }
    return status.idle_timeout_ms == INK_WIFI_COORDINATOR_DEFAULT_IDLE_MS
        && status.state == INK_WIFI_COORDINATOR_STATE_OFF;
}
```

- [ ] **Step 6: Re-run the build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- new coordinator module is compiled into `ink_net`

- [ ] **Step 7: Commit the skeleton**

Run:

```powershell
git add components/ink_net/CMakeLists.txt components/ink_net/ink_wifi_coordinator.h components/ink_net/ink_wifi_coordinator.c
git commit -m "feat: add wifi coordinator skeleton"
```

Expected:
- commit succeeds with only the coordinator skeleton files staged

### Task 2: Implement queue handling, result mapping, and lease-aware connect behavior

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_coordinator.c`
- Modify: `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_coordinator.h`

- [ ] **Step 1: Write the next failing self-test logic**

Extend `ink_wifi_coordinator_self_test()` with checks for:
- duplicate lease acquisition does not consume a second slot
- releasing a missing lease is harmless
- `CONNECT_SAVED` with `best_effort_saved=true` maps `ESP_ERR_NOT_FOUND` to `NO_CREDENTIAL`

Use a helper shape like:

```c
if (!coordinator_add_lease(INK_WIFI_COORDINATOR_OWNER_TIME_SYNC)) {
    return false;
}
if (coordinator_add_lease(INK_WIFI_COORDINATOR_OWNER_TIME_SYNC)) {
    return false;
}
```

- [ ] **Step 2: Run build to verify the new assertions fail**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- the new self-test logic is not satisfied yet when later wired into boot self-tests

- [ ] **Step 3: Add low-level operation helpers**

In `ink_wifi_coordinator.c`, add helpers that wrap the existing manager:

```c
static ink_wifi_coordinator_result_t coordinator_result_from_connect_error(esp_err_t err)
{
    if (err == ESP_OK) {
        return INK_WIFI_COORDINATOR_RESULT_OK;
    }
    if (err == ESP_ERR_TIMEOUT) {
        return INK_WIFI_COORDINATOR_RESULT_TIMEOUT;
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return INK_WIFI_COORDINATOR_RESULT_NO_CREDENTIAL;
    }
    return INK_WIFI_COORDINATOR_RESULT_CONNECT_FAILED;
}

static ink_wifi_coordinator_result_t coordinator_run_connect_saved(
    const ink_wifi_coordinator_request_t *request)
{
    esp_err_t err;
    ink_wifi_status_t status = {0};

    if (request->best_effort_saved) {
        err = ink_wifi_manager_connect_best(request->timeout_ms, &status);
    } else {
        err = ink_wifi_manager_connect_saved(request->ssid, request->timeout_ms, &status);
    }
    if (request->status_out != NULL) {
        *request->status_out = status;
    }
    return coordinator_result_from_connect_error(err);
}
```

- [ ] **Step 4: Implement the worker switch for all first-version request types**

Update the worker switch to support:
- `ENSURE_CONNECTED`
- `SCAN`
- `CONNECT_SAVED`
- `CONNECT_PASSWORD`
- `RELEASE_LEASE`
- `DISCONNECT_IF_IDLE`

Use behavior like:

```c
case INK_WIFI_COORDINATOR_REQUEST_SCAN:
    item->error = ink_wifi_manager_scan(item->request.scan_out);
    item->result = item->error == ESP_OK
        ? INK_WIFI_COORDINATOR_RESULT_OK
        : INK_WIFI_COORDINATOR_RESULT_SCAN_FAILED;
    break;
```

For `ENSURE_CONNECTED`:
- if already connected, return `OK`
- otherwise call `ink_wifi_manager_connect_best()`
- if `keep_alive` is true, add a lease for `owner`

For `CONNECT_PASSWORD`:
- call `ink_wifi_manager_connect_password()`
- write back `status_out`
- on success and `keep_alive=true`, add lease

- [ ] **Step 5: Add idle-disconnect policy in one place**

Add a helper:

```c
static void coordinator_maybe_disconnect_idle(void)
{
    if (coordinator_active_leases() != 0U) {
        return;
    }
    (void)ink_wifi_manager_disconnect();
    s_status.state = INK_WIFI_COORDINATOR_STATE_DISCONNECTED;
    s_status.last_result = INK_WIFI_COORDINATOR_RESULT_OK;
}
```

If `ink_wifi_manager_disconnect()` does not exist yet, add it in the next task instead of calling raw ESP-IDF APIs from the coordinator.

- [ ] **Step 6: Re-run build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- coordinator now supports the first complete request set

- [ ] **Step 7: Commit the completed coordinator core**

Run:

```powershell
git add components/ink_net/ink_wifi_coordinator.c components/ink_net/ink_wifi_coordinator.h
git commit -m "feat: implement wifi coordinator queue and leases"
```

Expected:
- commit succeeds

### Task 3: Fill the low-level gap in `ink_wifi_manager` and expose coordinator status to mainline

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_manager.h`
- Modify: `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_manager.c`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_system_services.h`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_system_services.c`
- Modify: `D:\FUCKIDF\ink-reader\main\app_main.c`

- [ ] **Step 1: Add the failing call site for explicit disconnect**

Modify `ink_wifi_coordinator.c` to call:

```c
(void)ink_wifi_manager_disconnect();
```

before that function exists in the manager API.

- [ ] **Step 2: Run build to verify the missing symbol failure**

Run:

```powershell
idf.py build
```

Expected:
- build fails because `ink_wifi_manager_disconnect` is undeclared or undefined

- [ ] **Step 3: Add the manager disconnect API**

In `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_manager.h`, add:

```c
esp_err_t ink_wifi_manager_disconnect(void);
```

In `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_manager.c`, implement:

```c
esp_err_t ink_wifi_manager_disconnect(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }
    esp_err_t ret = esp_wifi_disconnect();
    if (ret == ESP_ERR_WIFI_NOT_CONNECT || ret == ESP_ERR_WIFI_CONN) {
        ret = ESP_OK;
    }
    s_status.connected = false;
    if (ret == ESP_OK) {
        s_status.last_error = ESP_OK;
    }
    return ret;
}
```

- [ ] **Step 4: Add coordinator status storage to system services**

In `D:\FUCKIDF\ink-reader\main\ink_system_services.h`, add:

```c
#include "ink_wifi_coordinator.h"
```

and add fields:

```c
    bool wifi_coordinator_ready;
    ink_wifi_coordinator_status_t wifi_coordinator_status;
```

In `ink_system_services_reset()`, zero them naturally through `memset`.

- [ ] **Step 5: Initialize and self-test the coordinator during boot**

In `D:\FUCKIDF\ink-reader\main\ink_system_services.c`, update init flow:

```c
    services->wifi_ready = ink_wifi_manager_init() == ESP_OK;
    services->wifi_coordinator_ready = false;
    if (services->wifi_ready) {
        if (ink_wifi_coordinator_init() == ESP_OK
            && ink_wifi_coordinator_start() == ESP_OK) {
            services->wifi_coordinator_ready = true;
            (void)ink_wifi_coordinator_get_status(&services->wifi_coordinator_status);
        }
    }
```

In `D:\FUCKIDF\ink-reader\main\app_main.c`, add the boot self-test:

```c
    ESP_ERROR_CHECK(ink_wifi_coordinator_self_test() ? ESP_OK : ESP_FAIL);
```

near the existing service-related self-test block.

- [ ] **Step 6: Re-run build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- mainline now compiles with explicit coordinator initialization and self-test

- [ ] **Step 7: Commit the manager/service wiring**

Run:

```powershell
git add components/ink_net/ink_wifi_manager.h components/ink_net/ink_wifi_manager.c main/ink_system_services.h main/ink_system_services.c main/app_main.c
git commit -m "feat: wire wifi coordinator into system services"
```

Expected:
- commit succeeds

### Task 4: Migrate boot auto-connect to the coordinator

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\ink_system_services.c`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_system_services.h`

- [ ] **Step 1: Change the worker intent before changing implementation**

In `wifi_auto_connect_task()`, replace the direct manager connect block with a coordinator request shape:

```c
    ink_wifi_coordinator_request_t request = {
        .type = INK_WIFI_COORDINATOR_REQUEST_CONNECT_SAVED,
        .owner = INK_WIFI_COORDINATOR_OWNER_BOOT_AUTO_CONNECT,
        .keep_alive = false,
        .best_effort_saved = true,
        .timeout_ms = 12000,
    };
```

Leave the old direct code in place temporarily so the diff is easy to compare.

- [ ] **Step 2: Remove direct manager calls and route through the coordinator**

Replace:

```c
    if (ink_wifi_manager_connect_best(12000, &status) != ESP_OK) {
```

with:

```c
    ink_wifi_coordinator_result_t result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    if (ink_wifi_coordinator_request(&request, &result) != ESP_OK
        || result != INK_WIFI_COORDINATOR_RESULT_OK) {
```

and refresh cached status afterward:

```c
    (void)ink_wifi_coordinator_get_status(&services->wifi_coordinator_status);
```

- [ ] **Step 3: Stop polling raw manager status in the auto-connect task**

Replace:

```c
    if (ink_wifi_manager_status(&status) == ESP_OK && status.connected) {
```

with:

```c
    ink_wifi_coordinator_status_t coordinator_status = {0};
    if (ink_wifi_coordinator_get_status(&coordinator_status) == ESP_OK
        && coordinator_status.wifi_status.connected) {
```

- [ ] **Step 4: Re-run build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- auto-connect path no longer calls `ink_wifi_manager_connect_best()` directly

- [ ] **Step 5: Commit the boot migration**

Run:

```powershell
git add main/ink_system_services.c main/ink_system_services.h
git commit -m "refactor: move boot wifi auto connect to coordinator"
```

Expected:
- commit succeeds

### Task 5: Migrate time sync to coordinator-managed leases

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\ink_time_service.h`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_time_service.c`

- [ ] **Step 1: Add a failing compile-time dependency on the coordinator**

At the top of `D:\FUCKIDF\ink-reader\main\ink_time_service.c`, replace:

```c
#include "ink_wifi_manager.h"
```

with:

```c
#include "ink_wifi_coordinator.h"
```

before adjusting the logic below.

- [ ] **Step 2: Update the sync decision helper signature**

Change:

```c
static bool time_service_should_sync(
    const ink_time_service_t *service,
    bool wifi_connected,
    uint32_t now_ms);
```

to:

```c
static bool time_service_should_sync(
    const ink_time_service_t *service,
    uint32_t now_ms);
```

and remove the raw Wi-Fi boolean gate from that helper.

- [ ] **Step 3: Add a connectivity helper that requests a short lease**

In `ink_time_service.c`, add:

```c
static bool time_service_ensure_wifi(void)
{
    ink_wifi_coordinator_request_t request = {
        .type = INK_WIFI_COORDINATOR_REQUEST_ENSURE_CONNECTED,
        .owner = INK_WIFI_COORDINATOR_OWNER_TIME_SYNC,
        .keep_alive = true,
        .best_effort_saved = true,
        .timeout_ms = 12000,
    };
    ink_wifi_coordinator_result_t result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;

    return ink_wifi_coordinator_request(&request, &result) == ESP_OK
        && result == INK_WIFI_COORDINATOR_RESULT_OK;
}
```

- [ ] **Step 4: Wrap sync attempts with acquire/release**

In the task loop, replace:

```c
        ink_wifi_status_t wifi_status = {0};
        const bool wifi_connected =
            ink_wifi_manager_status(&wifi_status) == ESP_OK && wifi_status.connected;
```

with:

```c
        const bool should_sync = time_service_should_sync(service, now_ms);
```

and then:

```c
        if (should_sync && time_service_ensure_wifi()) {
            time_service_try_sync(service);
            (void)ink_wifi_coordinator_release_owner(
                INK_WIFI_COORDINATOR_OWNER_TIME_SYNC,
                3000);
            time_service_refresh_display(service);
        }
```

- [ ] **Step 5: Re-run build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- time sync no longer directly reads `ink_wifi_manager_status()`

- [ ] **Step 6: Commit the time-sync migration**

Run:

```powershell
git add main/ink_time_service.h main/ink_time_service.c
git commit -m "refactor: move time sync wifi usage to coordinator"
```

Expected:
- commit succeeds

### Task 6: Migrate Wi-Fi setup worker operations to the coordinator

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\main\apps\ink_wifi_setup_app.c`
- Modify: `D:\FUCKIDF\ink-reader\main\apps\ink_wifi_setup_app.h`

- [ ] **Step 1: Swap the worker dependency include**

At the top of `D:\FUCKIDF\ink-reader\main\apps\ink_wifi_setup_app.c`, replace:

```c
#include "ink_wifi_manager.h"
```

with:

```c
#include "ink_wifi_coordinator.h"
```

- [ ] **Step 2: Add small request builders inside the worker**

Near `wifi_setup_worker_task()`, add helpers:

```c
static void fill_wifi_setup_scan_request(ink_wifi_coordinator_request_t *request)
{
    memset(request, 0, sizeof(*request));
    request->type = INK_WIFI_COORDINATOR_REQUEST_SCAN;
    request->owner = INK_WIFI_COORDINATOR_OWNER_WIFI_SETUP;
    request->timeout_ms = 10000;
}
```

and:

```c
static void fill_wifi_setup_connect_request(
    ink_wifi_coordinator_request_t *request,
    ink_wifi_setup_request_type_t type,
    const ink_wifi_setup_app_work_item_t *item)
{
    memset(request, 0, sizeof(*request));
    request->owner = INK_WIFI_COORDINATOR_OWNER_WIFI_SETUP;
    request->keep_alive = true;
    request->timeout_ms = 10000;
    snprintf(request->ssid, sizeof(request->ssid), "%s", item->request.ssid);
    snprintf(request->password, sizeof(request->password), "%s", item->request.password);
    request->type = type == INK_WIFI_SETUP_REQUEST_CONNECT_SAVED
        ? INK_WIFI_COORDINATOR_REQUEST_CONNECT_SAVED
        : INK_WIFI_COORDINATOR_REQUEST_CONNECT_PASSWORD;
}
```

- [ ] **Step 3: Replace direct scan/connect calls in the worker switch**

Replace:

```c
                result.result = ink_wifi_manager_scan(&result.scan);
                (void)ink_wifi_manager_status(&result.status);
```

with:

```c
                ink_wifi_coordinator_request_t request;
                ink_wifi_coordinator_result_t wifi_result;
                fill_wifi_setup_scan_request(&request);
                request.scan_out = &result.scan;
                request.status_out = &result.status;
                result.result = ink_wifi_coordinator_request(&request, &wifi_result);
                if (result.result == ESP_OK && wifi_result != INK_WIFI_COORDINATOR_RESULT_OK) {
                    result.result = ESP_FAIL;
                    result.status.last_error = ESP_FAIL;
                }
```

Do the same replacement pattern for:
- `CONNECT_PASSWORD`
- `CONNECT_SAVED`

Leave `DELETE_SAVED` on `ink_wifi_manager_delete_credential()` for V1, because it is storage-only and not a Wi-Fi radio action.

- [ ] **Step 4: Release the Wi-Fi setup lease after each completed operation**

At the end of each scan/connect case, add:

```c
                (void)ink_wifi_coordinator_release_owner(
                    INK_WIFI_COORDINATOR_OWNER_WIFI_SETUP,
                    3000);
```

Do not add it to `DELETE_SAVED`.

- [ ] **Step 5: Re-run build**

Run:

```powershell
idf.py build
```

Expected:
- build succeeds
- Wi-Fi setup worker no longer directly invokes manager scan/connect APIs

- [ ] **Step 6: Commit the Wi-Fi setup migration**

Run:

```powershell
git add main/apps/ink_wifi_setup_app.c main/apps/ink_wifi_setup_app.h
git commit -m "refactor: route wifi setup worker through coordinator"
```

Expected:
- commit succeeds

### Task 7: Add final verification and mainline regression checks

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\components\ink_net\ink_wifi_coordinator.c`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_system_services.c`
- Modify: `D:\FUCKIDF\ink-reader\main\ink_time_service.c`
- Modify: `D:\FUCKIDF\ink-reader\main\apps\ink_wifi_setup_app.c`

- [ ] **Step 1: Tighten self-tests around final behavior**

Extend `ink_wifi_coordinator_self_test()` to assert:
- active lease count returns to zero after paired release
- `ink_wifi_coordinator_get_status()` mirrors lease count
- `ink_wifi_coordinator_release_owner(OWNER_NONE, ...)` returns `ESP_ERR_INVALID_ARG`

Extend `ink_system_services_self_test()` with:

```c
        && !services.wifi_coordinator_ready
```

so service reset behavior is explicitly covered.

- [ ] **Step 2: Run the full build**

Run:

```powershell
idf.py build
```

Expected:
- full build succeeds
- no compile errors remain in `ink_net`, `main`, or Wi-Fi setup app paths

- [ ] **Step 3: Flash and monitor the board for boot auto-connect smoke test**

Run:

```powershell
idf.py -p COM9 flash monitor
```

Expected:
- boot completes
- no self-test abort
- saved-network auto-connect still works

- [ ] **Step 4: Manual validation for time sync and Wi-Fi setup**

Verify on hardware:
- Wi-Fi setup app can scan
- Wi-Fi setup app can connect to a saved network
- device remains stable after setup app exits
- after enough runtime, time sync still completes and the time badge becomes valid

- [ ] **Step 5: Commit any verification-driven fixes**

Run:

```powershell
git add components/ink_net/ink_wifi_coordinator.c main/ink_system_services.c main/ink_time_service.c main/apps/ink_wifi_setup_app.c
git commit -m "test: finalize wifi coordinator verification fixes"
```

Expected:
- commit succeeds only if verification required small follow-up edits

## Spec Coverage Check

- Single owner for Wi-Fi policy: covered by Tasks 1-3.
- Request queue and state machine: covered by Tasks 1-2.
- Lease model and idle disconnect policy: covered by Tasks 2 and 7.
- Boot auto-connect migration: covered by Task 4.
- Time sync migration: covered by Task 5.
- Wi-Fi setup migration: covered by Task 6.
- Keep voice-tag ASR out of this step: preserved by limiting migration targets to existing mainline consumers.

## Placeholder Scan

The plan intentionally leaves exact timeout tuning and any optional diagnostics UI out of scope. No task depends on undefined files or `TODO` placeholders.

## Type Consistency Check

- All request types use `ink_wifi_coordinator_request_t`.
- All ownership uses `ink_wifi_coordinator_owner_t`.
- All result mapping uses `ink_wifi_coordinator_result_t`.
- Existing `ink_wifi_status_t` and `ink_wifi_scan_list_t` stay the shared data carriers between manager, coordinator, and UI.

Plan complete and saved to `docs/superpowers/plans/2026-06-29-mainline-wifi-coordinator.md`. Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
