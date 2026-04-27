// ---------------------------------------------------------------------------
// x4n_module.h — Station Module typed wrapper
// ---------------------------------------------------------------------------
// Part of the X4Native SDK. Included by x4native.h.
//
// Provides:
//   x4n::module::Module — lightweight handle for station modules
//                          (production, processing, storage, etc.)
//
// Usage:
//   x4n::module::Module mod(module_id);
//   if (mod.is_production() && mod.ware_id() == "energycells") {
//       mod.set_paused(true);
//   }
//
// Thin handle: UniverseID + cached X4Component*. No heap allocation.
// All functions require on_game_loaded to have fired.
// ---------------------------------------------------------------------------
#pragma once

#include "x4n_core.h"
#include "x4n_entity.h"
#include <cstdint>
#include <string>

#pragma push_macro("ND")
#define ND [[nodiscard]]

namespace x4n::module {

/// Lightweight typed handle for a station module (production, processing, etc.).
/// Constructor validates the entity is actually a Module class.
class Module : public entity::ComponentEntity
{
  public:
    explicit Module(UniverseID id)
        : ComponentEntity(id, GameClass::Module)
    {}

    ND bool is_production()  const { return valid() && entity::is_a(component(), GameClass::Production); }
    ND bool is_processing()  const { return valid() && entity::is_a(component(), GameClass::Processingmodule); }
    ND bool is_habitation()  const { return valid() && entity::is_a(component(), GameClass::Habitation); }
    ND bool is_buildmodule() const { return valid() && entity::is_a(component(), GameClass::Buildmodule); }
    ND bool is_storage()     const { return valid() && entity::is_a(component(), GameClass::Storage); }
    ND bool is_defence()     const { return valid() && entity::is_a(component(), GameClass::Defencemodule); }
    ND bool is_connection()  const { return valid() && entity::is_a(component(), GameClass::Connectionmodule); }

    ND bool is_operational() const
    {
        auto *g = game();
        return valid() && g && g->IsComponentOperational(id());
    }

    ND char const *macro() const { return valid() ? entity::get_component_macro(component()) : nullptr; }

    /// Extract ware_id from macro: "prod_gen_energycells_macro" -> "energycells".
    /// Returns empty string if macro doesn't match the production naming pattern.
    ND std::string ware_id() const
    {
        char const *m = macro();
        if (!m)
            return {};
        std::string_view s(m);
        if (s.size() < 12)
            return {};
        if (s.substr(s.size() - 6) != "_macro")
            return {};
        if (s.substr(0, 5) != "prod_")
            return {};
        size_t pos = s.find('_', 5);
        if (pos == std::string::npos)
            return {};
        return std::string{s.substr(pos + 1, s.size() - 6 - pos - 1)};
    }

    /// Pause or resume this module via the game's FFI.
    /// @note FFI checks player ownership — may silently fail on NPC modules.
    void set_paused(bool p)
    {
        auto *g = game();
        if (!valid() || !g)
            return;
        if (is_production())
            g->PauseProductionModule(id(), p);
        else if (is_processing())
            g->PauseProcessingModule(id(), p);
    }

    /// True iff this module has been manually paused.
    ///
    /// Handles both Production (class 78, paused_since sentinel at +0x398) and
    /// Processingmodule (class 77, pause byte at +0x3B8). For Production, reads the
    /// `paused_since` timestamp which is the game engine's authoritative pause check
    /// (not the redundant +0x3F4 flag byte). For Processingmodule, reads its pause byte
    /// directly — no timestamp exists on that class.
    ///
    /// See docs/rev/PRODUCTION_MODULES.md §5.3.
    /// Works for NPC-owned modules (read path is not gated; only the FFI write is).
    ND bool is_paused() const
    {
        if (!valid())
            return false;
        if (entity::is_a(component(), GameClass::Production)) {
            double t = *reinterpret_cast<double const *>(reinterpret_cast<uint8_t const *>(component()) + X4_PRODUCTION_PAUSED_SINCE_OFFSET);
            return t > -0.9999;
        }
        if (entity::is_a(component(), GameClass::Processingmodule))
            return *(reinterpret_cast<uint8_t const *>(component()) + X4_PROCESSINGMODULE_PAUSED_FLAG_OFFSET) != 0;
        return false;
    }

    /// `player.age` seconds at which this Production module was paused; returns -1.0
    /// if not currently paused or if this isn't a Production module.
    ///
    /// **Production-only.** Processingmodule (class 77) has no timestamp field — its
    /// pause/resume handlers never stamp a "since" value (confirmed by decompiling
    /// sub_14053C4A0 / sub_14053C560). If you need to track pause duration for a
    /// Processingmodule, you must maintain state outside the game (e.g., observe
    /// is_paused() transitions across polls).
    ///
    /// Subtract from current `player.age` to get seconds-paused for Production modules.
    /// Save-persistent — the timestamp survives save/load cycles.
    ///
    /// See docs/rev/PRODUCTION_MODULES.md §5.3.
    ND double paused_since() const
    {
        if (!is_production())
            return -1.0;
        return *reinterpret_cast<double const *>(reinterpret_cast<uint8_t const *>(component()) + X4_PRODUCTION_PAUSED_SINCE_OFFSET);
    }
};

} // namespace x4n::module

#pragma pop_macro("ND")