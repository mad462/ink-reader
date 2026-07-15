param([string]$TccPath)

$ErrorActionPreference = 'Stop'
$testDir = $PSScriptRoot
$componentDir = Split-Path $testDir -Parent
$runRoot = Join-Path $env:TEMP `
    ('ink-reader-usb-msc-host-' + [guid]::NewGuid().ToString('N'))
$testExe = Join-Path $runRoot 'usb_msc_state_host_test.exe'
$lifecycleTestExe = Join-Path $runRoot 'usb_msc_core_lifecycle_host_test.exe'

if (-not $TccPath) {
    $TccPath = Join-Path $env:TEMP 'ink-reader-host-tools\tcc\tcc.exe'
}
if (-not (Test-Path -LiteralPath $TccPath)) {
    throw "TinyCC not found: $TccPath"
}

New-Item -ItemType Directory -Force $runRoot | Out-Null
try {
    & $TccPath -std=c11 -Wall -Werror `
        -I (Join-Path $testDir 'host') `
        -I (Join-Path $componentDir 'include') `
        -I $componentDir `
        -o $testExe `
        (Join-Path $testDir 'usb_msc_state_host_test.c') `
        (Join-Path $componentDir 'ink_usb_msc_state.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $testExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $TccPath -std=c11 -Wall -Werror `
        -I (Join-Path $testDir 'host') `
        -I (Join-Path $componentDir 'include') `
        -I $componentDir `
        -o $lifecycleTestExe `
        (Join-Path $testDir 'usb_msc_core_lifecycle_host_test.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $lifecycleTestExe
    exit $LASTEXITCODE
} finally {
    if (Test-Path -LiteralPath $runRoot) {
        $resolvedRun = (Resolve-Path -LiteralPath $runRoot).Path
        $resolvedTemp = (Resolve-Path -LiteralPath $env:TEMP).Path
        if (-not $resolvedRun.StartsWith($resolvedTemp)) {
            throw "Host run directory escaped TEMP: $resolvedRun"
        }
        Remove-Item -LiteralPath $resolvedRun -Recurse -Force
    }
}
