// ---------------------------------------------------------------------------
// x4native_hello — Sample extension that logs lifecycle events
// ---------------------------------------------------------------------------
#include <x4native.h>

static X4GameFunctions* game = nullptr;
static int g_sub_loaded = 0;
static int g_sub_saved  = 0;

static void on_game_loaded() {
    x4n::log::info("hello: game loaded!");

    // Demo: call a game function through the cached pointer
    if (game && game->GetPlayerID) {
        UniverseID player = game->GetPlayerID();
        x4n::log::info("hello: player entity ID retrieved");
    }
}

static void on_game_save() {
    x4n::log::info("hello: game saved!");
}

X4N_EXTENSION {
    game = x4n::game();

    x4n::log::info("hello: init called");
    x4n::log::info(x4n::game_version());
    x4n::log::info(x4n::path());

    if (game)
        x4n::log::info("hello: game function table available");
    else
        x4n::log::warn("hello: game function table NOT available");

    g_sub_loaded = x4n::on("on_game_loaded", on_game_loaded);
    g_sub_saved  = x4n::on("on_game_save",   on_game_save);
}

X4N_SHUTDOWN {
    x4n::log::info("hello: shutting down");
    x4n::off(g_sub_loaded);
    x4n::off(g_sub_saved);
}
