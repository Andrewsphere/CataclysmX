#pragma once

#include "core/containers.h"
#include "core/timing.h"
#include "core/world_types.h"
#include <EASTL/optional.h>

namespace cataclysmx {

inline constexpr Tick powerQuantumTicks = ticks_per_turn;
inline constexpr Tick powerDayTicks = Tick{ticks_per_turn} * 60 * 60 * 24;
inline constexpr int maxPowerWiringExtent = 64;

struct PowerGridId {
    std::uint64_t value = 0;
    bool operator==(const PowerGridId&) const = default;
};
struct PowerBounds {
    TileCoord min{};
    TileCoord max{}; // Inclusive, aggregate coordinate order X/Z/Y.
    bool operator==(const PowerBounds&) const = default;
};
struct BuildingPowerRegistration {
    TileCoord origin{};
    PowerBounds bounds{};
};
bool ValidPowerBounds(PowerBounds bounds);
bool PowerContains(PowerBounds bounds, TileCoord position);
bool PowerOverlaps(PowerBounds a, PowerBounds b);
bool PowerConnects(PowerBounds a, PowerBounds b); // Overlap or shared tile face, never edge/corner.
Tick PowerIntervalStart(Tick now);
Tick PowerIntervalRemaining(Tick now);
Tick SeededMainsCutoff(std::uint32_t seed);
// Tick zero is 08:00; daylight rises linearly from 06:00 to noon and falls to 18:00.
std::uint32_t SolarDaylightRatio(Tick now);
bool IsSolarDaytime(Tick now);
// Integrated, integer-rounded energy over the remaining absolute power interval.
std::uint64_t SolarIntervalAllowance(Tick now, std::uint32_t peakOutputPerTick);
// Positive delay to the next dawn; callers separately decide whether it is needed.
Tick TicksUntilSolarDawn(Tick now);

struct PowerSettings {
    int maxWiringWidth = 16;
    int maxWiringDepth = 16;
    bool operator==(const PowerSettings&) const = default;
};
bool ValidPowerSettings(PowerSettings settings);
enum class PowerDeviceKind : std::uint8_t { Battery, Generator, Machine, Solar };
enum class PowerOutcome : std::uint8_t { Supplied, NoGrid, InsufficientPower };
struct PowerDemand {
    TileCoord position{};
    std::uint64_t energy = 0;
};
struct WiringQuote {
    std::uint32_t newTiles = 0;
    std::uint32_t wiringUnits = 0;
    std::uint32_t mergedGrids = 0; // Number of existing grids joined by the added region.
};

enum class PowerActionOutcome : std::uint8_t {
    Succeeded, InvalidArea, OutOfReach, MissingMaterials, InvalidEquipment,
    Occupied, Unavailable, NoNewCoverage
};
struct PowerActionResult {
    PowerActionOutcome outcome = PowerActionOutcome::Unavailable;
    std::uint32_t baseCost = 0;
};
const char* PowerActionMessage(PowerActionOutcome outcome);

// Full-height column residency, with independent demand reasons. Counts of
// overlapping reasons are not additive totals of resident columns.
struct ChunkResidencyDebug {
    ColumnCoord column{};
    bool active = false;
    std::uint32_t playerActiveRequests = 0;
    std::uint32_t playerResidentRequests = 0;
    std::uint32_t otherActiveRequests = 0;
    std::uint32_t otherResidentRequests = 0;
    std::uint32_t machineOwners = 0;
    std::uint32_t machineDependencies = 0;
};
struct ChunkResidencyCounters {
    std::uint32_t loadedColumns = 0;
    std::uint32_t activeColumns = 0;
    std::uint32_t playerColumns = 0;
    std::uint32_t machineColumns = 0;
    std::uint32_t machineOnlyColumns = 0;
    std::uint32_t sharedColumns = 0;
    std::uint64_t loadedChunks = 0;
    std::uint64_t archivedChunks = 0;
};

// Stable persistence DTOs; no runtime handles, fuel, or battery charge copies.
struct SavedPowerGrid {
    PowerGridId id;
    Vector<PowerBounds> regions;
    bool mainsConnected = false;
    PersistentId fairnessCursor = 0; // First contender of the last rotating batch.
    Tick processedInterval = 0;
    bool intervalProcessed = false;
    Tick mainsChargeInterval = 0;
    bool mainsChargeProcessed = false;
    bool operator==(const SavedPowerGrid&) const = default;
};
struct SavedPowerDevice {
    PersistentId object = 0;
    TileCoord position{};
    PowerDeviceKind kind = PowerDeviceKind::Battery;
    bool workshopHold = false;
    Tick chargeInterval = 0;
    bool chargeProcessed = false;
    bool operator==(const SavedPowerDevice&) const = default;
};
struct SavedBuildingGridBinding {
    TileCoord origin{};
    PowerGridId grid;
    bool operator==(const SavedBuildingGridBinding&) const = default;
};
struct SavedPowerState {
    std::uint32_t version = 2;
    std::uint64_t nextGridId = 1;
    Tick mainsCutoffTick = SeededMainsCutoff(42);
    PowerSettings settings;
    Vector<SavedPowerGrid> grids;
    Vector<SavedPowerDevice> devices;
    Vector<SavedBuildingGridBinding> generatedBindings;
    bool operator==(const SavedPowerState&) const = default;
};
bool ValidPowerState(const SavedPowerState& state, Tick now);

struct PowerInspection {
    eastl::optional<PowerGridId> grid;
    bool mainsAvailable = false;
    std::uint64_t batteryEnergy = 0;
    std::uint64_t batteryCapacity = 0;
    std::uint64_t generatorOutputAvailable = 0;
    std::uint32_t batteries = 0;
    std::uint32_t runningGenerators = 0;
    std::uint32_t runningMachines = 0;
    std::uint32_t waitingMachines = 0;
    std::uint32_t unloadedDevices = 0;
    std::uint64_t solarOutputAvailable = 0;
    std::uint32_t runningSolar = 0;
};
struct PowerGridDebug {
    PowerGridId id;
    Vector<PowerBounds> regions;
    PowerInspection inspection;
    PersistentId fairnessCursor = 0;
    std::uint32_t registeredDevices = 0;
    std::uint32_t workshopHolds = 0;
    Tick mainsCutoffTick = 0; // DEBUG only; never included in player inspection.
};
struct PowerColumnDebug {
    ColumnCoord column{};
    std::uint32_t ownerHolds = 0;
    std::uint32_t sourceDependencies = 0;
};
struct PowerCounters {
    std::uint64_t eventPasses = 0;
    std::uint64_t sourceScans = 0;
    std::uint64_t paymentAttempts = 0;
    std::uint64_t fundedMachineSlices = 0;
    std::uint64_t failedMachineSlices = 0;
    std::uint64_t chargedEnergy = 0;
};

}
