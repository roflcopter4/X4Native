#pragma once
// ---------------------------------------------------------------------------
// x4native_core.dll — Game API
//
// Resolves X4.exe's exported game functions into a type-safe function pointer
// table (X4GameFunctions). Extensions receive this table via api->game
// and can call game functions directly with compile-time type checking.
//
// Also provides a named lookup fallback for untyped exports via
// get_function(), exposed to extensions as api->get_game_function.
//
// Internal (non-exported) functions are resolved via RVA database
// (native/version_db/internal_functions.json) loaded at init time.
// ---------------------------------------------------------------------------

#include <string>
#include <unordered_map>

struct X4GameFunctions;

namespace x4n {

class GameAPI {
public:
    static bool init();
    static void shutdown();

    // Load internal function RVA database for the given game build.
    // Tries primary_build first (e.g. "900-beta2"), falls back to fallback_build
    // (e.g. "900") so beta builds can reuse the last known-good RVA entry.
    static void load_internal_db(const std::string& ext_root,
                                 const std::string& primary_build,
                                 const std::string& fallback_build = {});

    // The resolved function pointer table (NULL if not initialized)
    static X4GameFunctions* table();

    // Named lookup for any exported function (typed or untyped)
    static void* get_function(const char* name);

    // Named lookup for internal (non-exported) functions via RVA database.
    // Returns resolved address or nullptr if not found for this game version.
    static void* get_internal(const char* name);

    // X4.exe image base address (for resolving global RVAs)
    static uintptr_t exe_base();

    // Stats
    static int resolved_count();
    static int total_count();
    static int internal_count();

private:
    static X4GameFunctions s_table;
    static bool s_initialized;
    static std::unordered_map<std::string, void*> s_internal_funcs;
};

} // namespace x4n
