// ---------------------------------------------------------------------------
// x4native_core.dll — Game API Implementation
//
// Resolves all typed game functions via GetProcAddress into the
// X4GameFunctions struct. Uses the auto-generated x4_game_func_names.inc
// data to iterate over {name, offset} pairs.
//
// Internal (non-exported) functions are resolved from an RVA database
// (native/version_db/internal_functions.json).
// ---------------------------------------------------------------------------

#include "game_api.h"
#include "logger.h"

#include <x4_game_func_table.h>
#include <nlohmann/json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <fstream>

namespace x4n {

X4GameFunctions GameAPI::s_table = {};
bool GameAPI::s_initialized = false;
std::unordered_map<std::string, void*> GameAPI::s_internal_funcs;

// ---------------------------------------------------------------------------
// Resolver data — reuse the X4_FUNC list to build {name, offset} pairs
// ---------------------------------------------------------------------------

struct FuncEntry {
    const char* name;
    size_t      offset;
};

#define X4_FUNC(ret, name, params) { #name, offsetof(X4GameFunctions, name) },
static const FuncEntry s_func_entries[] = {
#include <x4_game_func_list.inc>
};
#undef X4_FUNC

static constexpr int TOTAL_FUNCTIONS =
    static_cast<int>(sizeof(s_func_entries) / sizeof(s_func_entries[0]));

// Internal function resolver data — same struct, resolved from RVA database
#define X4_FUNC(ret, name, params) { #name, offsetof(X4GameFunctions, name) },
static const FuncEntry s_ifunc_entries[] = {
#include <x4_internal_func_list.inc>
    { nullptr, 0 }  // sentinel (list may be empty)
};
#undef X4_FUNC

static HMODULE s_x4_module = nullptr;
static int     s_resolved  = 0;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool GameAPI::init() {
    // X4.exe is the host process — GetModuleHandle(NULL) gets its handle
    s_x4_module = GetModuleHandleA(nullptr);
    if (!s_x4_module) {
        Logger::error("GameAPI: Failed to get X4.exe module handle");
        return false;
    }

    memset(&s_table, 0, sizeof(s_table));
    s_resolved = 0;

    auto* base = reinterpret_cast<char*>(&s_table);
    for (const auto& entry : s_func_entries) {
        FARPROC proc = GetProcAddress(s_x4_module, entry.name);
        if (proc) {
            *reinterpret_cast<void**>(base + entry.offset) =
                reinterpret_cast<void*>(proc);
            s_resolved++;
        }
    }

    s_initialized = true;
    Logger::info("GameAPI: Resolved {}/{} game functions", s_resolved, TOTAL_FUNCTIONS);

    if (s_resolved < TOTAL_FUNCTIONS) {
        Logger::warn("GameAPI: {} functions could not be resolved",
                     TOTAL_FUNCTIONS - s_resolved);
    }

    return true;
}

void GameAPI::shutdown() {
    memset(&s_table, 0, sizeof(s_table));
    s_initialized = false;
    s_resolved = 0;
    s_x4_module = nullptr;
    s_internal_funcs.clear();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

X4GameFunctions* GameAPI::table() {
    return s_initialized ? &s_table : nullptr;
}

void* GameAPI::get_function(const char* name) {
    if (!s_x4_module || !name) return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(s_x4_module, name));
}

void* GameAPI::get_internal(const char* name) {
    if (!name) return nullptr;
    auto it = s_internal_funcs.find(name);
    if (it != s_internal_funcs.end()) return it->second;
    return nullptr;
}

void GameAPI::load_internal_db(const std::string& ext_root,
                               const std::string& primary_build,
                               const std::string& fallback_build) {
    std::string db_path = ext_root + "native/version_db/internal_functions.json";
    std::ifstream file(db_path);
    if (!file.is_open()) {
        Logger::debug("GameAPI: No internal functions database found");
        return;
    }

    try {
        auto db = nlohmann::json::parse(file);
        if (!db.contains("functions") || !db["functions"].is_object()) return;

        auto base = reinterpret_cast<uintptr_t>(s_x4_module);
        int count = 0;

        for (auto& [name, entry] : db["functions"].items()) {
            const std::string* key = nullptr;
            if (entry.contains(primary_build))                              key = &primary_build;
            else if (!fallback_build.empty() && entry.contains(fallback_build)) key = &fallback_build;
            if (!key) continue;
            auto& ver = entry[*key];
            if (!ver.contains("rva") || !ver["rva"].is_string()) continue;

            auto rva_str = ver["rva"].get<std::string>();
            uintptr_t rva = std::stoull(rva_str, nullptr, 16);
            void* addr = reinterpret_cast<void*>(base + rva);
            s_internal_funcs[name] = addr;
            count++;
        }

        // Populate internal function pointers into the unified game table
        auto* tbl_base = reinterpret_cast<char*>(&s_table);
        int struct_resolved = 0;
        for (const auto& ie : s_ifunc_entries) {
            if (!ie.name) break;  // sentinel
            auto it = s_internal_funcs.find(ie.name);
            if (it != s_internal_funcs.end()) {
                *reinterpret_cast<void**>(tbl_base + ie.offset) = it->second;
                struct_resolved++;
            }
        }

        if (count > 0)
            Logger::info("GameAPI: Resolved {} internal function(s) from RVA database",
                         count);
    } catch (const std::exception& e) {
        Logger::warn("GameAPI: Failed to parse internal_functions.json: {}", e.what());
    }
}

int GameAPI::resolved_count() { return s_resolved; }
int GameAPI::total_count()    { return TOTAL_FUNCTIONS; }
int GameAPI::internal_count() { return static_cast<int>(s_internal_funcs.size()); }

} // namespace x4n
