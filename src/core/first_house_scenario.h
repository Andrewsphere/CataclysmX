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

// Pure bounded plan query over the 3x3 region neighborhood of origin. Failure
// is InvalidData, with optional diagnostic context; no World/residency mutation.
ContentResult<FirstHouseStart> FindFirstHouseStart(const ContentCatalog& content,
    std::uint32_t seed, Diagnostics* diagnostics = nullptr);

}
