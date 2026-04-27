// ---------------------------------------------------------------------------
// x4n_sector.h — Sector typed wrapper with resource & environment access
// ---------------------------------------------------------------------------
// Part of the X4Native SDK.
//
// Provides:
//   x4n::sector::Sector — lightweight handle for sector entities
//
// Usage:
//   x4n::sector::Sector sec(sector_id);
//   if (sec.valid()) {
//       float sun = sec.sunlight();       // solar intensity (from parent cluster)
//       auto  res = sec.resources();       // all resource areas with max/current yield
//   }
//
// Thin handle: UniverseID + cached X4Component*. No heap allocation.
// All functions require on_game_loaded to have fired.
// ---------------------------------------------------------------------------
#pragma once

#include "x4n_core.h"
#include "x4n_entity.h"
#include "x4n_resources.h"
#include <cstdint>

#pragma push_macro("ND")
#define ND [[nodiscard]]

namespace x4n::sector {

/// Lightweight typed handle for a sector entity.
/// Constructor validates the entity is a Sector.
class Sector : public entity::ComponentEntity
{
  public:
    explicit Sector(UniverseID id)
        : ComponentEntity(id, GameClass::Sector)
    {}

    /// Sunlight intensity from the parent cluster (Space+0x368).
    /// Walks parent chain to find a Space entity with the sunlight flag.
    /// Returns -1.0 if not available.
    /// @verified v9.00 build 604402 (IDA: sub_1407B51D0 sunlight getter)
    ND float sunlight() const
    {
        if (!valid())
            return -1.0f;
        auto *cur = static_cast<X4Component const *>(component()->parent);
        while (cur) {
            auto p = reinterpret_cast<uint8_t const *>(cur);
            if (*(p + detail::offsets()->space_has_sunlight)) {
                double val;
                memcpy(&val, p + detail::offsets()->space_sunlight, sizeof val);
                return static_cast<float>(val);
            }
            cur = static_cast<X4Component const *>(cur->parent);
        }
        return -1.0f;
    }

    /// All resource data for this sector (unfiltered, with per-area pointers).
    /// Area pointers valid for current frame only.
    ND std::vector<resources::SectorResource> resources() const
    {
        if (!valid())
            return {};
        return resources::get_sector_resources(id_);
    }
};

} // namespace x4n::sector

#pragma pop_macro("ND")