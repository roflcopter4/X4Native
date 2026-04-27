// ==========================================================================
// x4_game_class_ids.inc - X4 Entity Class IDs
// ==========================================================================
// Auto-generated from class_ids.csv (game version: 900-606138).
// 120 classes, sentinel = 120.
//
// Usage:
//   x4n::GameClass cls = x4n::entity::get_class_id(comp);
//   if (cls == x4n::GameClass::Station) { ... }
//   if (x4n::entity::is_derived_from(comp, x4n::GameClass::Container)) { ... }
//
// WARNING: Class IDs shift between game builds when new classes are inserted.
// DO NOT EDIT - regenerate with: .\scripts\generate_class_ids.ps1
// ==========================================================================

#pragma once

#ifdef __cplusplus
# include <stdint.h>
# include <string_view>

namespace x4n {

/// Entity class ID enum. Values are auto-generated per game build.
/// Use with x4n::entity::get_class_id() and x4n::entity::is_derived_from().
enum class GameClass : uint32_t {
    Accessory                 = 0,
    Adsign                    = 1,
    Adsignobject              = 2,
    Anomaly                   = 3,
    Asteroid                  = 4,
    Attachment                = 5,
    Bomb                      = 6,
    Bomblauncher              = 7,
    Buildstorage              = 8,
    Buildmodule               = 9,
    Buildprocessor            = 10,
    Bullet                    = 11,
    Cargobay                  = 12,
    Celestialbody             = 13,
    Checkpoint                = 14,
    Cluster                   = 15,
    Cockpit                   = 16,
    Collectableshieldrestore  = 17,
    Collectableammo           = 18,
    Collectableblueprints     = 19,
    Collectablewares          = 20,
    Component                 = 21,
    Computer                  = 22,
    Connectionmodule          = 23,
    Controlroom               = 24,
    Countermeasure            = 25,
    Crate                     = 26,
    CrateL                    = 27,
    CrateM                    = 28,
    CrateS                    = 29,
    Crystal                   = 30,
    Cutsceneanchor            = 31,
    Datavault                 = 32,
    Defencemodule             = 33,
    Defensible                = 34,
    Destructible              = 35,
    Detector                  = 36,
    Dismantleprocessor        = 37,
    Dockarea                  = 38,
    Dockingbay                = 39,
    Drop                      = 40,
    Effectobject              = 41,
    Engine                    = 42,
    Entity                    = 43,
    Forceemitter              = 44,
    Fogvolume                 = 45,
    Galaxy                    = 46,
    Gate                      = 47,
    Habitation                = 48,
    Hackerprobe               = 49,
    Highway                   = 50,
    Highwayblocker            = 51,
    Highwaybooster            = 52,
    Highwayentrygate          = 53,
    Highwayexitgate           = 54,
    Highwayscene              = 55,
    Highwaytrigger            = 56,
    Holomap                   = 57,
    Influenceobject           = 58,
    Landmarkobject            = 59,
    Lensflare                 = 60,
    Lock                      = 61,
    Lockbox                   = 62,
    Mine                      = 63,
    Miningnode                = 64,
    Missile                   = 65,
    Missilelauncher           = 66,
    Missileturret             = 67,
    Moon                      = 68,
    Navbeacon                 = 69,
    Navcontext                = 70,
    Npc                       = 71,
    Object                    = 72,
    Pier                      = 73,
    Planet                    = 74,
    Player                    = 75,
    Positional                = 76,
    Processingmodule          = 77,
    Production                = 78,
    Radar                     = 79,
    Recyclable                = 80,
    Region                    = 81,
    Resourceprobe             = 82,
    Room                      = 83,
    Satellite                 = 84,
    Scanner                   = 85,
    Scene                     = 86,
    Sector                    = 87,
    Shieldgenerator           = 88,
    ShipXs                    = 89,
    ShipS                     = 90,
    ShipM                     = 91,
    ShipL                     = 92,
    ShipXl                    = 93,
    Signalleak                = 94,
    Spacesuit                 = 95,
    Stardust                  = 96,
    Station                   = 97,
    Storage                   = 98,
    Sun                       = 99,
    Switchable                = 100,
    Targetpoint               = 101,
    Textdisplay               = 102,
    Turret                    = 103,
    Uielement                 = 104,
    Ventureplatform           = 105,
    Weapon                    = 106,
    Welfaremodule             = 107,
    Zone                      = 108,
    Collectable               = 109,
    Container                 = 110,
    Controllable              = 111,
    Explosive                 = 112,
    Launcher                  = 113,
    Module                    = 114,
    Nonplayer                 = 115,
    Ship                      = 116,
    Space                     = 117,
    Triggerobject             = 118,
    Walkablemodule            = 119
};

/// Internal sentinel value -- not a real class. Returned by get_class() on failure.
/// Extension code should check for nullptr/null component instead of comparing to this.
inline constexpr uint32_t GAMECLASS_SENTINEL = 120;

/// Engine-facing class name (raw form from class_ids.csv).
/// Used by x4n::entity::find_ancestor() to pass GameClass into GetContextByClass,
/// and by typed wrappers around IsComponentClass / IsRealComponentClass.
/// Returns nullptr for out-of-range values (graceful fallback: caller gets null result).
constexpr char const *class_name(GameClass c)
{
    static constexpr char const *NAMES[] = {
        "accessory"                 , // 0 Accessory
        "adsign"                    , // 1 Adsign
        "adsignobject"              , // 2 Adsignobject
        "anomaly"                   , // 3 Anomaly
        "asteroid"                  , // 4 Asteroid
        "attachment"                , // 5 Attachment
        "bomb"                      , // 6 Bomb
        "bomblauncher"              , // 7 Bomblauncher
        "buildstorage"              , // 8 Buildstorage
        "buildmodule"               , // 9 Buildmodule
        "buildprocessor"            , // 10 Buildprocessor
        "bullet"                    , // 11 Bullet
        "cargobay"                  , // 12 Cargobay
        "celestialbody"             , // 13 Celestialbody
        "checkpoint"                , // 14 Checkpoint
        "cluster"                   , // 15 Cluster
        "cockpit"                   , // 16 Cockpit
        "collectableshieldrestore"  , // 17 Collectableshieldrestore
        "collectableammo"           , // 18 Collectableammo
        "collectableblueprints"     , // 19 Collectableblueprints
        "collectablewares"          , // 20 Collectablewares
        "component"                 , // 21 Component
        "computer"                  , // 22 Computer
        "connectionmodule"          , // 23 Connectionmodule
        "controlroom"               , // 24 Controlroom
        "countermeasure"            , // 25 Countermeasure
        "crate"                     , // 26 Crate
        "crate_l"                   , // 27 CrateL
        "crate_m"                   , // 28 CrateM
        "crate_s"                   , // 29 CrateS
        "crystal"                   , // 30 Crystal
        "cutsceneanchor"            , // 31 Cutsceneanchor
        "datavault"                 , // 32 Datavault
        "defencemodule"             , // 33 Defencemodule
        "defensible"                , // 34 Defensible
        "destructible"              , // 35 Destructible
        "detector"                  , // 36 Detector
        "dismantleprocessor"        , // 37 Dismantleprocessor
        "dockarea"                  , // 38 Dockarea
        "dockingbay"                , // 39 Dockingbay
        "drop"                      , // 40 Drop
        "effectobject"              , // 41 Effectobject
        "engine"                    , // 42 Engine
        "entity"                    , // 43 Entity
        "forceemitter"              , // 44 Forceemitter
        "fogvolume"                 , // 45 Fogvolume
        "galaxy"                    , // 46 Galaxy
        "gate"                      , // 47 Gate
        "habitation"                , // 48 Habitation
        "hackerprobe"               , // 49 Hackerprobe
        "highway"                   , // 50 Highway
        "highwayblocker"            , // 51 Highwayblocker
        "highwaybooster"            , // 52 Highwaybooster
        "highwayentrygate"          , // 53 Highwayentrygate
        "highwayexitgate"           , // 54 Highwayexitgate
        "highwayscene"              , // 55 Highwayscene
        "highwaytrigger"            , // 56 Highwaytrigger
        "holomap"                   , // 57 Holomap
        "influenceobject"           , // 58 Influenceobject
        "landmarkobject"            , // 59 Landmarkobject
        "lensflare"                 , // 60 Lensflare
        "lock"                      , // 61 Lock
        "lockbox"                   , // 62 Lockbox
        "mine"                      , // 63 Mine
        "miningnode"                , // 64 Miningnode
        "missile"                   , // 65 Missile
        "missilelauncher"           , // 66 Missilelauncher
        "missileturret"             , // 67 Missileturret
        "moon"                      , // 68 Moon
        "navbeacon"                 , // 69 Navbeacon
        "navcontext"                , // 70 Navcontext
        "npc"                       , // 71 Npc
        "object"                    , // 72 Object
        "pier"                      , // 73 Pier
        "planet"                    , // 74 Planet
        "player"                    , // 75 Player
        "positional"                , // 76 Positional
        "processingmodule"          , // 77 Processingmodule
        "production"                , // 78 Production
        "radar"                     , // 79 Radar
        "recyclable"                , // 80 Recyclable
        "region"                    , // 81 Region
        "resourceprobe"             , // 82 Resourceprobe
        "room"                      , // 83 Room
        "satellite"                 , // 84 Satellite
        "scanner"                   , // 85 Scanner
        "scene"                     , // 86 Scene
        "sector"                    , // 87 Sector
        "shieldgenerator"           , // 88 Shieldgenerator
        "ship_xs"                   , // 89 ShipXs
        "ship_s"                    , // 90 ShipS
        "ship_m"                    , // 91 ShipM
        "ship_l"                    , // 92 ShipL
        "ship_xl"                   , // 93 ShipXl
        "signalleak"                , // 94 Signalleak
        "spacesuit"                 , // 95 Spacesuit
        "stardust"                  , // 96 Stardust
        "station"                   , // 97 Station
        "storage"                   , // 98 Storage
        "sun"                       , // 99 Sun
        "switchable"                , // 100 Switchable
        "targetpoint"               , // 101 Targetpoint
        "textdisplay"               , // 102 Textdisplay
        "turret"                    , // 103 Turret
        "uielement"                 , // 104 Uielement
        "ventureplatform"           , // 105 Ventureplatform
        "weapon"                    , // 106 Weapon
        "welfaremodule"             , // 107 Welfaremodule
        "zone"                      , // 108 Zone
        "collectable"               , // 109 Collectable
        "container"                 , // 110 Container
        "controllable"              , // 111 Controllable
        "explosive"                 , // 112 Explosive
        "launcher"                  , // 113 Launcher
        "module"                    , // 114 Module
        "nonplayer"                 , // 115 Nonplayer
        "ship"                      , // 116 Ship
        "space"                     , // 117 Space
        "triggerobject"             , // 118 Triggerobject
        "walkablemodule"             // 119 Walkablemodule
    };

    auto idx = static_cast<size_t>(c);
    return idx < std::size(NAMES) ? NAMES[idx] : nullptr;
}

} // namespace x4n
#endif
