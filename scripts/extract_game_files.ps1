<#
.SYNOPSIS
    Extract text-based game files from X4: Foundations cat/dat archives.

.DESCRIPTION
    Uses Egosoft's XRCatTool to extract Lua scripts, XML (MD cues, AI scripts,
    libraries, UI definitions), schemas, and other text files from the base game
    and installed DLC extensions.

    Run this after a game update to refresh the reference dump. Use git diff to
    see what changed between versions.

.PARAMETER GameDir
    Path to X4: Foundations install. Auto-detected from Steam registry if omitted.

.PARAMETER ToolDir
    Path to X Tools install (contains XRCatTool.exe). Auto-detected if omitted.

.PARAMETER OutDir
    Output directory. Defaults to temp/x4_game_dump under the repo root.

.PARAMETER GameVersion
    Override the version string written to reference/game/VERSION.
    Used by update_references.ps1 to inject a beta suffix (e.g. "900-beta2")
    when version.dat has not been bumped by the patch.

.EXAMPLE
    .\scripts\extract_game_files.ps1
    .\scripts\extract_game_files.ps1 -GameDir "D:\Games\X4 Foundations"
#>
param(
    [string]$GameDir,
    [string]$ToolDir,
    [string]$OutDir,
    [string]$GameVersion
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Auto-detect paths
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

if (-not $ToolDir) {
    $steamPath = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -Name InstallPath -ErrorAction SilentlyContinue).InstallPath
    if ($steamPath) {
        $candidate = Join-Path $steamPath 'steamapps\common\X Tools'
        if (Test-Path "$candidate\XRCatTool.exe") { $ToolDir = $candidate }
    }
    if (-not $ToolDir) {
        Write-Error "Cannot find X Tools. Install via Steam (Library > Tools) or pass -ToolDir."
        exit 1
    }
}

$XRCat = Join-Path $ToolDir 'XRCatTool.exe'

if (-not $OutDir) {
    $repoRoot = Split-Path $PSScriptRoot -Parent
    $OutDir = Join-Path $repoRoot 'reference\game'
}

# ---------------------------------------------------------------------------
# Read game version (use override if provided, e.g. for beta builds)
# ---------------------------------------------------------------------------
if (-not $GameVersion) {
    $versionFile = Join-Path $GameDir 'version.dat'
    $GameVersion = if (Test-Path $versionFile) { (Get-Content $versionFile -Raw).Trim() } else { 'unknown' }
}

Write-Host "=== X4 Game File Extractor ===" -ForegroundColor Cyan
Write-Host "Game dir : $GameDir"
Write-Host "X Tools  : $ToolDir"
Write-Host "Output   : $OutDir"
Write-Host "Version  : $GameVersion"
Write-Host ""

# ---------------------------------------------------------------------------
# Prepare output directory
# ---------------------------------------------------------------------------
if (Test-Path $OutDir) {
    Write-Host "Clearing previous extraction..." -ForegroundColor Yellow
    Remove-Item "$OutDir\*" -Recurse -Force
} else {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

# Write version marker (useful for git commit messages)
Set-Content -Path (Join-Path $OutDir 'VERSION') -Value $GameVersion

# ---------------------------------------------------------------------------
# Include/exclude patterns
# ---------------------------------------------------------------------------
# We want:  md, aiscripts, libraries, index, ui (Lua + XML + schemas)
# We drop:  assets, music, sfx, voice, textures, particles, shaders, maps,
#           cutscenes, and any binary formats inside the included dirs.
$includePattern = '^(md|aiscripts|libraries|index|ui)'
$excludePattern = '\.(sig|gz|xmf|amw|bsg|dae|bgf|bgp|dds|jpg|png|tga|ogg|wav|wem|bnk|xpl)$'

# ---------------------------------------------------------------------------
# Extract base game (all numbered cats, merged so later overrides earlier)
# ---------------------------------------------------------------------------
Write-Host "Extracting base game files..." -ForegroundColor Green
$baseCats = Get-ChildItem "$GameDir\*.cat" |
    Where-Object { $_.Name -notmatch '_sig' } |
    Sort-Object Name

$output = & $XRCat -in @($baseCats.FullName) -out $OutDir `
    -include $includePattern -exclude $excludePattern 2>&1
$output | Where-Object { $_ -match 'Writing' } |
    ForEach-Object { Write-Host "  $_" }

# ---------------------------------------------------------------------------
# Extract from each official DLC extension (ego_dlc_* only)
# ---------------------------------------------------------------------------
$extDir = Join-Path $GameDir 'extensions'
if (Test-Path $extDir) {
    $extensions = Get-ChildItem $extDir -Directory | Where-Object { $_.Name -like 'ego_dlc_*' }
    foreach ($ext in $extensions) {
        $cats = Get-ChildItem "$($ext.FullName)\*.cat" -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -notmatch '_sig' } |
            Sort-Object Name
        if (-not $cats) { continue }

        Write-Host "Extracting extension: $($ext.Name)..." -ForegroundColor Green
        $extOut = Join-Path $OutDir "extensions\$($ext.Name)"
        New-Item -ItemType Directory -Path $extOut -Force | Out-Null

        $output = & $XRCat -in @($cats.FullName) -out $extOut `
            -include $includePattern -exclude $excludePattern 2>&1
        $output | Where-Object { $_ -match 'Writing' } |
            ForEach-Object { Write-Host "  $_" }
    }
}

# ---------------------------------------------------------------------------
# Clean up: remove empty directories and any binary stragglers
# ---------------------------------------------------------------------------
Write-Host "Cleaning up..." -ForegroundColor Yellow
do {
    $empty = Get-ChildItem $OutDir -Recurse -Directory |
        Where-Object { (Get-ChildItem $_.FullName -File -Recurse).Count -eq 0 }
    $empty | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
} while ($empty.Count -gt 0)

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
$totalFiles = (Get-ChildItem $OutDir -Recurse -File).Count
$breakdown = Get-ChildItem $OutDir -Directory |
    ForEach-Object {
        $c = (Get-ChildItem $_.FullName -Recurse -File).Count
        [PSCustomObject]@{ Directory = $_.Name; Files = $c }
    } | Sort-Object Files -Descending

Write-Host ""
Write-Host "=== Extraction Complete ===" -ForegroundColor Cyan
Write-Host "Version : $GameVersion"
Write-Host "Total   : $totalFiles files"
Write-Host ""
$breakdown | Format-Table -AutoSize

Write-Host "Done. Use 'git diff' to compare with previous extraction." -ForegroundColor Green
