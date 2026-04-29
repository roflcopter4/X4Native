#pragma once

#include "Common.h"

// Public constants (X4N_SETTING_*, X4NativeSettingChanged) live in the SDK.
#include <x4native_extension.h>

// ---------------------------------------------------------------------------
// Per-extension settings — ABI shared between proxy and core
// ---------------------------------------------------------------------------
// Core owns the backing data; proxy reads through `SettingInfo*` when
// building Lua tables. Pointers inside SettingInfo (id/name/strings/options)
// remain valid until the extension is unloaded or the value is re-set.

struct SettingOptionC {
    char const *id;
    char const *text;
};

struct SettingInfo {
    char const *id;
    char const *name;

    // Dropdown options (type == X4N_SETTING_DROPDOWN).
    SettingOptionC const *options;
    int option_count;

    int8_t type; // X4N_SETTING_*
    // Default value (populated for all types).
    uint8_t     default_bool;
    double      default_number;
    char const *default_string;

    // Current value — only the field matching `type` is meaningful.
    union {
        uint8_t     current_bool;   // X4N_SETTING_TOGGLE
        double      current_number; // X4N_SETTING_SLIDER
        char const *current_string; // X4N_SETTING_DROPDOWN (option id)
    };

    // Slider bounds (type == X4N_SETTING_SLIDER).
    double min;
    double max;
    double step;
};

// Tagged value passed to set_extension_setting. type must match the schema.
struct SettingValueC {
    int8_t type; // X4N_SETTING_*
    union {
        uint8_t     b;  // bool   (X4N_SETTING_TOGGLE)
        double      d;  // number (X4N_SETTING_SLIDER)
        char const *s;  // string (X4N_SETTING_DROPDOWN)
    };
};

// ---------------------------------------------------------------------------
// Interface between proxy and core DLL.
// The proxy owns the CoreDispatch struct; the core fills it with function
// pointers during core_init(). All Lua → DLL calls go through this table,
// so reloading the core DLL just re-fills the pointers.
// ---------------------------------------------------------------------------
struct CoreDispatch {
    void        (*discover_extensions)(void);
    void        (*raise_event)(char const *event_name, char const *param);
    char const *(*get_version)(void);
    char const *(*get_loaded_extensions)(void); // Returns JSON array string
    void        (*set_lua_state)(void *L);
    void        (*prepare_reload)(void);
    void        (*shutdown)(void);
    void        (*log)(int level, char const *message);

    // Per-extension settings
    // enumerate_settings returns the number of settings declared by the given
    // extension and fills *out with a pointer to core's internal array.
    // Pointer stays valid until the extension is unloaded.
    int (*enumerate_settings)(char const *ext_id, SettingInfo const **out);
    // Write a value. type mismatch or unknown key is logged and ignored.
    void (*set_extension_setting)(char const *ext_id, char const *key, SettingValueC const *value);
};

// Callback the proxy provides so the core can fire Lua events.
using raise_lua_event_fn = int (*)(char const *name, char const *param);

// Callback the proxy provides so the core can register Lua→C++ bridges.
using register_lua_bridge_fn = int (*)(char const *lua_event, char const *cpp_event);

// Stash (proxy-owned in-memory key-value, survives /reloadui and hot-reload)
// Namespace isolates keys per extension; extensions may read other namespaces.
using stash_set_fn    = int         (*)(char const *ns, char const *key, void const *data, uint32_t size);
using stash_get_fn    = void const *(*)(char const *ns, char const *key, uint32_t *out_size);
using stash_remove_fn = int         (*)(char const *ns, char const *key);
using stash_clear_fn  = void        (*)(char const *ns);

// Passed from proxy to core during core_init()
struct CoreInitContext {
    void                  *lua_state;           // lua_State* (void* to avoid lua header dep in core)
    char const *           ext_root;            // Absolute path to extensions/x4native/
    CoreDispatch          *dispatch;            // Proxy-owned table for core to fill
    raise_lua_event_fn     raise_lua_event;     // Proxy-implemented Lua bridge
    register_lua_bridge_fn register_lua_bridge; // Proxy-implemented Lua→C++ bridge registration

    // Stash (proxy-owned in-memory key-value, survives reloads)
    stash_set_fn    stash_set;
    stash_get_fn    stash_get;
    stash_remove_fn stash_remove;
    stash_clear_fn  stash_clear;
};

// Core DLL exported function signatures
typedef int (*core_init_fn)(CoreInitContext *ctx);
typedef void (*core_shutdown_fn)();
