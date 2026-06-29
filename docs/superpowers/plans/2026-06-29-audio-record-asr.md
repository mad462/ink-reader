# Audio Record Probe ASR Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Alibaba `qwen3-asr-flash` transcription to the local audio probe web server so uploaded recordings can be recognized and shown on the page.

**Architecture:** Keep the ESP32 upload path unchanged. Extend the local Python server to store ASR state, call Alibaba asynchronously after upload, persist a sidecar `.asr.json` file, and expose transcript fields through `GET /api/latest` for the existing polling page.

**Tech Stack:** Python 3 standard library, existing local HTTP server, pytest, Alibaba Model Studio OpenAI-compatible HTTP API.

---

### Task 1: Add failing tests for ASR metadata and startup recovery

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py`

- [ ] **Step 1: Write the failing tests**

Add tests for:
- upload with ASR disabled because no key is configured
- upload with empty audio skipping ASR
- startup recovery from `.asr.json`

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -k "asr" -v`
Expected: FAIL because ASR fields and sidecar recovery do not exist yet.

- [ ] **Step 3: Write minimal implementation**

Implement only enough server state and recovery logic to satisfy the new metadata expectations.

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -k "asr" -v`
Expected: PASS

### Task 2: Add failing test for successful Alibaba ASR completion path

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py`
- Modify: `D:\FUCKIDF\ink-reader\tools\audio_record_web_server.py`

- [ ] **Step 1: Write the failing test**

Add a test that injects a fake ASR client result after upload and verifies:
- `asr_status` becomes `done`
- `transcript_text` is exposed through `/api/latest`
- `asr_model`, `asr_language`, `asr_emotion` are stored

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -k "successful_asr" -v`
Expected: FAIL because no ASR execution path exists.

- [ ] **Step 3: Write minimal implementation**

Add:
- ASR config loading from environment
- injectable ASR helper
- background worker invocation
- latest metadata update on success

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -k "successful_asr" -v`
Expected: PASS

### Task 3: Add failing test for ASR error handling

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py`
- Modify: `D:\FUCKIDF\ink-reader\tools\audio_record_web_server.py`

- [ ] **Step 1: Write the failing test**

Add a test that injects an ASR failure and verifies:
- upload still succeeds
- `/api/latest` ends with `asr_status = "error"`
- `asr_error` contains a readable message

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -k "asr_error" -v`
Expected: FAIL because server does not track ASR failures yet.

- [ ] **Step 3: Write minimal implementation**

Implement:
- worker exception capture
- error state persistence
- sidecar writing for failure state

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -k "asr_error" -v`
Expected: PASS

### Task 4: Extend the page and latest JSON contract

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\tools\audio_record_web_server.py`

- [ ] **Step 1: Update HTML page**

Add visible sections for:
- ASR status
- transcript text
- language
- emotion
- ASR error

- [ ] **Step 2: Update `/api/latest` payload defaults**

Ensure default JSON includes:
- `asr_status`
- `transcript_text`
- `asr_error`
- `asr_model`
- `asr_language`
- `asr_emotion`

- [ ] **Step 3: Run focused tests**

Run: `python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -k "asr or latest or root_page" -v`
Expected: PASS

### Task 5: Add real Alibaba HTTP integration

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\tools\audio_record_web_server.py`

- [ ] **Step 1: Implement OpenAI-compatible Alibaba HTTP request helper**

Add a helper that:
- base64-encodes WAV as `data:audio/wav;base64,...`
- `POST`s to `{ASR_BASE_URL}/chat/completions`
- parses `choices[0].message.content`
- extracts `language` and `emotion` from annotations when present

- [ ] **Step 2: Keep helper injectable for tests**

Expose a narrow function boundary so tests can stub it without making network calls.

- [ ] **Step 3: Run test suite**

Run: `python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -v`
Expected: PASS

### Task 6: Update docs and startup guidance

**Files:**
- Modify: `D:\FUCKIDF\ink-reader\tests\audio_record_probe\README.md`
- Modify: `D:\FUCKIDF\ink-reader\tools\start_audio_record_web_server.ps1`

- [ ] **Step 1: Update README**

Document:
- `DASHSCOPE_API_KEY`
- optional `ASR_BASE_URL`
- optional `ASR_MODEL`
- expected `pending/done/error` page flow

- [ ] **Step 2: Improve startup guidance**

Ensure the PowerShell startup flow remains compatible with inherited environment variables.

- [ ] **Step 3: Run final verification**

Run:
- `python -m pytest D:\FUCKIDF\ink-reader\tools\test_audio_record_web_server.py -v`
- `powershell -ExecutionPolicy Bypass -File D:\FUCKIDF\ink-reader\tools\start_audio_record_web_server.ps1`

Expected:
- tests pass
- server starts normally

### Task 7: Manual end-to-end validation

**Files:**
- No code changes required

- [ ] **Step 1: Set environment variable**

PowerShell:

```powershell
$env:DASHSCOPE_API_KEY = "your-real-key"
$env:ASR_BASE_URL = "https://your-workspace.cn-beijing.maas.aliyuncs.com/compatible-mode/v1"
$env:ASR_MODEL = "qwen3-asr-flash"
```

- [ ] **Step 2: Restart local web server**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File D:\FUCKIDF\ink-reader\tools\stop_audio_record_web_server.ps1
powershell -ExecutionPolicy Bypass -File D:\FUCKIDF\ink-reader\tools\start_audio_record_web_server.ps1
```

- [ ] **Step 3: Record and upload one sample**

Expected page flow:
- latest audio appears
- `ASR status` becomes `pending`
- then `done`
- transcript text appears

- [ ] **Step 4: Validate restart recovery**

Restart the server again and refresh the page.
Expected:
- latest transcript still appears from the sidecar file
