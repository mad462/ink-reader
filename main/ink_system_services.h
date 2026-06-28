#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/portmacro.h"

#include "ink_button_input.h"
#include "ink_cpfont.h"
#include "ink_display_mailbox.h"
#include "ink_photo_catalog.h"
#include "ink_app_state.h"
#include "ink_runtime_shell.h"
#include "ink_usb_msc_service.h"

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
    ink_photo_catalog_t photo_catalog;
    ink_usb_msc_service_t usb_msc;
    bool tf_ready;
    bool wifi_ready;
    portMUX_TYPE latest_buttons_lock;
    ink_runtime_shell_button_state_t latest_buttons;
    uint32_t latest_buttons_ms;
    uint32_t latest_button_pressed_ms[INK_RUNTIME_SHELL_BUTTON_COUNT];
    uint32_t latest_button_released_ms[INK_RUNTIME_SHELL_BUTTON_COUNT];
    bool reader_resume_pending;
    char reader_resume_path[INK_APP_STATE_PATH_LENGTH + 1];
    size_t reader_resume_page;
    uint32_t storage_epoch;
} ink_system_services_t;

void ink_system_services_reset(ink_system_services_t *services);
esp_err_t ink_system_services_init_mailbox_only(ink_system_services_t *services);
esp_err_t ink_system_services_init(ink_system_services_t *services);
esp_err_t ink_system_services_reload_storage(ink_system_services_t *services);
void ink_system_services_set_latest_buttons(
    ink_system_services_t *services,
    const ink_runtime_shell_button_state_t *buttons,
    const ink_button_snapshot_t *snapshot,
    uint32_t buttons_ms);
bool ink_system_services_get_latest_buttons(
    const ink_system_services_t *services,
    ink_runtime_shell_button_state_t *buttons_out,
    uint32_t *buttons_ms_out);
bool ink_system_services_get_latest_button_edge_ms(
    const ink_system_services_t *services,
    ink_runtime_shell_button_t button,
    uint32_t *pressed_ms_out,
    uint32_t *released_ms_out);
bool ink_system_services_self_test(void);
