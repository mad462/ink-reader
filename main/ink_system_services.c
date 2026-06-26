#include "ink_system_services.h"

#include <string.h>

#include "esp_check.h"

#include "epd_gdey0426t82.h"
#include "ink_app_boot.h"
#include "ink_app_priv.h"

static const char *TAG = "ink_services";

void ink_system_services_reset(ink_system_services_t *services)
{
    if (services == NULL) {
        return;
    }

    memset(services, 0, sizeof(*services));
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

    services->tf_ready = ink_app_mount_tf_card() == ESP_OK;
    if (services->tf_ready) {
        (void)ink_app_load_reader_font(&services->reader_font);
        (void)ink_app_load_footer_font(&services->footer_font);
        (void)ink_app_load_menu_font(&services->menu_font);
    }

    return ESP_OK;
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

    return services.mailbox.bitmap_snapshot_buffers[0] == services.bitmap_snapshot_a
        && services.mailbox.bitmap_snapshot_buffers[1] == services.bitmap_snapshot_b
        && services.mailbox.native_snapshot_buffers[0] == services.native_snapshot_a
        && services.mailbox.native_snapshot_buffers[1] == services.native_snapshot_b;
}
