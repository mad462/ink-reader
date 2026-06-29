# Voice Note App Design

## Goal

Add a new mainline app, `voice_note`, that turns the already-validated device ASR chain into a user-facing voice note workflow:

- enter the app from Launcher
- hold `Confirm` to record
- release to stop
- persist the recording to TF card as WAV
- send the WAV to the ASR service through the unified Wi-Fi coordinator
- save the recognized text as a persistent note
- browse notes by status
- retry recognition later if networking or ASR fails

This is the first product-oriented integration of the audio work. It should feel like a real app, but stay deliberately narrow so the mainline remains stable.

## Product Intent

The voice note app is a lightweight capture tool for short spoken reminders and tasks.

The first release optimizes for:

1. reliable end-to-end note creation
2. persistent storage across reboot
3. clear in-app status while work is in progress
4. safe integration with the shared Wi-Fi policy

It does not yet optimize for:

1. audio playback
2. advanced note editing
3. background sync
4. smart summarization
5. multi-job concurrency

## Current Context

Three parts of the codebase now matter for this feature:

### Mainline app shell

The main firmware already has:

- launcher registration
- app descriptors
- runtime input dispatch
- runtime render dispatch
- active app switching

So `voice_note` should be implemented as another regular app, not as a standalone probe and not as a special-case boot mode.

### Wi-Fi coordination

Mainline now has a shared Wi-Fi coordination layer above `ink_wifi_manager`.

That means the voice note app must not directly own Wi-Fi lifetime. It should request connectivity through the coordinator and release its lease when the ASR transaction is complete.

### Probe validation

`tests/audio_asr_probe` has already proven the risky lower-level path:

- PDM microphone capture
- WAV packaging
- delayed Wi-Fi bring-up after recording
- HTTPS request to the Alibaba OpenAI-compatible ASR endpoint
- transcript parsing

The app design should reuse that validated flow conceptually, but not copy probe structure into the mainline UI layer.

## Scope

### In scope

- new launcher entry for `voice_note`
- `voice_note` app UI with three tabs:
  - `全部`
  - `未完成`
  - `已完成`
- one fixed `新建语音标签` card at the top of the relevant lists
- hold-to-record input flow on device
- TF-card persistence for:
  - note metadata
  - source WAV
- ASR request using the mainline Wi-Fi coordinator
- note creation after successful recognition
- failed-note creation after Wi-Fi or ASR failure
- note detail popup actions:
  - view full text
  - retry recognition
  - mark complete / incomplete
  - delete
- reboot-safe note restoration from TF card

### Out of scope

- local playback through `NS4168`
- overwrite by re-recording
- concurrent recording or concurrent recognition jobs
- cloud sync
- smart title generation through a second model call
- waveform or visual audio UI
- on-screen multi-line progress console outside the app card

Playback is intentionally deferred to the next version even though the hardware path is already known, because the current goal is to stabilize the note lifecycle first.

## Options Considered

### Option A: Build a real app with an internal service layer

Split the feature into:

- `voice_note_app` for UI and interaction
- `voice_note_service` for recording, storage, ASR, and retry

Pros:

- good fit for the existing app architecture
- clear separation between UI and asynchronous work
- easy to keep Wi-Fi policy centralized
- easier to extend later with playback and richer note actions

Cons:

- slightly more upfront structure than a direct port of probe code

### Option B: Embed probe logic directly into one app file

Pros:

- quickest path to something runnable

Cons:

- mixes UI and transport concerns
- makes persistence and failure recovery harder
- encourages test-only structure inside production code

### Option C: Build a generic background media job system first

Pros:

- potentially reusable for future cloud features

Cons:

- too much architecture for the first shipping slice
- slows down delivery of the actual voice note app

## Recommendation

Choose Option A.

This feature is large enough that it needs one clean UI boundary and one clean background-work boundary, but not large enough to justify a fully generic job framework yet.

## Proposed Architecture

## App Boundary

Add a new app descriptor:

- `main/apps/ink_voice_note_app.c`
- `main/apps/ink_voice_note_app.h`

Register it in the launcher like any other app.

The app owns:

- tab selection
- card selection
- modal state
- full-text view state
- status text shown on the `新建语音标签` card
- dispatch of user actions to the service

The app does not own:

- microphone driver lifetime details
- WAV construction
- HTTP request construction
- direct Wi-Fi control
- TF card file IO policy beyond asking the service for results

## Service Boundary

Add a feature-local service layer under `main/voice_note/`.

Suggested files:

- `voice_note_service.c`
- `voice_note_service.h`
- `voice_note_store.c`
- `voice_note_store.h`
- `voice_note_model.c`
- `voice_note_model.h`
- `voice_note_asr.c`
- `voice_note_asr.h`

The service owns:

1. record lifecycle
2. WAV persistence
3. note persistence
4. retry recognition from a saved WAV
5. Wi-Fi coordinator requests
6. ASR request / response translation
7. app-facing job status snapshots

The service should expose a small app-facing API such as:

- start capture
- stop capture
- request retry for note id
- delete note
- toggle note done state
- load note summaries
- load full note content
- query current job state

The exact C signatures can be finalized during planning. The design requirement is the separation, not the final function spelling.

## Data Model

Each note should have two persisted assets:

1. metadata
2. source WAV

Suggested note fields:

- `id`
- `created_at`
- `updated_at`
- `status`
  - `pending`
  - `done`
- `transcript_state`
  - `processing`
  - `ready`
  - `failed`
- `title`
- `text`
- `wav_path`
- `duration_ms`
- `sample_rate`
- `channels`
- `last_error`

### Title rule

The list title is not AI-generated in V1.

Rules:

- when recognition succeeds:
  - use the recognized text head and truncate with `...` when needed
- when recognition fails because of Wi-Fi or ASR:
  - use the fixed title `这是一条语音标签`

### Failure-note rule

If the recording and WAV persistence succeeded, but Wi-Fi or ASR failed, the note still exists.

That note:

- keeps its WAV
- keeps its metadata
- has empty or absent recognized text
- stores the failure reason
- can later be retried through `重新识别`

If storage itself fails and the app cannot safely persist the WAV, no formal note should be created.

## TF Card Storage Layout

Store voice notes in a dedicated app directory.

Suggested layout:

```text
/ink/voice_notes/
  index.json
  note_20260629_153045_001.json
  note_20260629_153045_001.wav
  note_20260629_160212_002.json
  note_20260629_160212_002.wav
```

### Why keep both index and per-note files

This design uses:

- one `index.json` for fast startup and list ordering
- one JSON file per note for local note details
- one WAV per note for recovery and future playback

This balances startup speed and corruption isolation:

- startup can read the index first
- a single damaged note file should not destroy the whole note set
- delete and update operations stay local

## Persistence Rules

The note creation pipeline should persist in this order:

1. finish PCM capture
2. package WAV
3. write WAV file
4. create note metadata with `transcript_state=processing`
5. update `index.json`
6. request Wi-Fi and run ASR
7. update note metadata to:
   - `ready` on success
   - `failed` on network or ASR failure

This order makes the capture durable before networking starts.

### Crash and reboot recovery

On app startup, the store should scan for notes stuck in `processing`.

Those notes should be normalized to:

- `transcript_state=failed`
- `title=这是一条语音标签`
- `last_error=interrupted`

This avoids leaving notes in a permanent ambiguous state after reset or power loss.

## UI Structure

## Tabs

The app has three top-level tabs:

1. `全部`
2. `未完成`
3. `已完成`

Default landing tab:

- `未完成`

Reason:

- newly created notes appear there
- it aligns better with the note-as-task use case

## List Structure

Each tab shows:

1. a header line with tab name and count
2. the fixed `新建语音标签` card when applicable
3. note cards in reverse chronological order

`新建语音标签` should appear in:

- `全部`
- `未完成`

It does not need to appear in `已完成`.

## New Note Card Status Text

All transient capture and recognition feedback must appear inside the `新建语音标签` card.

Recommended status chain:

- `正在录音`
- `结束录音`
- `正在打包`
- `正在连接网络`
- `上传中`
- `上传识别中`
- `识别完成`
- `识别失败`

User-visible error variants:

- `录音时间太短`
- `网络不可用`
- `WiFi 连接失败`
- `识别服务失败`
- `存储失败`

Only one job may run at a time. While this card is busy, the app may still allow browsing existing notes, but it must not allow starting a second recording job.

## Note Card Layout

Each note card uses:

- small text: creation time and done-state
- large text: note title or transcript preview

Suggested small line examples:

- `06-29 15:30  未完成`
- `06-29 15:30  已完成`

Large line rules:

- use recognized text head when available
- truncate and append `...` when too long
- show `这是一条语音标签` for failed recognition notes

## Detail Interaction

Selecting a note opens a lightweight popup with actions:

- `查看全文`
- `重新识别`
- `标记已完成/未完成`
- `删除`

### `查看全文`

This should open a dedicated full-screen text view rather than stuffing long content into a cramped popup.

### `重新识别`

This action must reuse the saved WAV and request a fresh ASR pass.

It does not re-record audio in V1.

### `删除`

This removes:

- the note JSON
- the note WAV
- the note entry from the index

### `标记已完成/未完成`

This updates metadata only and should persist immediately.

## Input Model

Use the existing runtime button model.

### In lists

- `Left/Right`
  - move selection through cards
  - or switch tabs according to the final list-navigation scheme chosen during implementation
- `Confirm`
  - on `新建语音标签`: hold to record, release to stop
  - on a note card: open popup
- `Back`
  - return to launcher
  - or close popup / full-text view when already inside them

### Recording

`Confirm` hold semantics are fixed:

- press and hold starts recording
- release stops recording

The app must not support software-only start/stop toggles in V1.

## Service State Machine

The service should run as a single-job state machine:

- `idle`
- `recording`
- `packaging`
- `persisting_wav`
- `wifi_connecting`
- `uploading`
- `recognizing`
- `persisting_result`
- `completed`
- `failed`

### State transition rules

1. new capture starts only from `idle`
2. `Confirm` press moves to `recording`
3. `Confirm` release or max duration exits recording
4. very short recordings do not continue to Wi-Fi
5. WAV persistence happens before ASR
6. Wi-Fi and ASR only start after local assets are durable
7. completion updates the note and returns to `idle`
8. failure updates state and returns to `idle`

### Short recording rule

If press duration is less than 2 seconds:

- do not connect Wi-Fi
- do not send ASR
- do not create a formal note
- show `录音时间太短` on the new-note card

This avoids wasting power and network work on accidental taps.

## Recording Constraints

Keep the validated capture format for V1:

- `16000 Hz`
- `16-bit`
- `mono`
- maximum duration `10 seconds`

Use the already validated microphone routing:

- `GPIO17` = PDM CLK
- `GPIO18` = PDM DATA

`Confirm` remains aligned with the main firmware input mapping.

## Networking Contract

The app must not talk to `ink_wifi_manager` directly for high-level behavior.

Instead:

1. service asks the Wi-Fi coordinator for connectivity
2. service performs the ASR request only after coordinator success
3. service releases its Wi-Fi lease immediately after the ASR transaction

This keeps the feature aligned with the mainline power and coordination model.

### Lease identity

The voice note feature should have its own coordinator consumer identity:

- `VOICE_NOTE_ASR`

Use a dedicated coordinator lease identity for this app and do not reuse time-sync or setup leases.

## Failure Handling

Normalize failures into app-meaningful outcomes.

### Wi-Fi failure

- keep the saved WAV
- keep the note metadata
- set failed transcript state
- set title to `这是一条语音标签`
- record `last_error=wifi_failed`

### ASR request or response failure

- same persistence behavior as Wi-Fi failure
- record `last_error=asr_failed`

### Storage failure

If the service cannot safely persist WAV or note metadata:

- do not create a formal note
- show `存储失败`
- return to idle

### Interrupted processing

If reboot happens during `processing`:

- note is converted to failed on next load
- user can later choose `重新识别`

## Relationship To Future Playback

The design intentionally stores WAV alongside metadata now so that later playback integration can reuse existing note assets without a storage migration.

Planned future playback hardware:

- `NS4168`
- `I2S_SDIN` = `GPIO8`
- `I2S_LRCK` = `GPIO13`
- `I2S_SCLK` = `GPIO14`

V1 does not expose playback UI or playback state.

## Testing Strategy

Minimum test coverage should include:

1. launcher can enter and leave `voice_note`
2. `全部 / 未完成 / 已完成` filtering is correct
3. the new-note card shows the expected state sequence
4. short recordings do not trigger Wi-Fi
5. successful recognition creates both WAV and metadata
6. Wi-Fi failure creates a failed note with retryable WAV
7. ASR failure creates a failed note with retryable WAV
8. storage failure does not create a half-valid note
9. reboot restores note list from TF card
10. interrupted `processing` notes recover as failed notes
11. delete removes JSON, WAV, and index entry
12. mark complete / incomplete persists correctly
13. retry recognition reuses the stored WAV
14. the app never bypasses the Wi-Fi coordinator

Manual validation should include:

1. create a successful note
2. create a failed note by forcing network failure
3. retry recognition later and confirm the note updates
4. reboot the device and verify notes are still present
5. toggle note completion state and verify tab movement
6. delete a note and verify it disappears after reboot

## Rollout Strategy

Implement in one focused slice, but keep code boundaries explicit:

### Step 1

- add app registration and empty app shell
- add store and note loading
- render tabs, cards, and detail actions with stubbed data

### Step 2

- integrate recording and WAV persistence
- wire the new-note card to the service state machine

### Step 3

- integrate ASR and Wi-Fi coordinator
- persist success and failure outcomes
- enable retry recognition

### Step 4

- add full validation and recovery checks

## Risks And Tradeoffs

### TF card dependency

This feature depends on TF availability for durable note storage. That is acceptable for V1 because it matches current mainline storage direction and keeps room for WAV persistence.

### Single-job limitation

Only one active job at a time is deliberately restrictive, but it sharply lowers state-management risk and is sufficient for a button-driven e-paper device.

### No playback in V1

Users cannot audit audio quality from inside the app yet. This is a conscious tradeoff to avoid delaying the core note lifecycle.

### Index consistency

Because storage uses both index and per-note files, implementation must update them carefully. The benefit is better recovery and corruption isolation than a monolithic file.

## Acceptance Criteria

This design is successful when:

1. `voice_note` appears as a normal launcher app
2. a user can hold `Confirm` to record and release to finish
3. successful recognition creates a persistent note and WAV on TF card
4. failed recognition still creates a retryable failed note when WAV persistence succeeded
5. notes survive reboot
6. note status can be changed between `未完成` and `已完成`
7. notes can be deleted safely
8. the feature uses the shared Wi-Fi coordinator instead of independent Wi-Fi control
9. the design leaves a clean path for later playback and richer note actions without redoing the storage model
