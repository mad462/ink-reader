#include "audio_record_wifi.h"

bool audio_record_wifi_self_test(void)
{
    audio_record_wifi_config_t cfg = {
        .ssid = "test-ssid",
        .password = "secret",
        .server_base_url = "http://192.168.1.10:8080",
    };

    return audio_record_wifi_config_valid(&cfg)
        && !audio_record_wifi_config_valid(&(audio_record_wifi_config_t){0})
        && !audio_record_wifi_config_valid(&(audio_record_wifi_config_t){
            .ssid = "x",
            .password = "",
            .server_base_url = "http://x",
        })
        && !audio_record_wifi_config_valid(&(audio_record_wifi_config_t){
            .ssid = "x",
            .password = "y",
            .server_base_url = "",
        })
        && audio_record_wifi_quiet() == ESP_OK;
}
