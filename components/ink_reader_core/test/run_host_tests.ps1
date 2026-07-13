param(
    [string]$TccPath
)

$ErrorActionPreference = 'Stop'
$testDir = $PSScriptRoot
$componentDir = Split-Path $testDir -Parent
$runRoot = Join-Path $env:TEMP `
    ('ink-reader-core-host-' + [guid]::NewGuid().ToString('N'))
$buildDir = Join-Path $runRoot 'build'
$scanRoot = Join-Path $runRoot 'sdcard'

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

New-Item -ItemType Directory -Force $buildDir | Out-Null
$testExe = Join-Path $buildDir 'reader_core_host_test.exe'
$scanRootC = $scanRoot.Replace('\', '/')
$configHeader = Join-Path $runRoot 'scan_root_config.h'
$config = "#define INK_READER_SCAN_ROOT `"$scanRootC`"`n" +
    "#define INK_READER_TEST_ROOT `"$scanRootC`"`n"
[System.IO.File]::WriteAllText($configHeader, $config)

try {
    Write-Host "HOST_RUN_ROOT=$runRoot"
    & $TccPath -Wall -Werror `
        -include $configHeader `
        -I (Join-Path $testDir 'host') `
        -I (Join-Path $componentDir 'include') `
        -o $testExe `
        (Join-Path $testDir 'reader_core_host_test.c') `
        (Join-Path $componentDir 'ink_reader_core.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $testExe
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
