// ---------------------------------------------------------------------------
// x4native_64.dll — Thin Proxy DLL
//
// This is the stable entry point loaded by Lua's package.loadlib().
// It rarely changes, so the Windows file lock is irrelevant.
//
// Responsibilities:
//   1. Resolve Lua API function pointers from host process
//   2. Copy-on-load x4native_core.dll → x4native_core_live.dll
//   3. LoadLibrary the copy, call core_init() to fill dispatch table
//   4. Return a Lua table whose functions dispatch through the table
//   5. On /reloadui: detect newer core on disk, hot-reload if needed
// ---------------------------------------------------------------------------

#include "Common.h"
#include "lua_api.h"
#include "x4native_defs.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// State (persists across /reloadui because the proxy stays mapped)
// ---------------------------------------------------------------------------
static lua_State*       g_lua            = nullptr;
static HMODULE          g_core_module    = nullptr;
static CoreDispatch     g_dispatch       = {};
static core_init_fn     g_core_init      = nullptr;
static core_shutdown_fn g_core_shutdown  = nullptr;
static std::u8string    g_ext_root_utf8;
static fs::path         g_ext_root;
static fs::path         g_core_path;
static fs::path         g_core_live_path;
static bool             g_initialized    = false;

static void output_debug_string(std::string_view const &msg)
{
#ifdef _WIN32
    wchar_t stack_buf[512];
    int size = ::MultiByteToWideChar(CP_UTF8, 0, msg.data(), static_cast<int>(msg.size()), stack_buf, 512);
    if (size > 0 && size < 512) {
        stack_buf[size] = L'\0';
        ::OutputDebugStringW(stack_buf);
    } else {
        // Fallback for long strings (rare)
        size = ::MultiByteToWideChar(CP_UTF8, 0, msg.data(), static_cast<int>(msg.size()), nullptr, 0);
        auto wbuf = std::make_unique<wchar_t[]>(size + 1);
        ::MultiByteToWideChar(CP_UTF8, 0, msg.data(), static_cast<int>(msg.size()), wbuf.get(), size);
        wbuf[size] = L'\0';
        ::OutputDebugStringW(wbuf.get());
    }
#else
    if (msg.ends_with('\n'))
        fwrite(msg.data(), 1, msg.size(), stderr);
    else
        fprintf(stderr, "%.*s\n", msg.size(), msg.data());
#endif
}

// ---------------------------------------------------------------------------
// Core hot-reload state (compiled in when X4N_WITH_RELOAD=1)
// ---------------------------------------------------------------------------
// Guarded on a dedicated feature flag rather than NDEBUG because the project's
// actually-run build config is Release/RelWithDebInfo (std::string layout
// must match the game's Release STL). Runtime behavior is separately gated
// by the `autoreload` key in x4native_settings.json.
#ifdef X4N_WITH_RELOAD
#include <fstream>
#include <nlohmann/json.hpp>

static bool g_autoreload_enabled   = false;   // from x4native_settings.json
static bool g_autoreload_checked   = false;   // settings file read?
static fs::file_time_type g_last_core_mtime  = {};       // last known timestamp

/// Read x4native_settings.json to check "autoreload" flag.
/// Called on each core load so the result is logged after hot-reloads too.
static void read_autoreload_setting()
{
    auto path = g_ext_root / "x4native_settings.json";
    auto file = std::ifstream(path);
    if (!file.is_open()) {
        if (g_dispatch.log)
            g_dispatch.log(2, "Core autoreload: settings file not found");
        return;
    }

    try {
        auto cfg = nlohmann::json::parse(file);
        auto it  = cfg.find("autoreload");
        if (it != cfg.end() && it->is_boolean())
            g_autoreload_enabled = it->get<bool>();
    } catch (...) {
    }

    if (g_dispatch.log)
        g_dispatch.log(1, g_autoreload_enabled ? "Core autoreload: ENABLED" : "Core autoreload: disabled");
}

/// Check if core DLL on disk is newer than last known timestamp.
/// Updates g_last_core_mtime on change.
static bool core_modified_since_last_check()
{
    std::error_code ec;
    auto mtime = fs::last_write_time(g_core_path, ec);
    if (ec)
        return false;
    if (g_last_core_mtime == fs::file_time_type{}) {
        // First call — seed with current timestamp
        g_last_core_mtime = mtime;
        return false;
    }
    if (mtime > g_last_core_mtime) {
        g_last_core_mtime = mtime;
        if (g_dispatch.log)
            g_dispatch.log(1, "Autoreload: core DLL modified on disk");
        return true;
    }
    return false;
}
#endif // X4N_WITH_RELOAD

// ---------------------------------------------------------------------------
// Stash — in-memory key-value that survives /reloadui and core hot-reload
// Keyed by namespace (typically extension name) + key.
// ---------------------------------------------------------------------------
static std::unordered_map<std::string, std::unordered_map<std::string, std::vector<uint8_t>>> g_stash;
static std::mutex g_stash_mutex;

static int proxy_stash_set(char const *ns, char const *key, void const *data, uint32_t size)
{
    if (!ns || !key || (!data && size > 0))
        return 0;
    std::scoped_lock lock(g_stash_mutex);
    auto &entry = g_stash[ns][key];
    entry.assign(static_cast<uint8_t const *>(data), static_cast<uint8_t const *>(data) + size);
    return 1;
}

static void const *proxy_stash_get(char const *ns, char const *key, uint32_t *out_size)
{
    if (!ns || !key)
        return nullptr;
    std::scoped_lock lock(g_stash_mutex);
    auto ns_it = g_stash.find(ns);
    if (ns_it == g_stash.end())
        return nullptr;
    auto it = ns_it->second.find(key);
    if (it == ns_it->second.end())
        return nullptr;
    if (out_size)
        *out_size = static_cast<uint32_t>(it->second.size());
    return it->second.data();
}

static int proxy_stash_remove(char const *ns, char const *key)
{
    if (!ns || !key)
        return 0;
    std::scoped_lock lock(g_stash_mutex);
    auto ns_it = g_stash.find(ns);
    if (ns_it == g_stash.end())
        return 0;
    return ns_it->second.erase(key) > 0 ? 1 : 0;
}

static void proxy_stash_clear(char const *ns)
{
    if (!ns)
        return;
    std::scoped_lock lock(g_stash_mutex);
    g_stash.erase(ns);
}

// Forward declarations (defined below with Lua-facing functions)
static int proxy_raise_lua_event(char const *name, char const *param);
static int proxy_register_lua_bridge(char const * lua_event, char const * cpp_event);

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

/// Derive the extension root from the proxy DLL path.
/// Proxy lives at <ext_root>/native/x4native_64.dll
static fs::path detect_ext_root()
{
    wchar_t buf[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&detect_ext_root), &self);
    GetModuleFileNameW(self, buf, std::size(buf));

    auto full_path = fs::path(buf);
    auto path = fs::path(full_path).parent_path();
    while (!path.empty() && path.filename() != "native")
        path = path.parent_path();
    if (path.empty())
        path = full_path.parent_path(); // Fallback: parent of DLL
    return path.parent_path();
}

// ---------------------------------------------------------------------------
// Core DLL loading
// ---------------------------------------------------------------------------

static bool load_core()
{
    std::error_code ec;
    // Copy-on-load: the original stays unlocked so builds can overwrite it
    if (!fs::copy_file(g_core_path, g_core_live_path, fs::copy_options::overwrite_existing, ec)) {
        output_debug_string("X4Native proxy: CopyFile failed for core DLL\n");
        return false;
    }

    g_core_module = ::LoadLibraryW(g_core_live_path.c_str());
    if (!g_core_module) {
        output_debug_string("X4Native proxy: LoadLibrary failed for core DLL\n");
        return false;
    }

    g_core_init     = reinterpret_cast<core_init_fn>(::GetProcAddress(g_core_module, "core_init"));
    g_core_shutdown = reinterpret_cast<core_shutdown_fn>(::GetProcAddress(g_core_module, "core_shutdown"));

    if (!g_core_init) {
        output_debug_string("X4Native proxy: core_init export not found\n");
        ::FreeLibrary(g_core_module);
        g_core_module = nullptr;
        return false;
    }

    CoreInitContext ctx = {
        .lua_state           = g_lua,
        .ext_root            = reinterpret_cast<char const *>(g_ext_root_utf8.c_str()),
        .dispatch            = &g_dispatch,
        .raise_lua_event     = &proxy_raise_lua_event,
        .register_lua_bridge = &proxy_register_lua_bridge,
        .stash_set           = &proxy_stash_set,
        .stash_get           = &proxy_stash_get,
        .stash_remove        = &proxy_stash_remove,
        .stash_clear         = &proxy_stash_clear,
    };

    if (g_core_init(&ctx) != 0) {
        output_debug_string("X4Native proxy: core_init returned error\n");
        ::FreeLibrary(g_core_module);
        g_core_module = nullptr;
        return false;
    }

#ifdef X4N_WITH_RELOAD
    read_autoreload_setting();
#endif

    return true;
}

static bool reload_core()
{
    // Tell current core to prepare (unhook, notify extensions)
    if (g_dispatch.prepare_reload)
        g_dispatch.prepare_reload();

    if (g_core_module) {
        if (g_core_shutdown)
            g_core_shutdown();
        ::FreeLibrary(g_core_module);
        g_core_module = nullptr;
    }

    // Reset dispatch table + function pointers
    g_dispatch      = {};
    g_core_init     = nullptr;
    g_core_shutdown = nullptr;

    return load_core();
}

/// Returns true if the on-disk core is newer than the live copy.
static bool core_needs_reload()
{
    std::error_code ec;
    auto disk_time = fs::last_write_time(g_core_path, ec);
    if (ec)
        return false;
    auto live_time = fs::last_write_time(g_core_live_path, ec);
    if (ec)
        return true;
    return disk_time > live_time;
}

// ---------------------------------------------------------------------------
// Lua-facing API functions  (thin forwarders into g_dispatch)
// ---------------------------------------------------------------------------

static int l_discover_extensions(lua_State *)
{
    if (g_dispatch.discover_extensions)
        g_dispatch.discover_extensions();
    return 0;
}

static int l_raise_event(lua_State *L)
{
    char const *ev    = x4n::lua::L_checkstring(L, 1);
    char const *param = nullptr;
    if (x4n::lua::gettop(L) >= 2 && x4n::lua::type(L, 2) == LUA_TSTRING)
        param = x4n::lua::tostring(L, 2);
    if (g_dispatch.raise_event)
        g_dispatch.raise_event(ev, param);
    return 0;
}

// ---------------------------------------------------------------------------
// Lua bridge: proxy_raise_lua_event
//
// Called by extensions (via core dispatch) to fire a Lua event.
// Uses X4's global CallEventScripts() — same path as MD <raise_lua_event>.
// ---------------------------------------------------------------------------
static int proxy_raise_lua_event(char const *name, char const *param)
{
    if (!g_lua)
        return -1;
    x4n::lua::getfield(g_lua, LUA_GLOBALSINDEX, "CallEventScripts");
    x4n::lua::pushstring(g_lua, name);
    if (param)
        x4n::lua::pushstring(g_lua, param);
    else
        x4n::lua::pushnil(g_lua);
    return x4n::lua::pcall(g_lua, 2, 0, 0);
}

// ---------------------------------------------------------------------------
// Dynamic Lua→C++ event bridge
//
// Extensions call register_lua_bridge(lua_event, cpp_event) to wire a
// Lua RegisterEvent handler that forwards into the C++ event bus.
// A single C function (bridge_handler) is registered as an upvalue-based
// Lua closure for each mapping.
// ---------------------------------------------------------------------------
static std::unordered_map<std::string, std::string> g_lua_bridges;

// Lua closure: upvalue 1 = C++ event name string
// Lua calls handler(eventName, argument1) — argument1 is at stack index 2
static int bridge_handler(lua_State *L)
{
    char const *cpp_event = x4n::lua::tostring(L, lua_upvalueindex(1));
    if (cpp_event && g_dispatch.raise_event) {
        char const *param = nullptr;
        if (x4n::lua::gettop(L) >= 2 && x4n::lua::type(L, 2) == LUA_TSTRING)
            param = x4n::lua::tostring(L, 2);
        g_dispatch.raise_event(cpp_event, param);
    }
    return 0;
}

static int proxy_register_lua_bridge(char const *lua_event, char const *cpp_event)
{
    if (!g_lua || !lua_event || !cpp_event)
        return -1;

    // Already registered?
    if (g_lua_bridges.contains(lua_event))
        return 0;

    // Create a Lua closure with cpp_event as upvalue, then call RegisterEvent
    // Stack: RegisterEvent, lua_event, closure
    x4n::lua::getfield(g_lua, LUA_GLOBALSINDEX, "RegisterEvent");
    x4n::lua::pushstring(g_lua, lua_event);
    x4n::lua::pushstring(g_lua, cpp_event);
    x4n::lua::pushcclosure(g_lua, bridge_handler, 1);
    int err = x4n::lua::pcall(g_lua, 2, 0, 0);

    if (err == 0) {
        g_lua_bridges[lua_event] = cpp_event;
        if (g_dispatch.log) {
            std::string str = "Lua bridge: registered '"s + lua_event + "' -> '" + cpp_event + '\'';
            g_dispatch.log(1, str.c_str());
        }
    }
    return err;
}

static int l_raise_lua_event(lua_State *L)
{
    char const *name  = x4n::lua::L_checkstring(L, 1);
    char const *param = nullptr;
    if (x4n::lua::gettop(L) >= 2 && x4n::lua::type(L, 2) == LUA_TSTRING)
        param = x4n::lua::tostring(L, 2);
    x4n::lua::pushinteger(L, proxy_raise_lua_event(name, param));
    return 1;
}

static int l_log(lua_State *L)
{
    int level = static_cast<int>(x4n::lua::tointeger(L, 1));
    char const *msg   = x4n::lua::L_checkstring(L, 2);
    if (g_dispatch.log)
        g_dispatch.log(level, msg);
    return 0;
}

static int l_get_version(lua_State *L)
{
    char const *v = g_dispatch.get_version ? g_dispatch.get_version() : "unknown";
    x4n::lua::pushstring(L, v);
    return 1;
}

static int l_get_loaded_extensions(lua_State *L)
{
    char const *j = g_dispatch.get_loaded_extensions
        ? g_dispatch.get_loaded_extensions()
        : "[]";
    x4n::lua::pushstring(L, j);
    return 1;
}

static int l_reload(lua_State *L)
{
    x4n::lua::pushboolean(L, reload_core() ? 1 : 0);
    return 1;
}

static int l_prepare_reload(lua_State *)
{
    if (g_dispatch.prepare_reload)
        g_dispatch.prepare_reload();
    return 0;
}

// ---------------------------------------------------------------------------
// Per-extension settings — Lua marshalling
// ---------------------------------------------------------------------------
//
// l_get_extension_settings(ext_id) returns an ordered array of row tables:
//   { {id, name, type, current, default, options?, min?, max?, step?}, ... }
// l_set_extension_setting(ext_id, key, value) writes the value (type inferred
// from the Lua value).

static void push_setting_row(lua_State *L, SettingInfo const &info)
{
    x4n::lua::createtable(L, 0, 8);

    x4n::lua::pushstring(L, info.id ? info.id : "");
    x4n::lua::setfield(L, -2, "id");
    x4n::lua::pushstring(L, info.name ? info.name : "");
    x4n::lua::setfield(L, -2, "name");

    char const *type_str = "toggle";
    if (info.type == X4N_SETTING_DROPDOWN)
        type_str = "dropdown";
    else if (info.type == X4N_SETTING_SLIDER)
        type_str = "slider";
    x4n::lua::pushstring(L, type_str);
    x4n::lua::setfield(L, -2, "type");

    switch (info.type) {
    case X4N_SETTING_TOGGLE:
        x4n::lua::pushboolean(L, info.current_bool);
        x4n::lua::setfield(L, -2, "current");
        x4n::lua::pushboolean(L, info.default_bool);
        x4n::lua::setfield(L, -2, "default");
        break;

    case X4N_SETTING_SLIDER:
        x4n::lua::pushnumber(L, info.current_number);
        x4n::lua::setfield(L, -2, "current");
        x4n::lua::pushnumber(L, info.default_number);
        x4n::lua::setfield(L, -2, "default");
        x4n::lua::pushnumber(L, info.min);
        x4n::lua::setfield(L, -2, "min");
        x4n::lua::pushnumber(L, info.max);
        x4n::lua::setfield(L, -2, "max");
        x4n::lua::pushnumber(L, info.step);
        x4n::lua::setfield(L, -2, "step");
        break;

    case X4N_SETTING_DROPDOWN:
        x4n::lua::pushstring(L, info.current_string ? info.current_string : "");
        x4n::lua::setfield(L, -2, "current");
        x4n::lua::pushstring(L, info.default_string ? info.default_string : "");
        x4n::lua::setfield(L, -2, "default");
        x4n::lua::createtable(L, info.option_count, 0);
        for (int i = 0; i < info.option_count; ++i) {
            x4n::lua::createtable(L, 0, 2);
            x4n::lua::pushstring(L, info.options[i].id ? info.options[i].id : "");
            x4n::lua::setfield(L, -2, "id");
            x4n::lua::pushstring(L, info.options[i].text ? info.options[i].text : "");
            x4n::lua::setfield(L, -2, "text");
            x4n::lua::rawseti(L, -2, i + 1);
        }
        x4n::lua::setfield(L, -2, "options");
        break;
    }
}

static int l_get_extension_settings(lua_State *L)
{
    char const        *ext_id  = x4n::lua::L_checkstring(L, 1);
    SettingInfo const *entries = nullptr;

    int n = 0;
    if (g_dispatch.enumerate_settings)
        n = g_dispatch.enumerate_settings(ext_id, &entries);
    x4n::lua::createtable(L, n, 0);
    for (int i = 0; i < n; ++i) {
        push_setting_row(L, entries[i]);
        x4n::lua::rawseti(L, -2, i + 1);
    }
    return 1;
}

static int l_set_extension_setting(lua_State *L)
{
    char const *ext_id = x4n::lua::L_checkstring(L, 1);
    char const *key    = x4n::lua::L_checkstring(L, 2);

    SettingValueC v;
    switch (x4n::lua::type(L, 3)) {
    case LUA_TBOOLEAN:
        v.type = X4N_SETTING_TOGGLE;
        v.b    = static_cast<uint8_t>(x4n::lua::toboolean(L, 3));
        break;
    case LUA_TNUMBER:
        v.type = X4N_SETTING_SLIDER;
        v.d    = static_cast<double>(x4n::lua::tonumber(L, 3));
        break;
    case LUA_TSTRING:
        v.type = X4N_SETTING_DROPDOWN;
        v.s    = x4n::lua::tostring(L, 3);
        break;
    default:
        return x4n::lua::L_error(L, "set_extension_setting: value must be boolean, number, or string");
    }

    if (g_dispatch.set_extension_setting)
        g_dispatch.set_extension_setting(ext_id, key, &v);
    return 0;
}

#ifdef X4N_WITH_RELOAD
static int l_should_autoreload(lua_State* L) {
    if (!g_autoreload_checked) {
        read_autoreload_setting();
        g_autoreload_checked = true;
    }
    if (!g_autoreload_enabled) {
        x4n::lua::pushboolean(L, 0);
        return 1;
    }
    x4n::lua::pushboolean(L, core_modified_since_last_check() ? 1 : 0);
    return 1;
}
#endif // X4N_WITH_RELOAD

// ---------------------------------------------------------------------------
// Entry point — called by Lua: package.loadlib("...dll", "luaopen_x4native")
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport)
int luaopen_x4native(lua_State *L)
{
    g_lua = L;

    // Resolve Lua C API function pointers (idempotent)
    if (!x4n::lua::resolve()) {
        output_debug_string("X4Native: FATAL — failed to resolve Lua API\n");
        return 0;
    }

    if (!g_initialized) {
        // --- First load (game start) ---
        g_ext_root       = detect_ext_root();
        g_ext_root_utf8  = g_ext_root.u8string();
        g_core_path      = g_ext_root / "native" / "x4native_core.dll";
        g_core_live_path = g_ext_root / "native" / "x4native_core_live.dll";

        if (!load_core())
            return x4n::lua::L_error(L, "X4Native: failed to load core DLL");

        g_initialized = true;
    } else {
        // --- /reloadui or save load: proxy already loaded, update lua_State ---
        if (g_dispatch.set_lua_state)
            g_dispatch.set_lua_state(L);

        // Clear bridge cache — the old Lua state (and its RegisterEvent handlers)
        // is gone; bridges must re-register on the new state.
        g_lua_bridges.clear();

        // Hot-reload core if a newer build is on disk
        if (core_needs_reload())
            reload_core();
    }

    // Build the Lua API table returned to x4native.lua
    x4n::lua::newtable(L);

    static constexpr struct lua_function_with_name {
        char const   *name;
        lua_CFunction fn;
    } funcs[] = {
        { "discover_extensions",     &l_discover_extensions    },
        { "raise_event",             &l_raise_event            },
        { "raise_lua_event",         &l_raise_lua_event        },
        { "log",                     &l_log                    },
        { "get_version",             &l_get_version            },
        { "get_loaded_extensions",   &l_get_loaded_extensions  },
        { "reload",                  &l_reload                 },
        { "prepare_reload",          &l_prepare_reload         },
        { "get_extension_settings",  &l_get_extension_settings },
        { "set_extension_setting",   &l_set_extension_setting  },
#ifdef X4N_WITH_RELOAD
        { "should_autoreload",       &l_should_autoreload      },
#endif
    };

    for (auto const &[name, fn] : funcs) {
        x4n::lua::pushcfunction(L, fn);
        x4n::lua::setfield(L, -2, name);
    }

    return 1;  // one return value: the API table
}

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------

extern BOOL WINAPI DllMain(_In_ HINSTANCE hinstDll, _In_ DWORD fdwReason, _In_ LPVOID lpvReserved);
BOOL WINAPI DllMain(_In_ HINSTANCE hinstDll, _In_ DWORD fdwReason, _In_ LPVOID lpvReserved)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH: {
        (void)::setlocale(LC_ALL, "en_US.UTF-8");
        // Pin this DLL so LuaJIT's FreeLibrary (on lua_close during save
        // load) cannot unload us. This preserves all static state —
        // g_initialized, g_core_module, g_dispatch — across Lua state
        // destruction and recreation. Without this, save loads cause the proxy
        // to unload while core_live.dll remains file-locked, making the next
        // load_core() CopyFile fail.
        HMODULE pinned = nullptr;
        ::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPWSTR>(hinstDll),
            &pinned
        );
        break;
    }
    case DLL_PROCESS_DETACH:
        if (lpvReserved != nullptr) {
            // Process is terminating. Other DLLs' statics may already be
            // destroyed, all threads killed, heap potentially corrupted.
            // The OS reclaims all memory, handles, pipes, and file locks.
            // Companion detects broken pipe and exits on its own.
            return TRUE;
        }
        // Dynamic unload (FreeLibrary): safe to clean up.
        // Unreachable while pinned, but correct if pinning is ever removed.
        if (g_core_shutdown)
            g_core_shutdown();
        if (g_core_module) {
            ::FreeLibrary(g_core_module);
            g_core_module = nullptr;
        }
        break;
    }

    return TRUE;
}
