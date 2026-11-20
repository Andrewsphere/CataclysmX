#include "power/power_system.h"
#include "core/world.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstdlib>
#include <limits>

namespace cataclysmx {
namespace {
void Require(bool value) { if (!value) { std::abort(); } }
std::uint64_t SaturatingAdd(std::uint64_t a, std::uint64_t b) {
    return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}
bool Touches(const SavedPowerGrid& grid, PowerBounds bounds) {
    for (auto region : grid.regions) { if (PowerConnects(region, bounds)) { return true; } }
    return false;
}
bool ColumnLess(ColumnCoord a, ColumnCoord b) { return a.x != b.x ? a.x < b.x : a.z < b.z; }
}

PowerSystem::PowerSystem(World& world, std::uint32_t seed) : world_(world) {
    state_.mainsCutoffTick = SeededMainsCutoff(seed);
}
PowerSettings PowerSystem::Settings() const { return state_.settings; }
WorldResult<void> PowerSystem::SetSettings(PowerSettings settings) {
    if (!ValidPowerSettings(settings)) { return eastl::unexpected(WorldError::invalid_request); }
    if (state_.settings == settings) { return {}; }
    state_.settings = settings;
    Invalidate();
    return {};
}
SavedPowerGrid* PowerSystem::Grid(PowerGridId id) {
    for (auto& grid : state_.grids) { if (grid.id == id) { return &grid; } }
    return nullptr;
}
const SavedPowerGrid* PowerSystem::Grid(PowerGridId id) const {
    for (const auto& grid : state_.grids) { if (grid.id == id) { return &grid; } }
    return nullptr;
}
eastl::optional<PowerGridId> PowerSystem::GridAt(TileCoord position) const {
    for (const auto& grid : state_.grids) {
        for (auto region : grid.regions) { if (PowerContains(region, position)) { return grid.id; } }
    }
    return eastl::nullopt;
}
WorldResult<WiringQuote> PowerSystem::Quote(PowerBounds bounds) const {
    if (!ValidPowerBounds(bounds) || bounds.min.y != bounds.max.y
        || bounds.max.x - bounds.min.x + 1 > state_.settings.maxWiringWidth
        || bounds.max.z - bounds.min.z + 1 > state_.settings.maxWiringDepth) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    WiringQuote result;
    for (int x = bounds.min.x; x <= bounds.max.x; ++x) {
        for (int z = bounds.min.z; z <= bounds.max.z; ++z) {
            if (!GridAt({x, z, bounds.min.y})) { ++result.newTiles; }
        }
    }
    for (const auto& grid : state_.grids) { if (Touches(grid, bounds)) { ++result.mergedGrids; } }
    result.wiringUnits = result.newTiles;
    return result;
}
WorldResult<PowerGridId> PowerSystem::ExtendBounds(PowerBounds bounds) {
    const auto quote = Quote(bounds);
    if (!quote) { return eastl::unexpected(quote.error()); }
    PowerGridId survivor;
    // Select only grids touched by the new region, not a transitive closure of old adjacency.
    for (const auto& grid : state_.grids) { if (Touches(grid, bounds)) { survivor = grid.id; break; } }
    if (!survivor.value) {
        if (state_.nextGridId == UINT64_MAX) { return eastl::unexpected(WorldError::capacity_exceeded); }
        SavedPowerGrid grid;
        grid.id = {state_.nextGridId++}; grid.regions.push_back(bounds);
        survivor = grid.id; state_.grids.push_back(eastl::move(grid));
    } else {
        auto* target = Grid(survivor);
        for (std::size_t i = 0; i < state_.grids.size();) {
            const auto& other = state_.grids[i];
            if (other.id == survivor || !Touches(other, bounds)) { ++i; continue; }
            const auto removed = other.id;
            target->regions.insert(target->regions.end(), other.regions.begin(), other.regions.end());
            target->mainsConnected |= other.mainsConnected;
            // A merge cannot reopen a charging pass or rotate a grid twice this interval.
            if (other.intervalProcessed && (!target->intervalProcessed || other.processedInterval > target->processedInterval)) {
                target->processedInterval = other.processedInterval; target->intervalProcessed = true;
            }
            if (other.mainsChargeProcessed && (!target->mainsChargeProcessed || other.mainsChargeInterval > target->mainsChargeInterval)) {
                target->mainsChargeInterval = other.mainsChargeInterval; target->mainsChargeProcessed = true;
            }
            for (auto& binding : state_.generatedBindings) { if (binding.grid == removed) { binding.grid = survivor; } }
            state_.grids.erase(state_.grids.begin() + i);
            target = Grid(survivor);
        }
        if (quote->newTiles) { target->regions.push_back(bounds); }
    }
    Invalidate();
    return survivor;
}
WorldResult<PowerGridId> PowerSystem::RegisterBuilding(TileCoord origin, PowerBounds bounds) {
    return RegisterBuilding(origin, {&bounds, 1});
}
WorldResult<void> PowerSystem::ValidateBuildings(eastl::span<const BuildingPowerRegistration> buildings) const {
    std::uint64_t additions = 0;
    for (std::size_t i = 0; i < buildings.size(); ++i) {
        const auto& building = buildings[i];
        if (!ValidPowerBounds({building.origin, building.origin}) || !ValidPowerBounds(building.bounds)) {
            return eastl::unexpected(WorldError::invalid_region);
        }
        bool known = false;
        for (const auto& binding : state_.generatedBindings) { known |= binding.origin == building.origin; }
        if (known) { continue; }
        bool repeated = false;
        for (std::size_t j = 0; j < i; ++j) {
            if (buildings[j].origin == building.origin) {
                if (buildings[j].bounds != building.bounds) { return eastl::unexpected(WorldError::invalid_region); }
                repeated = true;
            } else if (PowerOverlaps(buildings[j].bounds, building.bounds)) {
                return eastl::unexpected(WorldError::occupied);
            }
        }
        if (repeated) { continue; }
        for (const auto& grid : state_.grids) {
            for (auto region : grid.regions) {
                if (PowerOverlaps(region, building.bounds)) { return eastl::unexpected(WorldError::occupied); }
            }
        }
        if (additions >= UINT64_MAX - state_.nextGridId) {
            return eastl::unexpected(WorldError::capacity_exceeded);
        }
        ++additions;
    }
    return {};
}
WorldResult<PowerGridId> PowerSystem::RegisterBuilding(TileCoord origin, eastl::span<const PowerBounds> bounds) {
    if (!ValidPowerBounds({origin, origin}) || bounds.empty()) { return eastl::unexpected(WorldError::invalid_region); }
    for (const auto& binding : state_.generatedBindings) { if (binding.origin == origin) { return binding.grid; } }
    for (auto region : bounds) {
        if (!ValidPowerBounds(region)) { return eastl::unexpected(WorldError::invalid_region); }
        for (const auto& grid : state_.grids) {
            for (auto existing : grid.regions) {
                if (PowerOverlaps(region, existing)) { return eastl::unexpected(WorldError::occupied); }
            }
        }
    }
    if (state_.nextGridId == UINT64_MAX) { return eastl::unexpected(WorldError::capacity_exceeded); }
    SavedPowerGrid grid;
    grid.id = {state_.nextGridId++}; grid.mainsConnected = true;
    grid.regions.assign(bounds.begin(), bounds.end());
    const auto id = grid.id;
    state_.grids.push_back(eastl::move(grid));
    state_.generatedBindings.push_back({origin, id});
    Invalidate();
    return id;
}
void PowerSystem::SyncObject(ObjectHandle handle) {
    const auto* object = world_.GetObject(handle);
    if (!object) { return; }
    const auto* definition = world_.GetContent().TryGetObject(object->type);
    const auto view = world_.DescribeObjectState(handle);
    PowerDeviceKind kind;
    if (definition && definition->behavior == ObjectBehaviorKind::Generator) { kind = PowerDeviceKind::Generator; }
    else if (definition && definition->behavior == ObjectBehaviorKind::Solar) { kind = PowerDeviceKind::Solar; }
    else if (definition && definition->behavior == ObjectBehaviorKind::GridBattery) { kind = PowerDeviceKind::Battery; }
    else if (definition && definition->behavior == ObjectBehaviorKind::CraftingMachine && definition->machine.gridPowered) {
        kind = PowerDeviceKind::Machine;
    } else { UnregisterObject(object->id); return; }
    auto entry = eastl::lower_bound(state_.devices.begin(), state_.devices.end(), object->id,
        [](const SavedPowerDevice& device, PersistentId id) { return device.object < id; });
    if (entry == state_.devices.end() || entry->object != object->id) {
        entry = state_.devices.insert(entry, SavedPowerDevice{object->id, object->position, kind});
    }
    entry->position = object->position; entry->kind = kind;
    entry->workshopHold = object->enabled && !object->broken && view && view->runRequested && kind != PowerDeviceKind::Battery
        && (kind == PowerDeviceKind::Generator || kind == PowerDeviceKind::Solar || view->hasJob);
    Invalidate();
}
void PowerSystem::UnregisterObject(PersistentId object) {
    for (auto entry = state_.devices.begin(); entry != state_.devices.end(); ++entry) {
        if (entry->object == object) { state_.devices.erase(entry); Invalidate(); return; }
    }
}
void PowerSystem::Invalidate() {
    dirty_ = true;
    // Notification only: World must not reconcile or process power recursively.
    world_.PowerChanged();
}
bool PowerSystem::Mains(const SavedPowerGrid& grid) const {
    return grid.mainsConnected && world_.GetTick() < state_.mainsCutoffTick;
}
Tick PowerSystem::SliceLimit(Tick now) const {
    Tick result = PowerIntervalRemaining(now);
    if (now < state_.mainsCutoffTick) { result = eastl::min(result, state_.mainsCutoffTick - now); }
    return result;
}
bool PowerSystem::Battery(const SavedPowerDevice& device, Source& output) const {
    const auto handle = world_.FindObject(device.object);
    const auto* object = world_.GetObject(handle);
    if (!object || !object->enabled || object->broken || !world_.IsActive(object->position) || object->contents.size() != 1) { return false; }
    const auto* placed = world_.GetContent().TryGetObject(object->type);
    if (!placed || placed->behavior != ObjectBehaviorKind::GridBattery) { return false; }
    const auto item = object->contents.front();
    const auto* battery = world_.GetItem(item);
    if (!battery || battery->location.kind != LocationKind::object || battery->location.object != handle) { return false; }
    const auto& definition = world_.GetContent().GetItem(battery->type);
    if (definition.placedObject != object->type || !battery->contents.empty()
        || definition.energyStores.size() != 1) { return false; }
    const auto store = definition.energyStores.front().capability;
    const auto energy = world_.GetItemEnergy(item, store);
    if (!energy) { return false; }
    output.id = device.object; output.object = handle; output.battery = item;
    output.store = store; output.available = energy->energy;
    output.rechargeable = definition.energyStores.front().config.rechargeable;
    return true;
}
std::uint64_t PowerSystem::GeneratorAvailable(ObjectHandle handle) const {
    const auto* object = world_.GetObject(handle);
    const auto view = world_.DescribeObjectState(handle);
    if (!object || !object->enabled || object->broken || !world_.IsActive(object->position)
        || !view || !view->runRequested) { return 0; }
    const Tick now = world_.GetTick();
    if (view->generatorIntervalInitialized && view->generatorIntervalStart > PowerIntervalStart(now)) { return 0; }
    const auto allowance = view->generatorIntervalInitialized && view->generatorIntervalStart == PowerIntervalStart(now)
        ? view->generatorOutputRemaining : std::uint64_t{view->energyPerTick} * PowerIntervalRemaining(now);
    return eastl::min(view->energy, allowance);
}
std::uint64_t PowerSystem::SolarAvailable(ObjectHandle handle) const {
    const auto* object = world_.GetObject(handle);
    const auto view = world_.DescribeObjectState(handle);
    const auto now = world_.GetTick();
    if (!object || !object->enabled || object->broken || !world_.IsActive(object->position)
        || !view || view->behavior != ObjectBehaviorKind::Solar || !view->runRequested
        || !view->solarExposed || !IsSolarDaytime(now)) { return 0; }
    const auto interval = PowerIntervalStart(now);
    if (view->generatorIntervalInitialized && view->generatorIntervalStart > interval) { return 0; }
    return view->generatorIntervalInitialized && view->generatorIntervalStart == interval
        ? view->generatorOutputRemaining : SolarIntervalAllowance(now, view->energyPerTick);
}
void PowerSystem::GatherSources() const {
    sources_.clear();
    // Mains is handled by Plan; each finite source class retains persistent-ID order.
    for (auto kind : {PowerDeviceKind::Solar, PowerDeviceKind::Generator, PowerDeviceKind::Battery}) {
        for (const auto& device : state_.devices) {
            ++counters_.sourceScans;
            if (device.kind != kind) { continue; }
            const auto grid = GridAt(device.position);
            if (!grid) { continue; }
            Source source;
            source.kind = kind;
            if (kind == PowerDeviceKind::Generator || kind == PowerDeviceKind::Solar) {
                source.object = world_.FindObject(device.object); source.id = device.object;
                source.available = kind == PowerDeviceKind::Solar
                    ? SolarAvailable(source.object) : GeneratorAvailable(source.object);
            } else if (!Battery(device, source)) { continue; }
            source.grid = *grid;
            sources_.push_back(source);
        }
    }
}
PowerOutcome PowerSystem::Plan(eastl::span<const PowerDemand> demands) const {
    ++counters_.paymentAttempts;
    GatherSources();
    for (const auto& demand : demands) {
        if (!demand.energy) { continue; }
        const auto id = GridAt(demand.position);
        if (!id) { return PowerOutcome::NoGrid; }
        if (Mains(*Grid(*id))) { continue; }
        auto remaining = demand.energy;
        for (auto& source : sources_) {
            if (source.grid != *id) { continue; }
            const auto amount = eastl::min(remaining, source.available - source.spent);
            source.spent += amount; remaining -= amount;
            if (!remaining) { break; }
        }
        if (remaining) { return PowerOutcome::InsufficientPower; }
    }
    return PowerOutcome::Supplied;
}
PowerOutcome PowerSystem::CanSupply(eastl::span<const PowerDemand> demands) const { return Plan(demands); }
void PowerSystem::CommitPlan() {
    for (const auto source : sources_) {
        if (!source.spent) { continue; }
        if (source.kind == PowerDeviceKind::Solar) {
            Require(world_.PrepareSolarInterval(source.object, world_.GetTick()).has_value());
            Require(world_.SpendSolarEnergy(source.object, source.spent).has_value());
        } else if (source.kind == PowerDeviceKind::Generator) {
            Require(world_.PrepareGeneratorInterval(source.object, world_.GetTick()).has_value());
            Require(world_.SpendGeneratorEnergy(source.object, source.spent).has_value());
        } else {
            Require(world_.SetItemEnergy(source.battery, source.store,
                static_cast<std::uint32_t>(source.available - source.spent)).has_value());
        }
    }
}
PowerOutcome PowerSystem::TrySupply(eastl::span<const PowerDemand> demands) {
    const auto outcome = Plan(demands);
    if (outcome == PowerOutcome::Supplied) {
        world_.BeginCraftingMutation();
        CommitPlan();
        world_.EndCraftingMutation();
    }
    return outcome;
}
bool PowerSystem::HasWork() const {
    for (const auto& device : state_.devices) {
        if (device.workshopHold
            && (device.kind != PowerDeviceKind::Solar || IsSolarDaytime(world_.GetTick()))) { return true; }
        // Mains charging also runs when a player loads an idle installed battery.
        if (device.kind == PowerDeviceKind::Battery && world_.IsActive(device.position)) {
            const auto grid = GridAt(device.position);
            Source battery;
            if (grid && Mains(*Grid(*grid)) && Battery(device, battery) && battery.rechargeable) {
                const auto energy = world_.GetItemEnergy(battery.battery, battery.store);
                if (energy && energy->energy < energy->capacity) { return true; }
            }
        }
    }
    return false;
}
bool PowerSystem::HasSolarHold() const {
    for (const auto& device : state_.devices) {
        if (device.kind == PowerDeviceKind::Solar && device.workshopHold) { return true; }
    }
    return false;
}
eastl::optional<Tick> PowerSystem::NextEventIn() const {
    if (dirty_) { return Tick{0}; }
    const auto now = world_.GetTick();
    if (!HasWork()) {
        if (!HasSolarHold()) { return eastl::nullopt; }
        const auto dawn = TicksUntilSolarDawn(now);
        return dawn <= UINT64_MAX - now ? eastl::optional<Tick>{dawn} : eastl::nullopt;
    }
    if (!eventProcessed_ || PowerIntervalStart(lastEventTick_) != PowerIntervalStart(now)) { return Tick{0}; }
    const auto delay = SliceLimit(now);
    return delay ? eastl::optional<Tick>{delay} : eastl::nullopt;
}
bool PowerSystem::ProcessEvents() {
    Require(!processing_);
    const auto event = NextEventIn();
    if (!event || *event != 0) { return false; }
    processing_ = true;
    world_.BeginCraftingMutation();
    ++counters_.eventPasses;
    const auto now = world_.GetTick();
    const auto interval = PowerIntervalStart(now);
    // Prepare before any paid slices. Hooks may SyncObject, but must not change topology.
    for (std::size_t i = 0; i < state_.devices.size(); ++i) {
        const auto device = state_.devices[i];
        if (device.kind != PowerDeviceKind::Generator && device.kind != PowerDeviceKind::Solar) { continue; }
        const auto handle = world_.FindObject(device.object);
        const auto* object = world_.GetObject(handle);
        const auto view = world_.DescribeObjectState(handle);
        if (object && object->enabled && !object->broken && world_.IsActive(object->position) && view && view->runRequested) {
            if (device.kind == PowerDeviceKind::Solar) {
                Require(world_.PrepareSolarInterval(handle, now).has_value());
            } else {
                Require(world_.PrepareGeneratorInterval(handle, now).has_value());
            }
        }
    }
    bool changed = false;
    for (auto& grid : state_.grids) {
        contenders_.clear();
        for (const auto& device : state_.devices) {
            if (device.kind != PowerDeviceKind::Machine || GridAt(device.position) != eastl::optional<PowerGridId>{grid.id}) { continue; }
            const auto handle = world_.FindObject(device.object);
            const auto* object = world_.GetObject(handle);
            const auto view = world_.DescribeObjectState(handle);
            if (object && object->enabled && !object->broken && world_.IsActive(object->position)
                && view && view->runRequested && view->hasJob && view->remainingWork) { contenders_.push_back(device.object); }
        }
        if (contenders_.empty()) { continue; }
        std::size_t first = 0;
        if (!grid.intervalProcessed || grid.processedInterval != interval) {
            const auto next = eastl::upper_bound(contenders_.begin(), contenders_.end(), grid.fairnessCursor);
            first = next == contenders_.end() ? 0 : static_cast<std::size_t>(next - contenders_.begin());
            grid.fairnessCursor = contenders_[first]; grid.processedInterval = interval; grid.intervalProcessed = true;
        } else {
            const auto current = eastl::lower_bound(contenders_.begin(), contenders_.end(), grid.fairnessCursor);
            first = current == contenders_.end() ? 0 : static_cast<std::size_t>(current - contenders_.begin());
        }
        for (std::size_t offset = 0; offset < contenders_.size(); ++offset) {
            const auto handle = world_.FindObject(contenders_[(first + offset) % contenders_.size()]);
            const auto view = world_.DescribeObjectState(handle);
            if (!view || view->fundedTicks) { continue; }
            const Tick ticks = eastl::min(view->remainingWork, SliceLimit(now));
            if (!ticks) { continue; }
            const auto* object = world_.GetObject(handle);
            Require(object != nullptr);
            const PowerDemand demand{object->position, ticks * view->energyPerTick};
            if (TrySupply({&demand, 1}) == PowerOutcome::Supplied) {
                Require(world_.FundGridMachine(handle, ticks).has_value());
                ++counters_.fundedMachineSlices; changed = true;
            } else { ++counters_.failedMachineSlices; }
        }
    }
    const auto chargedBefore = counters_.chargedEnergy;
    ChargeSurplus();
    changed |= chargedBefore != counters_.chargedEnergy;
    lastEventTick_ = now; eventProcessed_ = true; dirty_ = false; processing_ = false;
    world_.EndCraftingMutation();
    return changed;
}
void PowerSystem::ChargeSurplus() {
    const auto interval = PowerIntervalStart(world_.GetTick());
    for (auto& grid : state_.grids) {
        const auto charge = [&](ObjectHandle source, PowerDeviceKind kind, std::uint64_t available) {
            for (std::size_t batteryIndex = 0; batteryIndex < state_.devices.size() && available; ++batteryIndex) {
                const auto device = state_.devices[batteryIndex];
                if (device.kind != PowerDeviceKind::Battery || GridAt(device.position) != eastl::optional<PowerGridId>{grid.id}) { continue; }
                Source battery;
                if (!Battery(device, battery) || !battery.rechargeable) { continue; }
                const auto energy = world_.GetItemEnergy(battery.battery, battery.store);
                Require(energy.has_value());
                const auto amount = eastl::min<std::uint64_t>(available, energy->capacity - energy->energy);
                if (!amount) { continue; }
                if (source.value) {
                    if (kind == PowerDeviceKind::Solar) {
                        Require(world_.PrepareSolarInterval(source, world_.GetTick()).has_value());
                        Require(world_.SpendSolarEnergy(source, amount).has_value());
                    } else {
                        Require(world_.PrepareGeneratorInterval(source, world_.GetTick()).has_value());
                        Require(world_.SpendGeneratorEnergy(source, amount).has_value());
                    }
                }
                Require(world_.SetItemEnergy(battery.battery, battery.store,
                    energy->energy + static_cast<std::uint32_t>(amount)).has_value());
                available -= amount;
                counters_.chargedEnergy = SaturatingAdd(counters_.chargedEnergy, amount);
            }
        };
        if (Mains(grid)) {
            if (!grid.mainsChargeProcessed || grid.mainsChargeInterval != interval) {
                grid.mainsChargeInterval = interval; grid.mainsChargeProcessed = true;
                charge({}, PowerDeviceKind::Battery, UINT64_MAX);
            }
            continue;
        }
        // Priority applies to charging too; an older fueled source cannot preempt solar.
        for (const auto kind : {PowerDeviceKind::Solar, PowerDeviceKind::Generator}) {
            for (std::size_t sourceIndex = 0; sourceIndex < state_.devices.size(); ++sourceIndex) {
                const auto device = state_.devices[sourceIndex];
                if (device.kind != kind
                    || GridAt(device.position) != eastl::optional<PowerGridId>{grid.id}
                    || (device.chargeProcessed && device.chargeInterval == interval)) { continue; }
                const auto source = world_.FindObject(device.object);
                const auto available = kind == PowerDeviceKind::Solar
                    ? SolarAvailable(source) : GeneratorAvailable(source);
                if (!available) { continue; }
                state_.devices[sourceIndex].chargeInterval = interval;
                state_.devices[sourceIndex].chargeProcessed = true;
                charge(source, kind, available);
            }
        }
    }
}
SavedPowerState PowerSystem::Capture() const { return state_; }
bool PowerSystem::IsPristine() const {
    return state_.nextGridId == 1 && state_.grids.empty() && state_.devices.empty()
        && state_.generatedBindings.empty() && state_.settings == PowerSettings{};
}
bool PowerSystem::CheckState(Tick now, PersistentId nextObjectId) const {
    if (!ValidPowerState(state_, now) || !nextObjectId) { return false; }
    for (const auto& grid : state_.grids) { if (grid.fairnessCursor >= nextObjectId) { return false; } }
    for (const auto& device : state_.devices) { if (device.object >= nextObjectId) { return false; } }
    return true;
}
eastl::span<const SavedPowerDevice> PowerSystem::Devices() const { return {state_.devices.data(), state_.devices.size()}; }
eastl::optional<SavedPowerDevice> PowerSystem::FindDevice(PersistentId id) const {
    const auto entry = eastl::lower_bound(state_.devices.begin(), state_.devices.end(), id,
        [](const SavedPowerDevice& device, PersistentId value) { return device.object < value; });
    if (entry == state_.devices.end() || entry->object != id) { return eastl::nullopt; }
    return *entry;
}
WorldResult<void> PowerSystem::Restore(const SavedPowerState& state, Tick now) {
    if (!ValidPowerState(state, now)) { return eastl::unexpected(WorldError::invalid_request); }
    Require(!processing_);
    state_ = state; state_.version = 2; sources_.clear(); contenders_.clear();
    lastEventTick_ = now; eventProcessed_ = true;
    Invalidate();
    return {};
}
void PowerSystem::CollectConsumerDemand(eastl::span<const PowerBounds> areas, Vector<ColumnCoord>& output) const {
    output.clear();
    for (const auto& device : state_.devices) {
        if (device.kind == PowerDeviceKind::Machine) { continue; }
        const auto grid = GridAt(device.position);
        if (!grid) { continue; }
        const auto* coverage = Grid(*grid);
        const bool needed = eastl::any_of(coverage->regions.begin(), coverage->regions.end(), [&](PowerBounds region) {
            return eastl::any_of(areas.begin(), areas.end(), [&](PowerBounds area) { return PowerOverlaps(region, area); });
        });
        if (!needed) { continue; }
        const auto chunk = ChunkOf(device.position);
        const ColumnCoord column{chunk.x, chunk.z};
        const auto entry = eastl::lower_bound(output.begin(), output.end(), column, ColumnLess);
        if (entry == output.end() || *entry != column) { output.insert(entry, column); }
    }
}
void PowerSystem::CollectWorkshopDebug(Vector<PowerColumnDebug>& output) const {
    output.clear();
    const auto add = [&output](TileCoord position, bool owner) {
        const auto chunk = ChunkOf(position);
        const ColumnCoord column{chunk.x, chunk.z};
        auto entry = eastl::lower_bound(output.begin(), output.end(), column,
            [](const PowerColumnDebug& a, ColumnCoord b) { return ColumnLess(a.column, b); });
        if (entry == output.end() || entry->column != column) { entry = output.insert(entry, PowerColumnDebug{column}); }
        if (owner) { ++entry->ownerHolds; } else { ++entry->sourceDependencies; }
    };
    for (const auto& owner : state_.devices) {
        if (!owner.workshopHold) { continue; }
        add(owner.position, true);
        const auto grid = GridAt(owner.position);
        if (!grid) { continue; }
        for (const auto& dependency : state_.devices) {
            if (dependency.object == owner.object || dependency.kind == PowerDeviceKind::Machine) { continue; }
            if (GridAt(dependency.position) == grid) { add(dependency.position, false); }
        }
    }
}
void PowerSystem::CollectWorkshopDemand(Vector<ColumnCoord>& output) const {
    output.clear();
    const auto add = [&output](TileCoord position) {
        const auto chunk = ChunkOf(position);
        const ColumnCoord column{chunk.x, chunk.z};
        const auto entry = eastl::lower_bound(output.begin(), output.end(), column, ColumnLess);
        if (entry == output.end() || *entry != column) { output.insert(entry, column); }
    };
    for (const auto& owner : state_.devices) {
        if (!owner.workshopHold) { continue; }
        add(owner.position);
        const auto grid = GridAt(owner.position);
        if (!grid) { continue; }
        for (const auto& dependency : state_.devices) {
            if (dependency.kind != PowerDeviceKind::Machine && GridAt(dependency.position) == grid) { add(dependency.position); }
        }
    }
}
void PowerSystem::Inspect(TileCoord position, PowerInspection& output) const {
    output = {};
    output.grid = GridAt(position);
    if (!output.grid) { return; }
    output.mainsAvailable = Mains(*Grid(*output.grid));
    for (const auto& device : state_.devices) {
        if (GridAt(device.position) != output.grid) { continue; }
        const auto handle = world_.FindObject(device.object);
        const auto* object = world_.GetObject(handle);
        if (!object || !world_.IsActive(device.position)) { ++output.unloadedDevices; continue; }
        if (device.kind == PowerDeviceKind::Battery) {
            Source source;
            if (!Battery(device, source)) { continue; }
            const auto energy = world_.GetItemEnergy(source.battery, source.store);
            ++output.batteries;
            output.batteryEnergy = SaturatingAdd(output.batteryEnergy, energy->energy);
            output.batteryCapacity = SaturatingAdd(output.batteryCapacity, energy->capacity);
        } else {
            const auto view = world_.DescribeObjectState(handle);
            if (!view || !view->runRequested) { continue; }
            if (device.kind == PowerDeviceKind::Solar) {
                if (object->enabled && !object->broken) { ++output.runningSolar; }
                output.solarOutputAvailable = SaturatingAdd(output.solarOutputAvailable, SolarAvailable(handle));
            } else if (device.kind == PowerDeviceKind::Generator) {
                if (object->enabled && !object->broken) { ++output.runningGenerators; }
                output.generatorOutputAvailable = SaturatingAdd(output.generatorOutputAvailable, GeneratorAvailable(handle));
            } else if (view->hasJob && view->remainingWork) {
                if (object->enabled && !object->broken && view->fundedTicks) { ++output.runningMachines; }
                else { ++output.waitingMachines; }
            }
        }
    }
}
void PowerSystem::CollectDebugGrids(Vector<PowerGridDebug>& output) const {
    output.clear();
    for (const auto& grid : state_.grids) {
        PowerGridDebug debug;
        debug.id = grid.id; debug.regions = grid.regions; debug.fairnessCursor = grid.fairnessCursor;
        debug.mainsCutoffTick = state_.mainsCutoffTick;
        Inspect(grid.regions.front().min, debug.inspection);
        for (const auto& device : state_.devices) {
            if (GridAt(device.position) != eastl::optional<PowerGridId>{grid.id}) { continue; }
            ++debug.registeredDevices;
            if (device.workshopHold) { ++debug.workshopHolds; }
        }
        output.push_back(eastl::move(debug));
    }
}
PowerCounters PowerSystem::Counters() const { return counters_; }
void PowerSystem::ResetCounters() { counters_ = {}; }
}
