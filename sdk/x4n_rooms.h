// ---------------------------------------------------------------------------
// x4n_rooms.h — Walkable Interior Utilities
// ---------------------------------------------------------------------------
// Part of the X4Native SDK. Included by x4native.h.
//
// Provides:
//   x4n::rooms::roomtype_name()  — convert X4RoomType enum to string
//
// Pure function — no game state dependency (safe to call anytime).
// ---------------------------------------------------------------------------
#pragma once

#include "x4n_core.h"

namespace x4n::rooms {

/// Convert an X4RoomType enum value to its lowercase string name.
/// Returns nullptr for out-of-range or sentinel values.
/// @stability STABLE — enum mapping, verified against game data.
/// @verified v9.00 build 600626
[[nodiscard]] constexpr char const *roomtype_name(X4RoomType type)
{
    switch (type) {
    case X4_ROOMTYPE_BAR:                return "bar";
    case X4_ROOMTYPE_CASINO:             return "casino";
    case X4_ROOMTYPE_CORRIDOR:           return "corridor";
    case X4_ROOMTYPE_CREWQUARTERS:       return "crewquarters";
    case X4_ROOMTYPE_EMBASSY:            return "embassy";
    case X4_ROOMTYPE_FACTIONREP:         return "factionrep";
    case X4_ROOMTYPE_GENERATORROOM:      return "generatorroom";
    case X4_ROOMTYPE_INFRASTRUCTURE:     return "infrastructure";
    case X4_ROOMTYPE_INTELLIGENCEOFFICE: return "intelligenceoffice";
    case X4_ROOMTYPE_LIVINGROOM:         return "livingroom";
    case X4_ROOMTYPE_MANAGER:            return "manager";
    case X4_ROOMTYPE_OFFICE:             return "office";
    case X4_ROOMTYPE_PLAYEROFFICE:       return "playeroffice";
    case X4_ROOMTYPE_PRISON:             return "prison";
    case X4_ROOMTYPE_SECURITY:           return "security";
    case X4_ROOMTYPE_SERVERROOM:         return "serverroom";
    case X4_ROOMTYPE_SERVICEROOM:        return "serviceroom";
    case X4_ROOMTYPE_SHIPTRADERCORNER:   return "shiptradercorner";
    case X4_ROOMTYPE_TRADERCORNER:       return "tradercorner";
    case X4_ROOMTYPE_TRAFFICCONTROL:     return "trafficcontrol";
    case X4_ROOMTYPE_WARROOM:            return "warroom";
    case X4_ROOMTYPE_NONE:
    default: 
        return nullptr;
    }
}

} // namespace x4n::rooms