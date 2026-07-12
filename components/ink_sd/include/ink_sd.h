#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define INK_SD_MOUNT_POINT "/sdcard"

esp_err_t ink_sd_mount(void);
void ink_sd_unmount(void);
bool ink_sd_is_mounted(void);
