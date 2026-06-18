# ink-reader

Minimal ESP-IDF bring-up for a 4.26-inch `GDEY0426T82` e-paper panel and FAT32 TF card on `ESP32-S3`.

## Environment

- ESP-IDF root: `C:\esp\v5.5.4\esp-idf`
- IDF tools path: `C:\Espressif`
- Expected shell export:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
. C:\esp\v5.5.4\esp-idf\export.ps1
```

## Hardware assumptions

- MCU: `ESP32-S3`
- Flash: `16 MB`
- PSRAM: `8 MB`
- Panel: `GDEY0426T82`
- Logical panel resolution: `480 x 800` portrait
- Native controller transfer resolution: `800 x 480` landscape
- TF card filesystem: `FAT32`

## E-Paper Wiring

- `EPD_GPIO_MOSI` -> `GPIO4`
- `EPD_GPIO_MISO` -> not used
- `EPD_GPIO_CLK` -> `GPIO5`
- `EPD_GPIO_CS` -> `GPIO6`
- `EPD_GPIO_DC` -> `GPIO7`
- `EPD_GPIO_RST` -> `GPIO15`
- `EPD_GPIO_BUSY` -> `GPIO16`

## TF Card (SDMMC 4-bit)

- `CLK` -> `GPIO40`
- `CMD` -> `GPIO39`
- `D0` -> `GPIO41`
- `D1` -> `GPIO42`
- `D2` -> `GPIO48`
- `D3` -> `GPIO38`

## Build and flash

```powershell
idf.py set-target esp32s3
idf.py -p COMx flash
```

## Current boot flow

Serial logs should show:

1. TF card mount start
2. SD card identification and root directory listing
3. TXT preview file scan and byte statistics
4. E-paper full white
5. E-paper full black
6. E-paper text demo pattern
7. E-paper TXT preview page
8. E-paper partial area refresh demo, changing only the bottom status line
9. Panel deep sleep

## Current TXT preview behavior

- The firmware scans `/sdcard` for the first `.txt` or `.TXT` file.
- It reads the first `512` bytes and renders a simple preview page.
- Current preview font is ASCII-only.
- Any non-ASCII UTF-8 byte is rendered as `#` for now.
- Serial logs report how many non-ASCII bytes were seen, so Chinese text support is not being overstated.

## Current partial refresh behavior

- Public framebuffer coordinates are portrait `480 x 800`.
- The driver rotates portrait framebuffers into the controller native `800 x 480` transfer layout before writing RAM.
- `epd_gdey0426t82_partial_refresh()` keeps an internal shadow framebuffer.
- Full refresh updates the shadow framebuffer after a successful refresh.
- Partial refresh sends the new frame to RAM command `0x24`, the previous frame to RAM command `0x26`, then triggers the seller demo partial update sequence `0x22/0xFF` + `0x20`.
- `epd_gdey0426t82_partial_refresh_area()` accepts a portrait-coordinate dirty rectangle and writes only the aligned native RAM window.
- The app currently validates this by full-refreshing the TXT preview page, then changing only the bottom status line to `PARTIAL AREA OK` through partial area refresh.
- Follow the seller guidance: after several partial refreshes, do a full refresh to clear ghosting.

## Suggested next display milestones

1. Observe the current partial refresh demo on hardware and tune the command sequence if it flickers or ghosts too much.
2. Observe whether the portrait mapping is the desired upright direction; if it is rotated the opposite way, swap the portrait-to-native transform.
3. Add 4-gray refresh pipeline after partial refresh is stable.
4. Once waveform behavior is understood, package the panel code as a reusable IDF component.
5. Then evaluate porting an existing e-book framework on top of the proven display + TF stack.

