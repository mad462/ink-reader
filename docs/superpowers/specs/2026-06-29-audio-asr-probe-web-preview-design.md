# Audio ASR Probe Web Preview Design

## Goal

Extend `tests/audio_asr_probe` with a minimal board-hosted web page so the latest capture can be previewed without relying on the serial monitor.

The page should let a tester:

- see the current device state
- see the latest ASR result text
- see the latest error, if any
- play only the most recent recording

This is a probe-only convenience feature. It is not a mainline product UI.

## Scope

### In scope

- Keep the existing `Confirm`-button recording flow unchanged
- Device remains on existing STA Wi-Fi mode and is accessed from the same LAN
- Add a tiny HTTP server hosted on the ESP32-S3
- Expose one minimal HTML page for the latest result
- Expose one JSON API for current status plus latest result metadata
- Expose one WAV endpoint for the latest recording
- Update state so testers can observe progress without opening serial

### Out of scope

- No web-triggered recording
- No recording history list
- No persistence to flash, TF card, or filesystem
- No authentication
- No waveform visualization
- No integration into the main firmware app architecture

## Why This Fits The Probe

`audio_asr_probe` already proves:

- on-device recording
- on-device Wi-Fi connect
- on-device ASR request
- on-device transcript return

What is still inconvenient for future audio testing is listening back to the captured audio. A board-hosted preview page solves that without bringing back the earlier PC relay workflow.

## Chosen Approach

Use a board-hosted HTTP server and keep the latest WAV entirely in memory.

### Why this approach

- simplest mental model
- easiest to test from phone or PC browser
- no extra computer-side helper process
- no need to add storage dependencies
- does not change the proven button-driven recording flow

### Why not the alternatives

- Dynamic WAV generation from PCM is possible, but it adds response-path complexity for little gain
- Reusing the old PC web server would solve playback, but it reintroduces a host dependency that this probe specifically removed

## User Flow

1. Device boots and joins the configured Wi-Fi only when needed for ASR, same as today
2. Tester opens the board web page from another device on the same LAN
3. Tester presses and holds `Confirm` on the device to record
4. Tester releases `Confirm`
5. Device:
   - updates web-visible state to indicate progress
   - connects Wi-Fi if needed
   - sends audio to ASR
   - stores the latest WAV in memory
   - updates transcript or error state
6. The browser page refreshes status automatically and allows playback of the latest WAV

## Networking Model

The board continues to use STA mode on the existing Wi-Fi network.

The page is accessed through the board IP, for example:

- `http://192.168.x.x/`

The board should print its current IP in serial logs when the HTTP server becomes reachable or when Wi-Fi reconnects.

The server should remain lightweight and single-purpose. It only needs to serve the latest test result.

## HTTP Surface

### `GET /`

Returns a minimal HTML page with:

- current status text
- latest transcript text
- latest error text
- latest duration
- latest capture size
- an `<audio controls>` player pointed at the latest WAV endpoint

The page should poll the JSON endpoint periodically, for example once per second.

### `GET /api/latest`

Returns JSON such as:

```json
{
  "status": "done",
  "sequence": 2,
  "duration_ms": 6464,
  "pcm_bytes": 206848,
  "wav_bytes": 206892,
  "wifi_elapsed_ms": 1258,
  "request_elapsed_ms": 3734,
  "transcript_text": "打开图书与最近阅读，连接网络与输入密码。",
  "error_text": null,
  "audio_url": "/audio/latest.wav",
  "has_audio": true
}
```

### `GET /audio/latest.wav`

Returns the most recent WAV in memory with:

- `Content-Type: audio/wav`
- exact content length

If no audio exists yet, return `404`.

## In-Memory Data Model

Keep one shared “latest preview” structure in RAM containing:

- current status enum
- sequence number
- latest transcript text
- latest error text
- latest timing metrics
- latest PCM byte count
- latest WAV byte count
- pointer to the latest WAV buffer

Only the latest result is retained.

When a new capture finishes successfully, replace the previous WAV buffer with the new one.

When a capture fails after recording but before ASR completes, keep the newest WAV if available so the tester can still listen to what was captured.

## State Model

Recommended web-visible states:

- `idle`
- `recording`
- `wifi_connecting`
- `asr_requesting`
- `done`
- `error`

These states mirror the current serial progress well enough for debugging without exposing internal complexity.

## Memory Strategy

The latest WAV is retained in memory after each capture.

Implications:

- a 10-second 16 kHz 16-bit mono WAV is about 320 KB plus header
- this is acceptable for the probe when PSRAM is available
- if PSRAM is unavailable, the probe should still compile, but runtime behavior must clearly report that preview retention may fail

Recommended allocation policy:

- prefer PSRAM for the retained latest WAV
- fall back to internal RAM only if practical
- if retention allocation fails, keep ASR working and expose `has_audio=false`

This keeps playback as a convenience feature instead of turning it into a hard dependency.

## Wi-Fi Interaction

This preview feature must not change the already-validated recording principle:

- keep Wi-Fi quiet during recording

The HTTP server lives conceptually on the board, but the network path is only available when Wi-Fi is active. For this probe extension, correctness and audio cleanliness still win over perfect page reachability.

That means:

- the page is most useful before and after recording
- during a full Wi-Fi stop, the board page may temporarily become unreachable
- after reconnect for ASR, the page becomes reachable again and can present the latest result

This is acceptable for the probe.

Future mainline work can solve continuous availability via a centralized Wi-Fi manager, but that is explicitly outside this probe scope.

## Logging

Serial logs should remain, but the page should no longer require them.

Add clear logs for:

- HTTP server started
- current board IP
- preview WAV retained or retention failed
- latest web status transitions

## Error Handling

The page should present readable error states for at least:

- no audio yet
- Wi-Fi failed
- ASR request failed
- JSON parse failed
- preview buffer allocation failed

If a failure occurs after a recording was captured, the page should still expose the latest available audio when possible.

## Testing

Minimum verification:

1. Open the page from another device on the same LAN
2. Record once and confirm the page eventually shows transcript text
3. Confirm the latest WAV plays in the browser
4. Record again and confirm only the most recent recording is served
5. Confirm no serial monitor is required to understand success or failure
6. Confirm behavior is still safe when PSRAM is absent or preview retention fails

## Risks

### 1. Memory pressure

Retaining the latest WAV adds a persistent RAM cost on top of the ASR request-body path.

Mitigation:

- keep only one recording
- prefer PSRAM
- degrade gracefully if preview retention fails

### 2. Temporary page unreachability

Because Wi-Fi is intentionally quieted outside the network phase, the page is not a continuously available product surface.

Mitigation:

- document this clearly as probe behavior
- keep status and latest result visible once Wi-Fi comes back

### 3. Accidental scope creep into product UI

It would be easy to turn this into a larger web control surface.

Mitigation:

- keep the page read-only
- keep only one latest audio
- avoid introducing remote control actions

## Recommendation

Implement this as a small extension to `tests/audio_asr_probe`, not as a new separate project.

That preserves the already-working ASR chain and adds a practical playback/debug surface with limited new complexity.
