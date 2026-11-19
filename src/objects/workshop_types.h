#pragma once

#include "core/world_types.h"
#include "content/object_definition.h"

namespace cataclysmx {

enum class StationActionOutcome : std::uint8_t {
    Unavailable, Placed, Packed, OutOfReach, Occupied, InvalidEquipment,
    StationBusy, NotEmpty, Broken, FixedFixture,
    FuelLoaded, Ignited, Extinguished, FuelDiscarded, ColdForge, InsufficientFuel,
    NoCapacity, InvalidFuel, Disabled, AlreadyReady, AlreadyCold, MustExtinguish, HasFuel
};
struct StationActionResult {
    StationActionOutcome outcome = StationActionOutcome::Unavailable;
    Tick cost = 0;
};
const char* StationActionMessage(StationActionOutcome outcome);

enum class ForgeAction : std::uint8_t { LoadFuel, Ignite, Extinguish, DiscardFuel };
struct ForgeState {
    std::uint64_t fuel = 0;
    bool ready = false;
    bool operator==(const ForgeState&) const = default;
};

struct StationView {
    PersistentId id = 0;
    ObjectTypeId type{};
    TileCoord position{};
    bool enabled = false;
    bool broken = false;
    PersistentId worker = 0;
    StationActionOutcome packing = StationActionOutcome::Unavailable;
    ForgeState forge;
    bool operator==(const StationView&) const = default;
};
}
