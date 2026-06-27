# Tilt Grid Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and verify a standalone MPU6050 tilt-grid test app.

**Architecture:** The app lives under `tests/tilt_grid` and reuses the existing `ink_hw` e-paper component. MPU6050 access and tilt state handling stay inside the test app so the main reader firmware is untouched.

**Tech Stack:** ESP-IDF 5.5.4, `esp_driver_i2c`, existing `ink_hw` e-paper driver, Unity component test for pure tilt logic.

---

### Task 1: Standalone Project

**Files:**
- Create: `tests/tilt_grid/CMakeLists.txt`
- Create: `tests/tilt_grid/main/CMakeLists.txt`
- Create: `tests/tilt_grid/sdkconfig.defaults`
- Create: `tests/tilt_grid/README.md`

- [ ] Create an ESP-IDF app that points `EXTRA_COMPONENT_DIRS` at the root `components` directory.
- [ ] Document build, flash, monitor, and expected serial/screen behavior.

### Task 2: Tilt Logic Unit Test

**Files:**
- Create: `tests/tilt_grid/main/tilt_grid_input.h`
- Create: `tests/tilt_grid/main/tilt_grid_input.c`
- Create: `tests/tilt_grid/main/test/test_tilt_grid_input.c`

- [ ] Add tests for neutral deadzone, right movement, left movement, clamp at edges, and return-to-neutral hysteresis.
- [ ] Run the test target and confirm the tests fail before implementation.
- [ ] Implement the minimal state machine until tests pass.

### Task 3: App Main

**Files:**
- Create: `tests/tilt_grid/main/app_main.c`

- [ ] Initialize panel GPIO/SPI using the same GDEY0426T82 config as the main app.
- [ ] Initialize MPU6050 on GPIO1/GPIO2, scan for `0x68` or `0x69`, wake it, and calibrate neutral X.
- [ ] Draw a 5x5 grid with the filled cell at the center.
- [ ] Poll MPU6050, update tilt state, refresh the grid only when movement occurs, and log tuning data.

### Task 4: Verification

- [ ] Build with `C:\esp\v5.5.4\esp-idf\export.ps1`, `idf.py set-target esp32s3`, and `idf.py build`.
- [ ] Flash to `COM9` if available.
- [ ] Capture serial logs and confirm boot, calibration, MPU samples, and movement events.
