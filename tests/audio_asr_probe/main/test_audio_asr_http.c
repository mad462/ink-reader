#include "audio_asr_http.h"
#include "audio_asr_json.h"

bool test_audio_asr_http_self_test(void)
{
    return audio_asr_http_self_test() && audio_asr_json_self_test();
}
