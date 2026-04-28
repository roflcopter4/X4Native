#pragma once
// ---------------------------------------------------------------------------
// x4native_core.dll — Settings Manager
//
// Per-extension user-configurable settings.
//
// Each native extension may declare a "settings" array in its x4native.json.
// Values picked up by the UI (via proxy) and by the extension itself (via
// X4NativeAPI::get_setting_*). Current values persist in
//   <profile>\x4native\<extension_id>.user.json
// where <profile> is `GetSaveFolderPath()`'s parent.
//
// Threading model
// ---------------
// SettingsManager is **not** internally synchronised. All public methods run
// on the X4 UI thread exclusively:
//   - Extension lifecycle (register/unregister/shutdown) — UI thread.
//   - Read/write accessors (get_*, set_*) — UI thread (extensions invoke from
//     their UI-thread callbacks; proxy dispatches UI → core on the UI thread).
//   - Settings menu interactions route UI → proxy → core on the UI thread.
//
// ABI pointer lifetime
// --------------------
// `enumerate()` returns a pointer into `ExtensionSettings::abi_cache`. That
// buffer is stable for the lifetime of the extension registration. It is
// rebuilt on register_extension() (schema changes on hot-reload) and its
// current-value fields are refreshed in-place on every enumerate() call.
// Callers must consume the pointer before the next register/unregister for
// the same extension.
// ---------------------------------------------------------------------------

#include "Common.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "logger.h"

#include "x4native_defs.h"

namespace x4n {

enum class SettingType : uint8_t { Toggle, Dropdown, Slider };

struct SettingOption {
    std::string id;
    std::string text;
};

struct SettingSchema {
    std::string id;
    std::string name;
    SettingType type = SettingType::Toggle;

    // Defaults (only the matching field is meaningful, but all are zeroed).
    bool        default_bool   = false;
    double      default_number = 0.0;
    std::string default_string;

    // Dropdown
    std::vector<SettingOption> options;

    // Slider
    double min  = 0.0;
    double max  = 100.0;
    double step = 1.0;
};

using SettingValue = std::variant<bool, double, std::string>;

// Per-extension container: schema + current values + ABI-shaped cache.
struct ExtensionSettings {
    std::vector<SettingSchema> schema;
    std::unordered_map<std::string, SettingValue> values;
    // Persistent per-option string storage for the ABI cache.
    std::vector<std::vector<SettingOptionC>> options_cache;
    // Mirrors `schema` in the ABI form. Refreshed on every enumerate().
    std::vector<SettingInfo> abi_cache;

    bool values_loaded   = false;
    bool abi_cache_built = false;
};

class SettingsManager
{
  public:
    SettingsManager() = delete;

    // Called from ExtensionManager. No-op if the extension has no settings.
    static void register_extension(std::string const &extension_id, std::vector<SettingSchema> schema);
    static void unregister_extension(std::string const &extension_id);

    // Clears everything (on core shutdown / reload).
    static void shutdown();

    // Read-side.
    static bool        has_key(std::string const &extension_id, std::string const &key);
    static SettingType type_of(std::string const &extension_id, std::string const &key);
    static bool        get_bool(std::string const &extension_id, std::string const &key, bool fallback);
    static double      get_number(std::string const &extension_id, std::string const &key, double fallback);
    static char const *get_string(std::string const &extension_id, std::string const &key, char const *fallback);

    // Write-side. Fires `on_setting_changed` via EventSystem on change.
    // Payload struct: X4NativeSettingChanged (see below).
    static void set_bool(std::string const &extension_id, std::string const &key, bool value);
    static void set_number(std::string const &extension_id, std::string const &key, double value);
    static void set_string(std::string const &extension_id, std::string const &key, char const *value);

    // ABI-facing (used by proxy via CoreDispatch).
    static int  enumerate(std::string const &extension_id, SettingInfo const **out);
    static void set_from_abi(std::string const &extension_id, std::string const &key, SettingValueC const &value);

    // JSON helpers for parse_config / loaded_extensions_json.
    // Returns true if `node` held a valid array; empty array → ok, returns true.
    //
    // `warnings` (optional): per-extension buffer that collects parse-time
    // diagnostics. When provided, issues are pushed here instead of (or in
    // addition to) the framework logger — the caller flushes these into the
    // extension's own log file once it's open. Pass nullptr for the legacy
    // "log straight to framework log" behaviour.
    static bool parse_schema_array(
        nlohmann::json const                          &node,
        std::vector<SettingSchema>                    &out,
        std::string const                             &context,
        std::vector<std::pair<LogLevel, std::string>> *warnings = nullptr);

  private:
    using Map = std::unordered_map<std::string, ExtensionSettings>;
    static Map s_map;

    static ExtensionSettings   *find(std::string const &extension_id);
    static SettingSchema const *schema_for(ExtensionSettings const &ext, std::string const &key);

    // <profile>\x4native\<extension_id>.user.json — returns empty path on failure.
    static std::filesystem::path user_file_path(std::string const &extension_id);

    static void ensure_loaded(std::string const &extension_id, ExtensionSettings &ext);
    static void write_user_file(std::string const &extension_id, ExtensionSettings const &ext);

    static void rebuild_abi_cache(ExtensionSettings &ext);
    static void refresh_abi_current_values(ExtensionSettings &ext);

    // Fires `on_setting_changed` with a pointer to X4NativeSettingChanged.
    static void fire_change(std::string const &extension_id, std::string const &key, SettingValue const &new_value);
};

// X4NativeSettingChanged is declared in <x4native_extension.h> (SDK header).

} // namespace x4n
