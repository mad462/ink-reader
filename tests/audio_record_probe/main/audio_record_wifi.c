#include "audio_record_wifi.h"

#include "local_wifi_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *TAG = "audio_record_wifi";

#define AUDIO_RECORD_WIFI_CONNECTED_BIT BIT0
#define AUDIO_RECORD_WIFI_FAILED_BIT BIT1

static const audio_record_wifi_config_t s_config = {
    .ssid = AUDIO_RECORD_WIFI_SSID,
    .password = AUDIO_RECORD_WIFI_PASSWORD,
    .server_base_url = AUDIO_RECORD_SERVER_BASE_URL,
};

static EventGroupHandle_t s_event_group;
static esp_netif_t *s_sta_netif;
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_wifi_connected;

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    snprintf(dst, dst_size, "%s", src != NULL ? src : "");
}

static void audio_record_wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        if (s_event_group != NULL) {
            xEventGroupClearBits(s_event_group, AUDIO_RECORD_WIFI_CONNECTED_BIT);
            xEventGroupSetBits(s_event_group, AUDIO_RECORD_WIFI_FAILED_BIT);
        }
        ESP_LOGW(TAG, "wifi disconnected");
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = event_data;
        s_wifi_connected = true;
        if (s_event_group != NULL) {
            xEventGroupClearBits(s_event_group, AUDIO_RECORD_WIFI_FAILED_BIT);
            xEventGroupSetBits(s_event_group, AUDIO_RECORD_WIFI_CONNECTED_BIT);
        }
        if (got_ip != NULL) {
            ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&got_ip->ip_info.ip));
        }
    }
}

static esp_err_t audio_record_wifi_init_once(void)
{
    if (s_wifi_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase nvs");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "init nvs");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "init netif");
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "create event loop");
    }

    s_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_event_group != NULL, ESP_ERR_NO_MEM, TAG, "alloc wifi event group");

    s_sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_sta_netif != NULL, ESP_ERR_NO_MEM, TAG, "create sta netif");

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "init wifi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &audio_record_wifi_event_handler, NULL), TAG, "register wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &audio_record_wifi_event_handler, NULL), TAG, "register ip events");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set wifi mode");

    s_wifi_initialized = true;
    return ESP_OK;
}

static esp_err_t audio_record_wifi_start_once(void)
{
    if (s_wifi_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start wifi");
    s_wifi_started = true;
    return ESP_OK;
}

bool audio_record_wifi_config_valid(const audio_record_wifi_config_t *config)
{
    return config != NULL
        && config->ssid != NULL
        && config->password != NULL
        && config->server_base_url != NULL
        && config->ssid[0] != '\0'
        && config->password[0] != '\0'
        && config->server_base_url[0] != '\0';
}

const audio_record_wifi_config_t *audio_record_wifi_get_config(void)
{
    return &s_config;
}

esp_err_t audio_record_wifi_connect(const audio_record_wifi_config_t *config, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(audio_record_wifi_config_valid(config), ESP_ERR_INVALID_ARG, TAG, "wifi config missing");
    ESP_RETURN_ON_ERROR(audio_record_wifi_init_once(), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(audio_record_wifi_start_once(), TAG, "wifi start");

    wifi_ap_record_t ap_info = {0};
    if (s_wifi_connected && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK && strcmp((const char *)ap_info.ssid, config->ssid) == 0) {
        return ESP_OK;
    }

    wifi_config_t wifi_config = {0};
    copy_string((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), config->ssid);
    copy_string((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), config->password);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    if (s_wifi_connected) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
        s_wifi_connected = false;
    }

    xEventGroupClearBits(s_event_group, AUDIO_RECORD_WIFI_CONNECTED_BIT | AUDIO_RECORD_WIFI_FAILED_BIT);
    ESP_LOGI(TAG, "connecting to ssid=%s", config->ssid);
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect wifi");

    EventBits_t bits = xEventGroupWaitBits(
        s_event_group,
        AUDIO_RECORD_WIFI_CONNECTED_BIT | AUDIO_RECORD_WIFI_FAILED_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if ((bits & AUDIO_RECORD_WIFI_CONNECTED_BIT) != 0) {
        ESP_LOGI(TAG, "wifi connected ssid=%s", config->ssid);
        return ESP_OK;
    }
    if ((bits & AUDIO_RECORD_WIFI_FAILED_BIT) != 0) {
        ESP_LOGW(TAG, "wifi connect failed ssid=%s", config->ssid);
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "wifi connect timeout ssid=%s timeout_ms=%lu", config->ssid, (unsigned long)timeout_ms);
    return ESP_ERR_TIMEOUT;
}

esp_err_t audio_record_wifi_quiet(void)
{
    if (!s_wifi_started) {
        return ESP_OK;
    }

    if (s_wifi_connected) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
        s_wifi_connected = false;
    }

    if (s_event_group != NULL) {
        xEventGroupClearBits(s_event_group, AUDIO_RECORD_WIFI_CONNECTED_BIT | AUDIO_RECORD_WIFI_FAILED_BIT);
    }

    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "stop wifi");
    s_wifi_started = false;
    ESP_LOGI(TAG, "wifi quieted for next capture");
    return ESP_OK;
}
