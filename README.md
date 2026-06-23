# ink-reader

Minimal ESP-IDF bring-up for a 4.26-inch `GDEY0426T82` e-paper panel on `ESP32-S3`.

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
## E-Paper Wiring

- `EPD_GPIO_MOSI` -> `GPIO4`
- `EPD_GPIO_MISO` -> not used
- `EPD_GPIO_CLK` -> `GPIO5`
- `EPD_GPIO_CS` -> `GPIO6`
- `EPD_GPIO_DC` -> `GPIO7`
- `EPD_GPIO_RST` -> `GPIO15`
- `EPD_GPIO_BUSY` -> `GPIO16`

## Build and flash

```powershell
idf.py set-target esp32s3
idf.py -p COMx flash
```

## Current boot flow

Serial logs should show:

1. display tuning lab startup
2. initial fixed test page submit
3. E-paper render timing logs for the selected refresh profile

## Current display tuning lab behavior

- The active runtime no longer depends on TF card or ebook state.
- Left/right buttons switch between a small set of fixed test pages.
- Confirm cycles refresh profiles.
- Back forces a full refresh of the current page.
- Footer text shows current page and refresh profile so visual artifacts can be correlated with serial logs.

## Current partial refresh behavior

- Public framebuffer coordinates are portrait `480 x 800`.
- The driver rotates portrait framebuffers into the controller native `800 x 480` transfer layout before writing RAM.
- `epd_gdey0426t82_partial_refresh()` keeps an internal shadow framebuffer.
- Full refresh updates the shadow framebuffer after a successful refresh.
- Partial refresh sends the new frame to RAM command `0x24`, the previous frame to RAM command `0x26`, then triggers the seller demo partial update sequence `0x22/0xFF` + `0x20`.
- `epd_gdey0426t82_partial_refresh_area()` accepts a portrait-coordinate dirty rectangle and writes only the aligned native RAM window.
- The tuning lab can compare full-screen refresh, full-screen fast refresh, dirty-rect partial refresh, and fixed-footer partial refresh on deterministic pages.
- Follow the seller guidance: after several partial refreshes, do a full refresh to clear ghosting.

## Suggested next display milestones

1. Observe the current partial refresh demo on hardware and tune the command sequence if it flickers or ghosts too much.
2. Observe whether the portrait mapping is the desired upright direction; if it is rotated the opposite way, swap the portrait-to-native transform.
3. Add 4-gray refresh pipeline after partial refresh is stable.
4. Once waveform behavior is understood, package the panel code as a reusable IDF component.
5. Then evaluate porting an existing e-book framework on top of the proven display + TF stack.

