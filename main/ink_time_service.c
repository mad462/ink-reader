#include "ink_time_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#include "ink_wifi_coordinator.h"

static const char *TAG = "ink_time";

enum {
    INK_TIME_SERVICE_STACK_BYTES = 4096,
    INK_TIME_SERVICE_POLL_MS = 5000,
    INK_TIME_SERVICE_SYNC_INTERVAL_MS = 60U * 60U * 1000U,
    INK_TIME_SERVICE_SYNC_WAIT_MS = 10000,
    INK_TIME_SERVICE_SYNC_WAIT_STEP_MS = 500,
};

static const time_t kMinimumValidEpoch = 1735689600;

static uint32_t time_service_now_ms(void);
static bool time_service_is_clock_valid(time_t now);
static void time_service_refresh_display(ink_time_service_t *service);
static bool time_service_should_sync(
    const ink_time_service_t *service,
    uint32_t now_ms);
static bool time_service_ensure_wifi(void);
static void time_service_try_sync(ink_time_service_t *service);
static void time_service_task(void *arg);

static uint32_t time_service_now_ms(void)
{
    return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}

static bool time_service_is_clock_valid(time_t now)
{
    return now >= kMinimumValidEpoch;
}

static void time_service_refresh_display(ink_time_service_t *service)
{
    const uint32_t now_ms = time_service_now_ms();
    const time_t now = time(NULL);
    const bool scheduler_running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    const bool clock_valid = scheduler_running && time_service_is_clock_valid(now);
    const uint32_t uptime_ms = now_ms - service->boot_ms;
    char display_text[24];

    if (clock_valid) {
        struct tm local_tm = {0};
        localtime_r(&now, &local_tm);
        snprintf(
            display_text,
            sizeof(display_text),
            "%02d:%02d",
            local_tm.tm_hour,
            local_tm.tm_min);
    } else {
        const uint32_t uptime_minutes = uptime_ms / 60000U;
        const uint32_t hours = uptime_minutes / 60U;
        const uint32_t minutes = uptime_minutes % 60U;
        snprintf(
            display_text,
            sizeof(display_text),
            "UP %02u:%02u",
            (unsigned)(hours % 100U),
            (unsigned)minutes);
    }

    taskENTER_CRITICAL(&service->lock);
    service->time_valid = clock_valid;
    snprintf(service->display_text, sizeof(service->display_text), "%s", display_text);
    taskEXIT_CRITICAL(&service->lock);
}

static bool time_service_should_sync(
    const ink_time_service_t *service,
    uint32_t now_ms)
{
    if (service == NULL || service->sync_in_progress) {
        return false;
    }

    if (!service->time_valid) {
        return true;
    }

    return (now_ms - service->last_sync_success_ms) >= INK_TIME_SERVICE_SYNC_INTERVAL_MS;
}

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

static void time_service_try_sync(ink_time_service_t *service)
{
    bool success = false;
    uint32_t waited_ms = 0U;

    if (service == NULL) {
        return;
    }

    taskENTER_CRITICAL(&service->lock);
    service->sync_in_progress = true;
    service->last_sync_attempt_ms = time_service_now_ms();
    taskEXIT_CRITICAL(&service->lock);

    setenv("TZ", "CST-8", 1);
    tzset();

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_init();

    while (waited_ms < INK_TIME_SERVICE_SYNC_WAIT_MS) {
        const time_t now = time(NULL);
        if (time_service_is_clock_valid(now)) {
            success = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(INK_TIME_SERVICE_SYNC_WAIT_STEP_MS));
        waited_ms += INK_TIME_SERVICE_SYNC_WAIT_STEP_MS;
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    taskENTER_CRITICAL(&service->lock);
    service->sync_in_progress = false;
    if (success) {
        service->last_sync_success_ms = time_service_now_ms();
        service->time_valid = true;
    }
    taskEXIT_CRITICAL(&service->lock);

    if (success) {
        ESP_LOGI(TAG, "network time synchronized");
    } else {
        ESP_LOGW(TAG, "network time sync timed out");
    }
}

static void time_service_task(void *arg)
{
    ink_time_service_t *service = (ink_time_service_t *)arg;

    if (service == NULL) {
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        const uint32_t now_ms = time_service_now_ms();
        const bool should_sync = time_service_should_sync(service, now_ms);

        time_service_refresh_display(service);
        if (should_sync && time_service_ensure_wifi()) {
            time_service_try_sync(service);
            (void)ink_wifi_coordinator_release_owner(
                INK_WIFI_COORDINATOR_OWNER_TIME_SYNC,
                3000);
            time_service_refresh_display(service);
        }
        vTaskDelay(pdMS_TO_TICKS(INK_TIME_SERVICE_POLL_MS));
    }
}

void ink_time_service_reset(ink_time_service_t *service)
{
    if (service == NULL) {
        return;
    }

    memset(service, 0, sizeof(*service));
    service->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    snprintf(service->display_text, sizeof(service->display_text), "%s", "UP 00:00");
}

esp_err_t ink_time_service_start(ink_time_service_t *service)
{
    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (service->worker_task != NULL) {
        return ESP_OK;
    }

    service->boot_ms = time_service_now_ms();
    time_service_refresh_display(service);
    if (xTaskCreate(
            time_service_task,
            "InkTimeSvc",
            INK_TIME_SERVICE_STACK_BYTES,
            service,
            2,
            &service->worker_task) != pdPASS) {
        service->worker_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ink_time_service_get_display_text(
    const ink_time_service_t *service,
    char *dst,
    size_t dst_size)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    dst[0] = '\0';
    if (service == NULL) {
        snprintf(dst, dst_size, "%s", "UP 00:00");
        return;
    }

    taskENTER_CRITICAL((portMUX_TYPE *)&service->lock);
    snprintf(dst, dst_size, "%s", service->display_text);
    taskEXIT_CRITICAL((portMUX_TYPE *)&service->lock);
}

bool ink_time_service_self_test(void)
{
    ink_time_service_t service;
    char text[24];

    memset(&service, 0xA5, sizeof(service));
    ink_time_service_reset(&service);
    ink_time_service_get_display_text(&service, text, sizeof(text));
    if (strcmp(text, "UP 00:00") != 0) {
        return false;
    }

    snprintf(service.display_text, sizeof(service.display_text), "%s", "UP 01:23");
    ink_time_service_get_display_text(&service, text, sizeof(text));
    return strcmp(text, "UP 01:23") == 0;
}
