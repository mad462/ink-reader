param(
    [string]$BindHost = "0.0.0.0",
    [int]$Port = 8080,
    [string]$CapturesDir = ""
)

$ErrorActionPreference = "Stop"

$localConfig = Join-Path $PSScriptRoot "audio_record_web_server.local.ps1"
$templateConfig = Join-Path $PSScriptRoot "audio_record_web_server.local.example.ps1"
$startScript = Join-Path $PSScriptRoot "start_audio_record_web_server.ps1"

if (-not (Test-Path $localConfig)) {
    Write-Error "missing local ASR config: $localConfig`nCopy $templateConfig to audio_record_web_server.local.ps1 and fill in your real API key."
}

. $localConfig

if ([string]::IsNullOrWhiteSpace($env:DASHSCOPE_API_KEY) -or $env:DASHSCOPE_API_KEY -like "replace-with-*") {
    Write-Error "DASHSCOPE_API_KEY is not configured in $localConfig"
}

if ([string]::IsNullOrWhiteSpace($env:ASR_BASE_URL)) {
    $env:ASR_BASE_URL = "https://your-workspace.cn-beijing.maas.aliyuncs.com/compatible-mode/v1"
}

if ([string]::IsNullOrWhiteSpace($env:ASR_MODEL)) {
    $env:ASR_MODEL = "qwen3-asr-flash"
}

Write-Host "starting audio_record_web_server with ASR"
Write-Host "ASR_BASE_URL: $env:ASR_BASE_URL"
Write-Host "ASR_MODEL: $env:ASR_MODEL"

& $startScript -BindHost $BindHost -Port $Port -CapturesDir $CapturesDir
