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

#include "Common.h"
#include "game_api.h"
#include "logger.h"

#include <x4_game_func_table.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace x4n {

X4GameFunctions GameAPI::s_table = {};
bool GameAPI::s_initialized = false;
std::unordered_map<std::string, void *> GameAPI::s_internal_funcs;

// ---------------------------------------------------------------------------
// Resolver data — reuse the X4_FUNC list to build {name, offset} pairs
// ---------------------------------------------------------------------------

struct FuncEntry {
    char const * name;
    size_t      offset;
};

#define X4_FUNC(ret, name, params) { #name, offsetof(X4GameFunctions, name) },
static FuncEntry const s_func_entries[] = {
#include <x4_game_func_list.inc>
};
#undef X4_FUNC

static constexpr unsigned TOTAL_FUNCTIONS = static_cast<unsigned>(std::size(s_func_entries));

// Internal function resolver data — same struct, resolved from RVA database
#define X4_FUNC(ret, name, params) { #name, offsetof(X4GameFunctions, name) },
static FuncEntry const s_ifunc_entries[] = {
#include <x4_internal_func_list.inc>
    { nullptr, 0 },  // sentinel (list may be empty)
};
#undef X4_FUNC

static HMODULE s_x4_module = nullptr;
static unsigned s_resolved  = 0;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool GameAPI::init()
{
    // X4.exe is the host process — GetModuleHandle(NULL) gets its handle
    s_x4_module = GetModuleHandleW(nullptr);
    if (!s_x4_module) {
        Logger::error("GameAPI: Failed to get X4.exe module handle");
        return false;
    }

    memset(&s_table, 0, sizeof(s_table));
    s_resolved = 0;

    auto *base = reinterpret_cast<uint8_t *>(&s_table);
    for (auto const &entry : s_func_entries) {
        if (FARPROC proc = GetProcAddress(s_x4_module, entry.name)) {
            *reinterpret_cast<void **>(base + entry.offset) = reinterpret_cast<void *>(proc);
            ++s_resolved;
        }
    }

    s_initialized = true;
    Logger::info("GameAPI: Resolved {}/{} game functions", s_resolved, TOTAL_FUNCTIONS);

    if (s_resolved < TOTAL_FUNCTIONS)
        Logger::warn("GameAPI: {} functions could not be resolved", TOTAL_FUNCTIONS - s_resolved);

    return true;
}

void GameAPI::shutdown()
{
    memset(&s_table, 0, sizeof(s_table));
    s_initialized = false;
    s_resolved    = 0;
    s_x4_module   = nullptr;
    s_internal_funcs.clear();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

X4GameFunctions *GameAPI::table()
{
    return s_initialized ? &s_table : nullptr;
}

void *GameAPI::get_function(char const *name)
{
    if (!s_x4_module || !name)
        return nullptr;
    return reinterpret_cast<void *>(GetProcAddress(s_x4_module, name));
}

void *GameAPI::get_internal(char const *name)
{
    if (!name)
        return nullptr;
    auto it = s_internal_funcs.find(name);
    if (it != s_internal_funcs.end())
        return it->second;
    return nullptr;
}

uintptr_t GameAPI::exe_base()
{
    return reinterpret_cast<uintptr_t>(s_x4_module);
}

void GameAPI::load_internal_db(fs::path const &ext_root, std::string const &primary_build, std::string const &fallback_build)
{
    auto db_path = ext_root / "native" / "version_db" / "internal_functions.json";
    auto file    = std::ifstream(db_path);
    if (!file.is_open()) {
        Logger::debug("GameAPI: No internal functions database found");
        return;
    }

    try {
        auto db    = nlohmann::json::parse(file);
        auto funcs = db.find("functions");
        if (funcs == db.end() || !funcs->is_object())
            return;

        auto base  = reinterpret_cast<uint8_t *>(s_x4_module);
        int  count = 0;

        for (auto const &[name, entry] : funcs->items()) {
            std::string const *key = nullptr;
            if (entry.contains(primary_build))
                key = &primary_build;
            else if (!fallback_build.empty() && entry.contains(fallback_build))
                key = &fallback_build;
            if (!key)
                continue;
            auto &ver = entry[*key];
            if (!ver.contains("rva") || !ver["rva"].is_string())
                continue;

            auto      rva_str = ver["rva"].get<std::string>();
            uintptr_t rva     = std::stoull(rva_str, nullptr, 16);
            void     *addr    = static_cast<void *>(base + rva);

            s_internal_funcs[name] = addr;
            ++count;
        }

        // Populate internal function pointers into the unified game table
        auto *tbl_base        = reinterpret_cast<uint8_t *>(&s_table);
        int   struct_resolved = 0;
        for (auto const &ie : s_ifunc_entries) {
            if (!ie.name)
                break; // sentinel
            auto it = s_internal_funcs.find(ie.name);
            if (it != s_internal_funcs.end()) {
                *reinterpret_cast<void **>(tbl_base + ie.offset) = it->second;
                ++struct_resolved;
            }
        }

        if (count > 0)
            Logger::info("GameAPI: Resolved {} internal function(s) from RVA database", count);
    } catch (std::exception const &e) {
        Logger::warn("GameAPI: Failed to parse internal_functions.json: {}", e.what());
    }
}

unsigned GameAPI::resolved_count() { return s_resolved; }
unsigned GameAPI::total_count()    { return static_cast<unsigned>(std::size(s_func_entries)); }
unsigned GameAPI::internal_count() { return static_cast<unsigned>(s_internal_funcs.size()); }

} // namespace x4n
