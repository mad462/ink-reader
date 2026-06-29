#include "audio_asr_wifi.h"

#include <string.h>

bool test_audio_asr_wifi_self_test(void)
{
    char ip[16];

    return audio_asr_wifi_self_test()
        && audio_asr_wifi_format_ipv4(0x1F03A8C0UL, ip, sizeof(ip))
        && strcmp(ip, "192.168.3.31") == 0;
}
