#pragma once

#include "content/content_result.h"
#include "core/diagnostics.h"
#include "core/world_types.h"

#include <cstdint>

namespace cataclysmx {

class ContentCatalog;

struct FirstHouseStart {
    TileCoord playerPosition{};
    TileCoord entrance{};
    bool operator==(const FirstHouseStart&) const = default;
};

// Same value layout for either scenario; preserve FirstHouseStart source usage.
using BuildingStart = FirstHouseStart;

// Pure bounded plan query over the 3x3 region neighborhood of origin. Failure
// is InvalidData, with optional diagnostic context; no World/residency mutation.
ContentResult<FirstHouseStart> FindFirstHouseStart(const ContentCatalog& content,
    std::uint32_t seed, Diagnostics* diagnostics = nullptr);

// Pure query over all nine origin-neighborhood region plans, independent of any
// named pattern. Eligible buildings have authored population in their common
// pattern or selected basement (furniture, loot, or monster markers).
// Select the usable entrance nearest horizontal origin by squared distance;
// ties: entrance x,z, owner region x,z, parcel. playerPosition is the adjacent
// exterior driveway tile: passable supported non-stair road, free of generated
// placements. Entrance terrain must be passable or openable to passable terrain,
// and not open space. Returns InvalidData if validation fails or none qualifies,
// with optional diagnostics. No World/residency mutation or simulation RNG use.
ContentResult<BuildingStart> FindTownStart(const ContentCatalog& content,
    std::uint32_t seed, Diagnostics* diagnostics = nullptr);

}
