<# .SYNOPSIS
    Build, deploy, and launch X4 Foundations.
.DESCRIPTION
    Runs build_deploy.ps1, then launches X4.exe if it isn't already running.
    If X4 is already running, skips launch (autoreload will pick up changes).
.EXAMPLE
    .\scripts\build_run.ps1
    .\scripts\build_run.ps1 -Release
    .\scripts\build_run.ps1 -SkipBuild
#>
param(
    [string]$Config = "Debug",
    [switch]$Release,
    [switch]$Clean,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Continue"

if ($Release) { $Config = "Release" }

# Build & deploy
if (-not $SkipBuild) {
    $buildArgs = @{ Config = $Config }
    if ($Clean) { $buildArgs.Clean = $true }
    & "$PSScriptRoot\build_deploy.ps1" @buildArgs
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

# Check if X4 is already running
$x4 = Get-Process -Name "X4" -ErrorAction SilentlyContinue
if ($x4) {
    Write-Host "`nX4 is already running (PID $($x4.Id)) -- autoreload will pick up changes." -ForegroundColor Yellow
    exit 0
}

# Locate X4.exe via CMakeCache
# Walk up from script dir (or cwd) to find build/CMakeCache.txt
$projectRoot = $PSScriptRoot
if ($projectRoot) {
    $projectRoot = Split-Path $projectRoot -Parent
}
if (-not $projectRoot) {
    $projectRoot = $PWD.Path
}
$cacheFile = "$projectRoot\build\CMakeCache.txt"
$gameDir = [string]::Empty
if (Test-Path $cacheFile) {
    foreach ($ln in Get-Content $cacheFile) {
        if ($ln -match "^X4_GAME_DIR:PATH=(.+)$") {
            $gameDir = $Matches[1].Replace("/", "\").Trim()
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($gameDir)) {
    Write-Error "Cannot find X4 game directory -- set X4_GAME_DIR in CMake or launch manually."
    exit 1
}

$x4exe = "$gameDir\X4.exe"
if (-not (Test-Path $x4exe)) {
    Write-Error "Cannot find X4.exe at $x4exe"
    exit 1
}

Write-Host "`n--- Launching X4 ---" -ForegroundColor Green
Start-Process -FilePath $x4exe -ArgumentList "-debug", "scripts", "-logfile", "debuglog.txt", "-skipintro" -WorkingDirectory $gameDir
Write-Host "X4 started from $gameDir" -ForegroundColor Cyan
