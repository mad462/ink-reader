#pragma once

#include "esp_err.h"

esp_err_t ink_boot_switch_to_launcher(void);
esp_err_t ink_boot_switch_to_reader(void);
esp_err_t ink_boot_switch_to_photo(void);
esp_err_t ink_boot_switch_to_usb_msc(void);
esp_err_t ink_boot_switch_to_wifi_setup(void);
