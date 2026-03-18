<#
.SYNOPSIS
    Extract the PE export table from X4.exe into a reference file.

.DESCRIPTION
    Uses dumpbin (MSVC toolchain) to dump all named exports from X4.exe.
    The result is a clean list of function names with ordinals and RVAs,
    saved to reference/x4_exports.txt.

    dumpbin is auto-detected via vswhere. Override with -DumpbinPath.

.PARAMETER GameDir
    Path to X4: Foundations install. Auto-detected from Steam registry if omitted.

.PARAMETER DumpbinPath
    Full path to dumpbin.exe. Auto-detected via vswhere if omitted.

.EXAMPLE
    .\scripts\extract_exports.ps1
    .\scripts\extract_exports.ps1 -GameDir "D:\Games\X4 Foundations"
#>
param(
    [string]$GameDir,
    [string]$DumpbinPath
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent

# ---------------------------------------------------------------------------
# Auto-detect X4 game directory
# ---------------------------------------------------------------------------
if (-not $GameDir) {
    $steamPath = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -Name InstallPath -ErrorAction SilentlyContinue).InstallPath
    if ($steamPath) {
        $candidate = Join-Path $steamPath 'steamapps\common\X4 Foundations'
        if (Test-Path "$candidate\X4.exe") { $GameDir = $candidate }
    }
    if (-not $GameDir) {
        Write-Error "Cannot auto-detect X4 install. Pass -GameDir explicitly."
        exit 1
    }
}

$x4exe = Join-Path $GameDir 'X4.exe'
if (-not (Test-Path $x4exe)) {
    Write-Error "X4.exe not found at: $x4exe"
    exit 1
}

# ---------------------------------------------------------------------------
# Auto-detect dumpbin
# ---------------------------------------------------------------------------
if (-not $DumpbinPath) {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -property installationPath 2>$null
        if ($vsPath) {
            $msvcDir = Get-ChildItem "$vsPath\VC\Tools\MSVC" -Directory |
                Sort-Object Name -Descending | Select-Object -First 1
            if ($msvcDir) {
                $candidate = Join-Path $msvcDir.FullName 'bin\Hostx64\x64\dumpbin.exe'
                if (Test-Path $candidate) { $DumpbinPath = $candidate }
            }
        }
    }
    if (-not $DumpbinPath) {
        Write-Error "Cannot find dumpbin.exe. Install VS2022 BuildTools or pass -DumpbinPath."
        exit 1
    }
}

# ---------------------------------------------------------------------------
# Read game version
# ---------------------------------------------------------------------------
$versionFile = Join-Path $GameDir 'version.dat'
$gameVersion = if (Test-Path $versionFile) { (Get-Content $versionFile -Raw).Trim() } else { 'unknown' }

# ---------------------------------------------------------------------------
# Run dumpbin
# ---------------------------------------------------------------------------
$outFile = Join-Path $repoRoot 'reference\x4_exports.txt'

Write-Host "=== PE Export Extraction ===" -ForegroundColor Cyan
Write-Host "X4.exe   : $x4exe"
Write-Host "dumpbin  : $DumpbinPath"
Write-Host "Version  : $gameVersion"
Write-Host "Output   : $outFile"
Write-Host ""

Write-Host "Running dumpbin /exports..." -ForegroundColor Green
$raw = & $DumpbinPath /exports $x4exe 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Error "dumpbin failed with exit code $LASTEXITCODE"
    exit 1
}

# Sanitize: replace local filesystem paths with just the filename
$sanitized = $raw | ForEach-Object { $_ -replace [regex]::Escape($x4exe), 'X4.exe' }

# Write sanitized output
$sanitized | Out-File -FilePath $outFile -Encoding UTF8

# Count exports (lines matching the "ordinal hint RVA name" pattern)
$exportCount = ($raw | Where-Object { $_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+\S' }).Count

Write-Host ""
Write-Host "=== Export Extraction Complete ===" -ForegroundColor Cyan
Write-Host "Version  : $gameVersion"
Write-Host "Exports  : $exportCount named functions"
Write-Host "Output   : $outFile"
Write-Host ""
