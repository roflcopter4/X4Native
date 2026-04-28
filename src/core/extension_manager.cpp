#include "Common.h"
#include "extension_manager.h"
#include "logger.h"
#include "event_system.h"
#include "game_api.h"
#include "hook_manager.h"
#include "settings_manager.h"
#include "x4native_defs.h"

#include <x4_game_types.h>
#include <x4_game_func_table.h>
#include <x4_game_offsets.h>

#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <ranges>
#include <regex>

// Runtime-resolved offsets populated by core.cpp::populate_offsets()
extern X4GameOffsets s_offsets;

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace x4n {

std::vector<ExtensionInfo> ExtensionManager::s_extensions;
fs::path    ExtensionManager::s_ext_root;
std::string ExtensionManager::s_game_version;
int         ExtensionManager::s_tick_frame     = 0;
bool        ExtensionManager::s_any_autoreload = false;

static raise_lua_event_fn     s_raise_lua_event     = nullptr;
static register_lua_bridge_fn s_register_lua_bridge = nullptr;
static stash_set_fn           s_stash_set           = nullptr;
static stash_get_fn           s_stash_get           = nullptr;
static stash_remove_fn        s_stash_remove        = nullptr;
static stash_clear_fn         s_stash_clear         = nullptr;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ExtensionManager::init(
    fs::path               ext_root,
    std::string const     &game_version,
    raise_lua_event_fn     raise_lua_event,
    register_lua_bridge_fn register_lua_bridge,
    stash_set_fn           stash_set,
    stash_get_fn           stash_get,
    stash_remove_fn        stash_remove,
    stash_clear_fn         stash_clear)
{
    s_ext_root            = std::move(ext_root);
    s_game_version        = game_version;
    s_raise_lua_event     = raise_lua_event;
    s_register_lua_bridge = register_lua_bridge;
    s_stash_set           = stash_set;
    s_stash_get           = stash_get;
    s_stash_remove        = stash_remove;
    s_stash_clear         = stash_clear;
    s_extensions.clear();
}

void ExtensionManager::shutdown()
{
    // Unload in reverse priority order (reverse of load order)
    for (ExtensionInfo &extension : std::ranges::reverse_view(s_extensions))
        unload_extension(extension);
    s_extensions.clear();
    s_any_autoreload = false;
    s_tick_frame     = 0;
}

// ---------------------------------------------------------------------------
// Discovery — scan sibling extension folders for x4native.json
// ---------------------------------------------------------------------------

void ExtensionManager::discover()
{
    // If extensions are already loaded, shut them down first.
    // This handles save-load and /reloadui where Lua re-executes
    // and calls discover_extensions() again.
    if (!s_extensions.empty()) {
        Logger::info("Re-discovery: shutting down {} existing extension(s)", s_extensions.size());
        shutdown();
    }

    // ext_root is e.g. "G:\...\extensions\x4native\"
    // We scan the parent "extensions\" dir for sibling extensions
    // Note: trailing separator means parent_path() just strips it,
    // so we need parent_path() twice (or strip trailing sep first).
    fs::path extensions_dir = s_ext_root.has_filename()
        ? s_ext_root.parent_path()
        : s_ext_root.parent_path().parent_path();

    if (!fs::is_directory(extensions_dir)) {
        Logger::warn("Extension discovery: cannot find extensions dir: {}", extensions_dir.string());
        return;
    }

    Logger::info("Scanning for native extensions in: {}", extensions_dir.string());

    for (fs::directory_entry const &entry : fs::directory_iterator(extensions_dir))
    {
        if (!entry.is_directory())
            continue;
        // Skip ourselves
        auto dir_name = entry.path().filename();
        if (dir_name == "x4native")
            continue;
        fs::path config_path = entry.path() / "x4native.json";
        if (!fs::exists(config_path))
            continue;
        ExtensionInfo info;
        info.path = entry.path().string();
        if (!parse_config(config_path, info))
            continue;

        // Identity comes from content.xml — X4 enforces id uniqueness.
        // The regex anchors on `<content ...>` so a stray `id=` attribute
        // elsewhere in the file can't hijack the match.
        fs::path content_xml = entry.path() / "content.xml";
        if (fs::exists(content_xml)) {
            auto xml_file = std::ifstream(content_xml);
            auto xml_data = std::string(std::istreambuf_iterator<char>(xml_file), std::istreambuf_iterator<char>());

            static auto const id_re   = std::regex(R"xml(<content\b[^>]*\bid="([^"]+)")xml",   std::regex_constants::ECMAScript | std::regex_constants::optimize);
            static auto const name_re = std::regex(R"xml(<content\b[^>]*\bname="([^"]+)")xml", std::regex_constants::ECMAScript | std::regex_constants::optimize);

            std::smatch m;
            if (std::regex_search(xml_data, m, id_re))
                info.extension_id = m[1].str();
            if (std::regex_search(xml_data, m, name_re))
                info.display_name = m[1].str();
        }
        // Fallbacks: content.xml missing or lacking these attributes is a
        // degenerate case (X4 wouldn't load the extension either). Still,
        // keep loading — framework-side we use extension_id everywhere.
        if (info.extension_id.empty())
            info.extension_id = dir_name.string();
        if (info.display_name.empty())
            info.display_name = info.extension_id;

        // Deprecated: legacy "name" in x4native.json. Warn (and track so
        // the warning also reaches the extension's own log once open).
        if (!info.json_name.empty()) {
            auto msg = std::format("x4native.json: 'name' field is deprecated (ignored in favour of content.xml id/name) — remove from {}",
                                   config_path.string());
            Logger::warn("{}", msg);
            info.pending_warnings.emplace_back(LogLevel::Warn, std::move(msg));
        }

        Logger::info("Discovered extension: {} (id={}, priority={}, api={})",
                     info.display_name, info.extension_id,
                     info.priority, info.api_version);

        // Register settings now that we know the extension_id.
        if (!info.settings_schema.empty()) {
            SettingsManager::register_extension(info.extension_id, std::move(info.settings_schema));
            info.settings_schema.clear();  // moved out; keep the vector empty
        }

        s_extensions.emplace_back(std::move(info));
    }

    // Sort by priority (lower = loads first)
    std::ranges::sort(
        s_extensions,
        [](ExtensionInfo const &a, ExtensionInfo const &b) {
            return a.priority < b.priority;
        }
    );

    Logger::info("Discovered {} native extension(s)", s_extensions.size());
}

// ---------------------------------------------------------------------------
// Config parsing
// ---------------------------------------------------------------------------

bool ExtensionManager::parse_config(fs::path const &json_path, ExtensionInfo &info)
{
    std::ifstream file(json_path);
    if (!file.is_open()) {
        Logger::warn("Cannot open config: {}", json_path.string());
        return false;
    }

    json cfg;
    try {
        file >> cfg;
    } catch (json::parse_error const & e) {
        Logger::error("JSON parse error in {}: {}", json_path.string(), e.what());
        return false;
    }

    // Required: library
    auto it = cfg.find("library");
    if (it == cfg.end() || !it->is_string()) {
        Logger::warn("Config missing 'library': {}", json_path.string());
        return false;
    }

    info.dll_path = fs::path(info.path) / it->get<std::string>();

    // Optional: legacy "name" field. Identity now comes from content.xml.
    // Stored so we can log a deprecation warning once the logger is open.
    it = cfg.find("name");
    if (it != cfg.end() && it->is_string())
        info.json_name = it->get<std::string>();

    it = cfg.find("logfile");
    if (it != cfg.end() && it->is_string())
        info.log_name = it->get<std::string>();

    it = cfg.find("priority");
    if (it != cfg.end() && it->is_number_integer())
        info.priority = it->get<int>();

    it = cfg.find("min_api_version");
    if (it != cfg.end() && it->is_number_integer())
        info.api_version = it->get<int>();

    it = cfg.find("autoreload");
    if (it != cfg.end() && it->is_boolean())
        info.autoreload = it->get<bool>();

    // Settings schema — optional. Actual registration happens in discover()
    // once we know the extension_id (pulled from content.xml). Context string
    // uses the folder path so warnings reference a concrete file.
    it = cfg.find("settings");
    if (it != cfg.end())
        SettingsManager::parse_schema_array(*it, info.settings_schema, json_path.string(), &info.pending_warnings);

    return true;
}

// ---------------------------------------------------------------------------
// Per-extension log helpers
// ---------------------------------------------------------------------------

// Resolves the (api, ext) pair from a `void* api_ptr` argument passed by the
// SDK-side log wrappers. Returns {nullptr, nullptr} if anything is missing.
// `ext` being null is the caller's early-return condition.
static std::pair<X4NativeAPI *, ExtensionInfo *> resolve_ext(void *api_ptr)
{
    if (!api_ptr)
        return {nullptr, nullptr};
    auto *api = static_cast<X4NativeAPI *>(api_ptr);
    auto *ext = static_cast<ExtensionInfo *>(api->_ext_info);
    return {api, ext};
}

// api_log_ext — routes x4n::log::info/warn/etc. to the extension's own log file.
// Falls back to the global framework log if the handle is not set.
static void api_log_ext(int level, char const *message, void *api_ptr)
{
    auto lv       = static_cast<LogLevel>(level);
    auto [api, _] = resolve_ext(api_ptr);
    if (api) {
        Logger::HandleType h = api->_ext_log_handle;
        if (h && h != Logger::invalid_handle_value) {
            Logger::write_to(h, lv, message);
            return;
        }
    }
    Logger::write(lv, message);
}

// api_init_log — called by x4n::log::set_log_file("filename") to redirect
// the extension's default log. Filename is relative to the extension's
// subfolder (<profile>\x4native\<ext_id>\) — absolute paths and traversal
// are rejected.
static void api_init_log(char const *filename, void *api_ptr)
{
    if (!filename || !filename[0])
        return;
    auto [api, ext] = resolve_ext(api_ptr);
    if (!ext)
        return;
    std::string context = "Extension '" + ext->display_name + "': set_log_file()";
    if (!Logger::is_safe_relative_name(filename, context.c_str()))
        return;

    fs::path ext_dir  = Logger::profile_ext_dir(ext->extension_id);
    fs::path base     = ext_dir.empty() ? ext->path : ext_dir;
    fs::path new_path = base / filename;

    // Ensure any author-requested intermediate directories exist.
    std::error_code ec;
    fs::create_directories(new_path.parent_path(), ec);
    if (ec) {
        Logger::warn("Extension '{}': set_log_file() failed to create '{}': {}",
                     ext->display_name, new_path.string(), ec.message());
        return;
    }

    Logger::HandleType new_h = Logger::open_log(new_path);
    if (new_h == Logger::invalid_handle_value) {
        Logger::warn("Extension '{}': set_log_file() could not open '{}'",
                     ext->display_name, new_path.string());
        return;
    }

    // Close the old handle only after the new one opened successfully.
    Logger::HandleType old_h = api->_ext_log_handle;
    Logger::flush_and_close_handle(old_h);

    ext->log_handle      = new_h;
    ext->log_path        = new_path;
    api->_ext_log_handle = new_h;

    Logger::write_to(new_h, LogLevel::Info, "Extension log initialized: " + new_path.string());
}

// api_log_named — backs x4n::log::to_file("name").info(...).
// Opens `<profile>\x4native\<ext_id>\<filename>` for append, writes one
// record, closes. Author-provided nested paths are allowed (intermediate
// subfolders are created on first write) but absolute paths and `..`
// segments are rejected — all writes stay inside the per-extension subfolder.
static void api_log_named(int level, char const *message, char const *filename, void *api_ptr)
{
    if (!filename || !filename[0]) {
        api_log_ext(level, message, api_ptr);
        return;
    }
    auto [api, ext] = resolve_ext(api_ptr);
    if (!ext)
        return;
    if (!Logger::is_safe_relative_name(filename, ("Extension '" + ext->display_name + "': to_file()").c_str()))
        return;

    fs::path base = Logger::profile_ext_dir(ext->extension_id);
    if (base.empty()) {
        // Profile unreachable — fall back to the extension folder, same as
        // earlier releases. The relative-name guard above still prevents
        // traversal even in this fallback.
        base = api->extension_path ? api->extension_path : "";
        if (base.empty())
            return;
    }

    fs::path full = base / filename;
    // Ensure any author-requested intermediate directories exist.
    std::error_code ec;
    fs::create_directories(full.parent_path(), ec);
    if (ec)
        return;

#ifdef _WIN32
    HANDLE h = ::CreateFileW(
        full.c_str(), GENERIC_WRITE | FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE)
        ::SetFilePointer(h, 0, nullptr, FILE_END);
#else
    int h = ::open(full.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif

    if (h != Logger::invalid_handle_value) {
        Logger::write_to(h, static_cast<LogLevel>(level), message);
        Logger::close_handle(h);
    }
}

// ---------------------------------------------------------------------------
// Loading — LoadLibrary + resolve exports + SEH-wrapped init
// ---------------------------------------------------------------------------

void ExtensionManager::load_all()
{
    for (ExtensionInfo &ext : s_extensions) {
        auto result = load_extension(ext);
        if (result == LoadResult::failed)
            Logger::error("Failed to load extension: {}", ext.display_name);
    }
    auto count = std::ranges::count_if(
        s_extensions, [](ExtensionInfo const &e) { return e.autoreload && e.initialized; }
    );
    if (count > 0) {
        s_any_autoreload = true;
        Logger::info("Autoreload: watching {} extension(s) for DLL changes", count);
    }
}

// SEH wrappers — must be in separate functions (no C++ objects requiring unwinding)
static int seh_call_api_version(ExtensionInfo::api_version_fn fn)
{
    __try {
        return fn();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

static int seh_call_init(ExtensionInfo::init_fn fn, X4NativeAPI *api)
{
    __try {
        return fn(api);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

static void seh_call_shutdown(ExtensionInfo::shutdown_fn fn)
{
    __try {
        fn();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // logged by caller
    }
}

ExtensionManager::LoadResult ExtensionManager::load_extension(ExtensionInfo &ext)
{
    // Check if the X4 extension is enabled in-game
    auto *game = GameAPI::table();
    if (game && game->IsExtensionEnabled) {
        if (!game->IsExtensionEnabled(ext.extension_id.c_str(), false)) {
            Logger::info("Extension '{}' (id={}) is disabled in-game — skipping", ext.display_name, ext.extension_id);
            return LoadResult::skipped;
        }
    }

    // Check API version compatibility
    if (ext.api_version > X4NATIVE_API_VERSION) {
        Logger::error("Extension '{}' requires API v{}, we have v{}",
                      ext.display_name, ext.api_version, X4NATIVE_API_VERSION);
        return LoadResult::failed;
    }

    // Check DLL exists
    if (!fs::exists(ext.dll_path)) {
        Logger::error("Extension '{}' DLL not found: {}", ext.display_name, ext.dll_path.string());
        return LoadResult::failed;
    }

    std::error_code ec;
    {
        // Copy-on-load: keep the original unlocked so the extension can be rebuilt
        // while the game is running. The live copy is what gets LoadLibrary'd.
        fs::path const &src  = ext.dll_path;
        fs::path        dest = src.parent_path() / (src.stem().string() + "_live" + src.extension().string());
        if (!fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec)) {
            Logger::error("Extension '{}': copy_file failed ({})", ext.display_name, ec.message());
            return LoadResult::failed;
        }
        ext.dll_live_path = std::move(dest);
    }

    auto mtime = std::filesystem::last_write_time(ext.dll_path, ec);
    if (!ec)
        ext.dll_mtime = mtime;

    ext.module = ::LoadLibraryW(ext.dll_live_path.c_str());
    if (!ext.module) {
        Logger::error("Extension '{}': LoadLibrary failed (error={})",
                      ext.display_name, ::GetLastError());
        fs::remove(ext.dll_live_path, ec);
        return LoadResult::failed;
    }

    // Resolve required exports
    ext.fn_api_version = reinterpret_cast<ExtensionInfo::api_version_fn>(GetProcAddress(ext.module, "x4native_api_version"));
    ext.fn_init        = reinterpret_cast<ExtensionInfo::init_fn>(GetProcAddress(ext.module, "x4native_init"));
    ext.fn_shutdown    = reinterpret_cast<ExtensionInfo::shutdown_fn>(GetProcAddress(ext.module, "x4native_shutdown"));

    auto unload_live = [&ext] {
        ::FreeLibrary(ext.module);
        ext.module = nullptr;
        std::error_code ignored_ec;
        fs::remove(ext.dll_live_path, ignored_ec);
        ext.dll_live_path.clear();
        // Close per-extension log if it was already opened (log open happens after this
        // lambda is defined, but ext.log_handle starts as INVALID_HANDLE_VALUE so early
        // failure paths are safe — they simply skip the close)
        if (ext.log_handle != Logger::invalid_handle_value) {
            Logger::flush_and_close_handle(ext.log_handle);
            ext.log_handle = Logger::invalid_handle_value;
        }
        ext.log_path.clear();
    };

    if (!ext.fn_api_version || !ext.fn_init || !ext.fn_shutdown) {
        Logger::error("Extension '{}': missing required exports (x4native_api_version, x4native_init, x4native_shutdown)",
                      ext.display_name);
        unload_live();
        return LoadResult::failed;
    }

    // Check runtime API version
    int ext_api = seh_call_api_version(ext.fn_api_version);
    if (ext_api == -1) {
        Logger::error("Extension '{}': x4native_api_version() crashed", ext.display_name);
        unload_live();
        return LoadResult::failed;
    }

    if (ext_api > X4NATIVE_API_VERSION) {
        Logger::error("Extension '{}': runtime API v{} > framework v{}",
                      ext.display_name, ext_api, X4NATIVE_API_VERSION);
        unload_live();
        return LoadResult::failed;
    }

    // Per-extension log file lives in the user profile, under a subfolder
    // named after the extension:
    //   <profile>\x4native\<extension_id>\<extension_id>.log
    // Authors that set "logfile" in x4native.json can pick the filename
    // (still relative to the subfolder, no traversal).
    // If the profile can't be resolved, fall back to the extension folder.
    {
        fs::path ext_dir = Logger::profile_ext_dir(ext.extension_id);
        fs::path log_dir = ext_dir.empty() ? ext.path : ext_dir;

        if (ext.log_name.empty()) {
            ext.log_path = log_dir / (ext.extension_id + ".log");
        } else if (Logger::is_safe_relative_name(ext.log_name, ("Extension '" + ext.display_name + "': 'logfile'").c_str())) {
            ext.log_path = log_dir / ext.log_name;
        } else {
            ext.log_path = log_dir / (ext.extension_id + ".log");
        }
        ext.log_handle = Logger::open_log(ext.log_path);
        if (ext.log_handle == Logger::invalid_handle_value) {
            Logger::warn("Extension '{}': could not open log at '{}'",
                         ext.display_name, ext.log_path.string());
        } else {
            Logger::write_to(ext.log_handle, LogLevel::Info,
                             "X4Native extension log — " + ext.display_name + " (" + ext.extension_id + ")");
            // Flush warnings buffered during discovery (pre-log) so the
            // extension author sees them at the top of their own log.
            for (auto const &[lv, msg] : ext.pending_warnings)
                Logger::write_to(ext.log_handle, lv, msg);
            ext.pending_warnings.clear();
        }
    }

    // Build the API struct for this extension (stored in ExtensionInfo,
    // persists until shutdown — extensions keep a pointer to it)
    fill_api(ext.api, ext);

    // SEH-wrapped init call
    int result = seh_call_init(ext.fn_init, &ext.api);
    if (result == -1) {
        Logger::error("Extension '{}': x4native_init() crashed (SEH exception)", ext.display_name);
        unload_live();
        return LoadResult::failed;
    }

    if (result != X4NATIVE_OK) {
        Logger::error("Extension '{}': x4native_init() returned error ({})", ext.display_name, result);
        unload_live();
        return LoadResult::failed;
    }

    ext.initialized = true;
    Logger::info("Extension '{}' loaded successfully", ext.display_name);
    return LoadResult::ok;
}

void ExtensionManager::unload_extension(ExtensionInfo &ext)
{
    if (ext.initialized && ext.fn_shutdown) {
        Logger::info("Shutting down extension: {}", ext.display_name);
        seh_call_shutdown(ext.fn_shutdown);
        ext.initialized = false;
    }
    // Remove event subscriptions owned by this extension
    for (int id : ext.subscription_ids)
        EventSystem::unsubscribe(id);
    ext.subscription_ids.clear();
    // Remove any hooks this extension registered (must happen before FreeLibrary
    // since hook callbacks point into the extension's DLL code). Keyed on
    // extension_id — the canonical unique identifier.
    HookManager::remove_all_for_extension(ext.extension_id);
    if (ext.module) {
        ::FreeLibrary(ext.module);
        ext.module = nullptr;
    }
    if (!ext.dll_live_path.empty()) {
        std::error_code ignored_ec;
        fs::remove(ext.dll_live_path, ignored_ec);
        ext.dll_live_path.clear();
    }
    // Close per-extension log (after shutdown so the extension can log until the end)
    if (ext.log_handle != Logger::invalid_handle_value) {
        Logger::flush_and_close_handle(ext.log_handle);
        ext.log_handle = Logger::invalid_handle_value;
    }
    ext.log_path.clear();  // reset so load_extension recomputes from log_name on hot-reload

    // Settings stay registered across hot-reload so values survive; they are
    // only dropped on full shutdown (SettingsManager::shutdown()).
}

// ---------------------------------------------------------------------------
// Autoreload — per-extension mtime polling and deferred reload
// ---------------------------------------------------------------------------

static constexpr int AUTORELOAD_TICK_INTERVAL = 120;  // frames between mtime checks (~2s @60fps)

void ExtensionManager::tick()
{
    if (!s_any_autoreload || ++s_tick_frame < AUTORELOAD_TICK_INTERVAL)
        return;
    s_tick_frame = 0;
    for (ExtensionInfo &ext : s_extensions) {
        if (!ext.autoreload || !ext.initialized || ext.reload_pending)
            continue;
        std::error_code ec;
        auto mtime = fs::last_write_time(ext.dll_path, ec);
        if (ec)
            continue;
        if (ext.dll_mtime < mtime) {
            Logger::info("Extension '{}': DLL changed on disk, queuing hot-reload", ext.display_name);
            ext.reload_pending = true;
        }
    }
}

void ExtensionManager::flush_pending_reloads()
{
    if (!s_any_autoreload)
        return;
    bool any_reloaded = false;
    for (ExtensionInfo &ext : s_extensions) {
        if (!ext.reload_pending)
            continue;
        ext.reload_pending = false;
        any_reloaded       = true;

        Logger::info("Hot-reloading extension: {}", ext.display_name);
        unload_extension(ext);

        if (load_extension(ext) == LoadResult::ok)
            Logger::info("Extension '{}': hot-reload complete", ext.display_name);
        else
            Logger::error("Extension '{}': hot-reload failed", ext.display_name);
    }

    if (any_reloaded) {
        s_any_autoreload = std::ranges::any_of(
            s_extensions,
            [](ExtensionInfo const &e) { return e.autoreload && e.initialized; }
        );
    }
}

// ---------------------------------------------------------------------------
// API struct — what extensions receive in x4native_init()
// ---------------------------------------------------------------------------

// Static wrappers that forward to EventSystem / Logger
static int api_subscribe(char const *event_name, X4NativeEventCallback cb, void *ud, void *api_ptr)
{
    int id = EventSystem::subscribe(event_name, cb, ud);
    // Track subscription for auto-cleanup on extension unload
    if (id > 0 && api_ptr) {
        auto *api = static_cast<X4NativeAPI *>(api_ptr);
        if (auto *ids = static_cast<std::vector<int> *>(api->_ext_subscription_ids))
            ids->emplace_back(id);
    }
    return id;
}

static void api_unsubscribe(int id)
{
    EventSystem::unsubscribe(id);
}

static void api_raise_event(char const *event_name, void *data)
{
    EventSystem::fire(event_name, data);
}

static int api_raise_lua_event(char const *event_name, char const *param)
{
    if (s_raise_lua_event)
        return s_raise_lua_event(event_name, param);
    return -1;
}

static void api_log(int level, char const *message)
{
    auto lv = static_cast<LogLevel>(level);
    Logger::write(lv, message);
}

static char const *s_game_ver_cache;
static constexpr char const *s_x4n_ver_cache = X4_GAME_VERSION_LABEL;

static char const *api_get_game_version()
{
    return s_game_ver_cache;
}

static char const *api_get_x4native_version()
{
    return s_x4n_ver_cache;
}

// Hook wrappers — extract extension context from the API pointer.
// HookManager keys on extension_id (canonical unique), not display name.
static int api_hook_before(char const *fn, X4HookCallback cb, void *ud, void *api_ptr)
{
    auto *api          = static_cast<X4NativeAPI *>(api_ptr);
    int   ext_priority = static_cast<int>(api->_ext_priority);
    return HookManager::hook_before(fn, cb, ud, ext_priority, api->_ext_id);
}

static int api_hook_after(char const *fn, X4HookCallback cb, void *ud, void *api_ptr)
{
    auto *api          = static_cast<X4NativeAPI *>(api_ptr);
    int   ext_priority = static_cast<int>(api->_ext_priority);
    return HookManager::hook_after(fn, cb, ud, ext_priority, api->_ext_id);
}

static void api_unhook(int hook_id)
{
    HookManager::unhook(hook_id);
}

static void *api_ensure_detour(char const *fn, void *detour_fn)
{
    return HookManager::ensure_detour(fn, detour_fn);
}

static void api_run_before_hooks(X4HookContext *ctx)
{
    HookManager::run_before_hooks(ctx);
}

static void api_run_after_hooks(X4HookContext *ctx)
{
    HookManager::run_after_hooks(ctx);
}

static void *api_resolve_internal(char const *name)
{
    return GameAPI::get_internal(name);
}

// MD event subscription wrappers
static int api_md_subscribe_before(uint32_t type_id, X4NativeEventCallback cb, void *ud, void * /*api*/)
{
    return EventSystem::md_subscribe_before(type_id, cb, ud);
}

static int api_md_subscribe_after(uint32_t type_id, X4NativeEventCallback cb, void *ud, void * /*api*/)
{
    return EventSystem::md_subscribe_after(type_id, cb, ud);
}

static int api_register_lua_bridge(char const *lua_event, char const *cpp_event)
{
    if (s_register_lua_bridge)
        return s_register_lua_bridge(lua_event, cpp_event);
    return -1;
}

// Settings wrappers — resolve the calling extension via _ext_info.
static char const *ext_id_from_api(void *api_ptr)
{
    if (!api_ptr)
        return nullptr;
    auto *api = static_cast<X4NativeAPI *>(api_ptr);
    auto *ext = static_cast<ExtensionInfo *>(api->_ext_info);
    return ext ? ext->extension_id.c_str() : nullptr;
}

static int api_get_setting_bool(char const *key, int fallback, void *api_ptr)
{
    char const *id = ext_id_from_api(api_ptr);
    if (!id || !key)
        return fallback;
    return SettingsManager::get_bool(id, key, fallback != 0) ? 1 : 0;
}

static double api_get_setting_number(char const *key, double fallback, void *api_ptr)
{
    char const *id = ext_id_from_api(api_ptr);
    if (!id || !key)
        return fallback;
    return SettingsManager::get_number(id, key, fallback);
}

static char const *api_get_setting_string(char const *key, char const *fallback, void *api_ptr)
{
    char const *id = ext_id_from_api(api_ptr);
    if (!id || !key)
        return fallback;
    return SettingsManager::get_string(id, key, fallback);
}

static void api_set_setting_bool(char const *key, int value, void *api_ptr)
{
    char const *id = ext_id_from_api(api_ptr);
    if (!id || !key)
        return;
    SettingsManager::set_bool(id, key, value != 0);
}

static void api_set_setting_number(char const *key, double value, void *api_ptr)
{
    char const *id = ext_id_from_api(api_ptr);
    if (!id || !key)
        return;
    SettingsManager::set_number(id, key, value);
}

static void api_set_setting_string(char const *key, char const *value, void *api_ptr)
{
    char const *id = ext_id_from_api(api_ptr);
    if (!id || !key)
        return;
    SettingsManager::set_string(id, key, value ? value : "");
}

void ExtensionManager::fill_api(X4NativeAPI &api, ExtensionInfo &ext)
{
    s_game_ver_cache = s_game_version.c_str();

    api = X4NativeAPI{
        .api_version           = X4NATIVE_API_VERSION,
        .subscribe             = api_subscribe,
        .unsubscribe           = api_unsubscribe,
        .raise_event           = api_raise_event,
        .raise_lua_event       = api_raise_lua_event,
        .register_lua_bridge   = api_register_lua_bridge,
        .log                   = api_log,
        .get_game_version      = api_get_game_version,
        .get_x4native_version  = api_get_x4native_version,
        .extension_path        = ext.path.c_str(),
        .game                  = GameAPI::table(),
        .get_game_function     = &GameAPI::get_function,
        .game_func_count       = GameAPI::total_count(),
        .game_types_build      = X4_GAME_TYPES_BUILD,
        .exe_base              = GameAPI::exe_base(),
        .hook_before           = api_hook_before,
        .hook_after            = api_hook_after,
        .unhook                = api_unhook,
        ._ensure_detour        = api_ensure_detour,
        ._run_before_hooks     = api_run_before_hooks,
        ._run_after_hooks      = api_run_after_hooks,
        .resolve_internal      = api_resolve_internal,
        .md_subscribe_before   = api_md_subscribe_before,
        .md_subscribe_after    = api_md_subscribe_after,
        .stash_set             = s_stash_set,
        .stash_get             = s_stash_get,
        .stash_remove          = s_stash_remove,
        .stash_clear           = s_stash_clear,
        ._ext_id               = ext.extension_id.c_str(),
        ._ext_display_name     = ext.display_name.c_str(),
        ._ext_priority         = static_cast<intptr_t>(ext.priority),
        ._ext_subscription_ids = &ext.subscription_ids,
        ._ext_log_handle       = ext.log_handle,
        ._ext_log_fn           = reinterpret_cast<void *>(api_log_ext),
        ._ext_init_log_fn      = reinterpret_cast<void *>(api_init_log),
        ._ext_log_named_fn     = reinterpret_cast<void *>(api_log_named),
        ._ext_info             = &ext,
        .offsets               = &s_offsets,
        .get_setting_bool      = api_get_setting_bool,
        .get_setting_number    = api_get_setting_number,
        .get_setting_string    = api_get_setting_string,
        .set_setting_bool      = api_set_setting_bool,
        .set_setting_number    = api_set_setting_number,
        .set_setting_string    = api_set_setting_string,
        ._reserved             = {},
    };
}

// ---------------------------------------------------------------------------
// JSON serialization of loaded extensions
// ---------------------------------------------------------------------------

char const *ExtensionManager::loaded_extensions_json()
{
    json arr = json::array();
    for (ExtensionInfo const &ext : s_extensions) {
        if (ext.initialized) {
            arr.emplace_back(json{
                {"id",           ext.extension_id},
                {"display_name", ext.display_name},
                {"path",         ext.path        },
                {"priority",     ext.priority    }
            });
        }
    }
    // Single static buffer — pointer is stable until the next call.
    static std::string cached;
    cached = arr.dump();
    return cached.c_str();
}

} // namespace x4n
