#pragma once

#include "core/perception_types.h"
#include <EASTL/array.h>

namespace cataclysmx {

// Normalized RGB output is dot(float4(rgb, 1), each row); alpha is unchanged.
struct PerceptionTransform {
    eastl::array<float, 4> red{1, 0, 0, 0};
    eastl::array<float, 4> green{0, 1, 0, 0};
    eastl::array<float, 4> blue{0, 0, 1, 0};
    bool operator==(const PerceptionTransform&) const = default;
};

RgbColor PerceptionColor(RgbColor color, Visibility visibility, LightBand light, unsigned depth, bool background);
PerceptionTransform MakePerceptionTransform(Visibility visibility, LightBand light, unsigned depth, bool background = false);

}
