# X4 State Mutation Safety — Reverse Engineering Notes

> **Binary:** X4.exe v9.00 (build 900) · **Date:** 2026-03
>
> All addresses are absolute (imagebase `0x140000000`). Subtract imagebase to get RVA.

---

## 1. Summary

Exported game API functions are **safe to call from our frame tick hook**. They use no locking, assume main-thread context (which our hook provides), and either modify state directly or post events to the engine's event bus for next-frame processing.

---

## 2. Calling Context

Our `frame_tick_detour` wraps the original `FrameTick` call. After the original returns:

- All subsystems have completed their frame update
- All events for this frame have been dispatched
- Rendering is complete
- Game state is fully consistent and quiescent

```mermaid
graph TD
    subgraph "Frame N — Original FrameTick()"
        T["Time accumulation"] --> SU["UpdateSubsystems()\n<i>all game logic</i>"]
        SU --> VR["VulkanRenderPrep()"]
        VR --> RF["RenderFrame()"]
        RF --> FL["FrameLimiter()"]
    end

    FL --> X4N

    subgraph "Post-Frame — x4native"
        X4N["on_native_frame_update"] --> EXT["Extension callbacks"]
        EXT --> RD["Read game state ✅"]
        EXT --> MUT["Call mutations ✅"]
        EXT --> EVP["Post events\n→ processed Frame N+1"]
    end

    style SU fill:#6b3a8a,color:#fff
    style X4N fill:#2d5a27,color:#fff,stroke:#4CAF50,stroke-width:3px
    style EXT fill:#8b6914,color:#fff
    style RD fill:#2d5a27,color:#fff
    style MUT fill:#2d5a27,color:#fff
    style EVP fill:#1a4a6e,color:#fff
```

---

## 3. Decompiled Function Analysis

### AddPlayerMoney — Direct State Mutation

```mermaid
sequenceDiagram
    participant EXT as Extension Code
    participant API as AddPlayerMoney
    participant MEM as Player Data
    participant ALLOC as Event Allocator
    participant BUS as Event Bus
    participant SU as UpdateSubsystems (N+1)

    EXT->>API: AddPlayerMoney(1000)
    API->>MEM: money += 1000
    Note over MEM: Direct write — NO LOCK
    API->>ALLOC: allocate 48 bytes
    ALLOC-->>API: U::MoneyUpdatedEvent
    API->>BUS: post_event(event)
    Note over BUS: Queued — NO LOCK
    BUS-->>EXT: returns

    Note over SU: Next frame...
    SU->>BUS: drain event queue
    Note over SU: UI updates balance<br/>MD cues react
```

**Exported wrapper:** `AddPlayerMoney` at `0x14013D5E0` (RVA `0x13D5E0`)

```c
// Thin wrapper — validates amount, delegates to inner function
void __fastcall AddPlayerMoney(__int64 amount) {
    if (amount)
        sub_140994350(qword_146C6B940, amount);  // inner implementation
}
```

**Inner implementation:** `sub_140994350` (RVA `0x994350`)

```c
void __fastcall AddPlayerMoney_Inner(ComponentSystem* sys, __int64 amount) {
    // 1. Direct state modification — NO LOCKING
    *(int64*)(player_data + offset) += amount;

    // 2. Allocate event (48 bytes, stack-like allocator)
    void* event = allocate_event(48);

    // 3. Set vtable → U::MoneyUpdatedEvent
    *(void**)event = &U_MoneyUpdatedEvent_vtable;

    // 4. Fill event payload
    event->player_id = get_player_id();
    event->old_amount = old;
    event->new_amount = old + amount;

    // 5. Post to event bus (sub_140953650)
    post_event(event_bus, event);
}
```

**Key observations:**
- **No CriticalSection, no mutex, no atomic operations** — pure sequential code
- Modifies player money directly in memory
- Creates a `U::MoneyUpdatedEvent` and posts it to the event bus
- The event will be processed in the next frame's `UpdateSubsystems` pass
- **Safe from our hook:** Yes — main thread, post-frame timing

### CreateOrder3 — Entity + Order Creation

**Exported wrapper:** `CreateOrder3` at `0x1401B9060` (RVA `0x1B9060`)

```c
// Creates an order for a controllable entity
int __fastcall CreateOrder3(uint64_t controllable, const char* orderid, bool instant) {
    // 1. Look up entity via component system (qword_146C6B940)
    void* entity = lookup_component(controllable);
    if (!entity) return 0;

    // 2. Validate player ownership
    if (!is_player_owned(entity)) return 0;

    // 3. Create order object — NO LOCKING
    void* order = sub_140423EA0(entity, orderid, instant);

    // 4. Returns 1-based order index
    return get_order_index(order);
}
```

**Key observations:**
- Component lookup via global `qword_146C6B940` — no synchronization
- Direct entity manipulation
- Order created inline, no deferred processing
- **Safe from our hook:** Yes — but the order won't be executed until next frame's AI update

---

## 4. Event Bus Architecture

### Event Posting (sub\_140953650)

All state mutations that need to notify other systems use the event bus:

```c
void post_event(EventBus* bus, Event* event) {
    // Append to event queue — NO LOCKING
    bus->queue[bus->count++] = event;
}
```

Events are typed C++ objects with vtables. Known event types from RTTI:

| Event Class | Namespace | Trigger |
|-------------|-----------|---------|
| `MoneyUpdatedEvent` | `U::` | `AddPlayerMoney` |
| `UpdateTradeOffersEvent` | `U::` | Trade system changes |
| `UpdateBuildEvent` | `U::` | Construction updates |
| `UpdateZoneEvent` | `U::` | Zone state changes |
| `UnitDestroyedEvent` | `U::` | Entity destruction |
| `UniverseGeneratedEvent` | `U::` | Universe creation |

### Event Timing

Events posted from our hook follow this lifecycle:

```mermaid
sequenceDiagram
    participant EXT as Extension (post-frame)
    participant MEM as Game Memory
    participant BUS as Event Bus
    participant SU as UpdateSubsystems
    participant UI as UI / MD / AI

    rect rgb(45, 90, 39)
    Note over EXT,BUS: Frame N (post-frame)
    EXT->>MEM: AddPlayerMoney(1000)
    Note over MEM: money modified immediately
    EXT->>BUS: U::MoneyUpdatedEvent queued
    end

    rect rgb(30, 74, 110)
    Note over SU,UI: Frame N+1
    SU->>BUS: drain event queue
    BUS->>UI: MoneyUpdatedEvent dispatched
    Note over UI: UI shows new balance<br/>MD cues react<br/>Other subsystems notified
    end
```

The 1-frame delay for event processing is invisible to the player and matches the engine's own event timing model.

---

## 5. Safety Classification

```mermaid
graph TD
    subgraph "Tier 1 — Safe: Property Mutations"
        T1A["AddPlayerMoney"] 
        T1B["SetPlayerName"]
        T1C["Property setters"]
    end

    subgraph "Tier 2 — Safe: Entity Operations"
        T2A["CreateOrder3"]
        T2B["Order manipulation"]
    end

    subgraph "Tier 3 — Safe: Read-Only Queries"
        T3A["GetPlayerName"]
        T3B["GetComponentName"]
        T3C["IsGamePaused"]
    end

    subgraph "Tier 4 — Caution: Complex Changes"
        T4A["Entity spawning"]
        T4B["Trade execution"]
        T4C["Sector/zone changes"]
    end

    T1A --> |"direct write\n+ event"| SAFE["✅ Safe from hook"]
    T2A --> |"inline creation\nexecutes next frame"| SAFE
    T3A --> |"zero side effects"| SAFE
    T4A --> |"may need subsystem\ncontext"| CAUTION["⚠️ Test case by case"]

    style SAFE fill:#2d5a27,color:#fff
    style CAUTION fill:#8b6914,color:#fff
    style T1A fill:#2d5a27,color:#fff
    style T2A fill:#1a4a6e,color:#fff
    style T3A fill:#4a4a4a,color:#fff
    style T4A fill:#8b1a1a,color:#fff
```

### Tier 1 — Safe: Simple Property Mutations

Functions that directly modify a value and optionally post an event. No ordering dependencies.

| Function | What It Does | Post-Frame Safe |
|----------|-------------|-----------------|
| `AddPlayerMoney(amount)` | Modifies money, posts MoneyUpdatedEvent | **Yes** |
| `SetPlayerName(name)` | Sets player name string | **Yes** |
| Property setters (general) | Write to entity fields | **Yes** |

### Tier 2 — Safe: Entity Operations

Functions that create/modify game objects. Results take effect next frame.

| Function | What It Does | Post-Frame Safe |
|----------|-------------|-----------------|
| `CreateOrder3(entity, order, flags)` | Creates AI order for entity | **Yes** (executes next frame) |
| Order manipulation functions | Modify order queue | **Yes** |

### Tier 3 — Safe With Care: Read-Only Queries

Functions that read game state. Always safe, no side effects.

| Function | What It Does | Notes |
|----------|-------------|-------|
| `GetPlayerName()` | Returns player name | Zero side effects |
| `GetPlayerFactionName(flag)` | Returns faction name | Zero side effects |
| `GetPlayerControlledShipID()` | Returns ship component ID | Zero side effects |
| `GetComponentName(id)` | Returns entity name | Zero side effects |
| `IsGamePaused()` | Returns pause state | Zero side effects |

### Tier 4 — Caution: Complex State Changes

Functions that trigger cascading updates or depend on specific subsystem state. Should work from post-frame but may need testing.

| Category | Concern |
|----------|---------|
| Entity spawning | May assume subsystem context for initialization |
| Trade execution | Complex multi-entity state changes |
| Sector/zone changes | May trigger subsystem-level recalculation |
| Save/load triggers | Assumes specific lifecycle state |

### Entity Spawning — Zone Auto-Creation

All entity spawn functions that accept a `sectorid` parameter auto-create a **tempzone** at the target position if no zone exists there. This is confirmed by the `common.xsd` MD action schema documentation:

> `create_station` sector attribute: "Creates a tempzone if a zone does not exist at the coordinates"

This applies to all entity creation paths:
- C++: `SpawnStationAtPos(macro, sectorid, offset, planid, owner)` → auto-creates tempzone
- C++: `SpawnObjectAtPos2(macro, sectorid, offset, owner)` → auto-creates tempzone
- MD: `create_station sector=...`, `create_ship sector=...`, `create_object sector=...` → auto-creates tempzone

When using the `zone=` parameter instead, the entity is placed in the specified existing zone (takes precedence over sector).

`GetZoneAt(sectorid, &offset)` can be used to check whether a zone exists at a given position before spawning.

---

## 6. Why No Locking?

The absence of synchronization in exported functions is **by design**, not an oversight:

1. **Single-threaded game logic** — all simulation runs on the main thread (see [THREADING.md](THREADING.md))
2. **Lua calling context** — these functions are called from Lua scripts, which run within the subsystem update on the main thread
3. **No concurrent access** — physics (Jolt) and rendering (Vulkan) are isolated; they never call game API functions
4. **Event bus is single-producer** — only main-thread code posts events

Our hook maintains this invariant: we run on the main thread, between frames, with no concurrent game code executing.

---

## 7. Future Work: In-Simulation Timing

For extensions that need to run **within** the simulation update (same timing as MD cues), a future hook on `UpdateSubsystems` (RVA `0xE999D0`) could provide pre/post callbacks:

```mermaid
graph TD
    FT["FrameTick"] --> PRE["pre_update callback\n<i>x4native</i>"]
    PRE --> BST["Iterate BST\nvtable[1] per node\n<i>normal game logic</i>"]
    BST --> POST["post_update callback\n<i>x4native</i>"]
    POST --> VR["VulkanRenderPrep"]
    VR --> RF["RenderFrame"]
    RF --> FL["FrameLimiter"]
    FL --> HOOK["Current hook point\n<i>frame_tick_detour</i>"]

    style PRE fill:#8b6914,color:#fff,stroke:#FFD700,stroke-width:2px
    style POST fill:#8b6914,color:#fff,stroke:#FFD700,stroke-width:2px
    style BST fill:#6b3a8a,color:#fff
    style HOOK fill:#2d5a27,color:#fff,stroke:#4CAF50,stroke-width:3px
```

This would give code execution inside the frame's logical phase rather than post-frame. Most use cases don't need this — post-frame is sufficient and safer.

---

## 8. Position & Rotation API — Units and Internals

### Critical: Angle Unit Mismatch

`GetObjectPositionInSector` and `SetObjectSectorPos` use **different angle units**.

| Function | Angle Unit | Evidence |
|----------|-----------|---------|
| `GetObjectPositionInSector` | **Radians** | Uses `atan2f` / `asinf` internally to extract Euler angles from rotation matrix |
| `SetObjectSectorPos` | **Degrees** | Multiplies input angles by `dword_142C40EE4` = `0x3c8efa35` = **π/180** before matrix construction |

**Consequence:** Passing radians from `GetObjectPositionInSector` directly into `SetObjectSectorPos` causes rotation ≈ 57× too small — effectively invisible (a ship rotated "1 degree" when we intended "57 degrees").

**Fix:**
```cpp
// Read position (returns radians for yaw/pitch/roll)
UIPosRot pos = game->GetObjectPositionInSector(entity_id);

// Convert angles to degrees before sending or applying
constexpr float RAD_TO_DEG = 180.0f / 3.14159265f;
pos.yaw   *= RAD_TO_DEG;
pos.pitch *= RAD_TO_DEG;
pos.roll  *= RAD_TO_DEG;

// Now safe to pass to SetObjectSectorPos
game->SetObjectSectorPos(proxy_id, sector_id, pos);
```

### SetObjectSectorPos — What It Actually Does

`SetObjectSectorPos` (`0x14017e850`, RVA `0x17e850`):
1. Loads x, y, z from `UIPosRot` + all three angles (yaw, pitch, roll)
2. Multiplies angles by π/180 (degrees→radians conversion) via xmm registers
3. Calls `sub_14030D010` — builds a full 4×4 rotation matrix from yaw/pitch/roll using SIMD sin/cos
4. Applies position + rotation matrix via vtable call on the entity's transform component

All three rotation axes are applied. The rotation is **not** yaw-only — it's full 6DOF.

### Sector Enumeration — Lua-Only

`GetClusters` and `GetSectors` are **Lua-registered functions** (not PE exports, not FFI cdef). They appear as strings in `.rdata` (`0x142a0c858`, `0x1429d4d20`) but cannot be called directly from C++.

**Strategy for sector enumeration from C++:** Use x4native's Lua bridge at `on_game_loaded`. Inject a Lua snippet that iterates `GetClusters(true)` → `GetSectors(cluster)` and forwards each sector ID to C++ via `raise_lua_event`.

```cpp
// In on_game_loaded:
x4n::raise_lua("x4online.enumerate_sectors", "");  // trigger Lua enumeration

// In extension Lua (ui/x4online.lua):
-- function OnEnumerateSectors()
--   for _, cluster in ipairs(GetClusters(true)) do
--     for _, sector in ipairs(GetSectors(cluster)) do
--       RaiseLuaEvent("x4online.sector_found", tostring(sector))
--     end
--   end
-- end

// In C++:
x4n::on("x4online.sector_found", [](const char* id_str) {
    uint64_t sector_id = strtoull(id_str, nullptr, 10);
    registry.register_sector(sector_id);
});
```

### Ships in Sector — No Direct API

There is no `GetShipsInSector` or `GetComponentsByClass` export. Available approaches:

1. **`GetAllFactionShips(result, resultlen, factionid)`** — enumerate by faction. Call for all known factions to seed the registry at session start.
2. **Spawn/destroy hooks** — hook `SpawnObjectAtPos2` (after) and MD `event_object_destroyed` for incremental tracking.

The hook-based approach is the primary strategy for Phase 1.

---

## 9. Game Pause Mechanism

### Summary

There is **no exported `SetGamePaused` or `PauseGame` function**. The game pause system is a reference-counted internal mechanism. The only exported read-only probe is `IsGamePaused` (thunk → `IsGamePaused_0`). To set or clear pause state from C++, you must call the internal `TimerBase` functions or trigger the Lua global `Pause()` / `Unpause()` via the Lua bridge.

### Pause State Storage

| Global | Address | Type | Role |
|--------|---------|------|------|
| `qword_146ADB61C` | `0x146ADB61C` | `uint32_t` (lo-DWORD of qword) | **Pause refcount** — non-zero = paused |
| `qword_146ADB610` | `0x146ADB610` | `double` | **Current game speed** — 0.0 when paused, 1.0 normal |
| `qword_146ADB628` | `0x146ADB628` | `double` | **Saved pre-pause speed** — restored on unpause |
| `stru_146ADB5C0` | `0x146ADB5C0` | `CRITICAL_SECTION` | Lock protecting all three globals |

The refcount design means multiple callers can independently pause the game; the game only unpauses when all of them release. `IsGamePaused_0` returns `true` if the refcount is non-zero.

### Key Functions

| Name (IDA) | Address | RVA | Signature | Role |
|------------|---------|-----|-----------|------|
| `IsGamePaused` | `0x140178A50` | `0x178A50` | `bool ()` | **Exported PE thunk** — callable via `x4n::game()->IsGamePaused()` |
| `TimerBase_IsGamePaused` | `0x14145A020` | `0x145A020` | `bool ()` | Internal implementation — reads refcount under lock |
| `TimerBase_TB_Pause` | `0x1411C83B0` | `0x11C83B0` | `void (__int64 game_client)` | **Increments** refcount, zeros game speed, fires side effects |
| `TimerBase_TB_UnPause` | `0x14145A090` | `0x145A090` | `bool ()` | **Decrements** refcount, restores game speed |
| `Lua_Pause` | `0x14027E920` | `0x27E920` | Lua: `Pause(bool pause, bool permanent?)` | Lua FFI — registered as global `Pause()` |
| `Lua_Unpause` | `0x14027EAC0` | `0x27EAC0` | Lua: `Unpause(bool permanent?)` | Lua FFI — registered as global `Unpause()` |
| `FireEvent_gamePaused` | `0x140AEF7A0` | `0xAEF7A0` | Internal | Fires `"gamePaused"` Lua event, updates infobar4 |
| `FireEvent_gameUnpaused` | `0x140AEFC90` | `0xAEFC90` | Internal | Fires `"gameUnpaused"` Lua event |

### `TimerBase_TB_Pause` — What It Does

```c
// sub_1411C83B0(__int64 game_client_ptr)
// Gate: byte_14386FDCA must be 0 (re-entrancy guard)
byte_14386FDCA = 1;
EnterCriticalSection(&stru_146ADB5C0);
LODWORD(qword_146ADB61C) += 1;           // increment refcount
if (qword_146ADB610 > 0.0)
    qword_146ADB628 = qword_146ADB610;   // save current speed
qword_146ADB610 = 0.0;                  // zero game speed
LeaveCriticalSection(&stru_146ADB5C0);
sub_1400DD650(qword_146C6B9B8, 0);      // cursor clip update
```

### `TimerBase_TB_UnPause` — What It Does

```c
// sub_14145A090()
EnterCriticalSection(&stru_146ADB5C0);
if (LODWORD(qword_146ADB61C) == 0) {
    log_error("TB_UnPause: not paused");  // guard against over-decrement
    goto done;
}
LODWORD(qword_146ADB61C) -= 1;
if (!IsGamePaused_0()) {
    // refcount hit zero — restore speed
    if (byte_146ADB618)
        qword_146ADB610 = 1.0;           // reduced-speed mode: restore to 1x
    else
        qword_146ADB610 = qword_146ADB628; // restore saved speed
}
done:
LeaveCriticalSection(&stru_146ADB5C0);
```

### `Lua_Pause` — Signature and Semantics

```lua
-- arg1 (bool): true = pause, false = "soft pause" (uses alternate code path
--              that sets byte_14386FDC1 flag — same pause counter increment)
-- arg2 (bool): optional, if true marks as "permanent" (sets byte_14386FDC4)
Pause(true)         -- standard game pause
Pause(false)        -- "soft pause" via alternate flag path
Pause(false, true)  -- permanent soft pause
Unpause()           -- standard unpause (calls TB_UnPause via sub_1411C8490)
Unpause(true)       -- also calls TB_UnPause (via sub_1411C8490 path)
```

The game UI calls `Pause()` and `Unpause()` from menu Lua scripts (gameoptions, detailmonitor, onlineupdate, etc.) when entering/leaving menus that freeze gameplay.

### Lua Events Fired

When pause state changes, the engine fires these Lua events (subscribable via `registerForEvent`):

| Event | Fired when |
|-------|-----------|
| `"gamePaused"` | refcount goes from 0 → 1 (first pause) |
| `"gameUnpaused"` | refcount goes to 0 (last release) |

### How to Pause/Unpause from C++

**Option A — Lua bridge (recommended, matches game pattern):**
```cpp
// In on_game_loaded or on_frame_update:
x4n::raise_lua("Pause", "true");    // pause
x4n::raise_lua("Unpause", "");      // unpause
```

**Option B — Direct internal call via hook or function pointer:**

`TimerBase_TB_Pause` takes `game_client_ptr` (the `qword_146C6B960` global). It is NOT in the exported function table and NOT in `X4GameFunctions`, so it cannot be called via `x4n::game()`. It requires a bare function pointer call:

```cpp
// NOT available via x4n::game() — internal only
// Address: 0x1411C83B0 (RVA 0x11C83B0)
// Calling this directly is fragile across game updates.
// Use Option A instead.
```

**Option C — Read-only query (already in SDK):**
```cpp
bool paused = x4n::game()->IsGamePaused();  // safe, exported
```

### Threading

`TimerBase_TB_Pause` and `TimerBase_TB_UnPause` are **safe to call from any thread** — they take `stru_146ADB5C0` (`CRITICAL_SECTION`) before touching the refcount. However, the side effects (cursor clipping via `PostMessageW`, Lua event firing) are UI-thread operations. Always call from the UI thread (our frame hook or `on_game_loaded`).

### Classification

| Operation | Safety | Notes |
|-----------|--------|-------|
| `IsGamePaused()` | Tier 3 — Safe read | Exported, no side effects |
| `Pause()` via Lua bridge | Tier 1 — Safe | Triggers `gamePaused` event, cursor update |
| `Unpause()` via Lua bridge | Tier 1 — Safe | Triggers `gameUnpaused` event |
| Direct `TimerBase_TB_Pause` call | Tier 4 — Caution | Not exported, not in game func table, fragile |

---

## 10. Function Reference

| Name | Address | RVA | Category |
|------|---------|-----|----------|
| `AddPlayerMoney` | `0x14013D5E0` | `0x13D5E0` | Tier 1 mutation |
| `AddPlayerMoney` (inner) | `0x140994350` | `0x994350` | Direct money modification |
| `CreateOrder3` | `0x1401B9060` | `0x1B9060` | Tier 2 entity operation |
| `GetObjectPositionInSector` | `0x1401685A0` | `0x1685A0` | Reads pos (m) + angles (**radians**) |
| `SetObjectSectorPos` | `0x14017e850` | `0x17e850` | Sets pos (m) + angles (**degrees**) |
| `GetAllFactionShips` | — | `0x14D1D0` | Enumerate ships by faction |
| Event bus post | `0x140953650` | `0x953650` | Event dispatch (no lock) |
| Component lookup | via `0x146C6B940` | — | Entity resolution (no lock) |
| `IsGamePaused` (thunk) | `0x140178A50` | `0x178A50` | Exported read-only query |
| `TimerBase_IsGamePaused` | `0x14145A020` | `0x145A020` | Internal pause read (refcount) |
| `TimerBase_TB_Pause` | `0x1411C83B0` | `0x11C83B0` | Internal pause set (increment) |
| `TimerBase_TB_UnPause` | `0x14145A090` | `0x145A090` | Internal pause clear (decrement) |
| `Lua_Pause` | `0x14027E920` | `0x27E920` | Lua global `Pause()` FFI binding |
| `Lua_Unpause` | `0x14027EAC0` | `0x27EAC0` | Lua global `Unpause()` FFI binding |
| `FireEvent_gamePaused` | `0x140AEF7A0` | `0xAEF7A0` | Fires `"gamePaused"` Lua event |
| `FireEvent_gameUnpaused` | `0x140AEFC90` | `0xAEFC90` | Fires `"gameUnpaused"` Lua event |
| Rotation matrix builder | `0x14030D010` | `0x30D010` | sin/cos → 4×4 matrix (internal) |
| Deg-to-rad constant | `0x142C40EE4` | — | `0x3c8efa35` = π/180 |

---

## 11. Player State API — Runtime Behavior (v9.00)

Runtime testing of player state query functions across on-foot scenarios. Tested 2026-03-20.

### Test Scenario

Player docked at station. Transitions: pilot seat → ship interior (elevator) → station (elevator → crew quarters) → back to ship → pilot seat → leave seat.

### Function Return Values by State

| Function | Pilot Seat | Ship Interior (on-foot) | Station (on-foot) | Notes |
|----------|-----------|------------------------|-------------------|-------|
| `GetPlayerOccupiedShipID()` | ship_id (93546) | 0 | 0 | Non-zero ONLY when sitting in pilot seat |
| `GetPlayerControlledShipID()` | ship_id (93546) | 0 | 0 | Same as occupied for pilot seat |
| `GetPlayerShipID()` | ship_id (93546) | ship_id (93546) | 0 | Non-zero when inside own ship (even on-foot), 0 on station |
| `GetPlayerContainerID()` | ship_id (93546) | ship_id (93546) | station_id (94053) | Always the containing entity |
| `GetPlayerObjectID()` | ship_id (93546) | ship_id (93546) | station_id (94053) | **Returns container, NOT avatar** (IDA analysis was wrong) |
| `GetEnvironmentObject()` | 0 | 0 | 0 | **Always 0** — contradicts IDA analysis (expected room ID) |
| `GetPlayerID()` | 93553 | 93553 | 93553 | Unique stable entity across all states |
| `IsPlayerOccupiedShipDocked()` | true (1) | false (0) | false (0) | Only true when piloting a docked ship |
| `GetPlayerZoneID()` | 93474 | 93474 | 93474 | Stable zone ID across all on-foot states and pilot seat |

### Key Findings

1. **`GetPlayerObjectID()` ≠ avatar.** Despite IDA showing a class-71 parent walk, at runtime it returns the same value as `GetPlayerContainerID()`. The "avatar entity" does not appear as a distinct game object accessible via these APIs.

2. **`GetEnvironmentObject()` = 0 always.** IDA shows it reads `player->data[+29496]`. Either this field is never populated in normal gameplay, or it requires conditions not met in testing (e.g., specific room types, or the player must have entered via a specific path). Needs further investigation.

3. **On-foot detection formula:** `GetPlayerOccupiedShipID() == 0 && GetPlayerContainerID() != 0`. Reliable across ship interior and station on-foot states.

4. **`GetPlayerShipID()` distinguishes location.** Non-zero when on own ship (even on-foot), zero when on station. Useful for determining whether player is in ship or station.

5. **`event_player_changed_activity` unreliable for on-foot.** Only fires for in-ship activities (travel, scan, seta). Does NOT fire for cockpit → on-foot transition. Replaced with 100ms polling.

6. **`GetPlayerID()` is the only unique entity** but **unusable for position.** `GetObjectPositionInSector(GetPlayerID())` returns [0,0,0]. The player entity (93653) has no sector-space position. Only the container (ship/station) has a valid position.

7. **`GetPlayerZoneID()` works.** Returns a stable zone ID (93474) across all states including pilot seat. Consistent even when container changes (ship→station→ship).

8. **No exported API for room-local walking position.** `GetObjectPositionInSector(container)` gives the ship/station's sector position, not the player's position within the interior. Room-local coordinates may require Lua `C.GetPlayerRoom()` or decompiling the first-person camera position source.
