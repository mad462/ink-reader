param(
    [int]$Port = 8080,
    [string]$PidFile = ""
)

$ErrorActionPreference = "Stop"

$pidFile = if ([string]::IsNullOrWhiteSpace($PidFile)) {
    Join-Path $PSScriptRoot "audio_record_web_server.pid"
} else {
    $PidFile
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
            Write-Host "stopped listener on port $TargetPort, pid=$($process.Id)"
        }
    }
}

if (-not (Test-Path $pidFile)) {
    Stop-ListenerOnPort -TargetPort $Port
    Write-Host "audio_record_web_server is not running"
    exit 0
}

$pidText = (Get-Content $pidFile -Raw).Trim()
if (-not $pidText) {
    Remove-Item $pidFile -Force -ErrorAction SilentlyContinue
    Write-Host "stale pid file removed"
    exit 0
}

$process = Get-Process -Id ([int]$pidText) -ErrorAction SilentlyContinue
if ($process) {
    Stop-Process -Id $process.Id -Force
    Write-Host "audio_record_web_server stopped, pid=$pidText"
} else {
    Write-Host "audio_record_web_server process not found, removing stale pid file"
}

Remove-Item $pidFile -Force -ErrorAction SilentlyContinue
Stop-ListenerOnPort -TargetPort $Port
