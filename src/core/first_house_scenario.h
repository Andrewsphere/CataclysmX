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

// Pure bounded plan query over the 3x3 planning-patch neighborhood of origin.
// Searches small settlements for the named furnished_house. Failure
// is InvalidData, with optional diagnostic context; no World/residency mutation.
ContentResult<FirstHouseStart> FindFirstHouseStart(const ContentCatalog& content,
    std::uint32_t seed, Diagnostics* diagnostics = nullptr);

// Ranks cheap small-settlement descriptors in the origin 3x3 planning patches
// by nearest-bound distance, then patch x,z and settlement ordinal; builds full
// plans lazily until the nearest usable settlement is found. Selects residential
// edge houses (no building behind them) with supported driveway/street retreat.
// Custom catalogs with no residential patterns may use populated Mixed patterns.
// Within that settlement select the usable entrance nearest horizontal origin;
// ties: entrance x,z, owner patch x,z, settlement, parcel. playerPosition is the adjacent
// exterior driveway tile: passable supported non-stair road, free of generated
// placements. Entrance terrain must be passable or openable to passable terrain,
// and not open space. Returns InvalidData if validation fails or none qualifies,
// with optional diagnostics. No World/residency mutation or simulation RNG use.
ContentResult<BuildingStart> FindTownStart(const ContentCatalog& content,
    std::uint32_t seed, Diagnostics* diagnostics = nullptr);

}
