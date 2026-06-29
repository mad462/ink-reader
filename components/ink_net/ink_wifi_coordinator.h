#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ink_wifi_manager.h"

typedef enum {
    INK_WIFI_COORDINATOR_OWNER_NONE = 0,
    INK_WIFI_COORDINATOR_OWNER_BOOT_AUTO_CONNECT,
    INK_WIFI_COORDINATOR_OWNER_TIME_SYNC,
    INK_WIFI_COORDINATOR_OWNER_WIFI_SETUP,
    INK_WIFI_COORDINATOR_OWNER_VOICE_TAG_ASR,
} ink_wifi_coordinator_owner_t;

typedef enum {
    INK_WIFI_COORDINATOR_STATE_OFF = 0,
    INK_WIFI_COORDINATOR_STATE_STARTING,
    INK_WIFI_COORDINATOR_STATE_DISCONNECTED,
    INK_WIFI_COORDINATOR_STATE_CONNECTING,
    INK_WIFI_COORDINATOR_STATE_ONLINE,
    INK_WIFI_COORDINATOR_STATE_ERROR,
} ink_wifi_coordinator_state_t;

typedef enum {
    INK_WIFI_COORDINATOR_RESULT_OK = 0,
    INK_WIFI_COORDINATOR_RESULT_TIMEOUT,
    INK_WIFI_COORDINATOR_RESULT_NO_CREDENTIAL,
    INK_WIFI_COORDINATOR_RESULT_CONNECT_FAILED,
    INK_WIFI_COORDINATOR_RESULT_SCAN_FAILED,
    INK_WIFI_COORDINATOR_RESULT_BUSY_RETRYABLE,
    INK_WIFI_COORDINATOR_RESULT_INVALID_STATE,
} ink_wifi_coordinator_result_t;

typedef enum {
    INK_WIFI_COORDINATOR_REQUEST_ENSURE_CONNECTED = 0,
    INK_WIFI_COORDINATOR_REQUEST_SCAN,
    INK_WIFI_COORDINATOR_REQUEST_CONNECT_SAVED,
    INK_WIFI_COORDINATOR_REQUEST_CONNECT_PASSWORD,
    INK_WIFI_COORDINATOR_REQUEST_RELEASE_LEASE,
    INK_WIFI_COORDINATOR_REQUEST_DISCONNECT_IF_IDLE,
} ink_wifi_coordinator_request_type_t;

typedef struct {
    ink_wifi_coordinator_state_t state;
    ink_wifi_coordinator_result_t last_result;
    ink_wifi_status_t wifi_status;
    uint32_t active_leases;
    uint32_t idle_timeout_ms;
    bool request_in_progress;
} ink_wifi_coordinator_status_t;

typedef struct {
    ink_wifi_coordinator_request_type_t type;
    ink_wifi_coordinator_owner_t owner;
    bool keep_alive;
    bool best_effort_saved;
    uint32_t timeout_ms;
    char ssid[INK_WIFI_SSID_MAX_LEN + 1];
    char password[INK_WIFI_PASSWORD_MAX_LEN + 1];
    ink_wifi_scan_list_t *scan_out;
    ink_wifi_status_t *status_out;
} ink_wifi_coordinator_request_t;

esp_err_t ink_wifi_coordinator_init(void);
esp_err_t ink_wifi_coordinator_start(void);
esp_err_t ink_wifi_coordinator_request(
    const ink_wifi_coordinator_request_t *request,
    ink_wifi_coordinator_result_t *out_result);
esp_err_t ink_wifi_coordinator_release_owner(
    ink_wifi_coordinator_owner_t owner,
    uint32_t timeout_ms);
esp_err_t ink_wifi_coordinator_disconnect_if_idle(uint32_t timeout_ms);
esp_err_t ink_wifi_coordinator_get_status(ink_wifi_coordinator_status_t *out_status);
bool ink_wifi_coordinator_self_test(void);
