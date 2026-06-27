#include "ink_usb_msc_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

#include "ink_app_boot.h"
#include "ink_system_services.h"

static const char *TAG = "ink_usb_msc";

static void usb_msc_device_event_cb(tinyusb_event_t *event, void *arg);
static void usb_msc_storage_event_cb(
    tinyusb_msc_storage_handle_t handle,
    tinyusb_msc_event_t *event,
    void *arg);
static esp_err_t ensure_tinyusb_device_installed(ink_usb_msc_service_t *service);
static esp_err_t ensure_msc_driver_installed(ink_usb_msc_service_t *service);
static esp_err_t destroy_usb_storage(ink_usb_msc_service_t *service);
static void set_status_text(ink_usb_msc_service_t *service, const char *text);

static void set_status_text(ink_usb_msc_service_t *service, const char *text)
{
    if (service == NULL) {
        return;
    }

    snprintf(
        service->status_text,
        sizeof(service->status_text),
        "%s",
        text != NULL ? text : "");
}

static void usb_msc_device_event_cb(tinyusb_event_t *event, void *arg)
{
    ink_usb_msc_service_t *service = (ink_usb_msc_service_t *)arg;

    if (service == NULL || event == NULL) {
        return;
    }

    ESP_LOGI(TAG, "device event id=%d state=%d exported=%d attached=%d", (int)event->id, (int)service->state, service->tf_exported ? 1 : 0, service->usb_attached ? 1 : 0);
    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            (void)ink_usb_msc_service_handle_attach(service);
            break;
        case TINYUSB_EVENT_DETACHED:
            (void)ink_usb_msc_service_handle_detach(service);
            break;
        default:
            break;
    }
}

static void usb_msc_storage_event_cb(
    tinyusb_msc_storage_handle_t handle,
    tinyusb_msc_event_t *event,
    void *arg)
{
    ink_usb_msc_service_t *service = (ink_usb_msc_service_t *)arg;

    (void)handle;
    if (service == NULL || event == NULL) {
        return;
    }

    ESP_LOGI(TAG, "storage event id=%d mount_point=%d", (int)event->id, (int)event->mount_point);
    switch (event->id) {
        case TINYUSB_MSC_EVENT_MOUNT_START:
            if (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB) {
                set_status_text(service, "SWITCHING TO USB");
            } else {
                set_status_text(service, "RESTORING TF");
            }
            break;
        case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
            if (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB) {
                set_status_text(service, "USB MSC ACTIVE");
            } else {
                set_status_text(service, "TF RESTORED");
            }
            break;
        case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
            service->state = INK_USB_MSC_STATE_ERROR;
            set_status_text(service, "TF FS NOT RECOGNIZED");
            break;
        case TINYUSB_MSC_EVENT_MOUNT_FAILED:
        case TINYUSB_MSC_EVENT_FORMAT_FAILED:
            service->state = INK_USB_MSC_STATE_ERROR;
            set_status_text(service, "MSC SWITCH FAILED");
            break;
        default:
            break;
    }
}

static esp_err_t ensure_tinyusb_device_installed(ink_usb_msc_service_t *service)
{
    esp_err_t ret;
    tinyusb_config_t tusb_cfg;

    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (service->usb_driver_installed) {
        return ESP_OK;
    }

    tusb_cfg = TINYUSB_DEFAULT_CONFIG(usb_msc_device_event_cb, service);
    ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    service->usb_driver_installed = true;
    service->usb_device_ready = true;
    ESP_LOGI(TAG, "tinyusb device driver ready");
    return ESP_OK;
}

static esp_err_t ensure_msc_driver_installed(ink_usb_msc_service_t *service)
{
    esp_err_t ret;
    tinyusb_msc_driver_config_t driver_cfg = {
        .user_flags.auto_mount_off = 1,
        .callback = usb_msc_storage_event_cb,
        .callback_arg = service,
    };

    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (service->msc_driver_installed) {
        return ESP_OK;
    }

    ret = tinyusb_msc_install_driver(&driver_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb msc driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    service->msc_driver_installed = true;
    ESP_LOGI(TAG, "tinyusb msc driver ready");
    return ESP_OK;
}

static esp_err_t destroy_usb_storage(ink_usb_msc_service_t *service)
{
    esp_err_t ret = ESP_OK;

    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (service->msc_handle != NULL) {
        ret = tinyusb_msc_delete_storage(service->msc_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "delete usb storage failed: %s", esp_err_to_name(ret));
        }
        service->msc_handle = NULL;
    }

    if (service->msc_card != NULL) {
        ink_app_close_tf_card_for_usb(service->msc_card);
        service->msc_card = NULL;
    }

    service->msc_storage_created = false;
    service->tf_exported = false;
    return ret;
}

void ink_usb_msc_service_reset(ink_usb_msc_service_t *service)
{
    if (service == NULL) {
        return;
    }

    memset(service, 0, sizeof(*service));
    service->state = INK_USB_MSC_STATE_DISABLED;
}

esp_err_t ink_usb_msc_service_init(ink_usb_msc_service_t *service)
{
    esp_err_t ret;

    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!service->initialized) {
        ink_usb_msc_service_reset(service);
        service->initialized = true;
    }

    ret = ensure_tinyusb_device_installed(service);
    if (ret != ESP_OK) {
        service->state = INK_USB_MSC_STATE_ERROR;
        set_status_text(service, "USB STACK INIT FAILED");
        return ret;
    }

    ret = ensure_msc_driver_installed(service);
    if (ret != ESP_OK) {
        service->state = INK_USB_MSC_STATE_ERROR;
        set_status_text(service, "USB MSC INIT FAILED");
        return ret;
    }

    service->state = service->usb_attached
        ? INK_USB_MSC_STATE_PROMPT
        : INK_USB_MSC_STATE_IDLE;
    set_status_text(service, service->usb_attached ? "USB CONNECTED" : "WAITING FOR USB");
    ESP_LOGI(TAG, "service init attached=%d state=%d", service->usb_attached ? 1 : 0, (int)service->state);
    return ESP_OK;
}

esp_err_t ink_usb_msc_service_deinit(ink_usb_msc_service_t *service)
{
    esp_err_t ret = ESP_OK;

    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (service->tf_exported) {
        ret = destroy_usb_storage(service);
    }

    if (service->msc_driver_installed) {
        esp_err_t msc_ret = tinyusb_msc_uninstall_driver();
        if (msc_ret != ESP_OK) {
            ESP_LOGW(TAG, "tinyusb msc uninstall failed: %s", esp_err_to_name(msc_ret));
            if (ret == ESP_OK) {
                ret = msc_ret;
            }
        } else {
            service->msc_driver_installed = false;
        }
    }

    if (service->usb_driver_installed) {
        esp_err_t usb_ret = tinyusb_driver_uninstall();
        if (usb_ret != ESP_OK) {
            ESP_LOGW(TAG, "tinyusb uninstall failed: %s", esp_err_to_name(usb_ret));
            if (ret == ESP_OK) {
                ret = usb_ret;
            }
        } else {
            service->usb_driver_installed = false;
        }
    }

    service->usb_device_ready = false;
    service->usb_attached = false;
    service->state = INK_USB_MSC_STATE_DISABLED;
    return ret;
}

esp_err_t ink_usb_msc_service_handle_attach(ink_usb_msc_service_t *service)
{
    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    service->usb_attached = true;
    if (service->state != INK_USB_MSC_STATE_ACTIVE) {
        service->state = INK_USB_MSC_STATE_PROMPT;
        set_status_text(service, "USB CONNECTED");
    }
    ESP_LOGI(TAG, "handle attach state=%d exported=%d", (int)service->state, service->tf_exported ? 1 : 0);
    return ESP_OK;
}

esp_err_t ink_usb_msc_service_handle_detach(ink_usb_msc_service_t *service)
{
    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    service->usb_attached = false;
    if (!service->tf_exported) {
        service->state = INK_USB_MSC_STATE_IDLE;
        set_status_text(service, "WAITING FOR USB");
    }
    ESP_LOGI(TAG, "handle detach state=%d exported=%d", (int)service->state, service->tf_exported ? 1 : 0);
    return ESP_OK;
}

esp_err_t ink_usb_msc_service_enter_export(
    ink_usb_msc_service_t *service,
    ink_system_services_t *services)
{
    esp_err_t ret;
    tinyusb_msc_storage_config_t storage_cfg;

    if (service == NULL || services == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (service->tf_exported) {
        return ESP_OK;
    }
    if (!services->tf_ready) {
        service->state = INK_USB_MSC_STATE_ERROR;
        set_status_text(service, "TF NOT READY");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "enter export begin attached=%d tf_ready=%d", service->usb_attached ? 1 : 0, services->tf_ready ? 1 : 0);
    ret = ensure_tinyusb_device_installed(service);
    if (ret != ESP_OK) {
        service->state = INK_USB_MSC_STATE_ERROR;
        set_status_text(service, "USB STACK INIT FAILED");
        return ret;
    }
    ret = ensure_msc_driver_installed(service);
    if (ret != ESP_OK) {
        service->state = INK_USB_MSC_STATE_ERROR;
        set_status_text(service, "USB MSC INIT FAILED");
        return ret;
    }

    ret = ink_app_unmount_tf_card();
    if (ret != ESP_OK) {
        service->state = INK_USB_MSC_STATE_ERROR;
        set_status_text(service, "TF UNMOUNT FAILED");
        return ret;
    }

    ret = ink_app_open_tf_card_for_usb(&service->msc_card);
    if (ret != ESP_OK || service->msc_card == NULL) {
        (void)ink_system_services_reload_storage(services);
        service->state = INK_USB_MSC_STATE_ERROR;
        set_status_text(service, "TF EXPORT FAILED");
        return ret != ESP_OK ? ret : ESP_FAIL;
    }

    memset(&storage_cfg, 0, sizeof(storage_cfg));
    storage_cfg.medium.card = service->msc_card;
    storage_cfg.mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB;
    storage_cfg.fat_fs.base_path = NULL;
    storage_cfg.fat_fs.config.max_files = 8;
    storage_cfg.fat_fs.do_not_format = true;
    storage_cfg.fat_fs.format_flags = 0;

    ret = tinyusb_msc_new_storage_sdmmc(&storage_cfg, &service->msc_handle);
    if (ret != ESP_OK || service->msc_handle == NULL) {
        ESP_LOGE(TAG, "create usb storage failed: %s", esp_err_to_name(ret));
        (void)destroy_usb_storage(service);
        (void)ink_system_services_reload_storage(services);
        service->state = INK_USB_MSC_STATE_ERROR;
        set_status_text(service, "USB STORAGE CREATE FAILED");
        return ret != ESP_OK ? ret : ESP_FAIL;
    }

    service->msc_storage_created = true;
    ret = tinyusb_msc_set_storage_mount_point(service->msc_handle, TINYUSB_MSC_STORAGE_MOUNT_USB);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "switch usb storage mount failed: %s", esp_err_to_name(ret));
        (void)destroy_usb_storage(service);
        (void)ink_system_services_reload_storage(services);
        service->state = INK_USB_MSC_STATE_ERROR;
        set_status_text(service, "MSC SWITCH FAILED");
        return ret;
    }

    service->tf_exported = true;
    service->state = INK_USB_MSC_STATE_ACTIVE;
    set_status_text(service, "USB MSC ACTIVE");
    services->tf_ready = false;
    ESP_LOGI(TAG, "enter export done attached=%d exported=%d state=%d", service->usb_attached ? 1 : 0, service->tf_exported ? 1 : 0, (int)service->state);
    return ESP_OK;
}

esp_err_t ink_usb_msc_service_exit_export(
    ink_usb_msc_service_t *service,
    ink_system_services_t *services)
{
    esp_err_t ret = ESP_OK;
    esp_err_t reload_ret;

    if (service == NULL || services == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (service->msc_handle != NULL) {
        (void)tinyusb_msc_set_storage_mount_point(service->msc_handle, TINYUSB_MSC_STORAGE_MOUNT_APP);
    }

    ESP_LOGI(TAG, "exit export begin attached=%d exported=%d", service->usb_attached ? 1 : 0, service->tf_exported ? 1 : 0);
    ret = destroy_usb_storage(service);
    reload_ret = ink_system_services_reload_storage(services);

    service->state = service->usb_attached ? INK_USB_MSC_STATE_PROMPT : INK_USB_MSC_STATE_IDLE;
    set_status_text(
        service,
        reload_ret == ESP_OK
            ? (service->usb_attached ? "USB CONNECTED" : "USB MSC EXITED")
            : "TF REMOUNT FAILED");
    ESP_LOGI(TAG, "exit export done attached=%d exported=%d state=%d reload=%s", service->usb_attached ? 1 : 0, service->tf_exported ? 1 : 0, (int)service->state, esp_err_to_name(reload_ret));

    if (ret != ESP_OK) {
        return ret;
    }
    return reload_ret;
}

bool ink_usb_msc_service_is_prompt_needed(const ink_usb_msc_service_t *service)
{
    return service != NULL
        && service->usb_attached
        && service->state == INK_USB_MSC_STATE_PROMPT;
}

bool ink_usb_msc_service_is_active(const ink_usb_msc_service_t *service)
{
    return service != NULL && service->state == INK_USB_MSC_STATE_ACTIVE;
}

bool ink_usb_msc_service_self_test(void)
{
    ink_usb_msc_service_t service;

    ink_usb_msc_service_reset(&service);
    if (service.state != INK_USB_MSC_STATE_DISABLED) {
        return false;
    }

    service.initialized = true;
    service.usb_driver_installed = true;
    service.msc_driver_installed = true;
    service.usb_device_ready = true;
    service.state = INK_USB_MSC_STATE_IDLE;

    if (ink_usb_msc_service_handle_attach(&service) != ESP_OK) {
        return false;
    }
    if (!ink_usb_msc_service_is_prompt_needed(&service)) {
        return false;
    }

    service.state = INK_USB_MSC_STATE_ACTIVE;
    service.tf_exported = true;
    if (ink_usb_msc_service_handle_detach(&service) != ESP_OK) {
        return false;
    }
    if (service.state != INK_USB_MSC_STATE_ACTIVE || service.usb_attached) {
        return false;
    }

    service.tf_exported = false;
    if (ink_usb_msc_service_handle_detach(&service) != ESP_OK) {
        return false;
    }

    return service.state == INK_USB_MSC_STATE_IDLE
        && !service.usb_attached
        && service.usb_device_ready;
}
