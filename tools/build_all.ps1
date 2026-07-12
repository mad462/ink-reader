$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'idf_env.ps1')

$root = Split-Path $PSScriptRoot -Parent
foreach ($app in @('launcher', 'reader', 'photo')) {
    Write-Host "Building $app"
    idf.py -C (Join-Path $root "apps\$app") build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
