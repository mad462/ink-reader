param([Parameter(Mandatory = $true)][string]$Port)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'idf_env.ps1')

$root = Split-Path $PSScriptRoot -Parent
$app = Join-Path $root 'apps\reader'
idf.py -C $app build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

python -m esptool --chip esp32s3 --port $Port -b 460800 `
    --before default_reset --after hard_reset write_flash `
    --flash_mode dio --flash_size 16MB --flash_freq 80m `
    0x120000 (Join-Path $app 'build\reader.bin')
exit $LASTEXITCODE
