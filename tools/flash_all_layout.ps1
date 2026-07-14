param([Parameter(Mandatory = $true)][string]$Port)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'idf_env.ps1')

$root = Split-Path $PSScriptRoot -Parent
$launcher = Join-Path $root 'apps\launcher'
$reader = Join-Path $root 'apps\reader'
$photo = Join-Path $root 'apps\photo'
$usbMsc = Join-Path $root 'apps\usb_msc'
$wifiSetup = Join-Path $root 'apps\wifi_setup'

foreach ($app in @($launcher, $reader, $photo, $usbMsc, $wifiSetup)) {
    idf.py -C $app build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

python -m esptool --chip esp32s3 --port $Port -b 460800 `
    --before default_reset --after hard_reset write_flash `
    --flash_mode dio --flash_size 16MB --flash_freq 80m `
    0x0 (Join-Path $launcher 'build\bootloader\bootloader.bin') `
    0x8000 (Join-Path $launcher 'build\partition_table\partition-table.bin') `
    0xF000 (Join-Path $launcher 'build\ota_data_initial.bin') `
    0x20000 (Join-Path $launcher 'build\launcher.bin') `
    0x220000 (Join-Path $reader 'build\reader.bin') `
    0x420000 (Join-Path $photo 'build\photo.bin') `
    0x620000 (Join-Path $usbMsc 'build\usb_msc.bin') `
    0x820000 (Join-Path $wifiSetup 'build\wifi_setup.bin')
exit $LASTEXITCODE
