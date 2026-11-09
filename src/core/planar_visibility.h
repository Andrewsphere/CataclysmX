#pragma once

#include <EASTL/span.h>
#include <cstdint>

namespace cataclysmx {

// Row-major flags: bit 0 resident, bit 1 opaque. Missing cells block without
// becoming visible. Output is caller-owned width*depth bytes, overwritten 0/1.
// Integer symmetric shadowcasting; see doc/bulk_perception.md for corner policy.
void BuildPlanarVisibility(eastl::span<const std::uint8_t> optics, int width, int depth,
    int originX, int originZ, int radius, eastl::span<std::uint8_t> visible,
    std::uint64_t* visitedCells = nullptr);

}
