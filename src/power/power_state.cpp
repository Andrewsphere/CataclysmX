#include "power/power_types.h"
#include <EASTL/algorithm.h>
#include <limits>

namespace cataclysmx {
namespace {
bool PositionValid(TileCoord p) {
    return p.x >= -world_coordinate_limit && p.x <= world_coordinate_limit
        && p.z >= -world_coordinate_limit && p.z <= world_coordinate_limit
        && p.y >= world_min_y && p.y <= world_max_y;
}
bool Overlap(int a, int b, int c, int d) { return a <= d && c <= b; }
bool Adjacent(int a, int b, int c, int d) {
    return std::int64_t{b} + 1 == c || std::int64_t{d} + 1 == a;
}
}
bool ValidPowerBounds(PowerBounds b) {
    return PositionValid(b.min) && PositionValid(b.max)
        && b.min.x <= b.max.x && b.min.z <= b.max.z && b.min.y <= b.max.y;
}
bool PowerContains(PowerBounds b, TileCoord p) {
    return p.x >= b.min.x && p.x <= b.max.x && p.z >= b.min.z && p.z <= b.max.z
        && p.y >= b.min.y && p.y <= b.max.y;
}
bool PowerOverlaps(PowerBounds a, PowerBounds b) {
    return Overlap(a.min.x, a.max.x, b.min.x, b.max.x)
        && Overlap(a.min.z, a.max.z, b.min.z, b.max.z)
        && Overlap(a.min.y, a.max.y, b.min.y, b.max.y);
}
bool PowerConnects(PowerBounds a, PowerBounds b) {
    const bool x = Overlap(a.min.x, a.max.x, b.min.x, b.max.x);
    const bool z = Overlap(a.min.z, a.max.z, b.min.z, b.max.z);
    const bool y = Overlap(a.min.y, a.max.y, b.min.y, b.max.y);
    return (x && z && y) || (z && y && Adjacent(a.min.x, a.max.x, b.min.x, b.max.x))
        || (x && y && Adjacent(a.min.z, a.max.z, b.min.z, b.max.z))
        || (x && z && Adjacent(a.min.y, a.max.y, b.min.y, b.max.y));
}
Tick PowerIntervalStart(Tick now) { return now - now % powerQuantumTicks; }
Tick PowerIntervalRemaining(Tick now) {
    return eastl::min<Tick>(powerQuantumTicks - now % powerQuantumTicks,
        std::numeric_limits<Tick>::max() - now);
}
Tick SeededMainsCutoff(std::uint32_t seed) {
    // Dedicated integer mixing; does not consume simulation RNG or depend on discovery order.
    std::uint32_t value = seed ^ 0x706f7765U;
    value ^= value >> 16; value *= 0x7feb352dU;
    value ^= value >> 15; value *= 0x846ca68bU; value ^= value >> 16;
    return (7 + value % 8) * powerDayTicks;
}
bool ValidPowerSettings(PowerSettings s) {
    return s.maxWiringWidth >= 1 && s.maxWiringWidth <= maxPowerWiringExtent
        && s.maxWiringDepth >= 1 && s.maxWiringDepth <= maxPowerWiringExtent;
}
bool ValidPowerState(const SavedPowerState& s, Tick now) {
    if (s.version != 1 || !s.nextGridId || !ValidPowerSettings(s.settings)
        || s.mainsCutoffTick < 7 * powerDayTicks || s.mainsCutoffTick > 14 * powerDayTicks
        || s.mainsCutoffTick % powerDayTicks) { return false; }
    const auto validInterval = [now](Tick interval, bool initialized) {
        return initialized ? interval <= now && interval % powerQuantumTicks == 0 : interval == 0;
    };
    std::uint64_t previous = 0;
    for (std::size_t i = 0; i < s.grids.size(); ++i) {
        const auto& grid = s.grids[i];
        if (grid.id.value <= previous || grid.id.value >= s.nextGridId || grid.regions.empty()
            || (!grid.intervalProcessed && grid.fairnessCursor != 0)
            || (!grid.mainsConnected && grid.mainsChargeProcessed)
            || !validInterval(grid.processedInterval, grid.intervalProcessed)
            || !validInterval(grid.mainsChargeInterval, grid.mainsChargeProcessed)) { return false; }
        previous = grid.id.value;
        for (auto bounds : grid.regions) {
            if (!ValidPowerBounds(bounds)) { return false; }
            for (std::size_t j = 0; j < i; ++j) {
                for (auto other : s.grids[j].regions) { if (PowerOverlaps(bounds, other)) { return false; } }
            }
        }
    }
    previous = 0;
    for (const auto& device : s.devices) {
        if (device.object <= previous || !PositionValid(device.position)
            || (device.kind != PowerDeviceKind::Battery && device.kind != PowerDeviceKind::Generator
                && device.kind != PowerDeviceKind::Machine)
            || (device.kind == PowerDeviceKind::Battery && device.workshopHold)
            || (device.kind != PowerDeviceKind::Generator && device.chargeProcessed)
            || !validInterval(device.chargeInterval, device.chargeProcessed)) { return false; }
        previous = device.object;
    }
    for (std::size_t i = 0; i < s.generatedBindings.size(); ++i) {
        const auto& binding = s.generatedBindings[i];
        if (!PositionValid(binding.origin)) { return false; }
        bool found = false;
        for (const auto& grid : s.grids) { found |= grid.id == binding.grid && grid.mainsConnected; }
        if (!found) { return false; }
        for (std::size_t j = 0; j < i; ++j) {
            if (s.generatedBindings[j].origin == binding.origin) { return false; }
        }
    }
    return true;
}
}
