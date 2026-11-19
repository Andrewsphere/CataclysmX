#pragma once

#include "core/world_types.h"
#include "content/object_definition.h"

namespace cataclysmx {

enum class StationActionOutcome : std::uint8_t {
    Unavailable, Placed, Packed, OutOfReach, Occupied, InvalidEquipment,
    StationBusy, NotEmpty, Broken, FixedFixture
};
struct StationActionResult {
    StationActionOutcome outcome = StationActionOutcome::Unavailable;
    Tick cost = 0;
};
const char* StationActionMessage(StationActionOutcome outcome);

struct StationView {
    PersistentId id = 0;
    ObjectTypeId type{};
    TileCoord position{};
    bool enabled = false;
    bool broken = false;
    PersistentId worker = 0;
    StationActionOutcome packing = StationActionOutcome::Unavailable;
    bool operator==(const StationView&) const = default;
};
}
