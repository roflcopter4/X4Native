// ---------------------------------------------------------------------------
// x4n_ship.h — Ship typed wrapper with order creation
// ---------------------------------------------------------------------------
// Part of the X4Native SDK. Included by x4native.h.
//
// Provides:
//   x4n::ship::Ship — lightweight handle for ship/controllable entities
//
// Usage:
//   x4n::ship::Ship ship(ship_id);
//   if (ship.valid()) {
//       ship.order_patrol();                      // patrol current zone
//       ship.order_attack(enemy_id);              // attack a target
//       ship.order_protect_station(station_id);   // guard a station
//       ship.order_trade_routine();               // autonomous trading
//       ship.order_mining_routine();              // autonomous mining
//   }
//
// Order creation bypasses the player-ownership check in the exported
// CreateOrder3, allowing orders on NPC faction ships.
//
// Thin handle: UniverseID + cached X4Component*. No heap allocation.
// All functions require on_game_loaded to have fired.
// ---------------------------------------------------------------------------
#pragma once

#include "x4n_core.h"
#include "x4n_entity.h"
#include "x4n_log.h"
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace x4n { namespace ship {

// ---- Order param value struct ----

struct OrderParamValue {
    int32_t  type;
    int32_t  _pad = 0;
    uint64_t data;
};

// ---- Aligned string view for internal game functions ----
// Same layout as std::string_view {data, length} but with 16-byte alignment.
// Required because CreateOrderInternal reads it with movaps (SSE aligned load).

struct alignas(16) AlignedStringView {
    const char* data;
    size_t      length;
};

/// Lightweight typed handle for a ship entity.
/// Constructor validates the entity is a Ship (any size class).
class Ship {
    UniverseID id_;
    X4Component* comp_;
public:
    explicit Ship(UniverseID id)
        : id_(id), comp_(entity::find_component(id)) {
        // Validate it's a Ship-derived entity (class 116 = Ship, parent of S/M/L)
        if (comp_ && !entity::is_derived_from(comp_, GameClass::Ship))
            comp_ = nullptr;
    }

    bool valid() const { return comp_ != nullptr; }
    UniverseID id() const { return id_; }

    /// Create an order on this ship. Returns order object pointer.
    /// Bypasses the player-ownership check in exported CreateOrder3.
    /// @param order_id  Order type name (e.g., "Patrol", "Attack", "TradeRoutine")
    /// @param default_order  If true, replaces the ship's default standing order
    void* create_order(const char* order_id, bool default_order = true) {
        if (!valid()) return nullptr;
        auto* g = game();
        if (!g || !g->CreateOrderInternal) return nullptr;

        AlignedStringView sv{ order_id, std::strlen(order_id) };
        int mode = default_order ? 3 : 1;
        void* result = g->CreateOrderInternal(comp_, &sv, mode, 0);
        if (!result) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "x4n::ship: create_order('%s') returned null. entity=%p",
                order_id, (void*)comp_);
            x4n::log::info(buf);
        }
        return result;
    }

    /// Set an entity-typed parameter on an order.
    /// @param order      Order object pointer from create_order()
    /// @param param_idx  0-based parameter index
    /// @param entity_uid UniverseID of the target entity
    bool set_param_entity(void* order, uint32_t param_idx, UniverseID entity_uid) {
        if (!order) return false;
        auto* g = game();
        if (!g || !g->SetOrderParamInternal) return false;

        OrderParamValue val{};
        val.type = X4_ORDER_PARAM_TYPE_ENTITY;
        val.data = entity_uid;
        return g->SetOrderParamInternal(order, param_idx, &val) != 0;
    }

    // ---------------------------------------------------------------
    // Convenience order methods
    //
    // Param indices verified from AI script XML declarations:
    //   Patrol:         0=space, 1=range, 2=pursuetargets, ...
    //   Attack:         0=primarytarget, 1=secondarytargets, 2=escort, 3=pursuedistance, ...
    //   ProtectStation: 0=station, 1=radius, 2=timeout, ...
    //   TradeRoutine:   0=warebasket, 1=range, 2=minbuy, 3=maxbuy, ...
    //   MiningRoutine:  0=warebasket, 1=range, 2=minbuy, 3=maxbuy, ...
    // ---------------------------------------------------------------

    /// Order this ship to patrol. If sector is provided, patrols that sector.
    /// Otherwise patrols the ship's current zone. Loops indefinitely.
    /// @param sector  Optional: UniverseID of sector to patrol (0 = current zone)
    bool order_patrol(UniverseID sector = 0) {
        void* order = create_order("Patrol", true);
        if (!order) return false;
        if (sector != 0) {
            set_param_entity(order, 0, sector); // param 0 = space
        }
        return true;
    }

    /// Order this ship to attack a specific target.
    /// @param target  UniverseID of the target ship or station
    bool order_attack(UniverseID target) {
        void* order = create_order("Attack", false);
        if (!order) return false;
        return set_param_entity(order, 0, target); // param 0 = primarytarget
    }

    /// Order this ship to guard a station.
    /// @param station  UniverseID of the station to protect
    bool order_protect_station(UniverseID station) {
        void* order = create_order("ProtectStation", true);
        if (!order) return false;
        return set_param_entity(order, 0, station); // param 0 = station
    }

    /// Order this ship to trade autonomously. Loops indefinitely.
    /// Uses the ship's existing ware basket (set by commander assignment).
    bool order_trade_routine() {
        return create_order("TradeRoutine", true) != nullptr;
    }

    /// Order this ship to mine autonomously. Loops indefinitely.
    /// Uses the ship's existing ware basket (set by commander assignment).
    /// @param sector  Optional: UniverseID of sector to mine in (0 = current area)
    bool order_mining_routine(UniverseID sector = 0) {
        void* order = create_order("MiningRoutine", true);
        if (!order) return false;
        if (sector != 0) {
            set_param_entity(order, 1, sector); // param 1 = range (sector)
        }
        return true;
    }
};

}} // namespace x4n::ship
