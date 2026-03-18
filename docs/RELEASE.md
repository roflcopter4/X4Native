# X4Native — Release & Versioning

## Release Packages

Two zip archives serve different audiences, built from the same source at the same commit.

### End-User Package

**Filename:** `x4native-v<VERSION>.zip`

```
x4native/
├── content.xml
├── ui.xml
├── ui/x4native.lua
├── md/x4native_main.xml
├── t/0001-L044.xml
└── native/
    ├── x4native_64.dll
    ├── x4native_core.dll
    └── version_db/*.json
```

Extract to `<X4>/extensions/`, enable in-game. No SDK headers.

### Developer SDK Package

**Filename:** `x4native-sdk-v<VERSION>.zip`

```
x4native-sdk/
├── x4native.h
├── x4native_extension.h
├── x4_game_types.h
├── x4_game_func_table.h
├── x4_game_func_list.inc
├── x4_manual_types.h
└── examples/hello/
```

### Debug Symbols

**Filename:** `x4native-symbols-v<VERSION>.zip` — PDB files for crash dump analysis. Retained in GitHub Releases, never shipped to players.

---

## Versioning Strategy

### The Problem

When X4 patches, exported functions and FFI types can change: functions added/removed, signatures changed, structs modified. Extensions compile against SDK headers — if the `X4GameFunctions` struct layout changes, extensions call wrong function pointers.

### Core Rule: Append-Only Function List

`x4_game_func_list.inc` is **append-only, ordered by game version**. Never remove, never reorder.

```c
// ======== Game v9.00 (build 900) — Initial extraction ========
X4_FUNC(void, AbortActiveDiplomacyActionOperation, (UniverseID entityid))
...
X4_FUNC(void, ZoomMap, (UniverseID holomapid, float zoomstep))

// ======== Game v9.10 (build 910) — Added ========
X4_FUNC(bool, SomeNewFunction, (UniverseID id))
```

**Why this works:**
- Struct layout determined by inclusion order
- Append-only = stable offsets for all existing fields
- Old extensions keep working (their offsets unchanged)
- Removed functions resolve to NULL at runtime
- New functions appear at the end

### Types Versioning

Types (`x4_game_types.h`) always contain the **latest** definitions. When a game patch changes struct layouts, the header is regenerated. Extensions using changed structs must recompile.

`#define X4_GAME_TYPES_BUILD 900` tracks which game version the types were extracted from.

### Compatibility Matrix

| Extension SDK | X4Native Runtime | Works? |
|---|---|---|
| Older | Newer | Always |
| Same | Same | Always |
| Newer | Older | Unsafe |

Extensions should NULL-check function pointers for defensive access:
```cpp
if (x4n::game()->SomeNewFunction)
    x4n::game()->SomeNewFunction(id);
```

### Function History

`native/version_db/func_history.json` tracks per-function lifecycle:
```json
{
  "functions": {
    "GetPlayerID": { "added": "900" },
    "SomeNewFunction": { "added": "910" },
    "OldFunction": { "added": "900", "removed": "910" }
  }
}
```

---

## Version Sources

| Location | What It Tracks |
|----------|---------------|
| `CMakeLists.txt` — `project(VERSION)` | Mod version (semver) |
| `x4native_defs.h` — `X4NATIVE_VERSION` | Compiled into DLLs |
| `x4native_extension.h` — `X4NATIVE_API_VERSION` | ABI version (breaking changes only) |
| `x4_game_types.h` — `X4_GAME_TYPES_BUILD` | Game version types extracted from |
| `x4_game_func_list.inc` — section comments | Game version per function group |
| `content.xml` — `version` | Minimum supported X4 version |
| `func_history.json` | Per-function added/removed |
| Git tag `vX.Y.Z` | Release marker |

### Version Bump Checklist

1. `CMakeLists.txt` → `project(x4native VERSION X.Y.Z)`
2. `x4native_defs.h` → `X4NATIVE_VERSION "X.Y.Z"`
3. `x4native_extension.h` → `X4NATIVE_API_VERSION` (only if ABI changed)
4. `content.xml` → `version` (only if min game version changed)
5. Git tag → `vX.Y.Z`

### After a Game Patch

1. `.\scripts\update_references.ps1` — re-extract everything
2. Verify `func_history.json` updated
3. Test in-game (check resolver log for resolution count changes)
4. Update `X4_GAME_TYPES_BUILD` in types header

---

## Release Process

```
1. Version Bump — CMakeLists.txt, x4native_defs.h, tag commit
2. Clean Build — cmake --build build --config Release --clean-first
3. Regenerate SDK Headers (if game updated)
4. Package Artifacts — scripts/make_release.ps1 -Version X.Y.Z
5. Test In-Game — deploy → launch → verify log → /reloadui
6. Publish — git tag, GitHub Releases, Nexus Mods
```

## Distribution Channels

| Channel | Audience | What ships |
|---------|----------|-----------|
| GitHub Releases | Developers, early adopters | 3 zips + changelog |
| Nexus Mods | Players | End-user zip only |
| Steam Workshop | Players (future) | End-user content |
