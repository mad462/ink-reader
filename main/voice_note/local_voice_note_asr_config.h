#pragma once

#include "local_voice_note_asr_config.example.h"

#define VOICE_NOTE_ASR_LOCAL_OVERRIDE_PRESENT 0

#if __has_include("local_voice_note_asr_config_override.h")
#ifdef VOICE_NOTE_ASR_API_KEY
#undef VOICE_NOTE_ASR_API_KEY
#endif
#ifdef VOICE_NOTE_ASR_BASE_URL
#undef VOICE_NOTE_ASR_BASE_URL
#endif
#ifdef VOICE_NOTE_ASR_MODEL
#undef VOICE_NOTE_ASR_MODEL
#endif
#undef VOICE_NOTE_ASR_LOCAL_OVERRIDE_PRESENT
#define VOICE_NOTE_ASR_LOCAL_OVERRIDE_PRESENT 1
#include "local_voice_note_asr_config_override.h"
#endif
