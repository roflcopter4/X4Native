#pragma once
// ---------------------------------------------------------------------------
// x4native_core.dll — Extension Manager
//
// Discovers and loads native extension DLLs. Each extension lives in its
// own X4 extension folder and declares itself via x4native.json.
//
// Lifecycle:
//   discover() → scan game extensions dirs for x4native.json
//   load_all() → LoadLibrary + x4native_init() for each (priority order)
//   shutdown() → x4native_shutdown() + FreeLibrary (reverse order)
// ---------------------------------------------------------------------------

#include "Common.h"

#include <x4native_extension.h>
#include "logger.h"
#include "settings_manager.h"
#include "x4native_defs.h"

#include <filesystem>
#include <utility>
#include <vector>

namespace x4n {

struct ExtensionInfo {
    // Canonical identifier — content.xml <content id="..."> attribute.
    // X4 enforces uniqueness of this id across enabled extensions. Use it
    // for every framework-internal key (logs, stash, settings, events).
    std::string extension_id;

    // Human-facing name — content.xml <content name="..."> attribute.
    // Used for log prefixes and UI only; never as an identity key.
    std::string display_name;

    // Deprecated: the "name" field from x4native.json. Parsed for backward
    // compatibility but ignored in favour of content.xml fields. Kept only
    // so we can log a deprecation warning if present.
    std::string json_name;

    std::string path;                    // Absolute path to the extension's folder
    std::filesystem::path dll_path;      // Absolute path to the extension DLL (original, never locked)
    std::filesystem::path dll_live_path; // Copy that is actually LoadLibrary'd (deleted on unload)

    int  priority    = 0;
    int  api_version = 0;
    bool autoreload  = false; // true → watch dll_path for changes and hot-reload

    // Per-extension log file.
    // log_name: raw value from "log" field in x4native.json (empty = use <name>.log default)
    // log_path: resolved absolute path (set in load_extension, may be updated by api_init_log)
    // log_handle: open HANDLE to the current log file (INVALID_HANDLE_VALUE if not open)
    std::string           log_name;
    std::filesystem::path log_path;
    Logger::HandleType    log_handle = Logger::invalid_handle_value;

    HMODULE  module         = nullptr;
    bool     initialized    = false;
    bool     reload_pending = false; // set by tick(), consumed by flush_pending_reloads()

    std::filesystem::file_time_type dll_mtime = {}; // mtime of dll_path at last load (for change detection)

    // Declared settings schema — parsed from x4native.json at discovery time.
    // The authoritative data lives in SettingsManager, keyed by extension_id.
    // This vector is only used to hand the schema off during registration.
    std::vector<SettingSchema> settings_schema;

    // Warnings accumulated during discover/parse before the extension's own
    // log file exists. Flushed into the extension log as the first entries
    // once load_extension opens it. Always also written to the framework log.
    std::vector<std::pair<LogLevel, std::string>> pending_warnings;

    // API struct for this extension — persists for the extension's lifetime.
    // Extensions store a pointer to this during x4native_init().
    X4NativeAPI api = {};

    // Event subscription IDs — tracked for auto-cleanup on unload
    std::vector<int> subscription_ids;

    // Resolved exports
    using api_version_fn = int  (*)();
    using init_fn        = int  (*)(X4NativeAPI*);
    using shutdown_fn    = void (*)();

    api_version_fn fn_api_version = nullptr;
    init_fn        fn_init        = nullptr;
    shutdown_fn    fn_shutdown    = nullptr;
};

class ExtensionManager
{
  public:
    ExtensionManager() = delete;

    using raise_lua_event_fn     = int (*)(char const *, char const *);
    using register_lua_bridge_fn = int (*)(char const *, char const *);

    static void init(std::filesystem::path ext_root,
                     std::string const     &game_version,
                     raise_lua_event_fn     raise_lua_event     = nullptr,
                     register_lua_bridge_fn register_lua_bridge = nullptr,
                     stash_set_fn           stash_set           = nullptr,
                     stash_get_fn           stash_get           = nullptr,
                     stash_remove_fn        stash_remove        = nullptr,
                     stash_clear_fn         stash_clear         = nullptr);
    static void shutdown();

    static void discover();
    static void load_all();
    static void tick();                  // throttled mtime check — call every frame
    static void flush_pending_reloads(); // safe reload point — call before firing frame events

    static std::vector<ExtensionInfo> const &extensions() { return s_extensions; }

    // Serializes the loaded-extension set as JSON. Returns a pointer into
    // a single internal static buffer — valid until the next call. Callers
    // that need to keep the string across calls must copy it.
    static char const *loaded_extensions_json();

  private:
    enum class LoadResult { ok, skipped, failed };

    static bool       parse_config(std::filesystem::path const &json_path, ExtensionInfo &info);
    static LoadResult load_extension(ExtensionInfo &ext);
    static void       unload_extension(ExtensionInfo &ext);
    static void       fill_api(X4NativeAPI &api, ExtensionInfo &ext);

    static std::vector<ExtensionInfo> s_extensions;
    static std::filesystem::path s_ext_root;
    static std::string s_game_version;
    static int  s_tick_frame;
    static bool s_any_autoreload; // true if at least one loaded extension has autoreload=true
};

} // namespace x4n
