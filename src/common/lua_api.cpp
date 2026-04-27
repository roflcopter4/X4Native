// ReSharper disable CppInconsistentNaming
#include "Common.h"
#include "lua_api.h"
#include <mutex>

namespace x4n::lua {

// ---------------------------------------------------------------------------
// Function pointer definitions (initially null)
// ---------------------------------------------------------------------------

// Stack
int  (*gettop)   (lua_State *)      = nullptr;
void (*settop)   (lua_State *, int) = nullptr;
void (*pushvalue)(lua_State *, int) = nullptr;
void (*remove)   (lua_State *, int) = nullptr;
void (*insert)   (lua_State *, int) = nullptr;

// Type checking
int         (*type)     (lua_State *, int) = nullptr;
char const *(*type_name)(lua_State *, int) = nullptr;

// Getting values
lua_Number  (*tonumber)  (lua_State *, int)           = nullptr;
lua_Integer (*tointeger) (lua_State *, int)           = nullptr;
int         (*toboolean) (lua_State *, int)           = nullptr;
char const *(*tolstring) (lua_State *, int, size_t *) = nullptr;
void *      (*touserdata)(lua_State *, int)           = nullptr;
size_t      (*objlen)    (lua_State *, int)           = nullptr;

// Pushing values
void        (*pushnil)          (lua_State *)                       = nullptr;
void        (*pushnumber)       (lua_State *, lua_Number)           = nullptr;
void        (*pushinteger)      (lua_State *, lua_Integer)          = nullptr;
char const *(*pushlstring)      (lua_State *, char const *, size_t) = nullptr;
char const *(*pushstring)       (lua_State *, char const *)         = nullptr;
void        (*pushboolean)      (lua_State *, int)                  = nullptr;
void        (*pushcclosure)     (lua_State *, lua_CFunction, int)   = nullptr;
void        (*pushlightuserdata)(lua_State *, void *)               = nullptr;

// Table operations
void (*createtable)(lua_State *, int, int)          = nullptr;
void (*getfield)   (lua_State *, int, char const *) = nullptr;
void (*setfield)   (lua_State *, int, char const *) = nullptr;
void (*rawget)     (lua_State *, int)               = nullptr;
void (*rawset)     (lua_State *, int)               = nullptr;
void (*rawgeti)    (lua_State *, int, int)          = nullptr;
void (*rawseti)    (lua_State *, int, int)          = nullptr;
int  (*next)       (lua_State *, int)               = nullptr;

// Calling
int (*pcall)(lua_State *, int, int, int) = nullptr;

// Aux library
int         (*L_error)(lua_State *, char const *, ...)    = nullptr;
char const *(*L_checklstring)(lua_State *, int, size_t *) = nullptr;
lua_Integer (*L_checkinteger)(lua_State *, int)           = nullptr;
lua_Number  (*L_checknumber)(lua_State *, int)            = nullptr;
int         (*L_newmetatable)(lua_State *, char const *)  = nullptr;
int         (*L_ref)(lua_State *, int)                    = nullptr;
void        (*L_unref)(lua_State *, int, int)             = nullptr;

// ---------------------------------------------------------------------------
// resolve() — locate lua symbols in the host process
// ---------------------------------------------------------------------------
bool resolve()
{
    static std::mutex mtx;
    static bool s_resolved = false;

    std::scoped_lock lock(mtx);
    if (s_resolved)
        return true;

    // X4 ships LuaJIT 2.1.0-beta3 as lua51_64.dll in the game root.
    // X4.exe does NOT export any Lua symbols — they're all in this DLL.
    HMODULE h = GetModuleHandleW(L"lua51_64.dll");
    if (!h)
        h = GetModuleHandleW(L"lua51.dll"); // fallback
    if (!h)
        return false;

#define RESOLVE(ptr, sym)                                                    \
    do {                                                                     \
        (ptr) = reinterpret_cast<decltype(ptr)>(::GetProcAddress(h, (sym))); \
        if (!(ptr))                                                          \
            return false;                                                    \
    } while (0)

    RESOLVE(gettop,           "lua_gettop");
    RESOLVE(settop,           "lua_settop");
    RESOLVE(pushvalue,        "lua_pushvalue");
    RESOLVE(remove,           "lua_remove");
    RESOLVE(insert,           "lua_insert");

    RESOLVE(type,             "lua_type");
    RESOLVE(type_name,        "lua_typename");

    RESOLVE(tonumber,         "lua_tonumber");
    RESOLVE(tointeger,        "lua_tointeger");
    RESOLVE(toboolean,        "lua_toboolean");
    RESOLVE(tolstring,        "lua_tolstring");
    RESOLVE(touserdata,       "lua_touserdata");
    RESOLVE(objlen,           "lua_objlen");

    RESOLVE(pushnil,          "lua_pushnil");
    RESOLVE(pushnumber,       "lua_pushnumber");
    RESOLVE(pushinteger,      "lua_pushinteger");
    RESOLVE(pushlstring,      "lua_pushlstring");
    RESOLVE(pushstring,       "lua_pushstring");
    RESOLVE(pushboolean,      "lua_pushboolean");
    RESOLVE(pushcclosure,     "lua_pushcclosure");
    RESOLVE(pushlightuserdata,"lua_pushlightuserdata");

    RESOLVE(createtable,      "lua_createtable");
    RESOLVE(getfield,         "lua_getfield");
    RESOLVE(setfield,         "lua_setfield");
    RESOLVE(rawget,           "lua_rawget");
    RESOLVE(rawset,           "lua_rawset");
    RESOLVE(rawgeti,          "lua_rawgeti");
    RESOLVE(rawseti,          "lua_rawseti");
    RESOLVE(next,             "lua_next");

    RESOLVE(pcall,            "lua_pcall");

    RESOLVE(L_error,          "luaL_error");
    RESOLVE(L_checklstring,   "luaL_checklstring");
    RESOLVE(L_checkinteger,   "luaL_checkinteger");
    RESOLVE(L_checknumber,    "luaL_checknumber");
    RESOLVE(L_newmetatable,   "luaL_newmetatable");
    RESOLVE(L_ref,            "luaL_ref");
    RESOLVE(L_unref,          "luaL_unref");

#undef RESOLVE

    s_resolved = true;
    return true;
}

} // namespace x4n::lua