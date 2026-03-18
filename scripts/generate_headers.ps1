<#
.SYNOPSIS
    Generate C headers from X4's FFI declarations and PE export data.

.DESCRIPTION
    Parses reference/x4_ffi_raw.txt to extract types and function signatures,
    cross-references with reference/x4_exports.txt, and generates:
      - sdk/x4_game_types.h       — Handle typedefs + struct definitions (dep-ordered)
      - sdk/x4_game_func_list.inc — X-macro function list
      - sdk/x4_game_func_table.h  — Function pointer table struct

    Requires extract_game_files.ps1, extract_exports.ps1, and extract_ffi.ps1
    to have been run first.

.EXAMPLE
    .\scripts\generate_headers.ps1
#>
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent

# Input files
$rawFile     = Join-Path $repoRoot 'reference\x4_ffi_raw.txt'
$exportsFile = Join-Path $repoRoot 'reference\x4_exports.txt'
$versionFile = Join-Path $repoRoot 'reference\game\VERSION'

# Output files
$sdkDir        = Join-Path $repoRoot 'sdk'
$typesOut      = Join-Path $sdkDir 'x4_game_types.h'
$funcListOut   = Join-Path $sdkDir 'x4_game_func_list.inc'
$funcTableOut  = Join-Path $sdkDir 'x4_game_func_table.h'

# Validate prerequisites
if (-not (Test-Path $rawFile)) {
    Write-Error "reference/x4_ffi_raw.txt not found. Run extract_ffi.ps1 first."
    exit 1
}
if (-not (Test-Path $exportsFile)) {
    Write-Error "reference/x4_exports.txt not found. Run extract_exports.ps1 first."
    exit 1
}

$gameVersion = if (Test-Path $versionFile) { (Get-Content $versionFile -Raw).Trim() } else { 'unknown' }
$versionDisplay = if ($gameVersion -match '^\d{3,}$') {
    "v$($gameVersion.Substring(0, $gameVersion.Length - 2)).$($gameVersion.Substring($gameVersion.Length - 2)) (build $gameVersion)"
} else { $gameVersion }

Write-Host "=== Header Generation ===" -ForegroundColor Cyan
Write-Host "Source   : $rawFile"
Write-Host "Exports  : $exportsFile"
Write-Host "Version  : $versionDisplay"
Write-Host ""

# ---------------------------------------------------------------------------
# Known fixups
# ---------------------------------------------------------------------------

# C++ reserved words used as parameter names
$cppReservedFixups = @{
    'CreateOrder' = @{ ' default)' = ' defaultorder)' }
}

# Non-game struct name patterns (LuaJIT internals for PE/ELF/Mach-O parsing)
$nonGameStructPatterns = @('^ELF32', '^ELF64', '^PE(header|obj|section|sym)', '^mach_', '^MachO')

# ===========================================================================
# PHASE 1: Parse FFI raw data
# ===========================================================================
Write-Host "Parsing FFI declarations..." -ForegroundColor Green

$lines = Get-Content $rawFile

$state = 'idle'
$structAccum = [System.Collections.Generic.List[string]]::new()
$funcAccum = $null

$handleTypedefs = [ordered]@{}   # name -> full typedef line
$structMap      = [ordered]@{}   # name -> @{ Name; Lines; Body }
$funcMap        = [ordered]@{}   # name -> full declaration line

foreach ($line in $lines) {
    # ---- Inside a struct definition ----
    if ($state -eq 'in_struct') {
        $structAccum.Add($line)
        if ($line -match '}\s*(\w+)\s*;') {
            $sname = $Matches[1]
            $state = 'idle'
            if (-not $structMap.Contains($sname)) {
                $allLines = $structAccum.ToArray()
                $bodyLines = if ($allLines.Count -gt 2) {
                    $allLines[1..($allLines.Count - 2)]
                } else { @() }
                $structMap[$sname] = @{
                    Name  = $sname
                    Lines = $allLines
                    Body  = ($bodyLines | ForEach-Object { $_.Trim() }) -join ' '
                }
            }
            $structAccum.Clear()
        }
        continue
    }

    # ---- Idle state ----
    if ([string]::IsNullOrWhiteSpace($line)) { $funcAccum = $null; continue }

    # Handle typedef: typedef uint64_t UniverseID;
    if ($line -match '^\s*typedef\s+(u?int(?:8|16|32|64)_t)\s+(\w+)\s*;') {
        if (-not $handleTypedefs.Contains($Matches[2])) {
            $handleTypedefs[$Matches[2]] = "typedef $($Matches[1]) $($Matches[2]);"
        }
        continue
    }

    # Struct start: typedef struct { (brace on same line or next line)
    if ($line -match '^\s*typedef\s+struct\s*\{') {
        # Check for single-line struct
        if ($line -match '}\s*(\w+)\s*;') {
            $sname = $Matches[1]
            if (-not $structMap.Contains($sname)) {
                $body = $line -replace '^\s*typedef\s+struct\s*\{\s*', '' -replace '\s*}\s*\w+\s*;\s*$', ''
                $structMap[$sname] = @{
                    Name  = $sname
                    Lines = @($line.Trim())
                    Body  = $body
                }
            }
        } else {
            $state = 'in_struct'
            $structAccum.Clear()
            $structAccum.Add($line)
        }
        continue
    }

    # Struct start: typedef struct (brace on NEXT line)
    if ($line -match '^\s*typedef\s+struct\s*$') {
        $state = 'in_struct'
        $structAccum.Clear()
        $structAccum.Add($line)
        continue
    }

    # Multi-line function continuation
    if ($funcAccum) {
        $funcAccum += ' ' + $line.Trim()
        if ($funcAccum -match '\)\s*;') {
            if ($funcAccum -match '(\w+)\s*\(') {
                $fname = $Matches[1]
                if (-not $funcMap.Contains($fname)) {
                    $funcMap[$fname] = $funcAccum
                }
            }
            $funcAccum = $null
        }
        continue
    }

    # Function declaration (has parenthesis, not a typedef)
    if ($line -match '\(' -and $line -notmatch '^\s*typedef') {
        $trimmed = $line.Trim()
        if ($trimmed -match '\)\s*;') {
            # Complete single-line declaration
            if ($trimmed -match '(\w+)\s*\(') {
                $fname = $Matches[1]
                if (-not $funcMap.Contains($fname)) {
                    $funcMap[$fname] = $trimmed
                }
            }
        } else {
            # Multi-line — start accumulating
            $funcAccum = $trimmed
        }
    }
}

Write-Host "  Handle typedefs: $($handleTypedefs.Count)"
Write-Host "  Structs:         $($structMap.Count) (raw, before filtering)"
Write-Host "  Functions:       $($funcMap.Count)"

# ===========================================================================
# PHASE 2: Filter non-game structs
# ===========================================================================
Write-Host "Filtering non-game types..." -ForegroundColor Green

$gameStructs = [ordered]@{}
foreach ($sname in @($structMap.Keys)) {
    $skip = $false
    foreach ($pat in $nonGameStructPatterns) {
        if ($sname -match $pat) { $skip = $true; break }
    }
    if (-not $skip -and ($structMap[$sname].Lines -join ' ') -match '__attribute') {
        $skip = $true
    }
    if (-not $skip) {
        $gameStructs[$sname] = $structMap[$sname]
    }
}

$filteredCount = $structMap.Count - $gameStructs.Count
Write-Host "  Game structs: $($gameStructs.Count) (filtered $filteredCount non-game)"

# ===========================================================================
# PHASE 3: Dependency sort (topological)
# ===========================================================================
Write-Host "Sorting by dependency..." -ForegroundColor Green

$sNames = [System.Collections.Generic.HashSet[string]]::new([string[]]@($gameStructs.Keys))

# Build dependency map: for each struct, find which other structs it references
$deps = @{}
foreach ($sname in $gameStructs.Keys) {
    $body = $gameStructs[$sname].Body
    $myDeps = [System.Collections.Generic.List[string]]::new()
    foreach ($other in $sNames) {
        if ($other -ne $sname -and $body -match "\b$([regex]::Escape($other))\b") {
            $myDeps.Add($other)
        }
    }
    $deps[$sname] = $myDeps
}

# Kahn's algorithm
$inDeg = @{}
$adj   = @{}
foreach ($sname in $gameStructs.Keys) {
    $inDeg[$sname] = 0
    $adj[$sname] = [System.Collections.Generic.List[string]]::new()
}
foreach ($sname in $gameStructs.Keys) {
    foreach ($dep in $deps[$sname]) {
        if ($adj.ContainsKey($dep)) {
            $adj[$dep].Add($sname)
            $inDeg[$sname]++
        }
    }
}

$queue = [System.Collections.Generic.Queue[string]]::new()
foreach ($sname in $gameStructs.Keys) {
    if ($inDeg[$sname] -eq 0) { $queue.Enqueue($sname) }
}

$sorted = [System.Collections.Generic.List[string]]::new()
while ($queue.Count -gt 0) {
    $node = $queue.Dequeue()
    $sorted.Add($node)
    foreach ($nbr in $adj[$node]) {
        $inDeg[$nbr]--
        if ($inDeg[$nbr] -eq 0) { $queue.Enqueue($nbr) }
    }
}

if ($sorted.Count -ne $gameStructs.Count) {
    $remaining = $gameStructs.Keys | Where-Object { $_ -notin $sorted }
    Write-Warning "Cycle detected - appending: $($remaining -join ', ')"
    foreach ($r in $remaining) { $sorted.Add($r) }
}

$depTotal = ($deps.Values | ForEach-Object { $_.Count } | Measure-Object -Sum).Sum
Write-Host "  $($sorted.Count) structs sorted ($depTotal dependency edges)"

# ===========================================================================
# PHASE 4: Cross-reference with PE exports
# ===========================================================================
Write-Host "Cross-referencing with exports..." -ForegroundColor Green

$exportNames = Get-Content $exportsFile |
    Where-Object { $_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)' } |
    ForEach-Object { $Matches[1] }
$exportSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$exportNames)

# Typed: in both FFI and exports (ready to use)
$typedFuncs = [ordered]@{}
foreach ($fname in $funcMap.Keys) {
    if ($exportSet.Contains($fname)) {
        $typedFuncs[$fname] = $funcMap[$fname]
    }
}

# Untyped: in exports but not in FFI
$ffiNameSet = [System.Collections.Generic.HashSet[string]]::new([string[]]@($funcMap.Keys))
$untypedList = ($exportNames | Where-Object { -not $ffiNameSet.Contains($_) }) | Sort-Object -Unique

# Classify untyped: has FFI sibling vs truly unknown
$withSibling = [System.Collections.Generic.List[PSCustomObject]]::new()
$noSibling   = [System.Collections.Generic.List[string]]::new()

foreach ($uname in $untypedList) {
    $base = $uname -replace '\d+$', ''
    $sibling = $funcMap.Keys |
        Where-Object { ($_ -replace '\d+$', '') -eq $base -and $_ -ne $uname } |
        Select-Object -First 1
    if ($sibling) {
        $withSibling.Add([PSCustomObject]@{ Name = $uname; Sibling = $sibling })
    } else {
        $noSibling.Add($uname)
    }
}

Write-Host "  Typed:              $($typedFuncs.Count)"
Write-Host "  Untyped (sibling):  $($withSibling.Count)"
Write-Host "  Untyped (unknown):  $($noSibling.Count)"

# ===========================================================================
# PHASE 5: Apply fixups
# ===========================================================================
Write-Host "Applying fixups..." -ForegroundColor Green
$fixCount = 0

# C++ reserved word fixups
foreach ($fname in @($typedFuncs.Keys)) {
    if ($cppReservedFixups.ContainsKey($fname)) {
        foreach ($old in $cppReservedFixups[$fname].Keys) {
            $new = $cppReservedFixups[$fname][$old]
            $decl = $typedFuncs[$fname]
            if ($decl.Contains($old)) {
                $typedFuncs[$fname] = $decl.Replace($old, $new)
                $fixCount++
            }
        }
    }
}

# Normalize empty param lists: () -> (void)
foreach ($fname in @($typedFuncs.Keys)) {
    $decl = $typedFuncs[$fname]
    if ($decl -match '\(\s*\)\s*;$') {
        $typedFuncs[$fname] = $decl -replace '\(\s*\)\s*;$', '(void);'
        $fixCount++
    }
}

Write-Host "  Applied $fixCount fixups"

# ===========================================================================
# PHASE 6: Emit headers
# ===========================================================================
Write-Host "Writing headers..." -ForegroundColor Green

# ---- x4_game_types.h ----
$sb = [System.Text.StringBuilder]::new(256KB)
[void]$sb.AppendLine("// ==========================================================================")
[void]$sb.AppendLine("// x4_game_types.h - X4: Foundations Game Type Definitions")
[void]$sb.AppendLine("// ==========================================================================")
[void]$sb.AppendLine("// Auto-generated from X4 $versionDisplay FFI declarations.")
[void]$sb.AppendLine("// Source: reference/x4_ffi_raw.txt")
[void]$sb.AppendLine("//")
[void]$sb.AppendLine("// DO NOT EDIT - regenerate with: .\scripts\generate_headers.ps1")
[void]$sb.AppendLine("// ==========================================================================")
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("// Game build version these types were extracted from.")
[void]$sb.AppendLine("// Extensions can use this to guard against struct layout mismatches.")
[void]$sb.AppendLine("#define X4_GAME_TYPES_BUILD $gameVersion")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#include <stdint.h>")
[void]$sb.AppendLine("#include <stdbool.h>")
[void]$sb.AppendLine("#include <stddef.h>")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#ifdef __cplusplus")
[void]$sb.AppendLine('extern "C" {')
[void]$sb.AppendLine("#endif")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("// --------------------------------------------------------------------------")
[void]$sb.AppendLine("// Handle Types ($($handleTypedefs.Count))")
[void]$sb.AppendLine("// --------------------------------------------------------------------------")
[void]$sb.AppendLine("")
foreach ($td in $handleTypedefs.Values) {
    [void]$sb.AppendLine($td)
}
[void]$sb.AppendLine("")
[void]$sb.AppendLine("// --------------------------------------------------------------------------")
[void]$sb.AppendLine("// Struct Types ($($gameStructs.Count) unique, dependency-ordered)")
[void]$sb.AppendLine("// --------------------------------------------------------------------------")
[void]$sb.AppendLine("")
foreach ($sname in $sorted) {
    foreach ($sl in $gameStructs[$sname].Lines) {
        # Replace tabs with 4 spaces, trim trailing whitespace
        $normalized = ($sl -replace "`t", '    ').TrimEnd()
        # Normalize: typedef/closing lines flush left, fields indented 4 spaces
        if ($normalized -match '^\s*typedef\s+struct') {
            $normalized = $normalized.TrimStart()
        } elseif ($normalized -match '^\s*}\s*\w+\s*;') {
            $normalized = $normalized.TrimStart()
        } else {
            # Field line: ensure exactly 4 spaces indent
            $normalized = '    ' + $normalized.TrimStart()
        }
        [void]$sb.AppendLine($normalized)
    }
    [void]$sb.AppendLine("")
}
[void]$sb.AppendLine("#ifdef __cplusplus")
[void]$sb.AppendLine("}")
[void]$sb.AppendLine("#endif")

[System.IO.File]::WriteAllText($typesOut, $sb.ToString())
$typesSize = [math]::Round((Get-Item $typesOut).Length / 1024, 1)

# ---- x4_game_func_list.inc (INCREMENTAL / APPEND-ONLY) ----
# The .inc file preserves field order across game versions. New functions are
# appended; removed functions stay as comments. This guarantees that the
# X4GameFunctions struct layout is ABI-stable for all compiled extensions.
[void]$sb.Clear()

$existingFuncOrder = [System.Collections.Generic.List[string]]::new()
$existingFuncSet   = [System.Collections.Generic.HashSet[string]]::new()
$existingRawLines  = @()
$isIncremental     = $false

if (Test-Path $funcListOut) {
    $existingRawLines = Get-Content $funcListOut
    foreach ($el in $existingRawLines) {
        if ($el -match '^X4_FUNC\(\s*(.+?),\s*(\w+),') {
            $existingFuncOrder.Add($Matches[2])
            [void]$existingFuncSet.Add($Matches[2])
        }
    }
    if ($existingFuncSet.Count -gt 0) {
        $isIncremental = $true
    }
}

if ($isIncremental) {
    Write-Host "  Incremental mode: $($existingFuncSet.Count) existing functions" -ForegroundColor Yellow

    # Build signature map from existing .inc lines for change detection
    $existingSigMap = @{}
    foreach ($el in $existingRawLines) {
        if ($el -match '^X4_FUNC\(\s*(.+?),\s*(\w+),\s*\((.+?)\)\s*\)') {
            $existingSigMap[$Matches[2]] = "$($Matches[1].Trim()) ($($Matches[3].Trim()))"
        }
    }

    # Find new and removed functions
    $newFuncs = [System.Collections.Generic.List[string]]::new()
    foreach ($fname in ($typedFuncs.Keys | Sort-Object)) {
        if (-not $existingFuncSet.Contains($fname)) {
            $newFuncs.Add($fname)
        }
    }

    $removedFuncs = [System.Collections.Generic.List[string]]::new()
    $typedFuncSet = [System.Collections.Generic.HashSet[string]]::new([string[]]@($typedFuncs.Keys))
    foreach ($fname in $existingFuncOrder) {
        if (-not $typedFuncSet.Contains($fname)) {
            $removedFuncs.Add($fname)
        }
    }

    # Detect signature changes (same name, different return type or params)
    $sigChanges = [System.Collections.Generic.List[PSCustomObject]]::new()
    foreach ($fname in $existingFuncOrder) {
        if (-not $existingSigMap.ContainsKey($fname)) { continue }
        if (-not $typedFuncs.Contains($fname)) { continue }
        $oldSig = $existingSigMap[$fname]
        $decl = $typedFuncs[$fname]
        if ($decl -match "^(.*?)\b$fname\s*\((.*)\)\s*;") {
            $retType = $Matches[1].Trim()
            $params  = $Matches[2].Trim()
            if (-not $params) { $params = 'void' }
            $newSig = "$retType ($params)"
            if ($oldSig -ne $newSig) {
                $sigChanges.Add([PSCustomObject]@{ Name = $fname; Old = $oldSig; New = $newSig })
            }
        }
    }

    Write-Host "  New functions:     $($newFuncs.Count)" -ForegroundColor $(if ($newFuncs.Count -gt 0) { 'Green' } else { 'DarkGray' })
    Write-Host "  Removed (kept):    $($removedFuncs.Count)" -ForegroundColor $(if ($removedFuncs.Count -gt 0) { 'Yellow' } else { 'DarkGray' })
    Write-Host "  Signature changes: $($sigChanges.Count)" -ForegroundColor $(if ($sigChanges.Count -gt 0) { 'Red' } else { 'DarkGray' })

    if ($sigChanges.Count -gt 0) {
        Write-Warning "Functions with changed signatures (struct keeps OLD signature for ABI stability):"
        foreach ($sc in $sigChanges) {
            Write-Warning "  $($sc.Name): $($sc.Old) -> $($sc.New)"
        }
    }

    # Preserve all existing lines verbatim (headers, version sections, X4_FUNC entries)
    foreach ($el in $existingRawLines) {
        [void]$sb.AppendLine($el)
    }

    # Append new functions as a version section
    if ($newFuncs.Count -gt 0) {
        [void]$sb.AppendLine("")
        [void]$sb.AppendLine("// ======== Game $versionDisplay - Added ($($newFuncs.Count) functions) ========")
        [void]$sb.AppendLine("")
        foreach ($fname in $newFuncs) {
            $decl = $typedFuncs[$fname]
            if ($decl -match "^(.*?)\b$fname\s*\((.*)\)\s*;") {
                $retType = $Matches[1].Trim()
                $params  = $Matches[2].Trim()
                if (-not $params) { $params = 'void' }
                [void]$sb.AppendLine("X4_FUNC($retType, $fname, ($params))")
            }
        }
    }

    # Append removed function comments (if any new removals this run)
    if ($removedFuncs.Count -gt 0) {
        # Check if these are already documented in the file
        $existingContent = $existingRawLines -join "`n"
        $newlyRemoved = $removedFuncs | Where-Object { $existingContent -notmatch "//\s+$_\s" }
        if ($newlyRemoved) {
            [void]$sb.AppendLine("")
            [void]$sb.AppendLine("// Functions removed in $versionDisplay (still in struct for ABI stability):")
            foreach ($fname in $newlyRemoved) {
                [void]$sb.AppendLine("//   $fname - no longer exported")
            }
        }
    }

    # Append signature change comments
    if ($sigChanges.Count -gt 0) {
        $existingContent = if (-not $existingContent) { $existingRawLines -join "`n" } else { $existingContent }
        $newSigChanges = $sigChanges | Where-Object { $existingContent -notmatch "//\s+$($_.Name):\s+signature changed" }
        if ($newSigChanges) {
            [void]$sb.AppendLine("")
            [void]$sb.AppendLine("// Signature changes in $versionDisplay (struct keeps OLD signature for ABI stability):")
            foreach ($sc in $newSigChanges) {
                [void]$sb.AppendLine("//   $($sc.Name): signature changed")
                [void]$sb.AppendLine("//     was: $($sc.Old)")
                [void]$sb.AppendLine("//     now: $($sc.New)")
            }
        }
    }

    $totalFuncCount = $existingFuncSet.Count + $newFuncs.Count
} else {
    Write-Host "  Fresh generation (no existing .inc)" -ForegroundColor Yellow

    # Full generation — write header + all functions as baseline version section
    [void]$sb.AppendLine("// ==========================================================================")
    [void]$sb.AppendLine("// x4_game_func_list.inc - X4 Game Function List (X-Macro)")
    [void]$sb.AppendLine("// ==========================================================================")
    [void]$sb.AppendLine("// Auto-generated from X4 $versionDisplay FFI declarations.")
    [void]$sb.AppendLine("//")
    [void]$sb.AppendLine("// Each line: X4_FUNC(return_type, function_name, (param_list))")
    [void]$sb.AppendLine("//")
    [void]$sb.AppendLine("// This file is included multiple times with different X4_FUNC definitions:")
    [void]$sb.AppendLine("//   - x4_game_func_table.h defines X4GameFunctions struct fields")
    [void]$sb.AppendLine("//   - game_api.cpp defines the resolver data for GetProcAddress")
    [void]$sb.AppendLine("//")
    [void]$sb.AppendLine("// APPEND-ONLY - never remove or reorder lines. New game versions append")
    [void]$sb.AppendLine("// at the end. Removed functions stay as dead slots (resolve to NULL).")
    [void]$sb.AppendLine("//")
    [void]$sb.AppendLine("// DO NOT EDIT - regenerate with: .\scripts\generate_headers.ps1")
    [void]$sb.AppendLine("// ==========================================================================")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("// ======== Game $versionDisplay - Initial extraction ($($typedFuncs.Count) functions) ========")
    [void]$sb.AppendLine("")

    foreach ($fname in ($typedFuncs.Keys | Sort-Object)) {
        $decl = $typedFuncs[$fname]
        if ($decl -match "^(.*?)\b$fname\s*\((.*)\)\s*;") {
            $retType = $Matches[1].Trim()
            $params  = $Matches[2].Trim()
            if (-not $params) { $params = 'void' }
            [void]$sb.AppendLine("X4_FUNC($retType, $fname, ($params))")
        }
    }

    $totalFuncCount = $typedFuncs.Count
}

[System.IO.File]::WriteAllText($funcListOut, $sb.ToString())
$funcListSize = [math]::Round((Get-Item $funcListOut).Length / 1024, 1)

# ---- x4_game_func_table.h ----
[void]$sb.Clear()
[void]$sb.AppendLine("// ==========================================================================")
[void]$sb.AppendLine("// x4_game_func_table.h - X4 Game Function Pointer Table")
[void]$sb.AppendLine("// ==========================================================================")
[void]$sb.AppendLine("// Auto-generated from X4 $versionDisplay FFI declarations.")
[void]$sb.AppendLine("//")
[void]$sb.AppendLine("// The X4GameFunctions struct provides compile-time type-safe access to")
[void]$sb.AppendLine("// resolved game function pointers. Populated at runtime via GetProcAddress.")
[void]$sb.AppendLine("//")
[void]$sb.AppendLine("// Usage in extensions (cache api->game during init):")
[void]$sb.AppendLine("//   if (game->GetPlayerID)")
[void]$sb.AppendLine("//       UniverseID player = game->GetPlayerID();")
[void]$sb.AppendLine("//")
[void]$sb.AppendLine("// DO NOT EDIT - regenerate with: .\scripts\generate_headers.ps1")
[void]$sb.AppendLine("// ==========================================================================")
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("")
[void]$sb.AppendLine('#include "x4_game_types.h"')
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#ifdef __cplusplus")
[void]$sb.AppendLine('extern "C" {')
[void]$sb.AppendLine("#endif")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("// --------------------------------------------------------------------------")
[void]$sb.AppendLine("// Function Pointer Table ($totalFuncCount entries)")
[void]$sb.AppendLine("// --------------------------------------------------------------------------")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#define X4_FUNC(ret, name, params) ret (*name) params;")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("typedef struct X4GameFunctions {")
[void]$sb.AppendLine('#include "x4_game_func_list.inc"')
[void]$sb.AppendLine("} X4GameFunctions;")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#undef X4_FUNC")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("// Named lookup for functions not in the table (untyped exports, etc.)")
[void]$sb.AppendLine("// Returns NULL if the function is not found in the game executable.")
[void]$sb.AppendLine("typedef void* (*X4GetGameFunctionFn)(const char* name);")
[void]$sb.AppendLine("")

# Untyped exports section (moved from the deleted x4_game_functions.h)
[void]$sb.AppendLine("// --------------------------------------------------------------------------")
[void]$sb.AppendLine("// Untyped Exports ($($untypedList.Count))")
[void]$sb.AppendLine("// --------------------------------------------------------------------------")
[void]$sb.AppendLine("// These functions exist in X4.exe's export table but have no known")
[void]$sb.AppendLine("// C signature from FFI data. Resolve with X4GetGameFunctionFn and cast.")
[void]$sb.AppendLine("//")

if ($withSibling.Count -gt 0) {
    [void]$sb.AppendLine("// --- With known FFI sibling (similar signature likely) ---")
    $pad = ($withSibling | ForEach-Object { $_.Name.Length } | Measure-Object -Maximum).Maximum
    foreach ($ws in $withSibling) {
        [void]$sb.AppendLine("//   $($ws.Name.PadRight($pad))  (see $($ws.Sibling))")
    }
    [void]$sb.AppendLine("//")
}

if ($noSibling.Count -gt 0) {
    [void]$sb.AppendLine("// --- No known sibling ---")
    foreach ($ns in $noSibling) {
        [void]$sb.AppendLine("//   $ns")
    }
}

[void]$sb.AppendLine("")
[void]$sb.AppendLine("#ifdef __cplusplus")
[void]$sb.AppendLine("}")
[void]$sb.AppendLine("#endif")

[System.IO.File]::WriteAllText($funcTableOut, $sb.ToString())
$funcTableSize = [math]::Round((Get-Item $funcTableOut).Length / 1024, 1)

# ===========================================================================
# Summary
# ===========================================================================
Write-Host ""
Write-Host "=== Header Generation Complete ===" -ForegroundColor Cyan
Write-Host "  x4_game_types.h        ${typesSize} KB"
Write-Host "    $($handleTypedefs.Count) typedefs + $($gameStructs.Count) structs"
Write-Host "  x4_game_func_list.inc  ${funcListSize} KB"
Write-Host "    $totalFuncCount X4_FUNC entries"
Write-Host "  x4_game_func_table.h   ${funcTableSize} KB"
Write-Host "    struct + $($untypedList.Count) untyped export comments"
Write-Host ""
