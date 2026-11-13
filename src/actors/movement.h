#pragma once

#include "core/world_types.h"
#include <EASTL/array.h>

namespace cataclysmx {

// Eight horizontal neighbors, then up/down. Stable indices also identify the
// candidates in an AI decision snapshot. Vertical steps still require stairs.
inline constexpr eastl::array<TileCoord, 10> actorStepDirections{{
    {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1},
    {0, 0, 1}, {0, 0, -1}}};

// Apply horizontal diagonal distance after locomotion/injury scaling and before
// speed conversion. Positive base cost and a legal step are preconditions.
std::uint32_t ActorStepBaseCost(std::uint32_t baseCost, TileCoord direction);

}
