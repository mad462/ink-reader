#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

typedef struct {
    portMUX_TYPE lock;
    TaskHandle_t worker_task;
    uint32_t boot_ms;
    uint32_t last_sync_attempt_ms;
    uint32_t last_sync_success_ms;
    bool time_valid;
    bool sync_in_progress;
    char display_text[24];
} ink_time_service_t;

void ink_time_service_reset(ink_time_service_t *service);
esp_err_t ink_time_service_start(ink_time_service_t *service);
void ink_time_service_get_display_text(
    const ink_time_service_t *service,
    char *dst,
    size_t dst_size);
bool ink_time_service_self_test(void);
