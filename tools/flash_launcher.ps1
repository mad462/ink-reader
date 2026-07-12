param([Parameter(Mandatory = $true)][string]$Port)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'idf_env.ps1')

$root = Split-Path $PSScriptRoot -Parent
$app = Join-Path $root 'apps\launcher'
idf.py -C $app build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

python -m esptool --chip esp32s3 --port $Port -b 460800 `
    --before default-reset --after hard-reset write-flash `
    --flash-mode dio --flash-size 16MB --flash-freq 80m `
    0x0 (Join-Path $app 'build\bootloader\bootloader.bin') `
    0x8000 (Join-Path $app 'build\partition_table\partition-table.bin') `
    0xF000 (Join-Path $app 'build\ota_data_initial.bin') `
    0x20000 (Join-Path $app 'build\launcher.bin')
exit $LASTEXITCODE
