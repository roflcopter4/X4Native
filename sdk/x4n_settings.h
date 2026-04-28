// ---------------------------------------------------------------------------
// x4n_settings.h — Per-Extension User Settings
// ---------------------------------------------------------------------------
// Part of the X4Native SDK. Included by x4native.h.
//
// Extensions declare their configurable settings in x4native.json under a
// "settings" array. Current values persist in
//   <profile>\x4native\<extension_id>\settings.user.json
// and are editable by the player via the vanilla
// Settings → Extensions → "..." → submenu.
//
// Usage:
//
//   // x4native.json:
//   // { ...,
//   //   "settings": [
//   //     { "id": "verbose",  "name": "Verbose logging",
//   //       "type": "toggle", "default": false },
//   //     { "id": "poll_s",   "name": "Poll interval (s)",
//   //       "type": "slider", "min": 1, "max": 30, "step": 1, "default": 5 },
//   //     { "id": "provider", "name": "LLM Provider",
//   //       "type": "dropdown", "default": "anthropic",
//   //       "options": [
//   //         { "id": "anthropic", "text": "Anthropic" },
//   //         { "id": "openai",    "text": "OpenAI"    }
//   //       ]
//   //     }
//   //   ]
//   // }
//
//   bool  verbose  = x4n::settings::get_bool  ("verbose", false);
//   double poll_s  = x4n::settings::get_number("poll_s",  5.0);
//   const char* p  = x4n::settings::get_string("provider", "anthropic");
//
//   x4n::settings::set_bool("verbose", true);
//
//   x4n::on_setting_changed([](const char* key, const auto& /*c*/) {
//       if (std::string_view(key) == "provider") /* re-init LLM */;
//   });
// ---------------------------------------------------------------------------
#pragma once

#include "x4n_core.h"
#include "x4n_events.h"

namespace x4n {
namespace settings {

/// Read a boolean setting. Returns `fallback` if the key is missing or
/// declared with a different type.
inline bool get_bool(char const *key, bool fallback = false)
{
    auto *api = detail::g_api;
    return api->get_setting_bool(key, fallback ? 1 : 0, api) != 0;
}

/// Read a numeric setting (slider).
inline double get_number(char const *key, double fallback = 0.0)
{
    auto *api = detail::g_api;
    return api->get_setting_number(key, fallback, api);
}

/// Read a string setting (dropdown — returns the selected option id).
inline char const *get_string(char const *key, char const *fallback = "")
{
    auto *api = detail::g_api;
    return api->get_setting_string(key, fallback, api);
}

/// Write a boolean setting. Persists to disk and fires `on_setting_changed`.
inline void set_bool(char const *key, bool value)
{
    auto *api = detail::g_api;
    api->set_setting_bool(key, value ? 1 : 0, api);
}

/// Write a numeric setting. Value is clamped to the declared [min, max] range.
inline void set_number(char const *key, double value)
{
    auto *api = detail::g_api;
    api->set_setting_number(key, value, api);
}

/// Write a string (dropdown) setting. Value must match one of the declared
/// option ids; other values are rejected with a warning.
inline void set_string(char const *key, char const *value)
{
    auto *api = detail::g_api;
    api->set_setting_string(key, value, api);
}

} // namespace settings

// ---------------------------------------------------------------------------
// on_setting_changed helper
// ---------------------------------------------------------------------------
//
// Payload type is X4NativeSettingChanged (declared in <x4native_extension.h>).
// Only the field matching `type` is meaningful:
//   X4N_SETTING_TOGGLE   → b
//   X4N_SETTING_SLIDER   → d
//   X4N_SETTING_DROPDOWN → s
//
// `extension_id` is included so handlers can ignore events from other
// extensions; events fire globally and extensions care only about their
// own keys.
using SettingChanged = X4NativeSettingChanged;

namespace detail {
// Trampoline: adapts void(const X4NativeSettingChanged&) to the raw
// X4NativeEventCallback signature.
inline void trampoline_setting_changed(char const *, void *data, void *ud)
{
    auto fn = reinterpret_cast<void (*)(X4NativeSettingChanged const &)>(ud);
    fn(*static_cast<X4NativeSettingChanged const *>(data));
}
} // namespace detail

/// Subscribe to setting changes **for this extension only**. The callback
/// fires whenever one of your own keys is written (via UI or code). Returns
/// the subscription id; pass to x4n::off() to unsubscribe.
///
/// Under the hood, subscribes to "on_setting_changed:<your extension_id>".
inline int on_setting_changed(void (*cb)(X4NativeSettingChanged const &info))
{
    auto *api = ::x4n::detail::g_api;
    char  name[256];
    (void)snprintf(name, std::size(name), "on_setting_changed:%s", api->_ext_id ? api->_ext_id : "");
    return api->subscribe(name, detail::trampoline_setting_changed, reinterpret_cast<void *>(cb), api);
}

// The core also fires the unscoped "on_setting_changed" for observers that
// want to see every setting change regardless of extension. If you ever need
// that, subscribe directly:
//   api->subscribe("on_setting_changed", trampoline_setting_changed, ...);
// It's not wrapped here to keep the happy-path API minimal.

} // namespace x4n
