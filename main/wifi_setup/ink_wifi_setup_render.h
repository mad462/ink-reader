#pragma once

#include <stdbool.h>

#include "esp_err.h"

bool ink_wifi_setup_partial_error_should_full_refresh(esp_err_t error);
