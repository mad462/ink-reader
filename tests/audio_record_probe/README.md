# ink-reader audio record probe

Standalone ESP-IDF app for validating the `LMD2718` microphone recording path without entering the main reader firmware app stack. This version records on the device, wraps the capture as WAV, uploads it to a local Python web server, and lets you play the latest file from a browser.

## GPIO map

| Device | Signal | GPIO |
| --- | --- | --- |
| LMD2718 mic | PDM CLK | GPIO17 |
| LMD2718 mic | PDM DATA | GPIO18 |
| Confirm button | Active low input | GPIO10 |

## Audio format

- `16000 Hz`
- `16-bit`
- `mono`
- max capture length `10 seconds`

## Local Wi-Fi config

Do not commit your real Wi-Fi credentials.

Tracked files:

- template: `tests/audio_record_probe/local_wifi_config.example.h`
- default include shim: `tests/audio_record_probe/main/local_wifi_config.h`

Local machine override:

1. Copy `tests/audio_record_probe/local_wifi_config.example.h` to `tests/audio_record_probe/main/local_wifi_config_override.h`
2. Fill in:
   - `AUDIO_RECORD_WIFI_SSID`
   - `AUDIO_RECORD_WIFI_PASSWORD`
   - `AUDIO_RECORD_SERVER_BASE_URL`
3. Keep that override file untracked

Notes:

- `AUDIO_RECORD_SERVER_BASE_URL` should point to your PC, for example `http://192.168.31.249:8080`
- ESP32-S3 needs a `2.4GHz` AP. If your PC is only on `5GHz`, the board will not connect.

## Build

```powershell
cd D:\FUCKIDF\ink-reader\tests\audio_record_probe
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

## Start the local web server

One-shot foreground run:

```powershell
python D:\FUCKIDF\ink-reader\tools\audio_record_web_server.py --host 0.0.0.0 --port 8080 --captures-dir D:\FUCKIDF\ink-reader\tools\captures
```

One-click background start:

```powershell
powershell -ExecutionPolicy Bypass -File D:\FUCKIDF\ink-reader\tools\start_audio_record_web_server.ps1
```

Stop the background server:

```powershell
powershell -ExecutionPolicy Bypass -File D:\FUCKIDF\ink-reader\tools\stop_audio_record_web_server.ps1
```

Then open:

```text
http://127.0.0.1:8080/
```

## Optional ASR setup

The local web server can send uploaded WAV files to Alibaba Model Studio `qwen3-asr-flash` and show recognized text on the page.

Set these environment variables before starting the web server:

```powershell
$env:DASHSCOPE_API_KEY = "your-api-key"
$env:ASR_BASE_URL = "https://your-workspace.cn-beijing.maas.aliyuncs.com/compatible-mode/v1"
$env:ASR_MODEL = "qwen3-asr-flash"
```

Notes:

- `DASHSCOPE_API_KEY` is required for ASR
- `ASR_BASE_URL` is optional if you want to override the default Beijing OpenAI-compatible endpoint
- `ASR_MODEL` is optional and defaults to `qwen3-asr-flash`
- keep the API key out of tracked files

## Expected flow

1. Start `audio_record_web_server.py`
2. Open `http://127.0.0.1:8080/`
3. Power the board and watch serial logs
4. Hold `Confirm` to start recording
5. Release `Confirm` to stop, or keep holding until the `10 second` limit auto-stops
6. The board connects to Wi-Fi if needed and uploads the WAV to `/api/upload`
7. The browser page shows the latest capture and plays an auto-boosted preview WAV by default so very quiet recordings are easier to hear
8. If `DASHSCOPE_API_KEY` is set, the page also shows:
   - `ASR status`
   - recognized transcript text
   - detected language and emotion when available

## Serial log expectations

Device logs remain text-only and now focus on:

- `pressed`
- `recording elapsed_ms=... remaining_ms=... bytes=...`
- `released`
- `uploading bytes=... duration_ms=...`
- `upload_done ...` or `upload_failed ...`

## Known limits

- max capture length is `10 seconds`
- fixed format `16 kHz / 16-bit / mono`
- the browser preview uses auto gain for easier listening, but the raw saved WAV is kept unchanged
- ASR runs on the PC-side Python web server, not on the ESP32
- ASR depends on network access from the PC to Alibaba Model Studio
- no live streaming
- no playback on device
- no TF card save
- no UI integration with the main firmware
