#pragma once

#include "core/world_types.h"

#include <cstdint>

namespace cataclysmx {

class World;

inline constexpr int maxAiSightRange = 32;

struct AiVisionStats {
    std::uint32_t rays = 0;
    std::uint32_t cells = 0;
};

// Allocation-free center-to-center 3D geometry only; actors do not occlude.
// Invalid, unavailable, opaque endpoints and Chebyshev deltas above the bound
// fail. Reads resident geometry without acquiring residency or updating player FOV.
// Floors are zero-thickness lower faces of upper tiles, not opaque tile volumes.
// A vertical face transmits iff the upper tile is openSpace OR both tiles are
// stairs. Conservative supercover includes cells/floor faces touched at ties.
// Optional counters accumulate calls (including rejected rays) and tile reads
// (including missing tiles); cached cells and face checks add no tile reads.
bool AiLineVisible(const World& world, TileCoord origin, TileCoord target,
    AiVisionStats* stats = nullptr);

}
