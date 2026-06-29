param(
    [string]$BindHost = "0.0.0.0",
    [int]$Port = 8080,
    [string]$CapturesDir = "",
    [string]$PidFile = "",
    [string]$StdoutLogFile = "",
    [string]$StderrLogFile = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$serverScript = Join-Path $PSScriptRoot "audio_record_web_server.py"
$captures = if ([string]::IsNullOrWhiteSpace($CapturesDir)) {
    Join-Path $PSScriptRoot "captures"
} else {
    $CapturesDir
}
$pidFile = if ([string]::IsNullOrWhiteSpace($PidFile)) {
    Join-Path $PSScriptRoot "audio_record_web_server.pid"
} else {
    $PidFile
}
$stdoutLogFile = if ([string]::IsNullOrWhiteSpace($StdoutLogFile)) {
    Join-Path $PSScriptRoot "audio_record_web_server.stdout.log"
} else {
    $StdoutLogFile
}
$stderrLogFile = if ([string]::IsNullOrWhiteSpace($StderrLogFile)) {
    Join-Path $PSScriptRoot "audio_record_web_server.stderr.log"
} else {
    $StderrLogFile
}

function Stop-ListenerOnPort {
    param(
        [int]$TargetPort
    )

    $listeners = Get-NetTCPConnection -LocalPort $TargetPort -State Listen -ErrorAction SilentlyContinue
    if (-not $listeners) {
        return
    }

    $pids = $listeners | Select-Object -ExpandProperty OwningProcess -Unique
    foreach ($owningPid in $pids) {
        if (-not $owningPid) {
            continue
        }
        $process = Get-Process -Id ([int]$owningPid) -ErrorAction SilentlyContinue
        if ($process) {
            Stop-Process -Id $process.Id -Force
            Write-Host "stopped stale listener on port $TargetPort, pid=$($process.Id)"
        }
    }
}

if (Test-Path $pidFile) {
    $existingPid = (Get-Content $pidFile -Raw).Trim()
    if ($existingPid) {
        $existingProcess = Get-Process -Id ([int]$existingPid) -ErrorAction SilentlyContinue
        if ($existingProcess) {
            Write-Host "audio_record_web_server already running, pid=$existingPid"
            Write-Host "stdout log: $stdoutLogFile"
            Write-Host "stderr log: $stderrLogFile"
            Write-Host "url: http://127.0.0.1:$Port/"
            exit 0
        }
    }
    Remove-Item $pidFile -Force -ErrorAction SilentlyContinue
}

Stop-ListenerOnPort -TargetPort $Port

New-Item -ItemType Directory -Force -Path $captures | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $pidFile) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $stdoutLogFile) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $stderrLogFile) | Out-Null
if (Test-Path $stdoutLogFile) {
    Remove-Item $stdoutLogFile -Force
}
if (Test-Path $stderrLogFile) {
    Remove-Item $stderrLogFile -Force
}

$pythonExe = (Get-Command python).Source
$argList = @(
    $serverScript,
    "--host", $BindHost,
    "--port", "$Port",
    "--captures-dir", $captures
)

$process = Start-Process `
    -FilePath $pythonExe `
    -ArgumentList $argList `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $stdoutLogFile `
    -RedirectStandardError $stderrLogFile `
    -WindowStyle Hidden `
    -PassThru

Set-Content -Path $pidFile -Value $process.Id -NoNewline

Start-Sleep -Milliseconds 800

if ($process.HasExited) {
    Write-Error "audio_record_web_server exited immediately, check $stdoutLogFile and $stderrLogFile"
}

$listener = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
    Where-Object { $_.OwningProcess -eq $process.Id } |
    Select-Object -First 1
if (-not $listener) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    Remove-Item $pidFile -Force -ErrorAction SilentlyContinue
    Write-Error "audio_record_web_server did not bind port $Port, check $stdoutLogFile and $stderrLogFile"
}

Write-Host "audio_record_web_server started"
Write-Host "pid: $($process.Id)"
Write-Host "stdout log: $stdoutLogFile"
Write-Host "stderr log: $stderrLogFile"
Write-Host "captures: $captures"
Write-Host "url: http://127.0.0.1:$Port/"
