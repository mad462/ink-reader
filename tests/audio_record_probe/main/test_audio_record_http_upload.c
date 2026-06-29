#include "audio_record_http_upload.h"

#include <string.h>

bool audio_record_http_upload_self_test(void)
{
    char url[128];
    if (!audio_record_http_upload_build_url("http://127.0.0.1:8080", url, sizeof(url))) {
        return false;
    }

    return strcmp(url, "http://127.0.0.1:8080/api/upload") == 0
        && audio_record_http_upload_build_url("http://127.0.0.1:8080/", url, sizeof(url))
        && strcmp(url, "http://127.0.0.1:8080/api/upload") == 0
        && audio_record_http_upload_fetch_headers_ok(0)
        && audio_record_http_upload_fetch_headers_ok(128)
        && !audio_record_http_upload_fetch_headers_ok(-1)
        && !audio_record_http_upload_build_url("", url, sizeof(url))
        && !audio_record_http_upload_build_url("http://x", url, 8U);
}
