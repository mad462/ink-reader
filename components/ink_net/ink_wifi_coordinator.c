#include "ink_wifi_coordinator.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
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
    uint8_t request_slot;
    ink_wifi_coordinator_result_t result;
    esp_err_t error;
} ink_wifi_coordinator_work_item_t;

typedef struct {
    bool in_use;
    bool caller_waiting;
    bool completed;
    bool has_status_out;
    bool has_scan_out;
    ink_wifi_coordinator_result_t result;
    esp_err_t error;
    ink_wifi_status_t status_out;
    ink_wifi_scan_list_t scan_out;
    SemaphoreHandle_t done_sem;
} ink_wifi_coordinator_request_slot_t;

typedef esp_err_t (*ink_wifi_coordinator_connect_saved_fn_t)(
    const char *ssid,
    uint32_t timeout_ms,
    ink_wifi_status_t *out_status);
typedef esp_err_t (*ink_wifi_coordinator_connect_best_fn_t)(
    uint32_t timeout_ms,
    ink_wifi_status_t *out_status);

static QueueHandle_t s_request_queue;
static TaskHandle_t s_worker_task;
static SemaphoreHandle_t s_state_lock;
static ink_wifi_coordinator_status_t s_status;
static ink_wifi_coordinator_lease_slot_t s_leases[INK_WIFI_COORDINATOR_MAX_LEASES];
static ink_wifi_coordinator_request_slot_t s_request_slots[INK_WIFI_COORDINATOR_QUEUE_LEN];
static ink_wifi_coordinator_connect_saved_fn_t s_connect_saved_fn = ink_wifi_manager_connect_saved;
static ink_wifi_coordinator_connect_best_fn_t s_connect_best_fn = ink_wifi_manager_connect_best;

static esp_err_t coordinator_ensure_runtime_primitives(void);
static void coordinator_lock(void);
static void coordinator_unlock(void);
static bool coordinator_add_lease(ink_wifi_coordinator_owner_t owner);
static void coordinator_remove_lease(ink_wifi_coordinator_owner_t owner);
static void coordinator_refresh_status_counts(void);
static uint32_t coordinator_active_leases_locked(void);
static ink_wifi_coordinator_result_t coordinator_result_from_connect_error(esp_err_t err);
static void coordinator_reset_request_slot_locked(int slot_index);
static void coordinator_store_request_status(int slot_index, const ink_wifi_status_t *status);
static void coordinator_store_request_scan(int slot_index, const ink_wifi_scan_list_t *scan);
static void coordinator_copy_request_outputs(
    int slot_index,
    const ink_wifi_coordinator_request_t *request);
static void coordinator_store_wifi_status(
    const ink_wifi_status_t *wifi_status,
    ink_wifi_coordinator_result_t result);
static ink_wifi_coordinator_result_t coordinator_run_connect_saved(
    const ink_wifi_coordinator_request_t *request,
    int slot_index,
    esp_err_t *out_error);
static ink_wifi_coordinator_result_t coordinator_run_connect_password(
    const ink_wifi_coordinator_request_t *request,
    int slot_index,
    esp_err_t *out_error);
static ink_wifi_coordinator_result_t coordinator_run_ensure_connected(
    const ink_wifi_coordinator_request_t *request,
    int slot_index,
    esp_err_t *out_error);
static ink_wifi_coordinator_result_t coordinator_complete_keep_alive(
    const ink_wifi_coordinator_request_t *request,
    ink_wifi_coordinator_result_t base_result);
static ink_wifi_coordinator_result_t coordinator_run_scan(
    const ink_wifi_coordinator_request_t *request,
    int slot_index,
    esp_err_t *out_error);
static void coordinator_maybe_disconnect_idle(void);
static int coordinator_acquire_request_slot(void);
static void coordinator_release_request_slot(int slot_index);
static void coordinator_worker_task(void *arg);
static esp_err_t coordinator_self_test_connect_best_not_found(
    uint32_t timeout_ms,
    ink_wifi_status_t *out_status);

static esp_err_t coordinator_ensure_runtime_primitives(void)
{
    if (s_state_lock == NULL) {
        s_state_lock = xSemaphoreCreateRecursiveMutex();
        ESP_RETURN_ON_FALSE(s_state_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock alloc failed");
    }

    for (size_t i = 0; i < INK_WIFI_COORDINATOR_QUEUE_LEN; ++i) {
        if (s_request_slots[i].done_sem == NULL) {
            s_request_slots[i].done_sem = xSemaphoreCreateBinary();
            ESP_RETURN_ON_FALSE(s_request_slots[i].done_sem != NULL, ESP_ERR_NO_MEM, TAG, "slot sem alloc failed");
        }
    }

    return ESP_OK;
}

static esp_err_t coordinator_self_test_connect_best_not_found(
    uint32_t timeout_ms,
    ink_wifi_status_t *out_status)
{
    (void)timeout_ms;

    if (out_status != NULL) {
        memset(out_status, 0, sizeof(*out_status));
        out_status->last_error = ESP_ERR_NOT_FOUND;
    }

    return ESP_ERR_NOT_FOUND;
}

static void coordinator_lock(void)
{
    configASSERT(s_state_lock != NULL);
    xSemaphoreTakeRecursive(s_state_lock, portMAX_DELAY);
}

static void coordinator_unlock(void)
{
    configASSERT(s_state_lock != NULL);
    xSemaphoreGiveRecursive(s_state_lock);
}

static uint32_t coordinator_active_leases_locked(void)
{
    uint32_t count = 0;

    for (size_t i = 0; i < INK_WIFI_COORDINATOR_MAX_LEASES; ++i) {
        if (s_leases[i].in_use) {
            ++count;
        }
    }

    return count;
}

static bool coordinator_add_lease(ink_wifi_coordinator_owner_t owner)
{
    bool ok = false;

    if (owner == INK_WIFI_COORDINATOR_OWNER_NONE) {
        return false;
    }

    coordinator_lock();
    for (size_t i = 0; i < INK_WIFI_COORDINATOR_MAX_LEASES; ++i) {
        if (s_leases[i].in_use && s_leases[i].owner == owner) {
            ok = true;
            goto done;
        }
    }

    for (size_t i = 0; i < INK_WIFI_COORDINATOR_MAX_LEASES; ++i) {
        if (!s_leases[i].in_use) {
            s_leases[i].in_use = true;
            s_leases[i].owner = owner;
            ok = true;
            goto done;
        }
    }

done:
    coordinator_unlock();
    return ok;
}

static void coordinator_remove_lease(ink_wifi_coordinator_owner_t owner)
{
    if (owner == INK_WIFI_COORDINATOR_OWNER_NONE) {
        return;
    }

    coordinator_lock();
    for (size_t i = 0; i < INK_WIFI_COORDINATOR_MAX_LEASES; ++i) {
        if (s_leases[i].in_use && s_leases[i].owner == owner) {
            s_leases[i].in_use = false;
            s_leases[i].owner = INK_WIFI_COORDINATOR_OWNER_NONE;
        }
    }
    coordinator_unlock();
}

static void coordinator_refresh_status_counts(void)
{
    coordinator_lock();
    s_status.active_leases = coordinator_active_leases_locked();
    coordinator_unlock();
}

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

static void coordinator_reset_request_slot_locked(int slot_index)
{
    memset(&s_request_slots[slot_index].status_out, 0, sizeof(s_request_slots[slot_index].status_out));
    memset(&s_request_slots[slot_index].scan_out, 0, sizeof(s_request_slots[slot_index].scan_out));
    s_request_slots[slot_index].in_use = false;
    s_request_slots[slot_index].caller_waiting = false;
    s_request_slots[slot_index].completed = false;
    s_request_slots[slot_index].has_status_out = false;
    s_request_slots[slot_index].has_scan_out = false;
    s_request_slots[slot_index].result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    s_request_slots[slot_index].error = ESP_OK;
}

static void coordinator_store_request_status(int slot_index, const ink_wifi_status_t *status)
{
    if (slot_index < 0 || slot_index >= INK_WIFI_COORDINATOR_QUEUE_LEN || status == NULL) {
        return;
    }

    coordinator_lock();
    if (s_request_slots[slot_index].in_use) {
        s_request_slots[slot_index].status_out = *status;
        s_request_slots[slot_index].has_status_out = true;
    }
    coordinator_unlock();
}

static void coordinator_store_request_scan(int slot_index, const ink_wifi_scan_list_t *scan)
{
    if (slot_index < 0 || slot_index >= INK_WIFI_COORDINATOR_QUEUE_LEN || scan == NULL) {
        return;
    }

    coordinator_lock();
    if (s_request_slots[slot_index].in_use) {
        s_request_slots[slot_index].scan_out = *scan;
        s_request_slots[slot_index].has_scan_out = true;
    }
    coordinator_unlock();
}

static void coordinator_copy_request_outputs(
    int slot_index,
    const ink_wifi_coordinator_request_t *request)
{
    bool has_status_out = false;
    bool has_scan_out = false;
    ink_wifi_status_t status_out = {0};
    ink_wifi_scan_list_t scan_out = {0};

    if (slot_index < 0 || slot_index >= INK_WIFI_COORDINATOR_QUEUE_LEN || request == NULL) {
        return;
    }

    coordinator_lock();
    has_status_out = s_request_slots[slot_index].has_status_out;
    has_scan_out = s_request_slots[slot_index].has_scan_out;
    if (has_status_out) {
        status_out = s_request_slots[slot_index].status_out;
    }
    if (has_scan_out) {
        scan_out = s_request_slots[slot_index].scan_out;
    }
    coordinator_unlock();

    if (has_status_out && request->status_out != NULL) {
        *request->status_out = status_out;
    }
    if (has_scan_out && request->scan_out != NULL) {
        *request->scan_out = scan_out;
    }
}

static void coordinator_store_wifi_status(
    const ink_wifi_status_t *wifi_status,
    ink_wifi_coordinator_result_t result)
{
    coordinator_lock();
    if (wifi_status != NULL) {
        s_status.wifi_status = *wifi_status;
        s_status.state = wifi_status->connected
            ? INK_WIFI_COORDINATOR_STATE_ONLINE
            : INK_WIFI_COORDINATOR_STATE_DISCONNECTED;
    } else if (result != INK_WIFI_COORDINATOR_RESULT_OK) {
        s_status.state = INK_WIFI_COORDINATOR_STATE_ERROR;
    }
    s_status.last_result = result;
    coordinator_unlock();
}

static ink_wifi_coordinator_result_t coordinator_run_connect_saved(
    const ink_wifi_coordinator_request_t *request,
    int slot_index,
    esp_err_t *out_error)
{
    esp_err_t err;
    ink_wifi_status_t status = {0};

    if (request->best_effort_saved) {
        err = s_connect_best_fn(request->timeout_ms, &status);
    } else {
        err = s_connect_saved_fn(request->ssid, request->timeout_ms, &status);
    }

    coordinator_store_request_status(slot_index, &status);
    if (out_error != NULL) {
        *out_error = err;
    }

    coordinator_store_wifi_status(&status, coordinator_result_from_connect_error(err));
    return coordinator_result_from_connect_error(err);
}

static ink_wifi_coordinator_result_t coordinator_run_connect_password(
    const ink_wifi_coordinator_request_t *request,
    int slot_index,
    esp_err_t *out_error)
{
    ink_wifi_status_t status = {0};
    esp_err_t err = ink_wifi_manager_connect_password(
        request->ssid,
        request->password,
        request->timeout_ms,
        &status);

    coordinator_store_request_status(slot_index, &status);
    if (out_error != NULL) {
        *out_error = err;
    }

    coordinator_store_wifi_status(&status, coordinator_result_from_connect_error(err));
    return coordinator_result_from_connect_error(err);
}

static ink_wifi_coordinator_result_t coordinator_complete_keep_alive(
    const ink_wifi_coordinator_request_t *request,
    ink_wifi_coordinator_result_t base_result)
{
    if (base_result != INK_WIFI_COORDINATOR_RESULT_OK || !request->keep_alive) {
        return base_result;
    }
    if (request->owner == INK_WIFI_COORDINATOR_OWNER_NONE) {
        return INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    }
    if (!coordinator_add_lease(request->owner)) {
        return INK_WIFI_COORDINATOR_RESULT_BUSY_RETRYABLE;
    }

    coordinator_refresh_status_counts();
    return INK_WIFI_COORDINATOR_RESULT_OK;
}

static ink_wifi_coordinator_result_t coordinator_run_ensure_connected(
    const ink_wifi_coordinator_request_t *request,
    int slot_index,
    esp_err_t *out_error)
{
    ink_wifi_status_t status = {0};
    esp_err_t err = ink_wifi_manager_status(&status);

    if (err == ESP_OK && status.connected) {
        coordinator_store_request_status(slot_index, &status);
        if (out_error != NULL) {
            *out_error = ESP_OK;
        }
        coordinator_store_wifi_status(&status, INK_WIFI_COORDINATOR_RESULT_OK);
        return coordinator_complete_keep_alive(request, INK_WIFI_COORDINATOR_RESULT_OK);
    }

    coordinator_lock();
    s_status.state = INK_WIFI_COORDINATOR_STATE_CONNECTING;
    coordinator_unlock();
    err = s_connect_best_fn(request->timeout_ms, &status);
    coordinator_store_request_status(slot_index, &status);
    if (out_error != NULL) {
        *out_error = err;
    }

    ink_wifi_coordinator_result_t result = coordinator_result_from_connect_error(err);
    coordinator_store_wifi_status(&status, result);
    return coordinator_complete_keep_alive(request, result);
}

static ink_wifi_coordinator_result_t coordinator_run_scan(
    const ink_wifi_coordinator_request_t *request,
    int slot_index,
    esp_err_t *out_error)
{
    ink_wifi_scan_list_t scan = {0};
    esp_err_t err = ink_wifi_manager_scan(&scan);

    if (err == ESP_OK) {
        coordinator_store_request_scan(slot_index, &scan);
    }
    if (out_error != NULL) {
        *out_error = err;
    }
    return err == ESP_OK
        ? INK_WIFI_COORDINATOR_RESULT_OK
        : INK_WIFI_COORDINATOR_RESULT_SCAN_FAILED;
}

static void coordinator_maybe_disconnect_idle(void)
{
    coordinator_refresh_status_counts();
}

static int coordinator_acquire_request_slot(void)
{
    int slot_index = -1;

    coordinator_lock();
    for (size_t i = 0; i < INK_WIFI_COORDINATOR_QUEUE_LEN; ++i) {
        if (!s_request_slots[i].in_use) {
            while (xSemaphoreTake(s_request_slots[i].done_sem, 0) == pdTRUE) {
            }
            coordinator_reset_request_slot_locked((int)i);
            s_request_slots[i].in_use = true;
            s_request_slots[i].caller_waiting = true;
            slot_index = (int)i;
            break;
        }
    }
    coordinator_unlock();

    return slot_index;
}

static void coordinator_release_request_slot(int slot_index)
{
    if (slot_index < 0 || slot_index >= INK_WIFI_COORDINATOR_QUEUE_LEN) {
        return;
    }

    coordinator_lock();
    while (xSemaphoreTake(s_request_slots[slot_index].done_sem, 0) == pdTRUE) {
    }
    coordinator_reset_request_slot_locked(slot_index);
    coordinator_unlock();
}

static void coordinator_worker_task(void *arg)
{
    ink_wifi_coordinator_work_item_t item;

    (void)arg;

    for (;;) {
        if (xQueueReceive(s_request_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        coordinator_lock();
        s_status.request_in_progress = true;
        coordinator_unlock();

        item.error = ESP_OK;
        item.result = INK_WIFI_COORDINATOR_RESULT_OK;

        switch (item.request.type) {
        case INK_WIFI_COORDINATOR_REQUEST_ENSURE_CONNECTED:
            item.result = coordinator_run_ensure_connected(&item.request, (int)item.request_slot, &item.error);
            break;

        case INK_WIFI_COORDINATOR_REQUEST_SCAN:
            item.result = coordinator_run_scan(&item.request, (int)item.request_slot, &item.error);
            break;

        case INK_WIFI_COORDINATOR_REQUEST_CONNECT_SAVED:
            coordinator_lock();
            s_status.state = INK_WIFI_COORDINATOR_STATE_CONNECTING;
            coordinator_unlock();
            item.result = coordinator_run_connect_saved(&item.request, (int)item.request_slot, &item.error);
            item.result = coordinator_complete_keep_alive(&item.request, item.result);
            break;

        case INK_WIFI_COORDINATOR_REQUEST_CONNECT_PASSWORD:
            coordinator_lock();
            s_status.state = INK_WIFI_COORDINATOR_STATE_CONNECTING;
            coordinator_unlock();
            item.result = coordinator_run_connect_password(&item.request, (int)item.request_slot, &item.error);
            item.result = coordinator_complete_keep_alive(&item.request, item.result);
            break;

        case INK_WIFI_COORDINATOR_REQUEST_RELEASE_LEASE:
            coordinator_remove_lease(item.request.owner);
            coordinator_maybe_disconnect_idle();
            break;

        case INK_WIFI_COORDINATOR_REQUEST_DISCONNECT_IF_IDLE:
            coordinator_refresh_status_counts();
            coordinator_lock();
            if (s_status.active_leases == 0U) {
                item.result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
            } else {
                item.result = INK_WIFI_COORDINATOR_RESULT_BUSY_RETRYABLE;
            }
            coordinator_unlock();
            break;

        default:
            item.result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
            break;
        }

        coordinator_lock();
        s_status.last_result = item.result;
        s_status.request_in_progress = false;
        if (item.request_slot < INK_WIFI_COORDINATOR_QUEUE_LEN && s_request_slots[item.request_slot].in_use) {
            bool caller_waiting = s_request_slots[item.request_slot].caller_waiting;
            s_request_slots[item.request_slot].result = item.result;
            s_request_slots[item.request_slot].error = item.error;
            s_request_slots[item.request_slot].completed = true;
            coordinator_unlock();

            if (caller_waiting) {
                xSemaphoreGive(s_request_slots[item.request_slot].done_sem);
            } else {
                coordinator_release_request_slot((int)item.request_slot);
            }
        } else {
            coordinator_unlock();
        }
    }
}

esp_err_t ink_wifi_coordinator_init(void)
{
    ESP_RETURN_ON_ERROR(coordinator_ensure_runtime_primitives(), TAG, "primitive init failed");

    coordinator_lock();
    memset(&s_status, 0, sizeof(s_status));
    memset(s_leases, 0, sizeof(s_leases));
    for (size_t i = 0; i < INK_WIFI_COORDINATOR_QUEUE_LEN; ++i) {
        while (xSemaphoreTake(s_request_slots[i].done_sem, 0) == pdTRUE) {
        }
        coordinator_reset_request_slot_locked((int)i);
    }
    s_status.state = INK_WIFI_COORDINATOR_STATE_OFF;
    s_status.last_result = INK_WIFI_COORDINATOR_RESULT_OK;
    s_status.idle_timeout_ms = INK_WIFI_COORDINATOR_DEFAULT_IDLE_MS;
    coordinator_unlock();

    return ESP_OK;
}

esp_err_t ink_wifi_coordinator_start(void)
{
    if (s_request_queue != NULL && s_worker_task != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ink_wifi_coordinator_init(), TAG, "init failed");

    if (s_request_queue == NULL) {
        s_request_queue = xQueueCreate(
            INK_WIFI_COORDINATOR_QUEUE_LEN,
            sizeof(ink_wifi_coordinator_work_item_t));
        ESP_RETURN_ON_FALSE(s_request_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue alloc failed");
    }

    if (s_worker_task == NULL) {
        BaseType_t ok = xTaskCreate(
            coordinator_worker_task,
            "ink_wifi_coord",
            INK_WIFI_COORDINATOR_TASK_STACK,
            NULL,
            tskIDLE_PRIORITY + 1,
            &s_worker_task);
        ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "worker create failed");
    }

    coordinator_lock();
    s_status.state = INK_WIFI_COORDINATOR_STATE_STARTING;
    coordinator_unlock();

    return ESP_OK;
}

esp_err_t ink_wifi_coordinator_request(
    const ink_wifi_coordinator_request_t *request,
    ink_wifi_coordinator_result_t *out_result)
{
    TickType_t wait_ticks;
    ink_wifi_coordinator_work_item_t work_item;
    ink_wifi_coordinator_result_t result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    esp_err_t err = ESP_OK;
    int slot_index;
    bool completed = false;

    ESP_RETURN_ON_FALSE(request != NULL, ESP_ERR_INVALID_ARG, TAG, "request required");
    ESP_RETURN_ON_FALSE(out_result != NULL, ESP_ERR_INVALID_ARG, TAG, "result required");
    ESP_RETURN_ON_FALSE(s_request_queue != NULL && s_worker_task != NULL, ESP_ERR_INVALID_STATE, TAG, "not started");

    slot_index = coordinator_acquire_request_slot();
    ESP_RETURN_ON_FALSE(slot_index >= 0, ESP_ERR_NO_MEM, TAG, "no request slots");

    memset(&work_item, 0, sizeof(work_item));
    work_item.request = *request;
    work_item.request_slot = (uint8_t)slot_index;
    work_item.result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    work_item.error = ESP_OK;

    wait_ticks = (request->timeout_ms == 0U)
        ? portMAX_DELAY
        : pdMS_TO_TICKS(request->timeout_ms);

    if (xQueueSend(s_request_queue, &work_item, wait_ticks) != pdTRUE) {
        coordinator_release_request_slot(slot_index);
        ESP_LOGE(TAG, "queue send timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (xSemaphoreTake(s_request_slots[slot_index].done_sem, wait_ticks) == pdTRUE) {
        completed = true;
    }

    coordinator_lock();
    if (s_request_slots[slot_index].completed) {
        completed = true;
        result = s_request_slots[slot_index].result;
        err = s_request_slots[slot_index].error;
        coordinator_unlock();
        coordinator_copy_request_outputs(slot_index, request);
        coordinator_release_request_slot(slot_index);
        *out_result = result;
        return err;
    }

    if (!completed) {
        s_request_slots[slot_index].caller_waiting = false;
    }
    coordinator_unlock();

    if (!completed) {
        return ESP_ERR_TIMEOUT;
    }

    coordinator_lock();
    result = s_request_slots[slot_index].result;
    err = s_request_slots[slot_index].error;
    coordinator_unlock();
    coordinator_copy_request_outputs(slot_index, request);
    coordinator_release_request_slot(slot_index);
    *out_result = result;
    return err;
}

esp_err_t ink_wifi_coordinator_release_owner(
    ink_wifi_coordinator_owner_t owner,
    uint32_t timeout_ms)
{
    ink_wifi_coordinator_request_t request = {
        .type = INK_WIFI_COORDINATOR_REQUEST_RELEASE_LEASE,
        .owner = owner,
        .timeout_ms = timeout_ms,
    };
    ink_wifi_coordinator_result_t result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    return ink_wifi_coordinator_request(&request, &result);
}

esp_err_t ink_wifi_coordinator_disconnect_if_idle(uint32_t timeout_ms)
{
    ink_wifi_coordinator_request_t request = {
        .type = INK_WIFI_COORDINATOR_REQUEST_DISCONNECT_IF_IDLE,
        .timeout_ms = timeout_ms,
    };
    ink_wifi_coordinator_result_t result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    return ink_wifi_coordinator_request(&request, &result);
}

esp_err_t ink_wifi_coordinator_get_status(ink_wifi_coordinator_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status != NULL, ESP_ERR_INVALID_ARG, TAG, "status required");

    coordinator_lock();
    s_status.active_leases = 0;
    for (size_t i = 0; i < INK_WIFI_COORDINATOR_MAX_LEASES; ++i) {
        if (s_leases[i].in_use) {
            ++s_status.active_leases;
        }
    }
    *out_status = s_status;
    coordinator_unlock();

    return ESP_OK;
}

bool ink_wifi_coordinator_self_test(void)
{
    ink_wifi_coordinator_status_t status;
    ink_wifi_status_t copied_status = {0};
    ink_wifi_scan_list_t copied_scan = {0};
    ink_wifi_coordinator_request_t request = {
        .type = INK_WIFI_COORDINATOR_REQUEST_CONNECT_SAVED,
        .best_effort_saved = true,
        .timeout_ms = 1,
    };
    ink_wifi_coordinator_connect_best_fn_t saved_connect_best_fn = s_connect_best_fn;
    ink_wifi_coordinator_result_t best_effort_result;

    if (ink_wifi_coordinator_init() != ESP_OK) {
        return false;
    }
    s_connect_best_fn = coordinator_self_test_connect_best_not_found;
    best_effort_result = coordinator_run_connect_saved(&request, -1, NULL);
    s_connect_best_fn = saved_connect_best_fn;
    coordinator_lock();
    s_status.wifi_status.connected = true;
    s_status.state = INK_WIFI_COORDINATOR_STATE_ONLINE;
    s_status.active_leases = 0;
    coordinator_unlock();
    coordinator_maybe_disconnect_idle();
    if (!s_status.wifi_status.connected || s_status.state != INK_WIFI_COORDINATOR_STATE_ONLINE) {
        return false;
    }
    if (coordinator_acquire_request_slot() < 0) {
        return false;
    }
    coordinator_lock();
    s_request_slots[0].status_out.connected = true;
    s_request_slots[0].status_out.rssi = -42;
    strcpy(s_request_slots[0].status_out.ssid, "copy-test");
    s_request_slots[0].has_status_out = true;
    s_request_slots[0].scan_out.count = 1;
    strcpy(s_request_slots[0].scan_out.results[0].ssid, "scan-copy");
    s_request_slots[0].scan_out.results[0].rssi = -55;
    s_request_slots[0].has_scan_out = true;
    coordinator_unlock();
    request.status_out = &copied_status;
    request.scan_out = &copied_scan;
    coordinator_copy_request_outputs(0, &request);
    coordinator_release_request_slot(0);
    if (!copied_status.connected || strcmp(copied_status.ssid, "copy-test") != 0) {
        return false;
    }
    if (copied_scan.count != 1 || strcmp(copied_scan.results[0].ssid, "scan-copy") != 0) {
        return false;
    }
    if (coordinator_acquire_request_slot() != 0) {
        return false;
    }
    coordinator_release_request_slot(0);
    if (!coordinator_add_lease(INK_WIFI_COORDINATOR_OWNER_TIME_SYNC)) {
        return false;
    }
    if (!coordinator_add_lease(INK_WIFI_COORDINATOR_OWNER_TIME_SYNC)) {
        return false;
    }
    coordinator_refresh_status_counts();
    if (s_status.active_leases != 1U) {
        return false;
    }
    coordinator_remove_lease(INK_WIFI_COORDINATOR_OWNER_VOICE_TAG_ASR);
    coordinator_refresh_status_counts();
    if (s_status.active_leases != 1U) {
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
    if (best_effort_result != INK_WIFI_COORDINATOR_RESULT_NO_CREDENTIAL) {
        return false;
    }
    if (coordinator_result_from_connect_error(ESP_ERR_TIMEOUT) != INK_WIFI_COORDINATOR_RESULT_TIMEOUT) {
        return false;
    }
    return status.idle_timeout_ms == INK_WIFI_COORDINATOR_DEFAULT_IDLE_MS
        && status.state == INK_WIFI_COORDINATOR_STATE_OFF;
}
