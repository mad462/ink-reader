# Hardware Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an isolated ESP-IDF hardware probe for MPU6050, LMD2718 microphone input, and NS4168 speaker output.

**Architecture:** The probe is a standalone app under `tests/hw_probe`. It uses ESP-IDF driver APIs directly and reports hardware status through serial logs, leaving the main reader firmware untouched.

**Tech Stack:** ESP-IDF 5.5.4, `esp_driver_i2c`, `esp_driver_i2s`, FreeRTOS.

---

### Task 1: Standalone Project Skeleton

**Files:**
- Create: `tests/hw_probe/CMakeLists.txt`
- Create: `tests/hw_probe/main/CMakeLists.txt`
- Create: `tests/hw_probe/sdkconfig.defaults`
- Create: `tests/hw_probe/README.md`

- [ ] Create a standalone ESP-IDF project that builds independently from the root reader app.
- [ ] Document the fixed GPIO map and run commands.

### Task 2: Probe Application

**Files:**
- Create: `tests/hw_probe/main/app_main.c`

- [ ] Implement I2C bus setup on GPIO1/GPIO2.
- [ ] Scan the I2C bus and read MPU6050 `WHO_AM_I`.
- [ ] Read MPU6050 raw accelerometer and gyro registers.
- [ ] Implement a PDM microphone level probe on GPIO17/GPIO18.
- [ ] Implement a standard I2S speaker tone on GPIO8/GPIO14/GPIO13.

### Task 3: Verification

- [ ] Build with `C:\esp\v5.5.4\esp-idf\export.ps1` and `idf.py set-target esp32s3 build`.
- [ ] If the build passes, flash with `idf.py -p COMx flash monitor`.
- [ ] Confirm serial output shows MPU6050 address and `WHO_AM_I=0x68`.
- [ ] Confirm microphone levels change when tapping or speaking near the microphone.
- [ ] Confirm the speaker plays a short 1 kHz tone.
