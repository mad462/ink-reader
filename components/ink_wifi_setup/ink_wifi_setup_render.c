#include "ink_wifi_setup_render.h"

bool ink_wifi_setup_partial_error_should_full_refresh(esp_err_t error)
{
    return error != ESP_OK && error != ESP_ERR_INVALID_STATE;
}

bool ink_wifi_setup_render_self_test(void)
{
    return !ink_wifi_setup_partial_error_should_full_refresh(ESP_ERR_INVALID_STATE)
        && ink_wifi_setup_partial_error_should_full_refresh(ESP_ERR_INVALID_ARG)
        && ink_wifi_setup_partial_error_should_full_refresh(ESP_FAIL)
        && !ink_wifi_setup_partial_error_should_full_refresh(ESP_OK);
}
