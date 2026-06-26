#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ink_cpfont.h"
#include "ink_display_mailbox.h"

typedef struct ink_system_services {
    QueueHandle_t ui_queue;
    ink_display_mailbox_t mailbox;
    uint8_t *framebuffer;
    uint8_t *previous_framebuffer;
    uint8_t *bitmap_snapshot_a;
    uint8_t *bitmap_snapshot_b;
    uint8_t *native_snapshot_a;
    uint8_t *native_snapshot_b;
    ink_cpfont_t menu_font;
    ink_cpfont_t footer_font;
    ink_cpfont_t reader_font;
    bool tf_ready;
} ink_system_services_t;

void ink_system_services_reset(ink_system_services_t *services);
esp_err_t ink_system_services_init_mailbox_only(ink_system_services_t *services);
esp_err_t ink_system_services_init(ink_system_services_t *services);
bool ink_system_services_self_test(void);
