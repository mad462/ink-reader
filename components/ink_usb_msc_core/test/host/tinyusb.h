#ifndef TINYUSB_HOST_H
#define TINYUSB_HOST_H

#include "esp_err.h"

typedef struct {
  int unused;
} tinyusb_config_t;

esp_err_t tinyusb_driver_install(const tinyusb_config_t *config);
esp_err_t tinyusb_driver_uninstall(void);

#endif
