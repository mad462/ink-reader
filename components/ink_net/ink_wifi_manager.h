#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

enum {
    INK_WIFI_SSID_MAX_LEN = 32,
    INK_WIFI_PASSWORD_MAX_LEN = 64,
    INK_WIFI_MAX_CREDENTIALS = 5,
    INK_WIFI_SCAN_MAX_RESULTS = 12,
};

typedef struct {
    char ssid[INK_WIFI_SSID_MAX_LEN + 1];
    char password[INK_WIFI_PASSWORD_MAX_LEN + 1];
} ink_wifi_credential_t;

typedef struct {
    char ssid[INK_WIFI_SSID_MAX_LEN + 1];
    int8_t rssi;
    uint8_t authmode;
    bool saved;
    uint8_t ap_count;
} ink_wifi_scan_result_t;

typedef struct {
    ink_wifi_scan_result_t results[INK_WIFI_SCAN_MAX_RESULTS];
    uint16_t count;
} ink_wifi_scan_list_t;

typedef struct {
    bool connected;
    char ssid[INK_WIFI_SSID_MAX_LEN + 1];
    int8_t rssi;
    esp_err_t last_error;
} ink_wifi_status_t;

esp_err_t ink_wifi_manager_init(void);
bool ink_wifi_scan_list_add_or_merge(ink_wifi_scan_list_t *list, const ink_wifi_scan_result_t *entry);
esp_err_t ink_wifi_manager_save_credential(const char *ssid, const char *password);
esp_err_t ink_wifi_manager_delete_credential(const char *ssid);
esp_err_t ink_wifi_manager_scan(ink_wifi_scan_list_t *out_list);
esp_err_t ink_wifi_manager_connect_password(const char *ssid, const char *password, uint32_t timeout_ms, ink_wifi_status_t *out_status);
esp_err_t ink_wifi_manager_connect_saved(const char *ssid, uint32_t timeout_ms, ink_wifi_status_t *out_status);
esp_err_t ink_wifi_manager_connect_best(uint32_t timeout_ms, ink_wifi_status_t *out_status);
esp_err_t ink_wifi_manager_disconnect(void);
esp_err_t ink_wifi_manager_status(ink_wifi_status_t *out_status);
