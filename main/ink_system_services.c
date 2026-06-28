#include "ink_system_services.h"

#include <string.h>

#include "esp_check.h"

#include "epd_gdey0426t82.h"
#include "ink_app_boot.h"
#include "ink_photo_catalog.h"
#include "ink_app_priv.h"
#include "ink_usb_msc_service.h"
#include "ink_wifi_manager.h"

static const char *TAG = "ink_services";

static void reload_service_fonts(ink_system_services_t *services);

void ink_system_services_reset(ink_system_services_t *services)
{
    if (services == NULL) {
        return;
    }

    memset(services, 0, sizeof(*services));
    services->latest_buttons_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    ink_photo_catalog_init(&services->photo_catalog);
    ink_usb_msc_service_reset(&services->usb_msc);
}

esp_err_t ink_system_services_init_mailbox_only(ink_system_services_t *services)
{
    if (services == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ink_display_mailbox_init(
        &services->mailbox,
        NULL,
        services->bitmap_snapshot_a,
        services->bitmap_snapshot_b,
        services->native_snapshot_a,
        services->native_snapshot_b);
    return ESP_OK;
}

esp_err_t ink_system_services_reload_storage(ink_system_services_t *services)
{
    if (services == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    services->tf_ready = ink_app_mount_tf_card() == ESP_OK;
    if (!services->tf_ready) {
        ink_cpfont_close(&services->reader_font);
        ink_cpfont_close(&services->footer_font);
        ink_cpfont_close(&services->menu_font);
        ink_photo_catalog_init(&services->photo_catalog);
        return ESP_FAIL;
    }

    (void)ink_photo_catalog_reload(&services->photo_catalog);
    reload_service_fonts(services);
    services->storage_epoch++;
    return ESP_OK;
}

static void reload_service_fonts(ink_system_services_t *services)
{
    if (services == NULL) {
        return;
    }

    ink_cpfont_close(&services->reader_font);
    ink_cpfont_close(&services->footer_font);
    ink_cpfont_close(&services->menu_font);
    (void)ink_app_load_reader_font(&services->reader_font);
    (void)ink_app_load_footer_font(&services->footer_font);
    (void)ink_app_load_menu_font(&services->menu_font);
}

esp_err_t ink_system_services_init(ink_system_services_t *services)
{
    if (services == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    services->framebuffer = ink_app_alloc_display_buffer("framebuffer", EPD_GDEY0426T82_BUFFER_SIZE);
    services->previous_framebuffer = ink_app_alloc_display_buffer("previous_framebuffer", EPD_GDEY0426T82_BUFFER_SIZE);
    services->bitmap_snapshot_a = ink_app_alloc_display_buffer("bitmap_snapshot_a", EPD_GDEY0426T82_BUFFER_SIZE);
    services->bitmap_snapshot_b = ink_app_alloc_display_buffer("bitmap_snapshot_b", EPD_GDEY0426T82_BUFFER_SIZE);
    services->native_snapshot_a = ink_app_alloc_display_buffer("native_snapshot_a", EPD_GDEY0426T82_NATIVE_BUFFER_SIZE);
    services->native_snapshot_b = ink_app_alloc_display_buffer("native_snapshot_b", EPD_GDEY0426T82_NATIVE_BUFFER_SIZE);
    ESP_RETURN_ON_FALSE(services->framebuffer != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer alloc");
    ESP_RETURN_ON_FALSE(services->previous_framebuffer != NULL, ESP_ERR_NO_MEM, TAG, "previous framebuffer alloc");
    ESP_RETURN_ON_FALSE(services->bitmap_snapshot_a != NULL, ESP_ERR_NO_MEM, TAG, "bitmap snapshot a alloc");
    ESP_RETURN_ON_FALSE(services->bitmap_snapshot_b != NULL, ESP_ERR_NO_MEM, TAG, "bitmap snapshot b alloc");
    ESP_RETURN_ON_FALSE(services->native_snapshot_a != NULL, ESP_ERR_NO_MEM, TAG, "native snapshot a alloc");
    ESP_RETURN_ON_FALSE(services->native_snapshot_b != NULL, ESP_ERR_NO_MEM, TAG, "native snapshot b alloc");

    memset(services->previous_framebuffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);
    memset(services->framebuffer, 0xFF, EPD_GDEY0426T82_BUFFER_SIZE);

    ESP_RETURN_ON_ERROR(ink_system_services_init_mailbox_only(services), TAG, "mailbox init");

    services->ui_queue = xQueueCreate(INK_UI_QUEUE_LENGTH, sizeof(ink_ui_event_t));
    ESP_RETURN_ON_FALSE(services->ui_queue != NULL, ESP_ERR_NO_MEM, TAG, "ui queue alloc");

    services->tf_ready = false;
    if (ink_system_services_reload_storage(services) == ESP_OK) {
        /* Fonts are reloaded as part of storage init/reload. */
    }
    (void)ink_usb_msc_service_init(&services->usb_msc);
    services->wifi_ready = ink_wifi_manager_init() == ESP_OK;

    return ESP_OK;
}

void ink_system_services_set_latest_buttons(
    ink_system_services_t *services,
    const ink_runtime_shell_button_state_t *buttons,
    const ink_button_snapshot_t *snapshot,
    uint32_t buttons_ms)
{
    static const ink_raw_button_t kRawMap[INK_RUNTIME_SHELL_BUTTON_COUNT] = {
        [INK_RUNTIME_SHELL_BUTTON_BACK] = INK_RAW_BUTTON_BACK,
        [INK_RUNTIME_SHELL_BUTTON_CONFIRM] = INK_RAW_BUTTON_CONFIRM,
        [INK_RUNTIME_SHELL_BUTTON_LEFT] = INK_RAW_BUTTON_LEFT,
        [INK_RUNTIME_SHELL_BUTTON_RIGHT] = INK_RAW_BUTTON_RIGHT,
        [INK_RUNTIME_SHELL_BUTTON_POWER] = INK_RAW_BUTTON_POWER,
    };

    if (services == NULL || buttons == NULL) {
        return;
    }

    taskENTER_CRITICAL(&services->latest_buttons_lock);
    services->latest_buttons = *buttons;
    services->latest_buttons_ms = buttons_ms;
    if (snapshot != NULL) {
        for (int i = 0; i < INK_RUNTIME_SHELL_BUTTON_COUNT; ++i) {
            const uint32_t raw_mask = ink_button_input_mask_for_raw(kRawMap[i]);
            if ((snapshot->pressed_mask & raw_mask) != 0U) {
                services->latest_button_pressed_ms[i] = buttons_ms;
            }
            if ((snapshot->released_mask & raw_mask) != 0U) {
                services->latest_button_released_ms[i] = buttons_ms;
            }
        }
    }
    taskEXIT_CRITICAL(&services->latest_buttons_lock);
}

bool ink_system_services_get_latest_buttons(
    const ink_system_services_t *services,
    ink_runtime_shell_button_state_t *buttons_out,
    uint32_t *buttons_ms_out)
{
    if (services == NULL || buttons_out == NULL) {
        return false;
    }

    taskENTER_CRITICAL((portMUX_TYPE *)&services->latest_buttons_lock);
    *buttons_out = services->latest_buttons;
    if (buttons_ms_out != NULL) {
        *buttons_ms_out = services->latest_buttons_ms;
    }
    taskEXIT_CRITICAL((portMUX_TYPE *)&services->latest_buttons_lock);
    return true;
}

bool ink_system_services_get_latest_button_edge_ms(
    const ink_system_services_t *services,
    ink_runtime_shell_button_t button,
    uint32_t *pressed_ms_out,
    uint32_t *released_ms_out)
{
    if (services == NULL
        || button < 0
        || button >= INK_RUNTIME_SHELL_BUTTON_COUNT) {
        return false;
    }

    taskENTER_CRITICAL((portMUX_TYPE *)&services->latest_buttons_lock);
    if (pressed_ms_out != NULL) {
        *pressed_ms_out = services->latest_button_pressed_ms[button];
    }
    if (released_ms_out != NULL) {
        *released_ms_out = services->latest_button_released_ms[button];
    }
    taskEXIT_CRITICAL((portMUX_TYPE *)&services->latest_buttons_lock);
    return true;
}

bool ink_system_services_self_test(void)
{
    ink_system_services_t services;

    memset(&services, 0xA5, sizeof(services));
    ink_system_services_reset(&services);
    if (services.ui_queue != NULL
        || services.framebuffer != NULL
        || services.previous_framebuffer != NULL
        || services.bitmap_snapshot_a != NULL
        || services.bitmap_snapshot_b != NULL
        || services.native_snapshot_a != NULL
        || services.native_snapshot_b != NULL) {
        return false;
    }

    services.bitmap_snapshot_a = (uint8_t *)0x11;
    services.bitmap_snapshot_b = (uint8_t *)0x22;
    services.native_snapshot_a = (uint8_t *)0x33;
    services.native_snapshot_b = (uint8_t *)0x44;
    if (ink_system_services_init_mailbox_only(&services) != ESP_OK) {
        return false;
    }

    {
        ink_runtime_shell_button_state_t buttons = {0};
        uint32_t buttons_ms = 0U;
        ink_button_snapshot_t snapshot = {0};
        uint32_t pressed_ms = 0U;
        uint32_t released_ms = 0U;

        snapshot.pressed_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
        snapshot.released_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_LEFT);
        ink_system_services_set_latest_buttons(&services, &buttons, &snapshot, 77U);
        if (!ink_system_services_get_latest_buttons(&services, &buttons, &buttons_ms)) {
            return false;
        }
        if (buttons_ms != 77U) {
            return false;
        }
        if (!ink_system_services_get_latest_button_edge_ms(
                &services,
                INK_RUNTIME_SHELL_BUTTON_RIGHT,
                &pressed_ms,
                &released_ms)
            || pressed_ms != 77U
            || released_ms != 0U) {
            return false;
        }
        if (!ink_system_services_get_latest_button_edge_ms(
                &services,
                INK_RUNTIME_SHELL_BUTTON_LEFT,
                &pressed_ms,
                &released_ms)
            || pressed_ms != 0U
            || released_ms != 77U) {
            return false;
        }
    }

    return services.mailbox.bitmap_snapshot_buffers[0] == services.bitmap_snapshot_a
        && services.mailbox.bitmap_snapshot_buffers[1] == services.bitmap_snapshot_b
        && services.mailbox.native_snapshot_buffers[0] == services.native_snapshot_a
        && services.mailbox.native_snapshot_buffers[1] == services.native_snapshot_b
        && services.photo_catalog.initialized
        && services.usb_msc.state == INK_USB_MSC_STATE_DISABLED;
}
