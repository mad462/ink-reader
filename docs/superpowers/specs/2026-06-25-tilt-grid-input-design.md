# Tilt Grid Input Design

## Goal

Build a standalone ESP-IDF test app that proves the MPU6050 can act as a coarse input device for future WiFi password entry. The screen shows a 5x5 grid with one filled cell starting in the center. Clear left or right tilt moves the filled cell one column; light tilt should not move it.

## Hardware Map

- ESP32-S3 with 16 MB flash and 8 MB PSRAM.
- MPU-60X0/MPU-6500/MPU-9250-style module I2C: `SCL=GPIO1`, `SDA=GPIO2`, address `0x68` or `0x69`.
- Module strap pins: `AD0/SDO=GND` for `0x68` or `VCC` for `0x69`, `NCS=VCC` for I2C mode, `FSYNC=GND` when unused.
- GDEY0426T82 e-paper panel: `MOSI=GPIO4`, `SCLK=GPIO5`, `CS=GPIO6`, `DC=GPIO7`, `RST=GPIO15`, `BUSY=GPIO16`.

## Behavior

On boot, the app initializes the e-paper panel, initializes the MPU-compatible sensor, samples the accelerometer while the board is held level, and stores that X-axis value as the neutral offset. It then draws the grid with the filled cell at row 2, column 2.

The app polls accelerometer samples every 50 ms. It low-pass filters the calibrated X-axis reading, then feeds it to a small hysteresis state machine:

- `abs(filtered_x) < 3500`: neutral zone, no movement.
- `filtered_x > 6500`: one step right if the prior state was neutral.
- `filtered_x < -6500`: one step left if the prior state was neutral.
- After a movement, the board must return to the neutral zone before another movement can fire.
- Movement is clamped to the 5x5 grid bounds.

The screen refreshes only when the cell moves. The first draw uses a full refresh; subsequent moves use partial area refresh for the grid rectangle. Serial logs print raw X/Y/Z, filtered X, direction, and current cell so we can tune thresholds without guessing.

## Scope

This is only a tilt-grid validation app under `tests/tilt_grid`. It does not integrate with the reader UI, WiFi setup, audio probes, or the future password keyboard yet.
