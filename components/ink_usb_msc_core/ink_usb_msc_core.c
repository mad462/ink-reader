#include "ink_usb_msc_core.h"

#include <stdlib.h>
#include <string.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "ink_usb_msc_state.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

static const char *TAG = "ink_usb_msc_core";

typedef struct {
  ink_usb_msc_state_machine_t machine;
  sdmmc_host_t host;
  sdmmc_card_t *card;
  tinyusb_msc_storage_handle_t storage;
  bool host_initialized;
  bool usb_driver_installed;
  bool msc_driver_installed;
} ink_usb_msc_context_t;

static ink_usb_msc_context_t s_core;

static esp_err_t close_sd_card(void) {
  if (!s_core.host_initialized) return ESP_OK;
  const esp_err_t ret =
      (s_core.host.flags & SDMMC_HOST_FLAG_DEINIT_ARG)
          ? s_core.host.deinit_p(s_core.host.slot)
          : s_core.host.deinit();
  if (ret != ESP_OK) return ret;
  s_core.host_initialized = false;
  free(s_core.card);
  s_core.card = NULL;
  return ESP_OK;
}

static esp_err_t release_resources(void) {
  if (s_core.storage) {
    const esp_err_t ret = tinyusb_msc_delete_storage(s_core.storage);
    if (ret != ESP_OK) return ret;
    s_core.storage = NULL;
  }
  if (s_core.msc_driver_installed) {
    const esp_err_t ret = tinyusb_msc_uninstall_driver();
    if (ret != ESP_OK) return ret;
    s_core.msc_driver_installed = false;
  }
  if (s_core.usb_driver_installed) {
    const esp_err_t ret = tinyusb_driver_uninstall();
    if (ret != ESP_OK) return ret;
    s_core.usb_driver_installed = false;
  }
  return close_sd_card();
}

static esp_err_t open_sd_card(void) {
  const sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  s_core.host = host;
  s_core.host.max_freq_khz = SDMMC_FREQ_DEFAULT;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 4;
  slot.clk = 40;
  slot.cmd = 39;
  slot.d0 = 41;
  slot.d1 = 42;
  slot.d2 = 48;
  slot.d3 = 38;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  s_core.card = calloc(1, sizeof(*s_core.card));
  if (!s_core.card) return ESP_ERR_NO_MEM;

  esp_err_t ret = s_core.host.init();
  if (ret != ESP_OK) {
    free(s_core.card);
    s_core.card = NULL;
    return ret;
  }
  s_core.host_initialized = true;

  ret = sdmmc_host_init_slot(s_core.host.slot, &slot);
  if (ret == ESP_OK) ret = sdmmc_card_init(&s_core.host, s_core.card);
  if (ret != ESP_OK) (void)close_sd_card();
  return ret;
}

esp_err_t ink_usb_msc_core_start(void) {
  if (s_core.machine.state == INK_USB_MSC_ACTIVE) return ESP_OK;
  if (s_core.storage || s_core.msc_driver_installed ||
      s_core.usb_driver_installed || s_core.host_initialized) {
    const esp_err_t cleanup_ret = release_resources();
    if (cleanup_ret != ESP_OK) {
      ink_usb_msc_state_start_finish(&s_core.machine, false);
      return cleanup_ret;
    }
  }

  ink_usb_msc_state_start_begin(&s_core.machine);
  esp_err_t ret = open_sd_card();
  if (ret != ESP_OK) goto failed;

  tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG();
  ret = tinyusb_driver_install(&usb_config);
  if (ret != ESP_OK) goto failed;
  s_core.usb_driver_installed = true;

  const tinyusb_msc_driver_config_t driver_config = {
      .user_flags.auto_mount_off = 1,
      .callback = NULL,
      .callback_arg = NULL,
  };
  ret = tinyusb_msc_install_driver(&driver_config);
  if (ret != ESP_OK) goto failed;
  s_core.msc_driver_installed = true;

  const tinyusb_msc_storage_config_t storage_config = {
      .medium.card = s_core.card,
      .fat_fs =
          {
              .base_path = NULL,
              .config.max_files = 1,
              .do_not_format = true,
              .format_flags = 0,
          },
      .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
  };
  ret = tinyusb_msc_new_storage_sdmmc(&storage_config, &s_core.storage);
  if (ret != ESP_OK || !s_core.storage) {
    if (ret == ESP_OK) ret = ESP_FAIL;
    goto failed;
  }

  ink_usb_msc_state_start_finish(&s_core.machine, true);
  ESP_LOGI(TAG, "USB MSC active");
  return ESP_OK;

failed:
  ESP_LOGE(TAG, "USB MSC start failed err=%s", esp_err_to_name(ret));
  const esp_err_t cleanup_ret = release_resources();
  ink_usb_msc_state_start_finish(&s_core.machine, false);
  return cleanup_ret != ESP_OK ? cleanup_ret : ret;
}

esp_err_t ink_usb_msc_core_stop(void) {
  ink_usb_msc_state_stop_begin(&s_core.machine);
  const esp_err_t ret = release_resources();
  ink_usb_msc_state_stop_finish(&s_core.machine, ret == ESP_OK);
  if (ret != ESP_OK)
    ESP_LOGE(TAG, "USB MSC stop failed err=%s", esp_err_to_name(ret));
  return ret;
}

ink_usb_msc_state_t ink_usb_msc_core_state(void) {
  return s_core.machine.state;
}

bool ink_usb_msc_core_can_return_launcher(void) {
  return ink_usb_msc_state_can_return_launcher(&s_core.machine) &&
         !s_core.storage && !s_core.msc_driver_installed &&
         !s_core.usb_driver_installed && !s_core.host_initialized &&
         !s_core.card;
}
