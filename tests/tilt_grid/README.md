# ink-reader tilt grid test

Standalone ESP-IDF app for checking whether the GY6500 / MPU-6500 style module can be used as a coarse tilt input.

## GPIO map

| Device | Signal | GPIO |
| --- | --- | --- |
| GY6500 / MPU-6500 | VIN | 3.3V or 5V module input |
| GY6500 / MPU-6500 | GND | GND |
| GY6500 / MPU-6500 | SCL | GPIO1 |
| GY6500 / MPU-6500 | SDA | GPIO2 |
| GY6500 / MPU-6500 | AD0 / SDO | module default pulldown selects `0x68` |
| GY6500 / MPU-6500 | nCS | module default pullup enables I2C |
| GY6500 / MPU-6500 | FSYNC | module default pulldown when unused |
| GDEY0426T82 | MOSI | GPIO4 |
| GDEY0426T82 | SCLK | GPIO5 |
| GDEY0426T82 | CS | GPIO6 |
| GDEY0426T82 | DC | GPIO7 |
| GDEY0426T82 | RST | GPIO15 |
| GDEY0426T82 | BUSY | GPIO16 |

## Behavior

The app draws a 5x5 grid and starts with the filled cell in the center. It calibrates the IMU X-axis at boot. Clear left or right tilt moves the filled cell one column; small tilt should stay inside the deadzone and not move. After one move, the board must return to neutral before another move is accepted.

The `doc/GY6500资料/9250-sch.jpg` schematic shows this module already has:

- SCL/SDA 10K pullups to module 3.3V.
- `AD0/SDO` 10K pulldown, so the default I2C address is `0x68`.
- `nCS` 10K pullup, so I2C mode is enabled by default.
- `FSYNC` 10K pulldown when unused.
- `EDA`, `ECL`, and `INT` can be left unconnected for this test.

## Build

```powershell
cd D:\FUCKIDF\ink-reader\tests\tilt_grid
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
idf.py set-target esp32s3
idf.py build
```

## Flash and monitor

```powershell
idf.py -p COM9 flash monitor
```

Expected serial evidence:

- I2C scan reports `0x68`.
- MPU-6500/9250 `WHO_AM_I` reports `0x70`.
- Boot calibration logs a neutral X value.
- Clear left/right tilt logs `move=left` or `move=right` and updates the filled grid cell.
