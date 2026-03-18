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

#ifdef __cplusplus
}
#endif
