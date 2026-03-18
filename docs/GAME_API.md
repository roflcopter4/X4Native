# X4Native — Game API

## Function Identification

X4Native uses an automated toolchain to extract, cross-reference, and generate headers for all game functions.

### Sources

**PE Export Table:** X4.exe exports 2,359 named C functions — the same functions LuaJIT's `ffi.C.*` resolves. Extracted via `dumpbin /exports`.

**FFI cdef Declarations:** X4's UI Lua files contain `ffi.cdef` blocks declaring C functions and structs exposed to Lua: 310 struct/typedef definitions and 2,052 function signatures with full parameter types.

### Cross-Reference Results

| Category | Count | Description |
|----------|-------|-------------|
| Typed (FFI ∩ Exports) | 2,051 | Full signature known — used in `X4GameFunctions` |
| Untyped (Export only) | 308 | Exported but no FFI signature |
| FFI-only | 1 | Declared in FFI but not exported |

### Extraction Toolchain

| Script | Purpose | Output |
|--------|---------|--------|
| `extract_game_files.ps1` | Lua/XML from cat/dat archives | `reference/game/` |
| `extract_exports.ps1` | PE export table via `dumpbin` | `reference/x4_exports.txt` |
| `extract_ffi.ps1` | `ffi.cdef` blocks from game Lua | `reference/x4_ffi_raw.txt`, `x4_ffi_summary.txt` |
| `generate_headers.ps1` | Cross-references FFI + exports → C headers | `sdk/x4_game_*.h`, `.inc` |
| `update_references.ps1` | Runs the full pipeline | All of the above |

Run after any game patch:
```powershell
.\scripts\update_references.ps1
git diff reference/ sdk/
```

### Auto-Generated Headers

The generator uses an **X4_FUNC x-macro** pattern for single-source-of-truth:

**`sdk/x4_game_func_list.inc`** — 2,051 entries:
```c
X4_FUNC(UniverseID, GetPlayerID, (void))
X4_FUNC(const char*, GetComponentName, (UniverseID componentid))
X4_FUNC(bool, IsComponentClass, (UniverseID componentid, const char* classname))
```

**`sdk/x4_game_func_table.h`** — consumes the list to define the struct:
```c
typedef struct X4GameFunctions {
    #define X4_FUNC(ret, name, params) ret (*name) params;
    #include "x4_game_func_list.inc"
    #undef X4_FUNC
} X4GameFunctions;
```

**`sdk/x4_game_types.h`** — 13 handle typedefs + 283 game structs, dependency-ordered via topological sort.

The same `.inc` file is reused in the core's resolver with a different macro expansion. One source, two consumers, zero redundancy.

---

## Runtime Resolution

At startup, the core DLL resolves all 2,051 typed game functions from X4.exe into `X4GameFunctions`. Extensions receive a pointer via `x4n::game()` and call functions as direct pointers — zero overhead.

```cpp
// game_api.cpp — resolver
#define X4_FUNC(ret, name, params) { #name, offsetof(X4GameFunctions, name) },
static const FuncEntry s_func_entries[] = {
#include <x4_game_func_list.inc>
};
#undef X4_FUNC

bool GameAPI::init() {
    HMODULE x4 = GetModuleHandleA(nullptr);  // X4.exe is the host
    for (const auto& entry : s_func_entries) {
        FARPROC proc = GetProcAddress(x4, entry.name);
        if (proc)
            *reinterpret_cast<void**>(base + entry.offset) = (void*)proc;
    }
    // "GameAPI: Resolved 2051/2051 game functions"
}
```

For untyped exports (308 functions without FFI signatures), use named lookup:
```cpp
auto fn = (SomeFunc_t)x4n::game_fn("SomeUntypedExport");
```

### Manual Type Additions

`sdk/x4_manual_types.h` holds reverse-engineered struct fields or types not exposed through `ffi.cdef`. This file is hand-maintained and not overwritten by the generator.

---

## Extracting Game Files Manually

X4 stores files in `.cat`/`.dat` archive pairs. Egosoft's **XRCatTool** (shipped with X Tools on Steam) extracts them. The automated `extract_game_files.ps1` script handles this, but manual extraction is sometimes useful for research.

### Prerequisites

- **X Tools** — Steam Library → Tools → "X Tools" (provides `XRCatTool.exe`)
- **X4: Foundations** installed

### XRCatTool Commands

```powershell
$XRCat = "G:\Steam\steamapps\common\X Tools\XRCatTool.exe"
$X4    = "G:\Steam\steamapps\common\X4 Foundations"

# Extract all Lua from base game (08.cat)
& $XRCat -in "$X4\08.cat" -out $Out -include "\.lua$"

# Extract DLC Lua
$dlcs = Get-ChildItem "$X4\extensions\ego_*" -Directory
foreach ($dlc in $dlcs) {
    $cats = Get-ChildItem "$($dlc.FullName)\ext_*.cat" |
            Where-Object { $_.Name -notmatch "_sig" } | Sort-Object Name
    if ($cats) {
        & $XRCat -in @($cats.FullName) -out "$Out\extensions\$($dlc.Name)" -include "\.lua$"
    }
}

# Dry run (list without extracting)
& $XRCat -in "$X4\08.cat" -out $Out -include "\.lua$" -dump
```

### Cat File Contents

| Cat file | Contents |
|----------|----------|
| 01.cat | Core assets (models, textures) |
| 08.cat | UI / Lua scripts (menus, helpers, core systems) |
| 09.cat | Additional data |
| ext_01-03.cat (per DLC) | DLC-specific overrides |

### Key Lua Directories

| Path | Description |
|------|-------------|
| `ui/addons/ego_detailmonitor/` | Detail panel menus |
| `ui/addons/ego_gameoptions/` | Game options menus |
| `ui/addons/ego_interactmenu/` | Right-click context menus |
| `ui/core/lua/` | Core UI systems |
| `ui/widget/lua/` | Fullscreen widget (~868 KB) |
