# ink-reader audio ASR probe

Standalone ESP-IDF app for validating the full device-side chain:

- hold `Confirm` to record
- release to stop
- device connects to Wi-Fi after recording
- device sends WAV directly to Alibaba `qwen3-asr-flash`
- transcript or error is printed to serial
- board hosts a tiny LAN page for latest status, transcript, and audio preview

This probe is intentionally separate from:

- `tests/audio_record_probe`
- the main firmware app architecture

## Scope

V1 only validates:

- recording
- device-direct ASR
- serial result output
- board-hosted latest-result preview page

V1 does not include:

- on-device app UI
- tags
- persistence

## GPIO map

| Device | Signal | GPIO |
| --- | --- | --- |
| LMD2718 mic | PDM CLK | GPIO17 |
| LMD2718 mic | PDM DATA | GPIO18 |
| Confirm button | Active low input | GPIO10 |

## Local config

1. Copy `tests/audio_asr_probe/local_asr_config.example.h` to `tests/audio_asr_probe/main/local_asr_config_override.h`
2. Fill in local Wi-Fi and Alibaba ASR values
3. Keep that override file untracked

## Build

```powershell
cd D:\FUCKIDF\ink-reader\tests\audio_asr_probe
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py set-target esp32s3
idf.py build
```

## Flash and monitor

```powershell
idf.py -p COM9 flash monitor
```

## Web preview

After one successful Wi-Fi connection, the probe logs:

```text
web preview url: http://<board-ip>/
```

Open that address from a phone or PC on the same LAN to view:

- current state
- latest transcript
- latest error
- latest timing and byte counts
- an audio player for only the most recent recording

The page is read-only. Recording still starts and stops only from the device `Confirm` button.

## Local override example

Create `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\local_asr_config_override.h`:

```c
#pragma once

#define AUDIO_ASR_WIFI_SSID "replace-with-your-ssid"
#define AUDIO_ASR_WIFI_PASSWORD "replace-with-your-password"
#define AUDIO_ASR_API_KEY "replace-with-your-dashscope-api-key"
```

## Expected V1 serial flow

- `AUDIO_ASR_EVENT pressed`
- `AUDIO_ASR_EVENT released`
- `AUDIO_ASR_STATUS recording_stopped bytes=... duration_ms=...`
- `AUDIO_ASR_STATUS wav_ready pcm_bytes=... wav_bytes=... base64_bytes=... body_estimate=...`
- `AUDIO_ASR_STATUS wifi_connecting`
- `AUDIO_ASR_STATUS wifi_connected ...`
- `AUDIO_ASR_STATUS asr_request_started ...`
- `AUDIO_ASR_STATUS asr_request_done ...`
- `AUDIO_ASR_RESULT text=...`
- or `AUDIO_ASR_ERROR reason=...`

## Expected web flow

1. Power on the board and open serial once so you can see the board IP after the first upload
2. Hold `Confirm` to record
3. Release `Confirm`
4. Wait for `wifi_connected` and `asr_request_done`
5. Open `http://<board-ip>/`
6. Check the latest transcript and press play for the latest WAV

Each new recording replaces the previous browser preview audio.

## Known V1 limits

- no persistence
- Wi-Fi is intentionally quieted after each request
- optimized for correctness, not minimum latency
- request body is built fully in memory, so PSRAM is strongly recommended
- the page may be temporarily unreachable while Wi-Fi is quiet
- only the latest recording is retained for preview
- preview audio retention prefers PSRAM and may fall back to internal RAM
- if preview WAV retention fails, ASR can still succeed but the page will show no playable audio
