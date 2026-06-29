#include "audio_asr_wifi.h"

#include "local_asr_config.h"

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

static const char *TAG = "audio_asr_wifi";

#define AUDIO_ASR_WIFI_CONNECTED_BIT BIT0
#define AUDIO_ASR_WIFI_FAILED_BIT BIT1

static const audio_asr_wifi_config_t s_config = {
    .ssid = AUDIO_ASR_WIFI_SSID,
    .password = AUDIO_ASR_WIFI_PASSWORD,
};

static EventGroupHandle_t s_event_group;
static esp_netif_t *s_sta_netif;
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_wifi_connected;
static esp_ip4_addr_t s_last_ip;
static wifi_err_reason_t s_last_disconnect_reason;

static bool audio_asr_wifi_value_is_placeholder(const char *value)
{
    return value != NULL
        && (strcmp(value, "replace-with-your-ssid") == 0
            || strcmp(value, "replace-with-your-password") == 0);
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    snprintf(dst, dst_size, "%s", src != NULL ? src : "");
}

static void audio_asr_wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = (const wifi_event_sta_disconnected_t *)event_data;
        s_wifi_connected = false;
        s_last_ip.addr = 0U;
        s_last_disconnect_reason = disconnected != NULL ? disconnected->reason : WIFI_REASON_UNSPECIFIED;
        if (s_event_group != NULL) {
            xEventGroupClearBits(s_event_group, AUDIO_ASR_WIFI_CONNECTED_BIT);
            xEventGroupSetBits(s_event_group, AUDIO_ASR_WIFI_FAILED_BIT);
        }
        ESP_LOGW(TAG, "wifi disconnected reason=%u", (unsigned)s_last_disconnect_reason);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = (const ip_event_got_ip_t *)event_data;
        s_wifi_connected = true;
        s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
        if (s_event_group != NULL) {
            xEventGroupClearBits(s_event_group, AUDIO_ASR_WIFI_FAILED_BIT);
            xEventGroupSetBits(s_event_group, AUDIO_ASR_WIFI_CONNECTED_BIT);
        }
        if (got_ip != NULL) {
            s_last_ip = got_ip->ip_info.ip;
            ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&got_ip->ip_info.ip));
        }
    }
}

static esp_err_t audio_asr_wifi_init_once(void)
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
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &audio_asr_wifi_event_handler, NULL), TAG, "register wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &audio_asr_wifi_event_handler, NULL), TAG, "register ip events");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set wifi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set wifi mode");

    s_wifi_initialized = true;
    return ESP_OK;
}

static esp_err_t audio_asr_wifi_start_once(void)
{
    if (s_wifi_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start wifi");
    s_wifi_started = true;
    return ESP_OK;
}

bool audio_asr_wifi_config_valid(const audio_asr_wifi_config_t *config)
{
    return config != NULL
        && config->ssid != NULL
        && config->password != NULL
        && config->ssid[0] != '\0'
        && config->password[0] != '\0'
        && !audio_asr_wifi_value_is_placeholder(config->ssid)
        && !audio_asr_wifi_value_is_placeholder(config->password);
}

const audio_asr_wifi_config_t *audio_asr_wifi_get_config(void)
{
    return &s_config;
}

bool audio_asr_wifi_format_ipv4(uint32_t addr, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return false;
    }

    esp_ip4_addr_t ip = {
        .addr = addr,
    };
    int written = snprintf(buffer, buffer_size, IPSTR, IP2STR(&ip));
    return written > 0 && (size_t)written < buffer_size;
}

bool audio_asr_wifi_get_ipv4_string(char *buffer, size_t buffer_size)
{
    if (!s_wifi_connected || s_last_ip.addr == 0U) {
        return false;
    }
    return audio_asr_wifi_format_ipv4(s_last_ip.addr, buffer, buffer_size);
}

esp_err_t audio_asr_wifi_connect(const audio_asr_wifi_config_t *config, uint32_t timeout_ms)
{
    wifi_ap_record_t ap_info = {0};

    ESP_RETURN_ON_FALSE(audio_asr_wifi_config_valid(config), ESP_ERR_INVALID_ARG, TAG, "wifi config missing");
    ESP_RETURN_ON_ERROR(audio_asr_wifi_init_once(), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(audio_asr_wifi_start_once(), TAG, "wifi start");

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
        s_last_ip.addr = 0U;
    }

    xEventGroupClearBits(s_event_group, AUDIO_ASR_WIFI_CONNECTED_BIT | AUDIO_ASR_WIFI_FAILED_BIT);
    ESP_LOGI(TAG, "connecting to ssid=%s", config->ssid);
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect wifi");

    EventBits_t bits = xEventGroupWaitBits(
        s_event_group,
        AUDIO_ASR_WIFI_CONNECTED_BIT | AUDIO_ASR_WIFI_FAILED_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if ((bits & AUDIO_ASR_WIFI_CONNECTED_BIT) != 0) {
        ESP_LOGI(TAG, "wifi connected ssid=%s", config->ssid);
        return ESP_OK;
    }
    if ((bits & AUDIO_ASR_WIFI_FAILED_BIT) != 0) {
        ESP_LOGW(TAG, "wifi connect failed ssid=%s reason=%u", config->ssid, (unsigned)s_last_disconnect_reason);
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "wifi connect timeout ssid=%s timeout_ms=%lu", config->ssid, (unsigned long)timeout_ms);
    return ESP_ERR_TIMEOUT;
}

esp_err_t audio_asr_wifi_quiet(void)
{
    if (!s_wifi_started) {
        return ESP_OK;
    }

    if (s_wifi_connected) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
        s_wifi_connected = false;
        s_last_ip.addr = 0U;
    }

    if (s_event_group != NULL) {
        xEventGroupClearBits(s_event_group, AUDIO_ASR_WIFI_CONNECTED_BIT | AUDIO_ASR_WIFI_FAILED_BIT);
    }

    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "stop wifi");
    s_wifi_started = false;
    ESP_LOGI(TAG, "wifi quieted for next capture");
    return ESP_OK;
}

bool audio_asr_wifi_self_test(void)
{
    audio_asr_wifi_config_t cfg = {
        .ssid = "test-ssid",
        .password = "secret",
    };
    char ip[16];

    return audio_asr_wifi_config_valid(&cfg)
        && !audio_asr_wifi_config_valid(&(audio_asr_wifi_config_t){0})
        && !audio_asr_wifi_config_valid(&(audio_asr_wifi_config_t){
            .ssid = "x",
            .password = "",
        })
        && audio_asr_wifi_format_ipv4(0x1F03A8C0UL, ip, sizeof(ip))
        && strcmp(ip, "192.168.3.31") == 0
        && audio_asr_wifi_quiet() == ESP_OK;
}
