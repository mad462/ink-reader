# Hardware Probe Design

## Goal

Add an isolated ESP-IDF bring-up project for checking the newly wired MPU6050, LMD2718 microphone input, and NS4168 speaker output before integrating them into the main ink-reader firmware.

## Hardware Map

- ESP32-S3 module with 16 MB flash and 8 MB PSRAM.
- MPU6050 I2C: `SCL=GPIO1`, `SDA=GPIO2`.
- LMD2718 microphone probe: `CLK=GPIO17`, `DATA=GPIO18`, treated as a two-wire PDM microphone.
- NS4168 speaker I2S: `DIN/SDA=GPIO8`, `BCLK=GPIO14`, `LRCLK/WS=GPIO13`.

## Approach

Create `tests/hw_probe` as a standalone ESP-IDF app using the default local ESP-IDF root `C:\esp\v5.5.4\esp-idf` and `IDF_TOOLS_PATH=C:\Espressif`. The project does not reuse or modify the main reader app, so hardware validation can be flashed independently.

The probe logs clear pass/fail evidence over serial:

- I2C scan result and MPU6050 `WHO_AM_I` readback.
- MPU6050 accelerometer and gyro raw samples after waking the device.
- LMD2718 PDM microphone read byte count plus min/max/mean/RMS style level readings.
- Speaker 1 kHz tone on the NS4168 I2S pins.

## Notes

The pictured audio board combines an LMD2718 microphone section and an NS4168 amplifier section. This probe intentionally matches the board's exposed `MIC DATA/CLK` and `AMP LRCLK/BCLK/SDA` pins.
