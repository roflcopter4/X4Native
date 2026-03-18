<#
.SYNOPSIS
    Extract FFI C declarations from X4's Lua files and produce reference outputs.

.DESCRIPTION
    Parses all ffi.cdef[[ ... ]] blocks from Lua files in reference/game/ui/,
    then produces:
      - reference/x4_ffi_raw.txt      — All raw cdef content (deduplicated blocks)
      - reference/x4_struct_names.txt  — Sorted list of unique struct type names
      - reference/x4_ffi_summary.txt   — Cross-reference statistics

    Requires reference/game/ to be populated first (run extract_game_files.ps1).
    Requires reference/x4_exports.txt to exist (run extract_exports.ps1).

.EXAMPLE
    .\scripts\extract_ffi.ps1
#>
param()

$ErrorActionPreference = 'Stop'

$repoRoot  = Split-Path $PSScriptRoot -Parent
$gameUIDir = Join-Path $repoRoot 'reference\game\ui'
$exportsFile = Join-Path $repoRoot 'reference\x4_exports.txt'

# ---------------------------------------------------------------------------
# Validate prerequisites
# ---------------------------------------------------------------------------
if (-not (Test-Path $gameUIDir)) {
    Write-Error "reference/game/ui/ not found. Run extract_game_files.ps1 first."
    exit 1
}

Write-Host "=== FFI Declaration Extraction ===" -ForegroundColor Cyan
Write-Host "Source   : $gameUIDir"
Write-Host ""

# ---------------------------------------------------------------------------
# Step 1: Extract all ffi.cdef blocks from Lua files
# ---------------------------------------------------------------------------
Write-Host "Extracting ffi.cdef blocks..." -ForegroundColor Green

$luaFiles = Get-ChildItem $gameUIDir -Recurse -Filter '*.lua'
$allBlocks = [System.Collections.Generic.List[string]]::new()

foreach ($file in $luaFiles) {
    $content = Get-Content $file.FullName -Raw -ErrorAction SilentlyContinue
    if (-not $content) { continue }

    $matches = [regex]::Matches($content, '(?s)ffi\.cdef\[\[(.+?)\]\]')
    foreach ($m in $matches) {
        $block = $m.Groups[1].Value.Trim()
        if ($block.Length -gt 0) {
            $allBlocks.Add($block)
        }
    }
}

if ($allBlocks.Count -eq 0) {
    Write-Error "No ffi.cdef blocks found in Lua files."
    exit 1
}

# Write raw FFI content (all blocks, separated by blank lines)
$rawFile = Join-Path $repoRoot 'reference\x4_ffi_raw.txt'
$allBlocks -join "`n`n" | Out-File -FilePath $rawFile -Encoding UTF8
$rawLines = (Get-Content $rawFile).Count

Write-Host "  Found $($allBlocks.Count) cdef blocks ($rawLines lines total)"

# ---------------------------------------------------------------------------
# Step 2: Extract unique struct type names
# ---------------------------------------------------------------------------
Write-Host "Extracting struct type names..." -ForegroundColor Green

$rawContent = Get-Content $rawFile -Raw
$structNames = [regex]::Matches($rawContent, '}\s+(\w+)\s*;') |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique

$structFile = Join-Path $repoRoot 'reference\x4_struct_names.txt'
$structNames | Out-File -FilePath $structFile -Encoding UTF8

Write-Host "  Found $($structNames.Count) unique struct types"

# ---------------------------------------------------------------------------
# Step 3: Extract unique function names from FFI
# ---------------------------------------------------------------------------
Write-Host "Extracting FFI function signatures..." -ForegroundColor Green

# Match C function declarations: return_type FunctionName(...)
$ffiMatches = [regex]::Matches($rawContent, '(?m)^\s*(?:const\s+)?[\w*]+(?:\s+const)?\s*\*?\s+(\w+)\s*\(')
$ffiFunctions = $ffiMatches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$ffiFunctionCount = $ffiFunctions.Count

Write-Host "  Found $ffiFunctionCount unique function declarations"

# ---------------------------------------------------------------------------
# Step 4: Extract unique typedef names (simple typedefs like handles)
# ---------------------------------------------------------------------------
Write-Host "Extracting handle typedefs..." -ForegroundColor Green

$typedefMatches = [regex]::Matches($rawContent, 'typedef\s+(u?int(?:8|16|32|64)_t)\s+(\w+)\s*;')
$typedefs = $typedefMatches | ForEach-Object {
    [PSCustomObject]@{
        BaseType = $_.Groups[1].Value
        Name     = $_.Groups[2].Value
    }
} | Sort-Object Name -Unique

Write-Host "  Found $($typedefs.Count) handle typedefs"

# ---------------------------------------------------------------------------
# Step 5: Cross-reference with PE exports (if available)
# ---------------------------------------------------------------------------
$crossRef = $null
if (Test-Path $exportsFile) {
    Write-Host "Cross-referencing with PE exports..." -ForegroundColor Green

    $exportContent = Get-Content $exportsFile
    $exportNames = $exportContent |
        Where-Object { $_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)' } |
        ForEach-Object { $Matches[1] }
    $exportSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$exportNames)
    $ffiSet    = [System.Collections.Generic.HashSet[string]]::new([string[]]$ffiFunctions)

    $inBoth = ($ffiFunctions | Where-Object { $exportSet.Contains($_) }).Count
    $exportOnly = ($exportNames | Where-Object { -not $ffiSet.Contains($_) }).Count
    $ffiOnly = ($ffiFunctions | Where-Object { -not $exportSet.Contains($_) }).Count

    $crossRef = [PSCustomObject]@{
        TotalExports  = $exportSet.Count
        TotalFFI      = $ffiSet.Count
        InBoth        = $inBoth
        ExportOnly    = $exportOnly
        FFIOnly       = $ffiOnly
    }

    Write-Host "  PE exports      : $($crossRef.TotalExports)"
    Write-Host "  FFI functions   : $($crossRef.TotalFFI)"
    Write-Host "  In both         : $($crossRef.InBoth) (ready to hook)"
    Write-Host "  Export-only     : $($crossRef.ExportOnly) (need manual typing)"
    Write-Host "  FFI-only        : $($crossRef.FFIOnly) (not in export table)"
} else {
    Write-Host "  Skipping cross-reference (x4_exports.txt not found)" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# Step 6: Write summary file
# ---------------------------------------------------------------------------
Write-Host "Writing summary..." -ForegroundColor Green

$summaryFile = Join-Path $repoRoot 'reference\x4_ffi_summary.txt'
$summary = @"
X4 FFI Declaration Summary
===========================

Source: reference/game/ui/**/*.lua
Lua files scanned: $($luaFiles.Count)
cdef blocks found: $($allBlocks.Count)

Functions:  $ffiFunctionCount unique declarations
Structs:    $($structNames.Count) unique type names
Typedefs:   $($typedefs.Count) handle types

Handle Types:
$(($typedefs | ForEach-Object { "  typedef $($_.BaseType) $($_.Name);" }) -join "`n")
"@

if ($crossRef) {
    $summary += @"


Cross-Reference (PE exports vs FFI):
  PE exports:       $($crossRef.TotalExports)
  FFI functions:    $($crossRef.TotalFFI)
  In both:          $($crossRef.InBoth)  (ready to use)
  Export-only:      $($crossRef.ExportOnly) (need manual typing)
  FFI-only:         $($crossRef.FFIOnly)  (not exported)
  Coverage:         $([math]::Round($crossRef.InBoth / $crossRef.TotalExports * 100, 1))% of exports have FFI signatures
"@
}

$summary | Out-File -FilePath $summaryFile -Encoding UTF8

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "=== FFI Extraction Complete ===" -ForegroundColor Cyan
Write-Host "Raw cdef   : $rawFile ($rawLines lines)"
Write-Host "Structs    : $structFile ($($structNames.Count) types)"
Write-Host "Summary    : $summaryFile"
Write-Host ""
