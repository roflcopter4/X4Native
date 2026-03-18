// ---------------------------------------------------------------------------
// X4 Internal (Non-Exported) Function List — X-Macro
//
// Same pattern as x4_game_func_list.inc but for reverse-engineered functions
// that aren't in X4.exe's export table. Addresses are resolved at runtime
// from native/version_db/internal_functions.json (RVA per game build).
//
// Format:  X4_FUNC(return_type, function_name, (param_list))
//
// Example:
//   X4_FUNC(void, PhysicsUpdate, (float deltaTime))
//   X4_FUNC(bool, IsEntityVisible, (UniverseID entity, UniverseID observer))
//
// The JSON database must have a matching entry:
//   "PhysicsUpdate": { "900": { "rva": "0x00A1B2C3" } }
// ---------------------------------------------------------------------------

// -- Add reverse-engineered function declarations below this line --

// Game loop frame tick — called once per frame from the message pump.
// a1 = engine context (opaque, 736-byte struct), a2 = true when suspended/minimized.
// Verified: v9.00 build 900, RVA 0x00F4A2A0
X4_FUNC(void, X4_FrameTick, (void* engineCtx, bool isSuspended))
