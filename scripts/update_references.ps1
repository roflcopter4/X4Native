<#
.SYNOPSIS
    Master script: update all X4 reference data after a game patch.

.DESCRIPTION
    Runs the full reference extraction pipeline in order:
      1. extract_game_files.ps1  — Lua/XML from cat/dat archives
      2. extract_exports.ps1     — PE export table from X4.exe
      3. extract_ffi.ps1         — FFI cdef parsing and cross-reference
      4. generate_headers.ps1    — Auto-generate C headers from FFI data
      5. generate_version_db.ps1 — Version metadata (func_history, type_changes)

    All parameters are auto-detected but can be overridden.
    After running, use 'git diff reference/' to review changes.

.PARAMETER GameDir
    Path to X4: Foundations install. Auto-detected from Steam registry if omitted.

.PARAMETER ToolDir
    Path to X Tools install (contains XRCatTool.exe). Auto-detected if omitted.

.PARAMETER DumpbinPath
    Full path to dumpbin.exe. Auto-detected via vswhere if omitted.

.PARAMETER SkipGameFiles
    Skip the game file extraction step (useful if you only want to refresh exports/FFI).

.PARAMETER SkipExports
    Skip the PE export extraction step.

.PARAMETER SkipFFI
    Skip the FFI extraction step.

.PARAMETER SkipHeaders
    Skip the header generation step.

.PARAMETER SkipVersionDb
    Skip the version_db metadata generation step.

.EXAMPLE
    .\scripts\update_references.ps1
    .\scripts\update_references.ps1 -SkipGameFiles
    .\scripts\update_references.ps1 -GameDir "D:\Games\X4 Foundations"
#>
param(
    [string]$GameDir,
    [string]$ToolDir,
    [string]$DumpbinPath,
    [switch]$SkipGameFiles,
    [switch]$SkipExports,
    [switch]$SkipFFI,
    [switch]$SkipHeaders,
    [switch]$SkipVersionDb
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent

# ---------------------------------------------------------------------------
# Auto-detect game directory (shared across steps)
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

# Read game version for display
$versionFile = Join-Path $GameDir 'version.dat'
$gameVersion = if (Test-Path $versionFile) { (Get-Content $versionFile -Raw).Trim() } else { 'unknown' }
$versionDisplay = if ($gameVersion -match '^\d+$' -and $gameVersion.Length -ge 3) {
    "v$($gameVersion.Substring(0, $gameVersion.Length - 2)).$($gameVersion.Substring($gameVersion.Length - 2)) (build $gameVersion)"
} else {
    $gameVersion
}

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  X4Native Reference Update Pipeline" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  Game dir : $GameDir"
Write-Host "  Version  : $versionDisplay"
Write-Host "  Repo     : $repoRoot"
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

$steps = @()
if (-not $SkipGameFiles) { $steps += 'game_files' }
if (-not $SkipExports)   { $steps += 'exports' }
if (-not $SkipFFI)       { $steps += 'ffi' }
if (-not $SkipHeaders)   { $steps += 'headers' }
if (-not $SkipVersionDb) { $steps += 'version_db' }

if ($steps.Count -eq 0) {
    Write-Host "All steps skipped. Nothing to do." -ForegroundColor Yellow
    exit 0
}

$stepNum = 0
$totalSteps = $steps.Count

# ---------------------------------------------------------------------------
# Step 1: Extract game files
# ---------------------------------------------------------------------------
if ($steps -contains 'game_files') {
    $stepNum++
    Write-Host "[$stepNum/$totalSteps] Extracting game files..." -ForegroundColor Cyan
    Write-Host "------------------------------------------------------------"

    $gameFileArgs = @{}
    if ($GameDir) { $gameFileArgs['GameDir'] = $GameDir }
    if ($ToolDir) { $gameFileArgs['ToolDir'] = $ToolDir }

    & "$PSScriptRoot\extract_game_files.ps1" @gameFileArgs
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        Write-Error "extract_game_files.ps1 failed"
        exit 1
    }
    Write-Host ""
}

# ---------------------------------------------------------------------------
# Step 2: Extract PE exports
# ---------------------------------------------------------------------------
if ($steps -contains 'exports') {
    $stepNum++
    Write-Host "[$stepNum/$totalSteps] Extracting PE exports..." -ForegroundColor Cyan
    Write-Host "------------------------------------------------------------"

    $exportArgs = @{}
    if ($GameDir)      { $exportArgs['GameDir'] = $GameDir }
    if ($DumpbinPath)  { $exportArgs['DumpbinPath'] = $DumpbinPath }

    & "$PSScriptRoot\extract_exports.ps1" @exportArgs
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        Write-Error "extract_exports.ps1 failed"
        exit 1
    }
    Write-Host ""
}

# ---------------------------------------------------------------------------
# Step 3: Extract FFI declarations and cross-reference
# ---------------------------------------------------------------------------
if ($steps -contains 'ffi') {
    $stepNum++
    Write-Host "[$stepNum/$totalSteps] Extracting FFI declarations..." -ForegroundColor Cyan
    Write-Host "------------------------------------------------------------"

    & "$PSScriptRoot\extract_ffi.ps1"
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        Write-Error "extract_ffi.ps1 failed"
        exit 1
    }
    Write-Host ""
}

# ---------------------------------------------------------------------------
# Step 4: Generate C headers
# ---------------------------------------------------------------------------
if ($steps -contains 'headers') {
    $stepNum++
    Write-Host "[$stepNum/$totalSteps] Generating C headers..." -ForegroundColor Cyan
    Write-Host "------------------------------------------------------------"

    & "$PSScriptRoot\generate_headers.ps1"
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        Write-Error "generate_headers.ps1 failed"
        exit 1
    }
    Write-Host ""
}

# ---------------------------------------------------------------------------
# Step 5: Generate version_db metadata
# ---------------------------------------------------------------------------
if ($steps -contains 'version_db') {
    $stepNum++
    Write-Host "[$stepNum/$totalSteps] Generating version_db metadata..." -ForegroundColor Cyan
    Write-Host "------------------------------------------------------------"

    & "$PSScriptRoot\generate_version_db.ps1"
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        Write-Error "generate_version_db.ps1 failed"
        exit 1
    }
    Write-Host ""
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  Pipeline Complete" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

# Show reference file sizes
$refDir = Join-Path $repoRoot 'reference'
$refFiles = @(
    'x4_exports.txt',
    'x4_ffi_raw.txt',
    'x4_struct_names.txt',
    'x4_ffi_summary.txt'
)
foreach ($f in $refFiles) {
    $path = Join-Path $refDir $f
    if (Test-Path $path) {
        $size = (Get-Item $path).Length
        $sizeKB = [math]::Round($size / 1024, 1)
        Write-Host "  $($f.PadRight(25)) ${sizeKB} KB"
    }
}

$gameFileCount = if (Test-Path (Join-Path $refDir 'game')) {
    (Get-ChildItem (Join-Path $refDir 'game') -Recurse -File).Count
} else { 0 }
Write-Host "  game/ files              $gameFileCount"

# Show generated header sizes
$sdkDir = Join-Path $repoRoot 'sdk'
$headerFiles = @('x4_game_types.h', 'x4_game_func_list.inc', 'x4_game_func_table.h')
foreach ($f in $headerFiles) {
    $path = Join-Path $sdkDir $f
    if (Test-Path $path) {
        $size = (Get-Item $path).Length
        $sizeKB = [math]::Round($size / 1024, 1)
        Write-Host "  sdk/$($f.PadRight(23)) ${sizeKB} KB"
    }
}

# Show version_db sizes
$vdbDir = Join-Path $repoRoot 'native\version_db'
foreach ($f in @('func_history.json', 'type_changes.json')) {
    $path = Join-Path $vdbDir $f
    if (Test-Path $path) {
        $size = (Get-Item $path).Length
        $sizeKB = [math]::Round($size / 1024, 1)
        Write-Host "  version_db/$($f.PadRight(18)) ${sizeKB} KB"
    }
}

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  git diff reference/ sdk/                      # Review changes"
Write-Host "  git add reference/ sdk/ ; git commit -m 'reference: update to $versionDisplay'"
Write-Host ""
