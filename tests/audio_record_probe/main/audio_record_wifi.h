#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *ssid;
    const char *password;
    const char *server_base_url;
} audio_record_wifi_config_t;

bool audio_record_wifi_config_valid(const audio_record_wifi_config_t *config);

const audio_record_wifi_config_t *audio_record_wifi_get_config(void);

esp_err_t audio_record_wifi_connect(const audio_record_wifi_config_t *config, uint32_t timeout_ms);

esp_err_t audio_record_wifi_quiet(void);

bool audio_record_wifi_self_test(void);

#ifdef __cplusplus
}
#endif
