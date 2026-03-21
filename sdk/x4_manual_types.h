// ==========================================================================
// x4_manual_types.h - Hand-Authored Types (RE / Community)
// ==========================================================================
// Types not present in the game's FFI data surface. These come from reverse
// engineering, community contributions, or other sources.
//
// This file is NEVER overwritten by the generation pipeline.
//
// Guidelines:
//   - Prefix names with X4 to avoid collision with game-native types
//   - Note the game version each type was verified against
//   - Can depend on generated types (include x4_game_types.h first)
// ==========================================================================
#pragma once

#include "x4_game_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Add manually-authored types below.

// ---- Native frame update event payload (on_native_frame_update) ----
// Passed as the void* data arg to event callbacks.
// Verified: v9.00 build 900 (see docs/rev/GAME_LOOP.md)
typedef struct X4NativeFrameUpdate {
    double  delta;          // Frame delta in seconds (capped at 1.0)
    double  game_time;      // Accumulated game time (with speed multiplier)
    double  real_time;      // Accumulated real time (paused = no accumulation)
    float   fps;            // Current FPS (from engine context + 600)
    float   speed_multiplier; // Game speed (1x, 2x, 5x, 10x)
    bool    game_paused;    // True if game is paused
    bool    is_suspended;   // True if window minimized / lost focus
    int     frame_counter;  // Frame counter since last FPS sample
} X4NativeFrameUpdate;

// ---- RoomType enum (see docs/rev/WALKABLE_INTERIORS.md §17) ----
// Enum init at 0x1407521A0, data table at 0x1424794A0, 22 entries.
// Verified: v9.00 build 600626
typedef enum X4RoomType {
    X4_ROOMTYPE_BAR               = 0,
    X4_ROOMTYPE_CASINO            = 1,
    X4_ROOMTYPE_CORRIDOR          = 2,
    X4_ROOMTYPE_CREWQUARTERS      = 3,
    X4_ROOMTYPE_EMBASSY           = 4,
    X4_ROOMTYPE_FACTIONREP        = 5,
    X4_ROOMTYPE_GENERATORROOM     = 6,
    X4_ROOMTYPE_INFRASTRUCTURE    = 7,
    X4_ROOMTYPE_INTELLIGENCEOFFICE = 8,
    X4_ROOMTYPE_LIVINGROOM        = 9,
    X4_ROOMTYPE_MANAGER           = 10,
    X4_ROOMTYPE_OFFICE            = 11,
    X4_ROOMTYPE_PLAYEROFFICE      = 12,
    X4_ROOMTYPE_PRISON            = 13,
    X4_ROOMTYPE_SECURITY          = 14,
    X4_ROOMTYPE_SERVERROOM        = 15,
    X4_ROOMTYPE_SERVICEROOM       = 16,
    X4_ROOMTYPE_SHIPTRADERCORNER  = 17,
    X4_ROOMTYPE_TRADERCORNER      = 18,
    X4_ROOMTYPE_TRAFFICCONTROL    = 19,
    X4_ROOMTYPE_WARROOM           = 20,
    X4_ROOMTYPE_NONE              = 21,
} X4RoomType;

// ---- Room property offsets within Room entity (class 82) ----
// WARNING: These are raw struct offsets — fragile across builds. Any field
// added/removed/reordered in the Room class will shift these. Re-verify on
// every game update. Currently only used for documentation; the hook approach
// reads these via the game's own code paths, not direct memory access.
// Verified: v9.00 build 600626 (see docs/rev/WALKABLE_INTERIORS.md §22)
#define X4_ROOM_OFFSET_ROOMTYPE    0x2C0  /* int32  — X4RoomType enum */
#define X4_ROOM_OFFSET_UNK_3A0     0x3A0  /* uint8  — purpose unknown, set by CreateDynamicInterior */
#define X4_ROOM_OFFSET_NAME        0x3A8  /* string — room name (std::string) */
#define X4_ROOM_OFFSET_PRIVATE     0x408  /* uint8  — private flag */
#define X4_ROOM_OFFSET_PERSISTENT  0x409  /* uint8  — persistent flag */

// ---- Component data offsets ----
// Raw generation seed is at Object+0x08 (uint64). This is the component's OWN seed,
// before combination with the session seed. Confirmed by 4 independent functions.
// WARNING: struct offset — fragile across builds. Re-verify on game updates.
// Verified: v9.00 build 600626
#define X4_COMPONENT_OFFSET_RAW_SEED       0x08   /* uint64 — raw generation seed */
#define X4_COMPONENT_OFFSET_COMBINED_SEED  0x3C0  /* int64  — raw_seed + session_seed (= MD $Station.seed) */

// ---- Global data RVAs (add to imagebase to get absolute address) ----
// These are data pointers, not functions. Dereference to get the actual value.
// WARNING: data addresses change between builds. Re-verify on game updates.
// Verified: v9.00 build 600626
#define X4_RVA_COMPONENT_REGISTRY  0x06C73D30  /* void** — g_ComponentRegistry */
#define X4_RVA_SESSION_SEED        0x03C9F9C0  /* uint64* — g_SessionSeed */

// ---- Seed system constants (see docs/rev/WALKABLE_INTERIORS.md §16) ----
// LCG formula: next = ROR64(seed * multiplier + addend, 30)
// These are algorithm constants embedded in code, not data references.
// Likely stable across builds (PRNG design, not tunable), but verify on major engine changes.
// Found inside MD_EvalSeed_AutoAdvance (0x140C10740 in build 600626).
// Verified: v9.00 build 600626
#define X4_SEED_LCG_MULTIPLIER  0x5851F42D4C957F2DULL
#define X4_SEED_LCG_ADDEND     0x14057B7EF767814FULL
#define X4_SEED_LCG_ROTATE     30

#ifdef __cplusplus
}
#endif
