#pragma once

#include "esp_err.h"

#include "ink_app_priv.h"

void ink_app_initialize_context(ink_app_context_t *app);
esp_err_t ink_app_allocate_runtime_buffers(ink_app_context_t *app);
esp_err_t ink_app_prepare_storage_and_library(ink_app_context_t *app);
bool ink_app_startup_self_test(void);
