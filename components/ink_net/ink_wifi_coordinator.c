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

static uint32_t coordinator_active_leases(void)
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
    if (owner == INK_WIFI_COORDINATOR_OWNER_NONE) {
        return false;
    }

    for (size_t i = 0; i < INK_WIFI_COORDINATOR_MAX_LEASES; ++i) {
        if (s_leases[i].in_use && s_leases[i].owner == owner) {
            return true;
        }
    }

    for (size_t i = 0; i < INK_WIFI_COORDINATOR_MAX_LEASES; ++i) {
        if (!s_leases[i].in_use) {
            s_leases[i].in_use = true;
            s_leases[i].owner = owner;
            return true;
        }
    }

    return false;
}

static void coordinator_remove_lease(ink_wifi_coordinator_owner_t owner)
{
    if (owner == INK_WIFI_COORDINATOR_OWNER_NONE) {
        return;
    }

    for (size_t i = 0; i < INK_WIFI_COORDINATOR_MAX_LEASES; ++i) {
        if (s_leases[i].in_use && s_leases[i].owner == owner) {
            s_leases[i].in_use = false;
            s_leases[i].owner = INK_WIFI_COORDINATOR_OWNER_NONE;
        }
    }
}

static void coordinator_refresh_status_counts(void)
{
    s_status.active_leases = coordinator_active_leases();
}

static void coordinator_worker_task(void *arg)
{
    ink_wifi_coordinator_work_item_t *item = NULL;

    (void)arg;

    for (;;) {
        if (xQueueReceive(s_request_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (item == NULL) {
            continue;
        }

        s_status.request_in_progress = true;
        item->error = ESP_OK;
        item->result = INK_WIFI_COORDINATOR_RESULT_OK;

        switch (item->request.type) {
        case INK_WIFI_COORDINATOR_REQUEST_RELEASE_LEASE:
            coordinator_remove_lease(item->request.owner);
            coordinator_refresh_status_counts();
            if (s_status.active_leases == 0U) {
                s_status.state = INK_WIFI_COORDINATOR_STATE_OFF;
            }
            break;

        case INK_WIFI_COORDINATOR_REQUEST_DISCONNECT_IF_IDLE:
            coordinator_refresh_status_counts();
            if (s_status.active_leases == 0U) {
                memset(&s_status.wifi_status, 0, sizeof(s_status.wifi_status));
                s_status.state = INK_WIFI_COORDINATOR_STATE_OFF;
            } else {
                item->result = INK_WIFI_COORDINATOR_RESULT_BUSY_RETRYABLE;
            }
            break;

        case INK_WIFI_COORDINATOR_REQUEST_ENSURE_CONNECTED:
        case INK_WIFI_COORDINATOR_REQUEST_SCAN:
        case INK_WIFI_COORDINATOR_REQUEST_CONNECT_SAVED:
        case INK_WIFI_COORDINATOR_REQUEST_CONNECT_PASSWORD:
        default:
            item->result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
            break;
        }

        s_status.last_result = item->result;
        s_status.request_in_progress = false;

        if (item->reply_task != NULL) {
            xTaskNotifyGive(item->reply_task);
        }
    }
}

esp_err_t ink_wifi_coordinator_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    memset(s_leases, 0, sizeof(s_leases));
    s_status.state = INK_WIFI_COORDINATOR_STATE_OFF;
    s_status.last_result = INK_WIFI_COORDINATOR_RESULT_OK;
    s_status.idle_timeout_ms = INK_WIFI_COORDINATOR_DEFAULT_IDLE_MS;
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
            sizeof(ink_wifi_coordinator_work_item_t *));
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

    s_status.state = INK_WIFI_COORDINATOR_STATE_STARTING;
    return ESP_OK;
}

esp_err_t ink_wifi_coordinator_request(
    const ink_wifi_coordinator_request_t *request,
    ink_wifi_coordinator_result_t *out_result)
{
    TickType_t wait_ticks;
    ink_wifi_coordinator_work_item_t work_item;
    ink_wifi_coordinator_work_item_t *work_ptr = &work_item;

    ESP_RETURN_ON_FALSE(request != NULL, ESP_ERR_INVALID_ARG, TAG, "request required");
    ESP_RETURN_ON_FALSE(out_result != NULL, ESP_ERR_INVALID_ARG, TAG, "result required");
    ESP_RETURN_ON_FALSE(s_request_queue != NULL && s_worker_task != NULL, ESP_ERR_INVALID_STATE, TAG, "not started");

    memset(&work_item, 0, sizeof(work_item));
    work_item.request = *request;
    work_item.reply_task = xTaskGetCurrentTaskHandle();
    work_item.result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    work_item.error = ESP_OK;

    wait_ticks = (request->timeout_ms == 0U)
        ? portMAX_DELAY
        : pdMS_TO_TICKS(request->timeout_ms);

    xTaskNotifyStateClear(NULL);
    ESP_RETURN_ON_FALSE(
        xQueueSend(s_request_queue, &work_ptr, wait_ticks) == pdTRUE,
        ESP_ERR_TIMEOUT,
        TAG,
        "queue send timeout");
    ESP_RETURN_ON_FALSE(
        ulTaskNotifyTake(pdTRUE, wait_ticks) > 0U,
        ESP_ERR_TIMEOUT,
        TAG,
        "request timeout");

    *out_result = work_item.result;
    return work_item.error;
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
    esp_err_t err = ink_wifi_coordinator_request(&request, &result);
    if (err == ESP_OK) {
        s_status.last_result = result;
    }
    return err;
}

esp_err_t ink_wifi_coordinator_disconnect_if_idle(uint32_t timeout_ms)
{
    ink_wifi_coordinator_request_t request = {
        .type = INK_WIFI_COORDINATOR_REQUEST_DISCONNECT_IF_IDLE,
        .timeout_ms = timeout_ms,
    };
    ink_wifi_coordinator_result_t result = INK_WIFI_COORDINATOR_RESULT_INVALID_STATE;
    esp_err_t err = ink_wifi_coordinator_request(&request, &result);
    if (err == ESP_OK) {
        s_status.last_result = result;
    }
    return err;
}

esp_err_t ink_wifi_coordinator_get_status(ink_wifi_coordinator_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status != NULL, ESP_ERR_INVALID_ARG, TAG, "status required");
    coordinator_refresh_status_counts();
    *out_status = s_status;
    return ESP_OK;
}

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
