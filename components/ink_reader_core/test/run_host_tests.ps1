param(
    [string]$TccPath
)

$ErrorActionPreference = 'Stop'
$testDir = $PSScriptRoot
$componentDir = Split-Path $testDir -Parent
$repoRoot = (Resolve-Path (Join-Path $componentDir '..\..')).Path
$toolsDir = Join-Path $env:TEMP 'ink-reader-host-tools'
$buildDir = Join-Path $env:TEMP 'ink-reader-core-host-build'

if (-not $TccPath) {
    $TccPath = Join-Path $toolsDir 'tcc\tcc.exe'
}
if (-not (Test-Path -LiteralPath $TccPath)) {
    New-Item -ItemType Directory -Force $toolsDir | Out-Null
    $archive = Join-Path $toolsDir 'tcc.zip'
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest `
            -Uri 'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip' `
            -OutFile $archive
    }
    Expand-Archive -Path $archive -DestinationPath $toolsDir -Force
}

if (Test-Path -LiteralPath $buildDir) {
    $resolvedBuild = (Resolve-Path $buildDir).Path
    $resolvedTemp = (Resolve-Path $env:TEMP).Path
    if (-not $resolvedBuild.StartsWith($resolvedTemp)) {
        throw "Host build directory escaped TEMP: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}
New-Item -ItemType Directory -Force $buildDir | Out-Null
$testExe = Join-Path $buildDir 'reader_core_host_test.exe'

try {
    & $TccPath -Wall -Werror `
        -I (Join-Path $testDir 'host') `
        -I (Join-Path $componentDir 'include') `
        -o $testExe `
        (Join-Path $testDir 'reader_core_host_test.c') `
        (Join-Path $componentDir 'ink_reader_core.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Push-Location $repoRoot
    try {
        & $testExe
        exit $LASTEXITCODE
    } finally {
        Pop-Location
    }
} finally {
    if (Test-Path -LiteralPath $buildDir) {
        Remove-Item -LiteralPath $buildDir -Recurse -Force
    }
}
