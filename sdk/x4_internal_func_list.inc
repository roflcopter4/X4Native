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

// ComponentRegistry_Find — universal UniverseID to Object* converter.
// 1,901 callers in the binary. Returns the internal component pointer for a given ID.
// access_type: 4 = read access (most common). Returns nullptr if not found.
// g_ComponentRegistry global at 0x146C73D30 (RVA 0x06C73D30) is the first arg.
// Verified: v9.00 build 600626, RVA 0x000CE6B0
X4_FUNC(void*, ComponentRegistry_Find, (void* registry, uint64_t universe_id, int access_type))

// Component_GetCombinedSeed — returns the pre-computed combined seed (raw + session).
// This is exactly what MD's $Station.seed returns.
// 2-instruction function: mov rax,[rcx+3C0h]; retn
// Takes the component Object* (from ComponentRegistry_Find). Returns int64.
// Verified: v9.00 build 600626, RVA 0x00315260
X4_FUNC(int64_t, Component_GetCombinedSeed, (void* component))

// Controllable::CreateDynamicInterior — creates a corridor+room interior on a station.
// Called by npc_instantiation during savegame load and by MD create_dynamic_interior action.
// Params 9-14 are variadic in the original, but on x64 MSVC they occupy the same stack
// slots as fixed params, so we declare all 14 as fixed for hooking.
// See docs/rev/WALKABLE_INTERIORS.md §14 for full analysis.
// Verified: v9.00 build 600626, RVA 0x004153A0
X4_FUNC(void, CreateDynamicInterior, (void* station, void* output, void* corridor_macro, void* door_conn, void* room_macro, int roomtype, void* interior_name, void* module, int64_t reserved, int64_t seed, uint8_t persistent, uint8_t private_flag, uint8_t unknown_flag, void* display_name))
