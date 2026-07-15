#ifndef TINYUSB_MSC_HOST_H
#define TINYUSB_MSC_HOST_H

#include <stdbool.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

typedef struct host_storage *tinyusb_msc_storage_handle_t;

typedef struct {
  union {
    struct {
      unsigned auto_mount_off : 1;
    };
    unsigned val;
  } user_flags;
  void *callback;
  void *callback_arg;
} tinyusb_msc_driver_config_t;

typedef struct {
  union {
    sdmmc_card_t *card;
  } medium;
  struct {
    const char *base_path;
    struct {
      int max_files;
    } config;
    bool do_not_format;
    int format_flags;
  } fat_fs;
  int mount_point;
} tinyusb_msc_storage_config_t;

#define TINYUSB_MSC_STORAGE_MOUNT_USB 1

esp_err_t tinyusb_msc_install_driver(
    const tinyusb_msc_driver_config_t *config);
esp_err_t tinyusb_msc_uninstall_driver(void);
esp_err_t tinyusb_msc_new_storage_sdmmc(
    const tinyusb_msc_storage_config_t *config,
    tinyusb_msc_storage_handle_t *handle);
esp_err_t tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t handle);

#endif
