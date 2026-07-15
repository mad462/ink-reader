param(
    [string]$TccPath
)

$ErrorActionPreference = 'Stop'
$testDir = $PSScriptRoot
$componentDir = Split-Path $testDir -Parent
$runRoot = Join-Path $env:TEMP `
    ('ink-reader-ui-host-' + [guid]::NewGuid().ToString('N'))
$testExe = Join-Path $runRoot 'reader_ui_host_test.exe'
$loadingTestExe = Join-Path $runRoot 'loading_ui_host_test.exe'

if (-not $TccPath) {
    $cachedTcc = Join-Path $env:TEMP 'ink-reader-host-tools\tcc\tcc.exe'
    if (Test-Path -LiteralPath $cachedTcc) {
        $TccPath = $cachedTcc
    } else {
        $TccPath = Join-Path $runRoot 'tools\tcc\tcc.exe'
    }
}
if (-not (Test-Path -LiteralPath $TccPath)) {
    $toolsDir = Split-Path (Split-Path $TccPath -Parent) -Parent
    New-Item -ItemType Directory -Force $toolsDir | Out-Null
    $archive = Join-Path $toolsDir 'tcc.zip'
    Invoke-WebRequest `
        -Uri 'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip' `
        -OutFile $archive
    Expand-Archive -Path $archive -DestinationPath $toolsDir -Force
}

New-Item -ItemType Directory -Force $runRoot | Out-Null
try {
    & $TccPath -std=c11 -Wall -Werror `
        -include (Join-Path $testDir 'host\compat.h') `
        -I (Join-Path $testDir 'host') `
        -I (Join-Path $componentDir 'include') `
        -I $componentDir `
        -o $testExe `
        (Join-Path $testDir 'reader_ui_host_test.c') `
        (Join-Path $componentDir 'ink_reader_ui.c') `
        (Join-Path $componentDir 'ink_epd_ui.c') `
        (Join-Path $componentDir 'ink_ui_text_assets.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $testExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $TccPath -std=c11 -Wall -Werror `
        -include (Join-Path $testDir 'host\compat.h') `
        -I (Join-Path $testDir 'host') `
        -I (Join-Path $componentDir 'include') `
        -I $componentDir `
        -o $loadingTestExe `
        (Join-Path $testDir 'loading_ui_host_test.c') `
        (Join-Path $componentDir 'ink_epd_ui.c') `
        (Join-Path $componentDir 'ink_ui_text_assets.c') `
        (Join-Path $componentDir 'ink_loading_asset.c') `
        (Join-Path $componentDir 'ink_usb_msc_assets.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $loadingTestExe
    exit $LASTEXITCODE
} finally {
    if (Test-Path -LiteralPath $runRoot) {
        $resolvedRun = (Resolve-Path $runRoot).Path
        $resolvedTemp = (Resolve-Path $env:TEMP).Path
        if (-not $resolvedRun.StartsWith($resolvedTemp)) {
            throw "Host run directory escaped TEMP: $resolvedRun"
        }
        Remove-Item -LiteralPath $resolvedRun -Recurse -Force
    }
}
