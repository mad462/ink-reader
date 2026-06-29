# Audio Record Web Upload Design

## Goal

Keep `tests/audio_record_probe` as an isolated ESP-IDF recording probe, but switch its export path from UART binary dump to Wi-Fi upload so the latest captured clip can be saved and previewed from a local browser page on the development PC.

This is still a microphone validation tool, not a main firmware feature. The purpose is to hear exactly what the device recorded without UART payload corruption getting mixed into the result.

## Scope

- Reuse the existing standalone project at `tests/audio_record_probe`.
- Keep the existing user interaction:
  - hold `Confirm` to record
  - release `Confirm` to stop
  - auto-stop after `10 seconds`
- Keep the existing fixed recording format:
  - `16000 Hz`
  - `16-bit`
  - `mono`
- Replace UART binary audio export with HTTP upload over Wi-Fi.
- Add a small local Python web server that:
  - accepts uploaded WAV files
  - saves them under `tools/captures/`
  - serves a browser page that shows and plays the latest capture

Out of scope for this change:

- no integration into reader / launcher / runtime / render / input
- no ASR
- no live streaming while the button is held
- no multi-user or internet-facing service
- no authentication
- no file management UI beyond the latest clip view

## Hardware And Network Assumptions

- Target chip remains `ESP32-S3`.
- Microphone wiring remains:
  - `GPIO17` = PDM CLK
  - `GPIO18` = PDM DATA
- Confirm button remains:
  - `GPIO10`, active low
- Local Wi-Fi for development:
  - SSID provided through a local untracked override
  - password provided through a local untracked override

The Wi-Fi credentials must not be committed into tracked defaults or docs. The implementation should allow a local untracked override or a generated local config include so the probe can connect in the developer's environment without storing the secret in repo defaults.

## Device-Side Design

The recording path stays local and memory-backed exactly as the current probe does today:

- allocate the capture buffer in PSRAM when available
- record PCM samples into RAM while `Confirm` is held
- stop on release or duration limit
- compute and log capture metrics after recording

After capture stops, the device should:

1. build a standard WAV file in memory
2. ensure Wi-Fi is connected
3. upload the WAV with `HTTP POST` to the development PC
4. log success or failure clearly
5. return to idle so another capture can be recorded

### WAV Packaging

The device should upload a full WAV file, not raw PCM plus metadata. This keeps the PC service simpler and makes every uploaded artifact directly playable and easy to compare outside the browser.

The WAV file should contain:

- RIFF header
- PCM format chunk
- mono `16 kHz / 16-bit`
- the exact captured byte count from the probe session

### Wi-Fi Handling

The probe should connect to the configured AP during boot or lazily before the first upload. The serial log should show:

- connect start
- connected SSID
- DHCP-acquired IP address
- reconnect attempts when disconnected

If Wi-Fi is unavailable, the probe must not crash. It should keep recording behavior working and report upload failure with a reason.

Important validated behavior from probe testing:

- Leaving Wi-Fi active after an upload can inject a weak but regular `滴滴滴` pattern into the next recording.
- The root cause was RF activity continuing between captures because the probe stayed associated to the AP after upload finished.
- The confirmed mitigation is to explicitly disconnect and stop Wi-Fi after each upload attempt, regardless of upload success, so the next recording starts from a quiet radio state.
- Reconnecting lazily before the next upload is acceptable because this probe optimizes for cleaner capture over minimum export latency.

### HTTP Upload Behavior

The device should send:

- `POST /api/upload`
- content type `audio/wav`
- optional metadata headers or query parameters for sequence, duration, and byte count if convenient

The response only needs to confirm success in JSON. If the upload fails or returns non-`200`, the probe should log the status code or transport error.

### Serial Logging Boundary

Serial remains text-only after this change. No binary audio should be sent over UART anymore. Logs should focus on:

- recording lifecycle
- capture metrics
- Wi-Fi state
- upload start / success / failure
- returned filename and size

This removes the UART payload corruption problem from the audio validation path.

## PC-Side Python Service Design

Add a new local tool:

- `tools/audio_record_web_server.py`

This service should do three jobs:

1. accept uploaded WAV files
2. save them to disk
3. serve a lightweight browser page for immediate playback

### Endpoints

- `GET /`
  - returns a simple HTML page
  - shows service status and latest uploaded capture
- `POST /api/upload`
  - accepts a WAV file body from the ESP32
  - validates basic WAV structure
  - stores it with a timestamped filename such as `capture_2026-06-28_213000.wav`
  - returns JSON including:
    - `ok`
    - `filename`
    - `bytes`
    - `duration_s`
    - `saved_path`
- `GET /api/latest`
  - returns JSON for the latest saved recording
- `GET /captures/<filename>`
  - serves saved WAV files for browser playback

### Browser Page

The page should remain intentionally minimal:

- server online indicator
- latest file name
- latest duration
- saved path
- HTML audio player
- refresh / polling for newest upload

The page should auto-refresh the latest metadata, but first version should not depend on forced autoplay because browser autoplay restrictions may block it. The user should be able to click play immediately when a new clip appears.

## File Layout

Expected new or changed files:

- `tests/audio_record_probe/main/app_main.c`
- optionally:
  - `tests/audio_record_probe/main/audio_record_wifi.c`
  - `tests/audio_record_probe/main/audio_record_wifi.h`
  - `tests/audio_record_probe/main/audio_record_http_upload.c`
  - `tests/audio_record_probe/main/audio_record_http_upload.h`
  - `tests/audio_record_probe/main/audio_record_wav.c`
  - `tests/audio_record_probe/main/audio_record_wav.h`
- `tools/audio_record_web_server.py`
- optional static assets under `tools/audio_record_web/`
- `tests/audio_record_probe/README.md`

## Failure Handling

The probe and service must fail clearly and locally:

- bad Wi-Fi credentials: serial log explains connect failure
- PC server unreachable: serial log shows socket / HTTP failure
- malformed WAV upload: server returns explicit error
- empty or very short capture: still generate a valid small WAV and accept it
- no PSRAM: continue using current fallback / warning behavior

## Validation Plan

Minimum validation for this design:

1. build the probe successfully under `C:\esp\v5.5.4\esp-idf`
2. start the Python web server locally on the development PC
3. flash the probe and confirm it connects to the configured local SSID
4. hold `Confirm` to record and release to upload
5. verify the server saves a WAV file under `tools/captures/`
6. open the browser page and confirm the latest clip is visible and playable
7. verify no UART binary payload is emitted anymore
8. verify short captures still upload and play
9. verify repeated recordings do not reintroduce the previously observed weak periodic `滴滴滴` interference after uploads

## Why This Design

This design removes the known UART export corruption path from the loop while keeping the recording probe isolated and easy to reason about. It does not assume the microphone path is already correct; instead, it gives a cleaner transport and playback route so future audio quality debugging reflects the device capture more faithfully.

Follow-up note from real-device validation:

- The initial Wi-Fi upload implementation solved UART corruption, but introduced a new acoustic artifact: a regular weak periodic tone after earlier exports.
- That artifact was not caused by browser playback, WAV packaging, or ASR.
- It was eliminated by quieting Wi-Fi immediately after upload so subsequent captures happen with the radio stopped.
