# Audio ASR Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `tests/audio_asr_probe` as a standalone ESP-IDF probe that records audio, sends it directly to an Alibaba `qwen3-asr-flash` endpoint, and prints status plus transcript to serial.

**Architecture:** Reuse the proven device-side recording and Wi-Fi quieting patterns from `audio_record_probe`, but replace PC relay upload with direct OpenAI-compatible HTTPS ASR calls from ESP32-S3. Keep V1 serial-only and optimize for correctness over latency.

**Tech Stack:** ESP-IDF 5.5.4, `esp_http_client`, ESP Wi-Fi STA, PDM RX I2S, standalone ESP-IDF test project, local untracked config override.

---

### Task 1: Scaffold the new standalone test project

**Files:**
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\CMakeLists.txt`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\sdkconfig.defaults`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\README.md`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\local_asr_config.example.h`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\CMakeLists.txt`

- [ ] **Step 1: Create minimal project files**
- [ ] **Step 2: Verify `idf.py reconfigure` can see the new app**
Run: `idf.py reconfigure`
Expected: project config completes without missing-file errors.

### Task 2: Add failing probe logic tests first

**Files:**
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_probe_logic.h`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_probe_logic.c`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\test_audio_asr_probe_logic.c`

- [ ] **Step 1: Write failing tests for state transitions**
Cover:
- idle -> recording on press
- recording -> request pending on release
- max-duration stop
- return to idle after request completes

- [ ] **Step 2: Run build to verify the tests fail**
Run: `idf.py build`
Expected: compile or self-test failure because logic functions are incomplete.

- [ ] **Step 3: Implement minimal logic to pass**
- [ ] **Step 4: Re-run build**
Run: `idf.py build`
Expected: logic self-tests pass.

### Task 3: Add WAV and size-estimation helpers

**Files:**
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_wav.h`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_wav.c`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\test_audio_asr_wav.c`

- [ ] **Step 1: Write failing tests**
Cover:
- valid WAV header generation
- total WAV size calculation
- base64 size estimate for request planning

- [ ] **Step 2: Run build to verify failure**
Run: `idf.py build`
Expected: self-test failure.

- [ ] **Step 3: Implement minimal helper code**
- [ ] **Step 4: Re-run build**
Run: `idf.py build`
Expected: WAV self-tests pass.

### Task 4: Add local config and Wi-Fi lifecycle module

**Files:**
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\local_asr_config.h`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_wifi.h`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_wifi.c`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\test_audio_asr_wifi.c`

- [ ] **Step 1: Write failing tests**
Cover:
- config validity
- quiet call is safe before start
- connect/quiet interface shape remains stable

- [ ] **Step 2: Run build to verify failure**
Run: `idf.py build`
Expected: self-test failure.

- [ ] **Step 3: Implement Wi-Fi connect and quieting behavior**
- [ ] **Step 4: Re-run build**
Run: `idf.py build`
Expected: Wi-Fi self-tests pass.

### Task 5: Add ASR HTTP body builder and response parser

**Files:**
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_http.h`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_http.c`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_json.h`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\audio_asr_json.c`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\test_audio_asr_http.c`

- [ ] **Step 1: Write failing tests**
Cover:
- OpenAI-compatible URL generation
- JSON body includes `input_audio` data URL
- response parser extracts transcript text
- response parser classifies missing/invalid payloads

- [ ] **Step 2: Run build to verify failure**
Run: `idf.py build`
Expected: self-test failure.

- [ ] **Step 3: Implement minimal HTTP/body/parser helpers**
- [ ] **Step 4: Re-run build**
Run: `idf.py build`
Expected: parser and request-builder self-tests pass.

### Task 6: Build the V1 `app_main.c` flow

**Files:**
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\app_main.c`

- [ ] **Step 1: Wire recording flow**
Behavior:
- wait for Confirm
- record with Wi-Fi quiet
- stop on release or timeout
- build WAV

- [ ] **Step 2: Wire ASR flow**
Behavior:
- connect Wi-Fi after recording
- estimate and log body sizes
- send HTTPS request
- parse transcript or error
- quiet Wi-Fi again

- [ ] **Step 3: Emit stable serial logs**
Required logs:
- press/release
- wav size
- connect timing
- request timing
- transcript or error

- [ ] **Step 4: Run full build**
Run: `idf.py build`
Expected: project builds successfully.

### Task 7: Add docs and local-secret handling

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\.gitignore`
- Modify: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\README.md`
- Create: `D:\FUCKIDF\ink-reader\tests\audio_asr_probe\main\local_asr_config_override.h`
  - local untracked file on the developer machine, not committed

- [ ] **Step 1: Ignore the local secret override**
- [ ] **Step 2: Document build/flash/test flow**
Document:
- Wi-Fi config
- Alibaba key config
- expected serial output
- known V1 limitations

### Task 8: Verify the new standalone probe

**Files:**
- No new files required

- [ ] **Step 1: Fresh build verification**
Run: `idf.py build`
Expected: success

- [ ] **Step 2: Flash and manual smoke test**
Run: `idf.py -p COM9 flash monitor`
Expected:
- can record
- Wi-Fi starts only after release
- request is attempted
- transcript or readable error is printed

- [ ] **Step 3: Repeat-capture validation**
Manual:
- record two times back-to-back
Expected:
- second capture still works
- periodic `滴滴滴` interference does not return
