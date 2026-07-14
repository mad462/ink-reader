param(
    [string]$TccPath
)

$ErrorActionPreference = 'Stop'
$testDir = $PSScriptRoot
$readerDir = Split-Path $testDir -Parent
$mainDir = Join-Path $readerDir 'main'
$repoRoot = Split-Path (Split-Path $readerDir -Parent) -Parent
$coreInclude = Join-Path $repoRoot 'components\ink_reader_core\include'
$runRoot = Join-Path $env:TEMP ('ink-reader-app-model-' + [guid]::NewGuid().ToString('N'))
$exe = Join-Path $runRoot 'reader_app_model_host_test.exe'
$refreshExe = Join-Path $runRoot 'reader_refresh_policy_host_test.exe'
$holdExe = Join-Path $runRoot 'reader_hold_paging_host_test.exe'
$cacheExe = Join-Path $runRoot 'reader_page_cache_host_test.exe'
$pageTurnExe = Join-Path $runRoot 'reader_page_turn_host_test.exe'
$inputQueueExe = Join-Path $runRoot 'reader_input_queue_host_test.exe'

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
    & $TccPath -Wall -Werror `
        -I $mainDir `
        -I $coreInclude `
        -o $exe `
        (Join-Path $testDir 'reader_app_model_host_test.c') `
        (Join-Path $mainDir 'reader_app_model.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $TccPath -Wall -Werror `
        -I $mainDir `
        -o $refreshExe `
        (Join-Path $testDir 'reader_refresh_policy_host_test.c') `
        (Join-Path $mainDir 'reader_refresh_policy.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $refreshExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $TccPath -Wall -Werror `
        -I $mainDir `
        -o $holdExe `
        (Join-Path $testDir 'reader_hold_paging_host_test.c') `
        (Join-Path $mainDir 'reader_hold_paging.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $holdExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $TccPath -Wall -Werror `
        -I $mainDir `
        -o $cacheExe `
        (Join-Path $testDir 'reader_page_cache_host_test.c') `
        (Join-Path $mainDir 'reader_page_cache.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cacheExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $TccPath -Wall -Werror `
        -I $mainDir `
        -o $pageTurnExe `
        (Join-Path $testDir 'reader_page_turn_host_test.c') `
        (Join-Path $mainDir 'reader_page_turn.c') `
        (Join-Path $mainDir 'reader_page_cache.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $pageTurnExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $TccPath -Wall -Werror `
        -I $mainDir `
        -o $inputQueueExe `
        (Join-Path $testDir 'reader_input_queue_host_test.c') `
        (Join-Path $mainDir 'reader_input_queue.c')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $inputQueueExe
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
