// ---------------------------------------------------------------------------
// x4n_station.h — Station typed wrapper
// ---------------------------------------------------------------------------
// Part of the X4Native SDK. Included by x4native.h.
//
// Provides:
//   x4n::station::Station — lightweight handle for station/defensible entities
//
// Usage:
//   x4n::station::Station station(station_id);
//   for (auto& mod : station.modules()) {
//       if (mod.is_production() && mod.ware_id() == "energycells") {
//           mod.set_paused(true);
//       }
//   }
//   // Or all-in-one:
//   uint32_t affected = station.set_ware_paused("energycells", true);
//
// Thin handle: UniverseID + cached X4Component*. No heap allocation
// except the module vector from enumeration.
// All functions require on_game_loaded to have fired.
// ---------------------------------------------------------------------------
#pragma once

#include "x4n_core.h"
#include "x4n_entity.h"
#include "x4n_module.h"
#include <cstdint>
#include <string>
#include <vector>

#pragma push_macro("ND")
#define ND [[nodiscard]]

namespace x4n::station {

/// Lightweight typed handle for a station/defensible entity.
/// Constructor validates the entity is actually a Station class.
class Station : public entity::ComponentEntity
{
  public:
    explicit Station(UniverseID id)
        : ComponentEntity(id, GameClass::Station)
    {}

    ND char const *name() const
    {
        auto *g = game();
        return (valid() && g) ? g->GetComponentName(id()) : "";
    }

    ND char const *macro() const
    {
        return valid() ? entity::get_component_macro(component()) : nullptr;
    }

    /// Enumerate all modules on this station.
    ND std::vector<module::Module> modules(bool include_construction = false, bool include_wrecks = false) const
    {
        std::vector<module::Module> result;
        auto *g = game();
        if (!valid() || !g)
            return result;

        uint32_t count = g->GetNumStationModules(id(), include_construction, include_wrecks);
        if (count == 0)
            return result;

        auto     buf    = std::vector<UniverseID>(count);
        uint32_t actual = g->GetStationModules(buf.data(), count, id(), include_construction, include_wrecks);
        result.reserve(actual);
        for (uint32_t i = 0; i < actual; ++i)
            result.emplace_back(buf[i]);
        return result;
    }

    /// Check if this station produces a given ware (fast, station-level check).
    /// Uses ignorestate=true so it returns > 0 even when modules are paused.
    ND bool produces(char const *ware_id) const
    {
        auto *g = game();
        if (!valid() || !g)
            return false;
        return g->GetContainerWareProduction(id(), ware_id, true) > 0.0;
    }

    /// Pause or resume all production/processing modules for a specific ware.
    /// Returns the number of modules affected (0 if ware not found).
    uint32_t set_ware_paused(std::string const &ware_id, bool paused)
    {
        if (!produces(ware_id.c_str()))
            return 0;
        uint32_t affected = 0;
        for (auto &mod : modules()) {
            if (!mod.is_operational())
                continue;
            if (!mod.is_production() && !mod.is_processing())
                continue;
            if (mod.ware_id() != ware_id)
                continue;
            mod.set_paused(paused);
            ++affected;
        }
        return affected;
    }

    // -----------------------------------------------------------------------
    // Per-ware state (engine-canonical; folds workforce + production + build)
    // -----------------------------------------------------------------------

    /// Effective per-ware consumption rate (units/hour). Engine-canonical:
    /// sums recipe inputs from running production modules, workforce consumption
    /// from habitation modules, and build-resource draw from active builds.
    /// @param ignore_state When true, returns the max capacity rate regardless
    ///        of current operational state (paused/stalled/no-workforce).
    ND double consumption(char const *ware_id, bool ignore_state = false) const
    {
        auto *g = game();
        if (!valid() || !g)
            return 0.0;
        return g->GetContainerWareConsumption(id(), ware_id, ignore_state);
    }

    /// Effective per-ware production rate (units/hour).
    /// @param ignore_state true → theoretical max, false → live (modifiers applied).
    ND double production_rate(char const *ware_id, bool ignore_state = false) const
    {
        auto *g = game();
        if (!valid() || !g)
            return 0.0;
        return g->GetContainerWareProduction(id(), ware_id, ignore_state);
    }

    /// How many units of this ware this station currently wants to buy (trade-offer target).
    ND int32_t buy_limit(char const *ware_id) const
    {
        auto *g = game();
        if (!valid() || !g)
            return 0;
        return g->GetContainerBuyLimit(id(), ware_id);
    }

    /// How many units of this ware this station currently wants to sell (trade-offer target).
    ND int32_t sell_limit(char const *ware_id) const
    {
        auto *g = game();
        if (!valid() || !g)
            return 0;
        return g->GetContainerSellLimit(id(), ware_id);
    }

    /// Engine-canonical list of ware IDs critical for this container.
    /// Includes workforce consumables, recipe inputs, and build resources
    /// — whatever the engine knows the container requires.
    /// Safe to call even when the station currently holds zero stock of any ware.
    ND std::vector<std::string> critical_wares() const
    {
        std::vector<std::string> result;
        auto *g = game();
        if (!valid() || !g)
            return result;
        uint32_t n = g->GetNumContainerCriticalWares(id());
        if (n == 0)
            return result;
        auto     buf    = std::vector<char const *>(n);
        uint32_t actual = g->GetContainerCriticalWares(buf.data(), n, id());
        result.reserve(actual);
        for (uint32_t i = 0; i < actual; ++i)
            if (buf[i])
                result.emplace_back(buf[i]);
        return result;
    }

    /// Wares required by the container's active build (if any).
    /// For a regular station this returns the build-queue's required wares;
    /// for a buildstorage object it returns its own build_resources.
    ND std::vector<std::string> build_resources() const
    {
        std::vector<std::string> result;
        auto *g = game();
        if (!valid() || !g)
            return result;
        uint32_t n = g->GetNumContainerBuildResources(id());
        if (n == 0)
            return result;
        auto     buf    = std::vector<char const *>(n);
        uint32_t actual = g->GetContainerBuildResources(buf.data(), n, id());
        result.reserve(actual);
        for (uint32_t i = 0; i < actual; ++i)
            if (buf[i])
                result.emplace_back(buf[i]);
        return result;
    }

    /// Per-ware cargo enumeration. Returns the station's current cargo entries
    /// (ware_id + amount) matching the given tag filter.
    /// @param tags Ware-tag filter; empty string = all wares.
    ND std::vector<UIWareInfo> cargo(char const *tags = "") const
    {
        std::vector<UIWareInfo> result;
        auto *g = game();
        if (!valid() || !g)
            return result;
        uint32_t n = g->GetNumCargo(id(), tags);
        if (n == 0)
            return result;
        result.resize(n);
        uint32_t actual = g->GetCargo(result.data(), n, id(), tags);
        result.resize(actual);
        return result;
    }

    /// Resolve this station's buildstorage sub-entity (if any).
    /// Buildstorage is a child container that holds build-resource cargo
    /// during station construction. Returns 0 if the station has none.
    ND UniverseID buildstorage_id() const
    {
        return valid() ? entity::find_ancestor(id(), GameClass::Buildstorage) : 0;
    }

    /// People capacity (workforce target) for this station.
    /// @param macro_name Macro filter (e.g. a specific module), or empty for whole station.
    /// @param include_pilot Whether to include pilot capacity (for ships); false for stations.
    ND uint32_t people_capacity(char const *macro_name = "", bool include_pilot = false) const
    {
        auto *g = game();
        if (!valid() || !g)
            return 0;
        return g->GetPeopleCapacity(id(), macro_name, include_pilot);
    }

    /// Workforce influence info for a race (capacity / growth / current / target / change).
    /// Wraps GetContainerWorkforceInfluence.
    ND WorkforceInfluenceInfo workforce_info(char const *race) const
    {
        WorkforceInfluenceInfo info{};
        auto *g = game();
        if (!valid() || !g)
            return info;
        g->GetContainerWorkforceInfluence(&info, id(), race);
        return info;
    }
};

/// Lightweight typed handle for a station's buildstorage sub-entity.
/// Constructor validates the entity is actually a Buildstorage class.
/// Use Station::buildstorage_id() to obtain the ID first.
class Buildstorage : public entity::ComponentEntity
{
  public:
    explicit Buildstorage(UniverseID id)
        : ComponentEntity(id, GameClass::Buildstorage)
    {}

    /// Wares critical for this buildstorage (engine-canonical, includes
    /// whatever the current build queue needs as inputs).
    ND std::vector<std::string> critical_wares() const
    {
        std::vector<std::string> result;
        auto *g = game();
        if (!valid() || !g)
            return result;
        uint32_t n = g->GetNumContainerCriticalWares(id());
        if (n == 0)
            return result;
        auto     buf    = std::vector<char const *>(n);
        uint32_t actual = g->GetContainerCriticalWares(buf.data(), n, id());
        result.reserve(actual);
        for (uint32_t i = 0; i < actual; ++i)
            if (buf[i])
                result.emplace_back(buf[i]);
        return result;
    }

    /// Build resources this buildstorage needs.
    ND std::vector<std::string> build_resources() const
    {
        std::vector<std::string> result;
        auto *g = game();
        if (!valid() || !g)
            return result;
        uint32_t n = g->GetNumContainerBuildResources(id());
        if (n == 0)
            return result;
        auto     buf    = std::vector<char const *>(n);
        uint32_t actual = g->GetContainerBuildResources(buf.data(), n, id());
        result.reserve(actual);
        for (uint32_t i = 0; i < actual; ++i)
            if (buf[i])
                result.emplace_back(buf[i]);
        return result;
    }

    /// Cargo currently held by this buildstorage (per-ware amounts).
    ND std::vector<UIWareInfo> cargo(char const *tags = "") const
    {
        std::vector<UIWareInfo> result;
        auto *g = game();
        if (!valid() || !g)
            return result;
        uint32_t n = g->GetNumCargo(id(), tags);
        if (n == 0)
            return result;
        result.resize(n);
        uint32_t actual = g->GetCargo(result.data(), n, id(), tags);
        result.resize(actual);
        return result;
    }

    /// Per-ware consumption rate in the buildstorage (build draw only).
    ND double consumption(char const *ware_id, bool ignore_state = false) const
    {
        auto *g = game();
        if (!valid() || !g)
            return 0.0;
        return g->GetContainerWareConsumption(id(), ware_id, ignore_state);
    }
};

} // namespace x4n::station

#pragma pop_macro("ND")