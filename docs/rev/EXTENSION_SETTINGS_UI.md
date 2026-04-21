# Extension Settings UI — Findings

Research into how X4 renders the per-extension settings panel shown via the
`...` button in **Settings → Extensions** (see `image_1.png` / `image_2.png`)
and what it would take to add custom rows for X4Native-loaded extensions.

All line numbers reference the shipped `v9.00` game files mirrored under
`reference/game/`. IDA addresses reference `X4.exe` build `9.00 (603098)`.

---

## 1. Menu flow

### 1.1 Extensions list — `menu.displayExtensions`

Defined in `reference/game/ui/addons/ego_gameoptions/gameoptions.lua:10298`.

Called from `menu.submenuHandler("extensions")` at line `5128`, which is reached
via the main `Settings → Extensions` row in `config.optionDefinitions["settings"][2]`
(line `1488`).

Per-row rendering is in `menu.displayExtensionRow` (line `10412`), which builds
7 columns: back arrow, name, id, version, date, **status button** (Enabled/Disabled),
**`...` button**. The two click handlers are:

```lua
-- gameoptions.lua:10433
row[6].handlers.onClick = function () return menu.callbackExtensionSettingEnabled(extension) end
row[7].handlers.onClick = function () menu.selectedExtension = extension;
                                       menu.openSubmenu("extensionsettings", extension.id) end
```

### 1.2 Per-extension submenu — `extensionsettings`

`menu.openSubmenu("extensionsettings", …)` (line `5185`) pushes a history entry
and calls `menu.submenuHandler("extensionsettings")` (line `5088`). The handler
falls through to the generic branch at line `5180`:

```lua
elseif config.optionDefinitions[optionParameter] then
    menu.displayOptions(optionParameter)
end
```

`menu.displayOptions` (line `9313`) looks up `config.optionDefinitions[optionParameter]`
and renders one row per entry using `menu.displayOption` (line `4698`, the row
builder that understands `valuetype = button | dropdown | slidercell | ...`).

### 1.3 Stock option definition

`config.optionDefinitions["extensionsettings"]` (lines `1455-1477`) contains
exactly **three** options:

| idx | id         | Displayed when              | Renders                                         |
|-----|------------|-----------------------------|-------------------------------------------------|
|  1  | `enable`   | always                      | Yes/No text value (from `valueExtensionSettingEnabled`) |
|  2  | `sync`     | `selectedExtension.isworkshop`  | Yes/No text value                          |
|  3  | `workshop` | `selectedExtension.isworkshop`  | button → `OpenWorkshop(id, personal)`      |

For a non-Steam-Workshop extension (which ours is), the submenu is exactly the
single **Enabled** row seen in `image_2.png`.

---

## 2. Engine plumbing behind the stock rows

`GetExtensionList`, `GetAllExtensionSettings`, `SetExtensionSettings`,
`HaveExtensionSettingsChanged`, `ResetAllExtensionSettings`, and
`GetExtensionUpdateWarningText` are **not** FFI functions — they don't appear
in `reference/x4_ffi_raw.txt` or `x4_exports.txt`. They are C closures
registered directly into the Lua global table by the engine bootstrap function
at `sub_1402378E0` (X4.exe), using `lua_pushcclosure` + `lua_setfield`.

Relevant registrations (extracted from the bootstrap):

| Lua name                        | C impl (X4.exe)     |
|---------------------------------|---------------------|
| `GetExtensionList`              | `sub_140266720`     |
| `GetExtensionUpdateWarningText` | `sub_1402664D0`     |
| `SetExtensionSettings`          | `sub_1402673D0`     |
| `ResetAllExtensionSettings`     | `sub_1402677E0`     |
| `HaveExtensionSettingsChanged`  | `sub_140267860`     |
| `GetAllExtensionSettings`       | `sub_1402678C0`     |

### 2.1 `GetExtensionList()` — shape of the extension record

From decompile of `sub_140266720`. Returns an array of tables; each entry has:

```lua
{
    id               = <string>,         -- matches content.xml id=""
    index            = <uint32>,         -- stable index used by GetAllExtensionSettings
    name, desc, author, date,
    location, version= <string>,
    enabled          = <bool>,           -- engine's current load state
    enabledbydefault = <bool>,
    sync             = <bool>,
    syncbydefault    = <bool>,
    personal         = <bool>,           -- true = user folder, false = game folder
    isworkshop       = <bool>,           -- Steam Workshop origin
    egosoftextension = <bool>,
    error, errortext, warning, warningtext, -- optional
    dependencies     = { {id, name, version}, ... },
}
```

### 2.2 `GetAllExtensionSettings()` — what the engine persists

From `sub_1402678C0`. Returns `{ [index] = { enabled = bool?, sync = bool? } }`.
**Those two booleans are the entirety of what the engine persists.** The
storage is the `std::vector<ExtensionSetting>` at `qword_14387FA28`, each entry
being 312 bytes (`sub_1402678C0`'s stride is `312`, which matches
`GetExtensionList` at `sub_140266720`).

Each 312-byte record carries flags for `{enabled_explicit?, enabled}` and
`{sync_explicit?, sync}` at offsets +4..+7 — i.e. a tri-state (unset / true /
false) for both keys.

### 2.3 `SetExtensionSettings(id, personal, setting, value)` — it is hard-coded

From `sub_1402673D0`, the 3rd argument (`setting` string) is only compared
against two literals:

```c
if (len == 6 && memcmp(s, "enable", 6) == 0) { ...set enable flag... }
else if (len == 4 && memcmp(s, "sync", 4) == 0) { ...set sync flag... }
// any other value: falls through, no write
```

→ The engine has **no generic key/value store for extensions**. Asking it to
persist `"x4native.llm_provider"` or any custom key is silently a no-op.

### 2.4 Related FFI helpers (these *do* appear in FFI)

From `reference/x4_ffi_raw.txt`:

```c
const char* GetExtensionErrorText(const char* extensionid, bool personal);
const char* GetExtensionName(uint32_t extensionidx);
const char* GetExtensionNameByID(const char* extensionid, bool personal);
const char* GetExtensionVersion(const char* extensionid, bool personal);
bool        HasExtension(const char* extensionid, bool personal);
bool        IsExtensionEnabled(const char* extensionid, bool personal);
const char* GetModifiedBasegameUIFilesExtensions(void);
```

Useful for lookups, but none of these accept custom settings.

---

## 3. Can we add custom rows to the stock submenu?

**Yes, but only from Lua, via monkey-patching.** The engine provides no
extension point. The relevant data (`config.optionDefinitions`, `menu`) is all
`local` to `gameoptions.lua`, so we reach it through the UI's normal registry.

Three viable entry points, in order of preference:

### Approach A (recommended) — intercept `submenuHandler`, splice options

1. Walk the global `Menus` table (populated at line `426`) to find
   `name == "OptionsMenu"`.
2. Pull the `config` upvalue out of `menu.displayOptions` with
   `debug.getupvalue` (LuaJIT 2.x supports this; the debug lib is whitelisted —
   `reference/game/ui/core/lua/jit/vmdef.lua:290-298` lists the full set).
3. Wrap `menu.submenuHandler`: when called with `"extensionsettings"`, look up
   `menu.selectedExtension.id`, append our per-extension option rows onto the
   **existing** `config.optionDefinitions["extensionsettings"]` (tagged so we
   can reliably remove them on the next call), then delegate to the original
   handler.

Pros:
- Exact UX the user asked for — new rows sit directly under the stock
  "Enabled" row in the same submenu.
- Reuses the stock row renderer, which supports `button / dropdown /
  slidercell / confirmation` out of the box (see `menu.displayOption`,
  line `4698`).
- Small surface area — one upvalue lookup, one function wrap.

Cons:
- Depends on `debug.getupvalue` and on the shape of `config.optionDefinitions`.
  Both have been stable across X4 patches, but must be re-verified when
  bumping game version.
- Requires Protected UI Mode to be disabled (same as current X4Native UI).

### Approach B — replace `menu.displayOptions` for `"extensionsettings"` only

Wrap `menu.displayOptions` with a specialised path for that one parameter that
builds its own frame and appends stock rows + our custom rows. Strictly local
(no `debug.*`), but duplicates ~60 lines of vanilla table/cell setup and
re-breaks on any UI refactor.

### Approach C — add a dedicated row on the extensions list

Instead of injecting into the `...` submenu, extend
`menu.displayExtensionRow` (or wrap `menu.displayExtensions`) to add a second
button ("⚙") next to `...` for any extension that registered settings, and
open **our own** menu. Cleaner separation, no `debug.*`, but diverges from
the UX in `image_2.png`.

---

## 4. Where to persist the values

The engine will not store anything besides `enable` and `sync`, so X4Native
must persist custom values itself. Three realistic targets:

| Target                                         | Scope                | Notes                                               |
|------------------------------------------------|----------------------|-----------------------------------------------------|
| `<savedvariable storage="userdata" …>` in our ui.xml | per-user, not per-save | Built-in Lua mechanism; survives `/reloadui`. Egosoft uses it for `__CORE_GAMEOPTIONS_RESTORE` etc. (line `9`). |
| JSON next to the extension's `x4native.json`   | per-user             | C++ side can read/write; already the pattern for `x4native_settings.json` (autoreload flag). |
| MD `<savedvariable>`                            | per-savegame         | Usually undesirable for config — mixes config with save state. |

Recommended: JSON file on disk, written/read by the **C++** extension via
standard file I/O, surfaced to Lua through the existing Lua ↔ C++ event
bridge (`raise_lua_event` / `RegisterEvent`). That keeps the Lua layer thin
(render + dispatch) and keeps values authoritative on the native side where
the extension's logic runs anyway.

---

## 5. Proposed API shape (for follow-up design doc)

Not implemented yet — this doc is only about locating the hook points. Sketch
for the next session:

1. **Extension manifest** — extend `x4native.json` with a `settings` array, or
   add a new `x4native_settings.json` (distinct from the autoreload config),
   e.g.:
   ```json
   {
       "settings": [
           { "id": "llm_provider", "name": "LLM Provider",
             "type": "dropdown", "default": "anthropic",
             "options": [ {"id": "anthropic", "text": "Anthropic"},
                          {"id": "openai",    "text": "OpenAI"} ] },
           { "id": "critical_poll_s", "name": "Critical poll (s)",
             "type": "slider", "min": 1, "max": 30, "default": 5 },
           { "id": "verbose", "name": "Verbose logging",
             "type": "toggle", "default": false }
       ]
   }
   ```
2. **C++ side** — `ExtensionManager` loads the declared settings, stores
   current values in a per-extension JSON, exposes:
   - `api->get_setting(key)` / `api->set_setting(key, value)`
   - event `on_setting_changed` (key, old, new) for the owning extension.
3. **Lua side** — a single file in `extension/ui/` (e.g. `x4n_settings_menu.lua`)
   implements Approach A: on each `extensionsettings` submenu open, queries
   the DLL for the `extension.id`'s declared settings + current values, and
   splices rows into `config.optionDefinitions["extensionsettings"]`.
4. **Row valuetypes** supported by the stock renderer we can plug into
   without new widgets: `button`, `confirmation`, `dropdown`, `sounddropdown`,
   `slidercell`, and plain text values. `checkbox` would need custom cell
   setup — use two-state `dropdown` or textual Yes/No button as a workaround.

---

## 6. Open questions / follow-ups

- Does UI Safe Mode need to be disabled for the injection to run? (Expected
  yes — same constraint as the existing X4Native UI module in
  `extension/ui/x4native.lua:74`.) Needs to be confirmed in-game once we wire
  the prototype.
- Row removal on submenu close — the stock code path leaves the option table
  live; we need to clean up our injected entries either on `"back"` or at the
  top of every submenu open to avoid duplicating rows across visits.
- Localisation: the stock menu uses `ReadText(page, id)` strings from
  `0001-L044.xml`. For mod settings we'll pass raw strings; that's fine but
  won't auto-translate. If we care, we add a `t_page`/`t_id` alternative in
  the manifest.
- Interaction with `HaveExtensionSettingsChanged()` / the "restart required"
  warning: the stock engine flag won't reflect our custom values. If a
  setting requires a reload, X4Native must raise its own indicator.

---

## References

- `reference/game/ui/addons/ego_gameoptions/gameoptions.lua` — stock UI
  (lines: `1455-1477` option table, `4698` row renderer, `5088` dispatcher,
  `9313` options renderer, `10298` extensions list, `10412` extension row).
- `reference/game/ui/addons/ego_detailmonitorhelper/helper.lua:1327` — 
  `Helper.registerMenu` (how addons publish `menu` onto the global `Menus`).
- `sub_1402378E0` (X4.exe) — Lua global registration for the extension APIs.
- `sub_140266720`, `sub_1402673D0`, `sub_1402678C0` (X4.exe) — implementations
  of `GetExtensionList` / `SetExtensionSettings` / `GetAllExtensionSettings`.
- `src/core/extension_manager.{h,cpp}` — current X4Native extension discovery
  (`x4native.json` parsing) that we'll extend with a `settings` section.
