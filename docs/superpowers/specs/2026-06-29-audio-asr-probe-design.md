# Audio ASR Probe Design

## Goal

Create a new standalone ESP-IDF test project that preserves the existing `audio_record_probe` flow and validates a different risk boundary:

- record on device
- send audio directly from ESP32-S3 to Alibaba Model Studio `qwen3-asr-flash`
- receive the recognition result on device
- print status and transcript to serial

This probe exists to prove the device-side end-to-end ASR chain before integrating anything into the main firmware app architecture.

## Why A Separate Probe

The current `tests/audio_record_probe` project is now a useful and stable reference for:

- microphone quality
- Wi-Fi upload to local PC
- browser playback
- PC-side ASR

That probe should stay intact.

`tests/audio_asr_probe` will be a separate project because device-direct ASR introduces different risks:

- ESP32 memory pressure from base64 and JSON request bodies
- TLS / HTTPS handling directly on device
- larger request and response parsing complexity
- device-side timeout and failure classification

Keeping these concerns separate avoids destabilizing the already-working recording probe.

## Scope

### In scope

- New standalone test project: `tests/audio_asr_probe`
- Hold `Confirm` to record
- Release `Confirm` to stop
- Build a WAV on device
- Connect to Wi-Fi only after recording ends
- Send the WAV directly to Alibaba OpenAI-compatible ASR endpoint
- Parse returned transcript on device
- Print device status and transcript to serial
- Quiet Wi-Fi after request completes so the next recording starts from a quiet radio state

### Out of scope

- Main firmware app integration
- Launcher/runtime/render/input integration
- Web page or PC relay service
- Voice tag data model
- Done/undone state
- Delete/rewrite actions
- Persistent storage
- Screen UI or popup UI
- On-device transcript list
- Title summarization

## Hardware And Input Assumptions

- Target chip: `ESP32-S3`
- Confirm button: `GPIO10`, active low
- Microphone:
  - `GPIO17` = PDM CLK
  - `GPIO18` = PDM DATA
- Recording format remains fixed:
  - `16000 Hz`
  - `16-bit`
  - `mono`
- Maximum capture length remains `10 seconds`

## V1 Principle

V1 optimizes for correctness and clean capture, not minimum latency.

That means:

- Wi-Fi stays quiet during recording
- after recording stops, Wi-Fi is started and connected
- ASR request is sent only after the WAV is ready
- once the request finishes, Wi-Fi is quieted again

This is deliberate because real-device validation already proved:

- if Wi-Fi remains active across captures, a weak periodic `滴滴滴` interference can appear in subsequent recordings
- the confirmed mitigation is to disconnect and stop Wi-Fi after each upload/request cycle

So V1 should prefer a clean recording path over faster reconnect behavior.

## Project Layout

Recommended structure:

- `tests/audio_asr_probe/`
  - `CMakeLists.txt`
  - `sdkconfig.defaults`
  - `README.md`
  - `local_asr_config.example.h`
  - `main/`
    - `CMakeLists.txt`
    - `app_main.c`
    - `audio_asr_wifi.c`
    - `audio_asr_wifi.h`
    - `audio_asr_http.c`
    - `audio_asr_http.h`
    - `audio_asr_json.c`
    - `audio_asr_json.h`
    - `audio_asr_wav.c`
    - `audio_asr_wav.h`
    - `audio_asr_probe_logic.c`
    - `audio_asr_probe_logic.h`

Local secret override should remain untracked, similar to the existing audio probe approach.

## Device-Side Flow

### Idle

Wait for `Confirm` press.

### Recording

When pressed:

- enable PDM RX
- record into PSRAM when available
- keep Wi-Fi quiet during the whole recording period
- do not emit periodic progress serial events that could reintroduce acoustic interference

When released or max duration reached:

- disable PDM RX
- compute capture metrics
- wrap the captured PCM as WAV

### ASR phase

After WAV is ready:

1. start/connect Wi-Fi
2. build OpenAI-compatible ASR request body
3. send HTTPS request to Alibaba
4. parse transcript result or error
5. print result to serial
6. quiet Wi-Fi again
7. return to idle

## Alibaba Request Shape

The request should match the already-validated PC-side format:

- `POST {ASR_BASE_URL}/chat/completions`
- `Authorization: Bearer <DASHSCOPE_API_KEY>`
- `Content-Type: application/json`

Request body:

```json
{
  "model": "qwen3-asr-flash",
  "messages": [
    {
      "role": "user",
      "content": [
        {
          "type": "input_audio",
          "input_audio": {
            "data": "data:audio/wav;base64,..."
          }
        }
      ]
    }
  ],
  "stream": false,
  "asr_options": {
    "enable_itn": false
  }
}
```

V1 only needs:

- transcript text from `choices[0].message.content`
- optional language/emotion can be ignored for the first pass unless parsing is trivial

## Local Configuration

Like the existing probe, credentials must not be committed to tracked source.

Required local config items:

- Wi-Fi SSID
- Wi-Fi password
- `DASHSCOPE_API_KEY`
- `ASR_BASE_URL`
  - default should be the user-provided Beijing OpenAI-compatible endpoint
- `ASR_MODEL`
  - default `qwen3-asr-flash`

## Serial Output Contract

V1 uses serial only. No screen UI is required yet.

Recommended serial messages:

- `AUDIO_ASR_EVENT pressed`
- `AUDIO_ASR_EVENT released`
- `AUDIO_ASR_STATUS recording_stopped bytes=... duration_ms=...`
- `AUDIO_ASR_STATUS wav_ready wav_bytes=...`
- `AUDIO_ASR_STATUS wifi_connecting`
- `AUDIO_ASR_STATUS wifi_connected elapsed_ms=...`
- `AUDIO_ASR_STATUS asr_request_started body_bytes=...`
- `AUDIO_ASR_STATUS asr_request_done elapsed_ms=...`
- `AUDIO_ASR_RESULT text=...`
- `AUDIO_ASR_ERROR reason=...`

These logs are meant to show exactly where failures occur without adding periodic noise during recording.

## Biggest Risks To Validate

### 1. Request size and memory pressure

The WAV must be converted to base64 and placed inside JSON.

V1 must explicitly log:

- raw WAV size
- estimated base64 size
- final JSON request size

This is the most important device-side resource risk.

### 2. End-to-end latency

V1 must log:

- Wi-Fi connect time
- HTTP/ASR request time
- total stop-to-transcript time

This establishes the baseline before any optimization work.

### 3. Failure classification

V1 must distinguish at least:

- `wifi_failed`
- `tls_failed`
- `http_failed`
- `json_parse_failed`
- `asr_empty`

This is necessary for future “rewrite/retry” behavior in the eventual voice-note app.

### 4. UTF-8 transcript handling

V1 must prove the device can safely print returned Chinese text without corrupting UTF-8 boundaries.

### 5. Repeat capture stability

At least two consecutive recordings must work:

- both can be recognized
- periodic `滴滴滴` interference does not return
- Wi-Fi quieting remains effective

### 6. Very short audio behavior

Short or nearly empty captures must fail clearly without crashing.

## Validation Plan

Minimum validation for V1:

1. build under `C:\esp\v5.5.4\esp-idf`
2. flash the new probe
3. press to record, release to stop
4. confirm Wi-Fi only becomes active after recording
5. confirm serial shows request body size and timing
6. confirm Alibaba transcript text is printed when successful
7. confirm readable error is printed when unsuccessful
8. confirm two consecutive recordings do not reintroduce periodic `滴滴滴` interference

## Future Follow-Up

Once `audio_asr_probe` is stable, the next stage can split into two separate tracks:

1. performance work
   - faster Wi-Fi resume/reconnect strategy
   - body size reduction or chunking strategy if needed

2. product integration work
   - move ASR logic into a proper voice-note app
   - introduce note creation, status, delete, rewrite, summary title, and persistence

That future app work should only start after `audio_asr_probe` proves the device-direct ASR path is actually viable.
