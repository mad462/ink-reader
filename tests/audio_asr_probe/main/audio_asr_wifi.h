#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *ssid;
    const char *password;
} audio_asr_wifi_config_t;

bool audio_asr_wifi_config_valid(const audio_asr_wifi_config_t *config);
const audio_asr_wifi_config_t *audio_asr_wifi_get_config(void);
bool audio_asr_wifi_format_ipv4(uint32_t addr, char *buffer, size_t buffer_size);
bool audio_asr_wifi_get_ipv4_string(char *buffer, size_t buffer_size);
esp_err_t audio_asr_wifi_connect(const audio_asr_wifi_config_t *config, uint32_t timeout_ms);
esp_err_t audio_asr_wifi_quiet(void);
bool audio_asr_wifi_self_test(void);

#ifdef __cplusplus
}
#endif
