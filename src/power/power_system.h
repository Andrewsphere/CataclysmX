#pragma once

#include "power/power_types.h"
#include "core/world_result.h"
#include "items/item_types.h"
#include <EASTL/span.h>

namespace cataclysmx {
class World;

class PowerSystem {
public:
    // Does not call World during construction (World::Impl is not published yet).
    PowerSystem(World& world, std::uint32_t seed);
    PowerSettings Settings() const;
    WorldResult<void> SetSettings(PowerSettings settings);
    eastl::optional<PowerGridId> GridAt(TileCoord position) const;
    WorldResult<WiringQuote> Quote(PowerBounds bounds) const;
    // Geometry only. World validates/consumes gameplay materials before this commit.
    WorldResult<PowerGridId> ExtendBounds(PowerBounds bounds);
    WorldResult<PowerGridId> RegisterBuilding(TileCoord origin, PowerBounds bounds);
    WorldResult<PowerGridId> RegisterBuilding(TileCoord origin, eastl::span<const PowerBounds> bounds);
    // Nonmutating whole-batch preflight, including duplicates, overlaps and ID headroom.
    WorldResult<void> ValidateBuildings(eastl::span<const BuildingPowerRegistration> buildings) const;

    void SyncObject(ObjectHandle object);
    void UnregisterObject(PersistentId object); // Destruction only, never unloading.
    void Invalidate(); // Topology/source changes; safe inside World payment hooks.
    Tick SliceLimit(Tick now) const;
    PowerOutcome CanSupply(eastl::span<const PowerDemand> demands) const;
    PowerOutcome TrySupply(eastl::span<const PowerDemand> demands);
    eastl::optional<Tick> NextEventIn() const;
    bool ProcessEvents();

    SavedPowerState Capture() const;
    bool IsPristine() const;
    bool CheckState(Tick now, PersistentId nextObjectId) const;
    eastl::optional<SavedPowerDevice> FindDevice(PersistentId id) const;
    eastl::span<const SavedPowerDevice> Devices() const;
    WorldResult<void> Restore(const SavedPowerState& state, Tick now);
    // Exact region-union intersection. Clears/fills reusable sorted unique source
    // columns from the persistent index, including unmaterialized equipment.
    void CollectConsumerDemand(eastl::span<const PowerBounds> areas, Vector<ColumnCoord>& output) const;
    void CollectWorkshopDemand(Vector<ColumnCoord>& output) const;
    void CollectWorkshopDebug(Vector<PowerColumnDebug>& output) const;
    void Inspect(TileCoord position, PowerInspection& output) const;
    void CollectDebugGrids(Vector<PowerGridDebug>& output) const;
    PowerCounters Counters() const;
    void ResetCounters();

private:
    struct Source {
        PowerGridId grid;
        PersistentId id = 0;
        ObjectHandle object{};
        ItemHandle battery{};
        ItemCapabilityId store{};
        std::uint64_t available = 0;
        std::uint64_t spent = 0;
        bool generator = false;
        bool rechargeable = false;
    };
    SavedPowerGrid* Grid(PowerGridId id);
    const SavedPowerGrid* Grid(PowerGridId id) const;
    bool Mains(const SavedPowerGrid& grid) const;
    bool Battery(const SavedPowerDevice& device, Source& output) const;
    std::uint64_t GeneratorAvailable(ObjectHandle object) const;
    void GatherSources() const;
    PowerOutcome Plan(eastl::span<const PowerDemand> demands) const;
    void CommitPlan();
    void ChargeSurplus();
    bool HasWork() const;

    World& world_;
    SavedPowerState state_;
    bool dirty_ = true;
    bool processing_ = false;
    Tick lastEventTick_ = 0;
    bool eventProcessed_ = false;
    mutable Vector<Source> sources_;
    Vector<PersistentId> contenders_;
    mutable PowerCounters counters_;
};
}
