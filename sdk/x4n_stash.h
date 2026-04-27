// ---------------------------------------------------------------------------
// x4n_stash.h — In-Memory Key-Value Stash
// ---------------------------------------------------------------------------
// Part of the X4Native SDK. Included by x4native.h.
//
// Survives /reloadui + extension hot-reload. Lost on game exit.
// Keys are scoped to the calling extension's content.xml id by default.
//
// Usage:
//   x4n::stash::set("hp", 100);
//   int hp; x4n::stash::get("hp", &hp);
//   x4n::stash::set_string("name", "test");
//   const char* s = x4n::stash::get_string("name");
// ---------------------------------------------------------------------------
#pragma once

#include "x4n_core.h"

namespace x4n::stash {

/// Stash a raw blob under the extension's default namespace.
inline bool set(char const *key, void const *data, uint32_t size)
{
    auto       *api = detail::g_api;
    char const *ns  = api->_ext_id;
    return api->stash_set(ns, key, data, size) != 0;
}

/// Retrieve a raw blob. Returns nullptr if not found.
/// *out_size receives the byte count. Pointer valid until next set/remove.
inline void const *get(char const *key, uint32_t *out_size = nullptr)
{
    auto       *api = detail::g_api;
    char const *ns  = api->_ext_id;
    return api->stash_get(ns, key, out_size);
}

/// Remove a single key. Returns true if the key existed.
inline bool remove(char const *key)
{
    auto       *api = detail::g_api;
    char const *ns  = api->_ext_id;
    return api->stash_remove(ns, key) != 0;
}

/// Remove all keys belonging to this extension.
inline void clear()
{
    auto       *api = detail::g_api;
    char const *ns  = api->_ext_id;
    api->stash_clear(ns);
}

/// Stash a trivially-copyable value.
template <typename T>
    requires std::is_trivially_copyable_v<T>
inline bool set(char const *key, T const &val)
{
    return set(key, &val, static_cast<uint32_t>(sizeof(T)));
}

/// Retrieve a trivially-copyable value. Returns true if found and size matches.
template <typename T>
    requires std::is_trivially_copyable_v<T>
inline bool get(char const *key, T *out)
{
    static_assert(std::is_trivially_copyable_v<T>, "x4n::stash::get<T> requires a trivially-copyable type");
    uint32_t    size = 0;
    void const *p    = get(key, &size);
    if (!p || size != sizeof(T))
        return false;
    ::memcpy(out, p, sizeof(T));
    return true;
}

/// Stash a null-terminated string (includes the null terminator).
inline bool set_string(char const *key, char const *value)
{
    if (!value)
        return false;
    return set(key, value, static_cast<uint32_t>(::strlen(value) + 1));
}

/// Retrieve a stored string. Returns nullptr if not found.
inline char const *get_string(char const *key)
{
    uint32_t    size = 0;
    void const *p    = get(key, &size);
    if (!p || size == 0)
        return nullptr;
    return static_cast<char const *>(p);
}

} // namespace x4n::stash