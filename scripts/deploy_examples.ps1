<#
.SYNOPSIS
    Build and deploy example extensions to the game directory.
    Deploys all examples by default, or a single one if -Example is specified.
.EXAMPLE
    .\scripts\deploy_example.ps1
    .\scripts\deploy_example.ps1 -Example hello
    .\scripts\deploy_example.ps1 -Example hello -Release
#>
param(
    [string]$Example = "",
    [string]$Config = "Debug",
    [switch]$Release
)

$ErrorActionPreference = "Stop"

if ($Release) { $Config = "Release" }

# Ensure CMake is on PATH
$vsRoot = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$cmakeBin = "$vsRoot\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if (Test-Path $cmakeBin) { $env:PATH = "$cmakeBin;$env:PATH" }

$root = Split-Path $PSScriptRoot -Parent

# Resolve game dir
$steamKey = "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam"
$steamDir = (Get-ItemProperty $steamKey -ErrorAction SilentlyContinue).InstallPath
$gameDir  = if ($steamDir) { Join-Path $steamDir "steamapps\common\X4 Foundations" } else { "" }

if (-not $gameDir -or -not (Test-Path "$gameDir\X4.exe")) {
    Write-Error "Could not locate X4 Foundations."
    exit 1
}

# Collect examples to deploy
if ($Example) {
    $targets = @($Example)
} else {
    $targets = Get-ChildItem (Join-Path $root "examples") -Directory | Select-Object -ExpandProperty Name
}

function Deploy-Example($name) {
    $exDir    = Join-Path $root "examples\$name"
    $buildDir = Join-Path $root "build\examples\$name"
    $extName  = "x4native_$name"
    $deployDir = Join-Path $gameDir "extensions\$extName"

    if (-not (Test-Path $exDir)) {
        Write-Warning "Example '$name' not found at $exDir - skipping"
        return
    }

    Write-Host "`n=== $name ($Config) ===" -ForegroundColor Cyan

    # Configure
    if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
        Write-Host "--- Configure ---" -ForegroundColor Green
        cmake -S $exDir -B $buildDir -G "Visual Studio 17 2022" -A x64
        if ($LASTEXITCODE -ne 0) { Write-Error "Configure failed for $name"; exit 1 }
    }

    # Build
    Write-Host "--- Build ---" -ForegroundColor Green
    cmake --build $buildDir --config $Config
    if ($LASTEXITCODE -ne 0) { Write-Error "Build failed for $name"; exit 1 }

    # Deploy
    New-Item -ItemType Directory -Force -Path "$deployDir\native" | Out-Null
    Copy-Item "$exDir\content.xml"   "$deployDir\content.xml"   -Force
    Copy-Item "$exDir\x4native.json" "$deployDir\x4native.json" -Force

    $dll = "$buildDir\$Config\x4native_$name.dll"
    if (-not (Test-Path $dll)) { $dll = "$buildDir\x4native_$name.dll" }
    if (-not (Test-Path $dll)) { Write-Error "DLL not found for $name"; exit 1 }

    Copy-Item $dll "$deployDir\native\x4native_$name.dll" -Force
    Write-Host "Deployed -> $deployDir" -ForegroundColor Green
}

foreach ($t in $targets) {
    Deploy-Example $t
}

$n = $targets.Count
Write-Host "`n=== Done: $n examples deployed ===" -ForegroundColor Cyan
