# Audio Record Probe ASR Design

## Goal

Extend the existing standalone audio probe so that after the device uploads a WAV file to the local Python web server, the server submits that audio to Alibaba Cloud Model Studio `qwen3-asr-flash`, receives the transcription result, and shows the recognized text on the local web page.

This change stays inside the existing probe path:

- device records WAV
- device uploads to local Python server
- local Python server stores audio
- local Python server calls Alibaba ASR
- browser page shows playback and transcript

No main firmware app integration, no on-device ASR, and no browser-side direct access to Alibaba credentials.

## Confirmed External API Shape

Based on Alibaba Cloud official documentation:

- The Beijing OpenAI-compatible base URL follows `https://{workspace-id}.cn-beijing.maas.aliyuncs.com/compatible-mode/v1`.
- The HTTP endpoint for OpenAI-compatible requests is `POST .../chat/completions`.
- `qwen3-asr-flash` accepts audio through `messages[].content[]` using an `input_audio` item.
- Local files can be sent as a Data URL in the form `data:audio/wav;base64,...`.
- Optional ASR controls are sent through `asr_options`; when using the OpenAI SDK they go through `extra_body`, but for our raw HTTP implementation they can live directly in the JSON body.
- Non-streaming response text comes from `choices[0].message.content`.
- Output metadata such as language and emotion is carried in `choices[0].message.annotations[]`.

For this probe we will use non-streaming mode because:

- the uploaded recordings are short
- the server already polls `GET /api/latest`
- simpler state handling is better for a local hardware test loop

## Scope

### In scope

- Add server-side ASR request support to `tools/audio_record_web_server.py`
- Read ASR credentials from local environment variables
- Kick off transcription after each successful upload
- Expose transcription status and result through `GET /api/latest`
- Render transcription state and text on the local web page
- Persist ASR result next to the WAV so restart recovery still works
- Add focused tests for success, disabled state, and error handling
- Update the probe README with ASR setup and usage

### Out of scope

- Device-side protocol changes
- Device-side streaming ASR
- Main app integration
- Speaker diarization, subtitle timing, long-audio async task APIs
- Remote/public deployment hardening
- Rich transcript history UI

## Audio Capture Constraint Carried Forward

The ASR layer depends on the existing probe producing a clean-enough recording. Real-device validation established an important device-side constraint:

- If Wi-Fi remains active after upload, the next recording can pick up a weak periodic `滴滴滴` interference pattern.
- That pattern is caused by RF activity from the ESP32 staying connected after export.
- The confirmed device-side mitigation is to disconnect and stop Wi-Fi immediately after each upload attempt so every new recording begins from a quiet radio state.

This ASR design assumes that mitigation remains in place. ASR itself did not create the interference; it only made the upload path stay in active use long enough for the underlying Wi-Fi-related artifact to reappear.

## Recommended Approach

Use server-side asynchronous transcription inside the local Python web server.

Why this is the best fit:

- keeps the ESP32 upload contract unchanged
- avoids exposing the Alibaba API key to the browser
- avoids blocking `/api/upload` on remote ASR latency
- fits the current page polling model
- keeps the implementation local to one Python service

## Configuration

The server will read these environment variables:

- `DASHSCOPE_API_KEY`
  - required to enable ASR
- `ASR_BASE_URL`
  - optional
  - default: the user-provided OpenAI-compatible Beijing endpoint
- `ASR_MODEL`
  - optional
  - default: `qwen3-asr-flash`
- `ASR_LANGUAGE`
  - optional
  - if set, sent as `asr_options.language`
  - recommended unset unless the user explicitly wants forced language
- `ASR_ENABLE_ITN`
  - optional
  - default: `false`

If `DASHSCOPE_API_KEY` is missing, the page should clearly show that ASR is disabled, while upload and playback continue to work.

## Server Design

### Existing upload path

Current flow:

1. `POST /api/upload`
2. Validate WAV body
3. Save raw WAV
4. Save preview WAV
5. Update latest metadata
6. Return JSON to client

### New upload path

Updated flow:

1. `POST /api/upload`
2. Validate WAV body
3. Save raw WAV
4. Save preview WAV
5. Create initial ASR metadata:
   - `asr_status = "pending"` if ASR is enabled and audio is non-empty
   - `asr_status = "disabled"` if API key is missing
   - `asr_status = "empty_audio"` if WAV has zero frames
6. Return upload success immediately
7. Start a background worker thread for transcription when status is `pending`
8. Worker updates latest metadata and writes a sidecar JSON file after success or failure

### Sidecar result file

Each raw capture will get an optional sidecar file:

- `capture_2026-06-29_123456_000001.asr.json`

Contents:

- `asr_status`
- `transcript_text`
- `asr_error`
- `asr_model`
- `language`
- `emotion`
- `updated_at`

This makes restart recovery deterministic and avoids losing transcript state when the local server restarts.

### Recovery behavior on startup

When the server starts and recovers the latest capture from disk, it should:

- recover the latest WAV and preview exactly as today
- if the ASR sidecar exists, load it into `latest`
- if the sidecar does not exist, leave ASR fields unset or disabled based on current config
- do not automatically re-submit old recordings for ASR on boot

That keeps startup simple and avoids surprise repeated API calls.

## ASR Request Format

The server will use raw HTTP with the Python standard library rather than adding new dependencies.

Request target:

- `POST {ASR_BASE_URL}/chat/completions`

Headers:

- `Authorization: Bearer <DASHSCOPE_API_KEY>`
- `Content-Type: application/json`

Request body shape:

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

If `ASR_LANGUAGE` is configured, the server adds:

```json
"language": "zh"
```

inside `asr_options`.

### Response extraction

The server will parse:

- transcript text from `choices[0].message.content`
- metadata from `choices[0].message.annotations[0]` when present
  - `language`
  - `emotion`

If the response body contains `error`, or if `choices[0].message.content` is missing, the server marks the job as failed with a readable error string.

## Latest Metadata Contract

`GET /api/latest` will be extended with:

- `asr_status`
- `transcript_text`
- `asr_error`
- `asr_model`
- `asr_language`
- `asr_emotion`

Expected statuses:

- `disabled`
- `empty_audio`
- `pending`
- `done`
- `error`

These fields are additive only; existing audio playback fields remain unchanged.

## Web Page Design

The current page is intentionally simple. We will keep it simple and add only high-signal ASR state:

- `ASR status`
- `Transcript`
- `Language`
- `Emotion`
- `ASR error` when present

Display behavior:

- while `pending`: show `recognizing...`
- on `done`: show recognized text
- on `disabled`: show `ASR disabled: missing DASHSCOPE_API_KEY`
- on `error`: show concise error text
- on `empty_audio`: show `empty audio, ASR skipped`

No transcript history list yet; only the current latest capture is shown.

## Error Handling

### Missing API key

- Upload succeeds
- Playback works
- `asr_status = "disabled"`
- No outbound ASR request

### Empty or extremely short audio

- Upload succeeds
- `asr_status = "empty_audio"`
- No ASR request

### Remote API failure

- Upload succeeds
- Transcript area shows failure
- Raw WAV and preview WAV remain available
- Error string is truncated to a reasonable size for UI readability

### Network timeout

- Treated as `error`
- No automatic retry in the first version

### Invalid JSON or missing content

- Treated as `error`
- Keep original response snippet in server logs if helpful

## Testing Strategy

### Unit and integration tests

Add tests in `tools/test_audio_record_web_server.py` that cover:

- upload succeeds and latest returns default `pending`/`disabled` ASR state
- successful ASR callback path updates transcript fields
- missing API key results in `disabled`
- empty audio results in `empty_audio`
- API error response results in `error`
- startup recovery loads saved `.asr.json` sidecar

To keep tests deterministic, ASR HTTP calls should be abstracted behind a small helper function that tests can stub.

### Manual validation

1. Set `DASHSCOPE_API_KEY` in the local shell
2. Start the local web server
3. Record and upload a short Chinese sample
4. Confirm page flow:
   - audio appears
   - `ASR status` becomes `pending`
   - then `done`
   - transcript text appears
5. Temporarily clear the key and confirm `disabled`
6. Confirm server restart still shows latest transcript from sidecar
7. Confirm repeated recordings still remain free of the previously observed weak periodic `滴滴滴` noise, proving ASR integration did not regress the Wi-Fi quieting fix

## Files To Modify

- `tools/audio_record_web_server.py`
- `tools/test_audio_record_web_server.py`
- `tools/start_audio_record_web_server.ps1`
  - likely only if we choose to pass ASR-related environment through startup workflow documentation
- `tools/stop_audio_record_web_server.ps1`
  - likely no behavior change needed
- `tests/audio_record_probe/README.md`

## Risks And Tradeoffs

### Why not use Alibaba long-audio async API

It provides richer task semantics and timestamps, but it adds task submission plus polling logic and is unnecessary for our short probe recordings. For this probe, `qwen3-asr-flash` over OpenAI-compatible synchronous request is the better fit.

### Why not stream ASR tokens to the page

Streaming is supported by the model, but our current UI already polls `latest`, and the extra streaming state would complicate the local test server without improving the hardware validation goal.

### Base64 size overhead

Base64 increases payload size, but our recordings are capped at 10 seconds and the current WAV size is well below the documented 10 MB input ceiling, so this is acceptable for the probe.

## Acceptance Criteria

- Existing upload and playback flow still works
- With `DASHSCOPE_API_KEY` configured, each uploaded recording triggers ASR
- The browser page shows transcript status and final recognized text
- API failures are shown clearly without breaking playback
- Latest transcript state survives local web server restart via sidecar metadata
- No device firmware changes are required for ASR
