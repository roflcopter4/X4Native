#pragma once

// ---------------------------------------------------------------------------
// Lua 5.1 / LuaJIT 2.1.0-beta3 C API — Runtime-Resolved Function Pointers
//
// X4: Foundations ships LuaJIT as lua51_64.dll in the game root directory.
// X4.exe does NOT export any Lua symbols — they all live in that DLL.
// We resolve every function via GetProcAddress at startup to avoid needing
// a Lua import library.
//
// Usage:
//   #include "lua_api.h"
//   if (!x4n::lua::resolve()) { /* fatal */ }
//   x4n::lua::pushstring(L, "hello");
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

// ---- Lua 5.1 type declarations -------------------------------------------
extern "C" {
    typedef struct lua_State lua_State;
    typedef int (*lua_CFunction)(lua_State* L);
    typedef double lua_Number;
    typedef ptrdiff_t lua_Integer;
}

// ---- Lua constants -------------------------------------------------------
#define LUA_MULTRET         (-1)
#define LUA_REGISTRYINDEX   (-10000)
#define LUA_ENVIRONINDEX    (-10001)
#define LUA_GLOBALSINDEX    (-10002)

// Pseudo-index for upvalues (Lua 5.1)
#define lua_upvalueindex(i) (LUA_GLOBALSINDEX - (i))

#define LUA_OK       0
#define LUA_YIELD    1
#define LUA_ERRRUN   2
#define LUA_ERRSYNTAX 3
#define LUA_ERRMEM   4
#define LUA_ERRERR   5

#define LUA_TNONE          (-1)
#define LUA_TNIL           0
#define LUA_TBOOLEAN       1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TNUMBER        3
#define LUA_TSTRING        4
#define LUA_TTABLE         5
#define LUA_TFUNCTION      6
#define LUA_TUSERDATA      7
#define LUA_TTHREAD        8

// ---- Resolved Lua function pointers --------------------------------------
namespace x4n { namespace lua {

/// Resolve all Lua function pointers from the host process.
/// Call once at proxy init. Returns false if resolution fails.
bool resolve();

// Stack manipulation
extern int   (*gettop)(lua_State* L);
extern void  (*settop)(lua_State* L, int idx);
extern void  (*pushvalue)(lua_State* L, int idx);
extern void  (*remove)(lua_State* L, int idx);
extern void  (*insert)(lua_State* L, int idx);

// Type checking
extern int         (*type)(lua_State* L, int idx);
extern const char* (*type_name)(lua_State* L, int tp);

// Getting values
extern lua_Number  (*tonumber)(lua_State* L, int idx);
extern lua_Integer (*tointeger)(lua_State* L, int idx);
extern int         (*toboolean)(lua_State* L, int idx);
extern const char* (*tolstring)(lua_State* L, int idx, size_t* len);
extern void*       (*touserdata)(lua_State* L, int idx);
extern size_t      (*objlen)(lua_State* L, int idx);

// Pushing values
extern void        (*pushnil)(lua_State* L);
extern void        (*pushnumber)(lua_State* L, lua_Number n);
extern void        (*pushinteger)(lua_State* L, lua_Integer n);
extern const char* (*pushlstring)(lua_State* L, const char* s, size_t len);
extern const char* (*pushstring)(lua_State* L, const char* s);
extern void        (*pushboolean)(lua_State* L, int b);
extern void        (*pushcclosure)(lua_State* L, lua_CFunction fn, int n);
extern void        (*pushlightuserdata)(lua_State* L, void* p);

// Table operations
extern void (*createtable)(lua_State* L, int narr, int nrec);
extern void (*getfield)(lua_State* L, int idx, const char* k);
extern void (*setfield)(lua_State* L, int idx, const char* k);
extern void (*rawget)(lua_State* L, int idx);
extern void (*rawset)(lua_State* L, int idx);
extern void (*rawgeti)(lua_State* L, int idx, int n);
extern void (*rawseti)(lua_State* L, int idx, int n);
extern int  (*next)(lua_State* L, int idx);

// Calling
extern int  (*pcall)(lua_State* L, int nargs, int nresults, int errfunc);

// Aux library
extern int         (*L_error)(lua_State* L, const char* fmt, ...);
extern const char* (*L_checklstring)(lua_State* L, int narg, size_t* l);
extern lua_Integer (*L_checkinteger)(lua_State* L, int narg);
extern lua_Number  (*L_checknumber)(lua_State* L, int narg);
extern int         (*L_newmetatable)(lua_State* L, const char* tname);
extern int         (*L_ref)(lua_State* L, int t);
extern void        (*L_unref)(lua_State* L, int t, int ref);

// ---- Convenience inlines ------------------------------------------------
inline void pop(lua_State* L, int n)                   { settop(L, -(n)-1); }
inline void newtable(lua_State* L)                     { createtable(L, 0, 0); }
inline void pushcfunction(lua_State* L, lua_CFunction f) { pushcclosure(L, f, 0); }
inline const char* tostring(lua_State* L, int idx)     { return tolstring(L, idx, nullptr); }
inline const char* L_checkstring(lua_State* L, int n)  { return L_checklstring(L, n, nullptr); }

}} // namespace x4n::lua
