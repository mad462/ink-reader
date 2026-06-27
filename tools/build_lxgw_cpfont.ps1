param(
    [string]$OutputDir = ".cache\fonts\LXGWWenKai",
    [int]$Size = 18,
    [string]$SdDrive = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$python = "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe"
$crossPointScripts = Resolve-Path (Join-Path $repoRoot "..\refs\crosspoint-reader\lib\EpdFont\scripts")
$fontCache = Join-Path $repoRoot ".cache\fonts"
$ttfPath = Join-Path $fontCache "LXGWWenKai-Regular.ttf"
$outDir = Join-Path $repoRoot $OutputDir

New-Item -ItemType Directory -Force $fontCache | Out-Null
New-Item -ItemType Directory -Force $outDir | Out-Null

if (!(Test-Path $ttfPath)) {
    Invoke-WebRequest `
        -Uri "https://github.com/lxgw/LxgwWenKai/releases/download/v1.522/LXGWWenKai-Regular.ttf" `
        -OutFile $ttfPath
}

& $python -m pip install -r (Join-Path $crossPointScripts "requirements.txt")

Push-Location $crossPointScripts
try {
    & $python ".\fontconvert_sdcard.py" `
        $ttfPath `
        --intervals "ascii,cjk" `
        --size $Size `
        --style regular `
        --name LXGWWenKai `
        --output-dir $outDir
} finally {
    Pop-Location
}

$fontFile = Join-Path $outDir ("LXGWWenKai_{0}.cpfont" -f $Size)
if ($SdDrive -ne "") {
    $sdRoot = $SdDrive.TrimEnd('\') + "\"
    if (Test-Path $sdRoot) {
        $targetDir = Join-Path $sdRoot "fonts"
        New-Item -ItemType Directory -Force $targetDir | Out-Null
        Copy-Item -LiteralPath $fontFile -Destination (Join-Path $targetDir (Split-Path -Leaf $fontFile)) -Force
    } else {
        Write-Warning "SD drive not found: $sdRoot"
    }
}

Get-Item $fontFile
