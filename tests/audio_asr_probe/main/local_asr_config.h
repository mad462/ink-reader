#pragma once

#include "../local_asr_config.example.h"

#define AUDIO_ASR_LOCAL_OVERRIDE_PRESENT 0

#if __has_include("local_asr_config_override.h")
#ifdef AUDIO_ASR_WIFI_SSID
#undef AUDIO_ASR_WIFI_SSID
#endif
#ifdef AUDIO_ASR_WIFI_PASSWORD
#undef AUDIO_ASR_WIFI_PASSWORD
#endif
#ifdef AUDIO_ASR_API_KEY
#undef AUDIO_ASR_API_KEY
#endif
#ifdef AUDIO_ASR_BASE_URL
#undef AUDIO_ASR_BASE_URL
#endif
#ifdef AUDIO_ASR_MODEL
#undef AUDIO_ASR_MODEL
#endif
#undef AUDIO_ASR_LOCAL_OVERRIDE_PRESENT
#define AUDIO_ASR_LOCAL_OVERRIDE_PRESENT 1
#include "local_asr_config_override.h"
#endif
