#pragma once
#include <cstdint>

#define X4NATIVE_API_VERSION 1
#define X4NATIVE_VERSION_MAJOR 0
#define X4NATIVE_VERSION_MINOR 9
#define X4NATIVE_VERSION_PATCH 0
#define X4NATIVE_VERSION_STR "0.9.0"

// ---------------------------------------------------------------------------
// Interface between proxy and core DLL.
// The proxy owns the CoreDispatch struct; the core fills it with function
// pointers during core_init(). All Lua → DLL calls go through this table,
// so reloading the core DLL just re-fills the pointers.
// ---------------------------------------------------------------------------
struct CoreDispatch {
    void        (*discover_extensions)();
    void        (*raise_event)(const char* event_name, const char* param);
    const char* (*get_version)();
    const char* (*get_loaded_extensions)();  // Returns JSON array string
    void        (*set_lua_state)(void* L);
    void        (*prepare_reload)();
    void        (*shutdown)();
    void        (*log)(int level, const char* message);
};

// Callback the proxy provides so the core can fire Lua events.
typedef int (*raise_lua_event_fn)(const char* name, const char* param);

// Callback the proxy provides so the core can register Lua→C++ bridges.
typedef int (*register_lua_bridge_fn)(const char* lua_event, const char* cpp_event);

// Passed from proxy to core during core_init()
struct CoreInitContext {
    void*         lua_state;    // lua_State* (void* to avoid lua header dep in core)
    const char*   ext_root;     // Absolute path to extensions/x4native/
    CoreDispatch* dispatch;     // Proxy-owned table for core to fill
    raise_lua_event_fn raise_lua_event;  // Proxy-implemented Lua bridge
    register_lua_bridge_fn register_lua_bridge;  // Proxy-implemented Lua→C++ bridge registration
};

// Core DLL exported function signatures
typedef int  (*core_init_fn)(CoreInitContext* ctx);
typedef void (*core_shutdown_fn)();
