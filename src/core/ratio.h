#pragma once

#include <cstdint>
#include <cstdlib>
#include <limits>

namespace cataclysmx {

// Project convention for integer ratios, efficiencies, and probabilities:
// 10000 == 1.0, 1000 == 10%. Rates/multipliers may exceed one; unit ratios may not.
inline constexpr std::uint32_t ratioScale = 10000;

constexpr bool ValidUnitRatio(std::uint32_t ratio) { return ratio <= ratioScale; }

// Floor(value * ratio / ratioScale), without overflowing the intermediate
// product. An unrepresentable result is a broken invariant.
constexpr std::uint64_t ScaleRatioFloor(std::uint64_t value, std::uint32_t ratio) {
    const auto whole = value / ratioScale;
    const auto fraction = (value % ratioScale) * ratio / ratioScale;
    if (ratio != 0 && whole > (std::numeric_limits<std::uint64_t>::max() - fraction) / ratio) {
        std::abort();
    }
    return whole * ratio + fraction;
}

}
