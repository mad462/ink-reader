#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "tinyusb_msc.h"

typedef enum {
    INK_USB_MSC_STATE_DISABLED = 0,
    INK_USB_MSC_STATE_IDLE,
    INK_USB_MSC_STATE_PROMPT,
    INK_USB_MSC_STATE_ACTIVE,
    INK_USB_MSC_STATE_ERROR,
} ink_usb_msc_state_t;

typedef struct ink_usb_msc_service {
    bool initialized;
    bool usb_attached;
    bool usb_device_ready;
    bool usb_driver_installed;
    bool msc_driver_installed;
    bool msc_storage_created;
    bool tf_exported;
    ink_usb_msc_state_t state;
    sdmmc_card_t *msc_card;
    tinyusb_msc_storage_handle_t msc_handle;
    char status_text[96];
} ink_usb_msc_service_t;

typedef struct ink_system_services ink_system_services_t;

void ink_usb_msc_service_reset(ink_usb_msc_service_t *service);
esp_err_t ink_usb_msc_service_init(ink_usb_msc_service_t *service);
esp_err_t ink_usb_msc_service_deinit(ink_usb_msc_service_t *service);
esp_err_t ink_usb_msc_service_handle_attach(ink_usb_msc_service_t *service);
esp_err_t ink_usb_msc_service_handle_detach(ink_usb_msc_service_t *service);
esp_err_t ink_usb_msc_service_enter_export(
    ink_usb_msc_service_t *service,
    ink_system_services_t *services);
esp_err_t ink_usb_msc_service_exit_export(
    ink_usb_msc_service_t *service,
    ink_system_services_t *services);
bool ink_usb_msc_service_is_prompt_needed(const ink_usb_msc_service_t *service);
bool ink_usb_msc_service_is_active(const ink_usb_msc_service_t *service);
bool ink_usb_msc_service_self_test(void);
