// ---------------------------------------------------------------------------
// x4native_hello — Sample extension that logs lifecycle events
//
// Also exercises the new per-extension logging API:
//   x4n::log::info("text")           → hello.log  (default, auto-opened)
//   x4n::log::info("text", false)    → x4native.log (global)
//   x4n::log::info("text", "f.log")  → named file in extension folder
//   x4n::log::set_log_file("f.log")  → redirect default to a different file
// ---------------------------------------------------------------------------
#include <x4n_core.h>
#include <x4n_events.h>
#include <x4n_log.h>
#include <x4n_settings.h>

#include <string_view>

static X4GameFunctions* game = nullptr;
static int g_sub_loaded  = 0;
static int g_sub_saved   = 0;
static int g_sub_setting = 0;

static void on_hello_setting_changed(const x4n::SettingChanged& info) {
    // Ignore changes from other extensions.
    if (!info.extension_id) return;
    if (std::string_view(info.extension_id) != "x4native_hello") return;

    switch (info.type) {
        case X4N_SETTING_TOGGLE:
            x4n::log::info("hello: setting changed: %s = %s",
                           info.key, info.b ? "true" : "false");
            break;
        case X4N_SETTING_SLIDER:
            x4n::log::info("hello: setting changed: %s = %.2f",
                           info.key, info.d);
            break;
        case X4N_SETTING_DROPDOWN:
            x4n::log::info("hello: setting changed: %s = %s",
                           info.key, info.s ? info.s : "(null)");
            break;
    }
}

static void on_game_loaded() {
    // Default: goes to hello.log
    x4n::log::info("hello: game loaded!");

    if (game && game->GetPlayerID) {
        UniverseID player = game->GetPlayerID();
        x4n::log::info("hello: player entity ID retrieved");
    }

    // Named file: one-shot write to a separate file in the extension folder
    x4n::log::info("game loaded event fired", "events.log");
}

static void on_game_save() {
    x4n::log::info("hello: game saved!");
    x4n::log::info("save event fired", "events.log");
}

X4N_EXTENSION {
    game = x4n::game();

    // Default per-extension log (hello.log) — framework opened it before this runs
    x4n::log::info("hello: init called");
    x4n::log::info("hello: game version: %s", x4n::game_version());
    x4n::log::info("hello: ext path: %s", x4n::path());

    // Route one message to the shared x4native.log
    x4n::log::info("hello extension loaded", false);

    if (game)
        x4n::log::info("hello: game function table available");
    else
        x4n::log::warn("hello: game function table NOT available");

    // Named file: write a startup note to a separate log in the extension folder
    x4n::log::info("hello extension initialised", "hello_startup.log");

    // Redirect the default log to a new file for the rest of the session
    // (uncomment to test set_log_file — subsequent info/warn/error go to hello_v2.log)
    // x4n::log::set_log_file("hello_v2.log");
    // x4n::log::info("hello: this goes to hello_v2.log");

    // Read current settings (declared in x4native.json "settings" array).
    // First run seeds defaults; subsequent runs read persisted user values.
    bool        verbose  = x4n::settings::get_bool  ("verbose", false);
    double      poll_s   = x4n::settings::get_number("poll_interval_s", 5.0);
    const char* greeting = x4n::settings::get_string("greeting", "hello");
    x4n::log::info("hello: settings: verbose=%s poll_interval_s=%.1f greeting=%s",
                   verbose ? "true" : "false", poll_s, greeting);

    g_sub_loaded  = x4n::on("on_game_loaded", on_game_loaded);
    g_sub_saved   = x4n::on("on_game_save",   on_game_save);
    g_sub_setting = x4n::on_setting_changed(on_hello_setting_changed);
}

X4N_SHUTDOWN {
    x4n::log::info("hello: shutting down");
    x4n::log::info("hello extension unloaded", false);  // also note in x4native.log
    x4n::off(g_sub_loaded);
    x4n::off(g_sub_saved);
    x4n::off(g_sub_setting);
}
