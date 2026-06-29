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
    INK_WIFI_COORDINATOR_INVALID_SLOT = 0xff,
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
    ink_wifi_coordinator_result_t result;
    esp_err_t error;
    SemaphoreHandle_t done_sem;
} ink_wifi_coordinator_request_slot_t;

static QueueHandle_t s_request_queue;
static TaskHandle_t s_worker_task;
static SemaphoreHandle_t s_state_lock;
static ink_wifi_coordinator_status_t s_status;
static ink_wifi_coordinator_lease_slot_t s_leases[INK_WIFI_COORDINATOR_MAX_LEASES];
static ink_wifi_coordinator_request_slot_t s_request_slots[INK_WIFI_COORDINATOR_QUEUE_LEN];

static esp_err_t coordinator_ensure_runtime_primitives(void);
static void coordinator_lock(void);
static void coordinator_unlock(void);
static uint32_t coordinator_active_leases(void);
static bool coordinator_add_lease(ink_wifi_coordinator_owner_t owner);
static void coordinator_remove_lease(ink_wifi_coordinator_owner_t owner);
static void coordinator_refresh_status_counts(void);
static int coordinator_acquire_request_slot(void);
static void coordinator_release_request_slot(int slot_index);
static void coordinator_worker_task(void *arg);

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

static uint32_t coordinator_active_leases(void)
{
    uint32_t count = 0;

    coordinator_lock();
    for (size_t i = 0; i < INK_WIFI_COORDINATOR_MAX_LEASES; ++i) {
        if (s_leases[i].in_use) {
            ++count;
        }
    }
    coordinator_unlock();

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
    s_status.active_leases = coordinator_active_leases();
    coordinator_unlock();
}

static int coordinator_acquire_request_slot(void)
{
    int slot_index = -1;

    coordinator_lock();
    for (size_t i = 0; i < INK_WIFI_COORDINATOR_QUEUE_LEN; ++i) {
        if (!s_request_slots[i].in_use) {
            while (xSemaphoreTake(s_request_slots[i].done_sem, 0) == pdTRUE) {
            }
            s_request_slots[i].in_use = true;
            s_request_slots[i].caller_waiting = true;
            s_request_slots[i].completed = false;
            s_request_slots[i].result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
            s_request_slots[i].error = ESP_OK;
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
    s_request_slots[slot_index].in_use = false;
    s_request_slots[slot_index].caller_waiting = false;
    s_request_slots[slot_index].completed = false;
    s_request_slots[slot_index].result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    s_request_slots[slot_index].error = ESP_OK;
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
        case INK_WIFI_COORDINATOR_REQUEST_RELEASE_LEASE:
            coordinator_remove_lease(item.request.owner);
            coordinator_refresh_status_counts();
            coordinator_lock();
            if (s_status.active_leases == 0U) {
                s_status.state = INK_WIFI_COORDINATOR_STATE_OFF;
            }
            coordinator_unlock();
            break;

        case INK_WIFI_COORDINATOR_REQUEST_DISCONNECT_IF_IDLE:
            coordinator_refresh_status_counts();
            coordinator_lock();
            if (s_status.active_leases == 0U) {
                memset(&s_status.wifi_status, 0, sizeof(s_status.wifi_status));
                s_status.state = INK_WIFI_COORDINATOR_STATE_OFF;
            } else {
                item.result = INK_WIFI_COORDINATOR_RESULT_BUSY_RETRYABLE;
            }
            coordinator_unlock();
            break;

        case INK_WIFI_COORDINATOR_REQUEST_ENSURE_CONNECTED:
        case INK_WIFI_COORDINATOR_REQUEST_SCAN:
        case INK_WIFI_COORDINATOR_REQUEST_CONNECT_SAVED:
        case INK_WIFI_COORDINATOR_REQUEST_CONNECT_PASSWORD:
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
        s_request_slots[i].in_use = false;
        s_request_slots[i].caller_waiting = false;
        s_request_slots[i].completed = false;
        s_request_slots[i].result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
        s_request_slots[i].error = ESP_OK;
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

    if (ink_wifi_coordinator_init() != ESP_OK) {
        return false;
    }
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
