#include "ink_wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "ink_wifi";
static const char *NVS_NAMESPACE = "ink_wifi";
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;

enum {
    WIFI_SCAN_RAW_MAX_RESULTS = 32,
};

static bool s_initialized;
static bool s_netif_initialized;
static esp_netif_t *s_sta_netif;
static EventGroupHandle_t s_wifi_events;
static ink_wifi_status_t s_status;

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static bool credential_is_valid(const ink_wifi_credential_t *credential)
{
    return credential != NULL && credential->ssid[0] != '\0';
}

static esp_err_t open_wifi_nvs(nvs_open_mode_t mode, nvs_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "nvs handle missing");
    return nvs_open(NVS_NAMESPACE, mode, out_handle);
}

static esp_err_t load_credential_at(nvs_handle_t handle, int index, ink_wifi_credential_t *out_credential)
{
    char ssid_key[8];
    char pass_key[8];
    size_t ssid_len = INK_WIFI_SSID_MAX_LEN + 1;
    size_t pass_len = INK_WIFI_PASSWORD_MAX_LEN + 1;

    if (out_credential == NULL || index < 0 || index >= INK_WIFI_MAX_CREDENTIALS) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_credential, 0, sizeof(*out_credential));
    snprintf(ssid_key, sizeof(ssid_key), "ssid%d", index);
    snprintf(pass_key, sizeof(pass_key), "pass%d", index);

    esp_err_t ret = nvs_get_str(handle, ssid_key, out_credential->ssid, &ssid_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "read ssid");

    ret = nvs_get_str(handle, pass_key, out_credential->password, &pass_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        out_credential->password[0] = '\0';
        return ESP_OK;
    }
    return ret;
}

static esp_err_t save_credential_at(nvs_handle_t handle, int index, const ink_wifi_credential_t *credential)
{
    char ssid_key[8];
    char pass_key[8];

    if (!credential_is_valid(credential) || index < 0 || index >= INK_WIFI_MAX_CREDENTIALS) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(ssid_key, sizeof(ssid_key), "ssid%d", index);
    snprintf(pass_key, sizeof(pass_key), "pass%d", index);
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, ssid_key, credential->ssid), TAG, "write ssid");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, pass_key, credential->password), TAG, "write password");
    return ESP_OK;
}

static esp_err_t erase_credential_at(nvs_handle_t handle, int index)
{
    char ssid_key[8];
    char pass_key[8];

    if (index < 0 || index >= INK_WIFI_MAX_CREDENTIALS) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(ssid_key, sizeof(ssid_key), "ssid%d", index);
    snprintf(pass_key, sizeof(pass_key), "pass%d", index);
    esp_err_t ret = nvs_erase_key(handle, ssid_key);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        return ret;
    }
    ret = nvs_erase_key(handle, pass_key);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        return ret;
    }
    return ESP_OK;
}

static bool ssid_matches_saved(const char *ssid)
{
    nvs_handle_t handle;
    bool matched = false;

    if (ssid == NULL || ssid[0] == '\0' || open_wifi_nvs(NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    for (int i = 0; i < INK_WIFI_MAX_CREDENTIALS; ++i) {
        ink_wifi_credential_t credential;
        if (load_credential_at(handle, i, &credential) == ESP_OK && strcmp(credential.ssid, ssid) == 0) {
            matched = true;
            break;
        }
    }
    nvs_close(handle);
    return matched;
}

static esp_err_t load_credential_by_ssid(const char *ssid, ink_wifi_credential_t *out_credential)
{
    nvs_handle_t handle;
    esp_err_t result = ESP_ERR_NOT_FOUND;

    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "ssid missing");
    ESP_RETURN_ON_FALSE(out_credential != NULL, ESP_ERR_INVALID_ARG, TAG, "credential output missing");
    ESP_RETURN_ON_ERROR(open_wifi_nvs(NVS_READONLY, &handle), TAG, "open wifi nvs");

    for (int i = 0; i < INK_WIFI_MAX_CREDENTIALS; ++i) {
        ink_wifi_credential_t credential;
        if (load_credential_at(handle, i, &credential) == ESP_OK && strcmp(credential.ssid, ssid) == 0) {
            *out_credential = credential;
            result = ESP_OK;
            break;
        }
    }
    nvs_close(handle);
    return result;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_status.connected = false;
        s_status.last_error = ESP_ERR_WIFI_NOT_CONNECT;
        if (s_wifi_events != NULL) {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_ap_record_t ap = {0};
        s_status.connected = true;
        s_status.last_error = ESP_OK;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            copy_string(s_status.ssid, sizeof(s_status.ssid), (const char *)ap.ssid);
            s_status.rssi = ap.rssi;
        }
        if (s_wifi_events != NULL) {
            xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
    }
}

esp_err_t ink_wifi_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs init");

    if (!s_netif_initialized) {
        ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
        esp_err_t ret = esp_event_loop_create_default();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(ret, TAG, "event loop");
        }
        s_netif_initialized = true;
    }

    s_wifi_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_events != NULL, ESP_ERR_NO_MEM, TAG, "wifi events");

    s_sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_sta_netif != NULL, ESP_FAIL, TAG, "sta netif");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG, "wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL), TAG, "ip handler");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    memset(&s_status, 0, sizeof(s_status));
    s_status.last_error = ESP_ERR_WIFI_NOT_CONNECT;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t ink_wifi_manager_save_credential(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    ink_wifi_credential_t credential = {0};
    int target = -1;

    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "ssid missing");
    copy_string(credential.ssid, sizeof(credential.ssid), ssid);
    copy_string(credential.password, sizeof(credential.password), password != NULL ? password : "");

    ESP_RETURN_ON_ERROR(open_wifi_nvs(NVS_READWRITE, &handle), TAG, "open wifi nvs");

    for (int i = 0; i < INK_WIFI_MAX_CREDENTIALS; ++i) {
        ink_wifi_credential_t existing;
        esp_err_t ret = load_credential_at(handle, i, &existing);
        if (ret == ESP_OK && strcmp(existing.ssid, credential.ssid) == 0) {
            target = i;
            break;
        }
        if (ret == ESP_ERR_NOT_FOUND && target < 0) {
            target = i;
        }
    }
    if (target < 0) {
        target = INK_WIFI_MAX_CREDENTIALS - 1;
    }

    esp_err_t ret = save_credential_at(handle, target, &credential);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "saved wifi credential slot=%d ssid=%s", target, credential.ssid);
    return ret;
}

esp_err_t ink_wifi_manager_delete_credential(const char *ssid)
{
    nvs_handle_t handle;
    esp_err_t result = ESP_ERR_NOT_FOUND;

    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "ssid missing");
    ESP_RETURN_ON_ERROR(open_wifi_nvs(NVS_READWRITE, &handle), TAG, "open wifi nvs");

    for (int i = 0; i < INK_WIFI_MAX_CREDENTIALS; ++i) {
        ink_wifi_credential_t existing;
        esp_err_t ret = load_credential_at(handle, i, &existing);
        if (ret == ESP_OK && strcmp(existing.ssid, ssid) == 0) {
            result = erase_credential_at(handle, i);
            if (result == ESP_OK) {
                result = nvs_commit(handle);
            }
            ESP_LOGI(TAG, "deleted wifi credential slot=%d ssid=%s ret=%s", i, ssid, esp_err_to_name(result));
            break;
        }
    }

    nvs_close(handle);
    return result;
}

bool ink_wifi_scan_list_add_or_merge(ink_wifi_scan_list_t *list, const ink_wifi_scan_result_t *entry)
{
    if (list == NULL || entry == NULL || entry->ssid[0] == '\0') {
        return false;
    }

    const uint8_t entry_ap_count = entry->ap_count > 0 ? entry->ap_count : 1;
    for (uint16_t i = 0; i < list->count; ++i) {
        if (strcmp(list->results[i].ssid, entry->ssid) != 0) {
            continue;
        }

        const uint16_t merged_count = (uint16_t)list->results[i].ap_count + entry_ap_count;
        list->results[i].ap_count = merged_count > UINT8_MAX ? UINT8_MAX : (uint8_t)merged_count;
        list->results[i].saved = list->results[i].saved || entry->saved;
        if (entry->rssi > list->results[i].rssi) {
            const uint8_t ap_count = list->results[i].ap_count;
            const bool saved = list->results[i].saved;
            list->results[i] = *entry;
            list->results[i].ap_count = ap_count;
            list->results[i].saved = saved;
        }
        return true;
    }

    if (list->count >= INK_WIFI_SCAN_MAX_RESULTS) {
        return false;
    }

    list->results[list->count] = *entry;
    list->results[list->count].ap_count = entry_ap_count;
    ++list->count;
    return true;
}

esp_err_t ink_wifi_manager_scan(ink_wifi_scan_list_t *out_list)
{
    wifi_ap_record_t records[WIFI_SCAN_RAW_MAX_RESULTS] = {0};
    uint16_t count = WIFI_SCAN_RAW_MAX_RESULTS;
    uint16_t raw_count = 0;

    ESP_RETURN_ON_FALSE(out_list != NULL, ESP_ERR_INVALID_ARG, TAG, "scan list missing");
    memset(out_list, 0, sizeof(*out_list));
    ESP_RETURN_ON_ERROR(ink_wifi_manager_init(), TAG, "wifi init before scan");

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
    };
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_cfg, true), TAG, "wifi scan");
    if (esp_wifi_scan_get_ap_num(&raw_count) != ESP_OK || raw_count == 0) {
        raw_count = count;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&count, records), TAG, "scan records");

    for (uint16_t i = 0; i < count; ++i) {
        ink_wifi_scan_result_t entry = {0};
        copy_string(entry.ssid, sizeof(entry.ssid), (const char *)records[i].ssid);
        entry.rssi = records[i].rssi;
        entry.authmode = records[i].authmode;
        entry.saved = ssid_matches_saved(entry.ssid);
        entry.ap_count = 1;
        (void)ink_wifi_scan_list_add_or_merge(out_list, &entry);
    }
    ESP_LOGI(TAG, "scan found %u raw APs, %u merged SSIDs", (unsigned)raw_count, (unsigned)out_list->count);
    return ESP_OK;
}

static esp_err_t connect_with_credential(const ink_wifi_credential_t *credential, uint32_t timeout_ms, ink_wifi_status_t *out_status)
{
    ESP_RETURN_ON_ERROR(ink_wifi_manager_init(), TAG, "wifi init before connect saved");
    ESP_RETURN_ON_FALSE(credential_is_valid(credential), ESP_ERR_INVALID_ARG, TAG, "credential missing");

    wifi_config_t wifi_config = {0};
    copy_string((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), credential->ssid);
    copy_string((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), credential->password);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(TAG, "wifi disconnect before connect returned %s", esp_err_to_name(ret));
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "wifi connect");

    const EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        s_status.connected = true;
        s_status.last_error = ESP_OK;
        copy_string(s_status.ssid, sizeof(s_status.ssid), credential->ssid);
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_status.rssi = ap.rssi;
        }
    } else {
        s_status.connected = false;
        s_status.last_error = (bits & WIFI_FAIL_BIT) != 0 ? ESP_ERR_WIFI_NOT_CONNECT : ESP_ERR_TIMEOUT;
    }

    if (out_status != NULL) {
        *out_status = s_status;
    }
    return s_status.last_error;
}

esp_err_t ink_wifi_manager_connect_password(const char *ssid, const char *password, uint32_t timeout_ms, ink_wifi_status_t *out_status)
{
    ink_wifi_credential_t credential = {0};

    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "ssid missing");
    copy_string(credential.ssid, sizeof(credential.ssid), ssid);
    copy_string(credential.password, sizeof(credential.password), password != NULL ? password : "");

    esp_err_t ret = connect_with_credential(&credential, timeout_ms, out_status);
    ESP_LOGI(TAG, "connect password ssid=%s connected=%d ret=%s", credential.ssid, s_status.connected ? 1 : 0, esp_err_to_name(ret));
    return ret;
}

esp_err_t ink_wifi_manager_connect_saved(const char *ssid, uint32_t timeout_ms, ink_wifi_status_t *out_status)
{
    ink_wifi_credential_t credential = {0};

    ESP_RETURN_ON_ERROR(ink_wifi_manager_init(), TAG, "wifi init before connect saved");
    ESP_RETURN_ON_ERROR(load_credential_by_ssid(ssid, &credential), TAG, "load saved wifi credential");

    esp_err_t ret = connect_with_credential(&credential, timeout_ms, out_status);
    ESP_LOGI(TAG, "connect saved ssid=%s connected=%d ret=%s", credential.ssid, s_status.connected ? 1 : 0, esp_err_to_name(ret));
    return ret;
}

esp_err_t ink_wifi_manager_connect_best(uint32_t timeout_ms, ink_wifi_status_t *out_status)
{
    nvs_handle_t handle;
    ink_wifi_scan_list_t scan;
    ink_wifi_credential_t best = {0};
    int8_t best_rssi = -127;

    ESP_RETURN_ON_ERROR(ink_wifi_manager_init(), TAG, "wifi init before connect");
    ESP_RETURN_ON_ERROR(ink_wifi_manager_scan(&scan), TAG, "scan before connect");
    ESP_RETURN_ON_ERROR(open_wifi_nvs(NVS_READONLY, &handle), TAG, "open wifi nvs");

    for (uint16_t i = 0; i < scan.count; ++i) {
        for (int slot = 0; slot < INK_WIFI_MAX_CREDENTIALS; ++slot) {
            ink_wifi_credential_t credential;
            if (load_credential_at(handle, slot, &credential) != ESP_OK) {
                continue;
            }
            if (strcmp(credential.ssid, scan.results[i].ssid) == 0 && scan.results[i].rssi > best_rssi) {
                best = credential;
                best_rssi = scan.results[i].rssi;
            }
        }
    }
    nvs_close(handle);

    if (!credential_is_valid(&best)) {
        s_status.connected = false;
        s_status.last_error = ESP_ERR_NOT_FOUND;
        if (out_status != NULL) {
            *out_status = s_status;
        }
        ESP_LOGW(TAG, "no saved wifi found in scan");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_config_t wifi_config = {0};
    copy_string((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), best.ssid);
    copy_string((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), best.password);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(TAG, "wifi disconnect before connect returned %s", esp_err_to_name(ret));
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "wifi connect");

    const EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        s_status.connected = true;
        s_status.last_error = ESP_OK;
        copy_string(s_status.ssid, sizeof(s_status.ssid), best.ssid);
        s_status.rssi = best_rssi;
    } else {
        s_status.connected = false;
        s_status.last_error = (bits & WIFI_FAIL_BIT) != 0 ? ESP_ERR_WIFI_NOT_CONNECT : ESP_ERR_TIMEOUT;
    }

    if (out_status != NULL) {
        *out_status = s_status;
    }
    ESP_LOGI(TAG, "connect best ssid=%s connected=%d ret=%s", best.ssid, s_status.connected ? 1 : 0, esp_err_to_name(s_status.last_error));
    return s_status.last_error;
}

esp_err_t ink_wifi_manager_status(ink_wifi_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status != NULL, ESP_ERR_INVALID_ARG, TAG, "status missing");
    *out_status = s_status;
    return ESP_OK;
}
