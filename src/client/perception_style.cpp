#include "client/perception_style.h"

#include <algorithm>

namespace cataclysmx {
namespace {

constexpr eastl::array<unsigned, 3> DepthColor{48, 96, 192};
constexpr eastl::array<unsigned, 3> LuminanceWeights{30, 59, 11};
constexpr eastl::array<unsigned, 3> MemoryOffsets{40, 30, 20};
constexpr eastl::array<unsigned, 3> MemoryScales{55, 43, 28};
constexpr unsigned DimPercent = 55;
constexpr unsigned DarkDivisor = 4;
constexpr unsigned DarkForeground = 24;

unsigned DepthStrength(unsigned depth, bool background, bool memory) {
    const unsigned percent = depth == 0 ? 0 : std::min(70U, 15U + std::min(depth, 20U) * 10U);
    return (background ? percent / 2 : percent) / (memory ? 4 : 1);
}

}

RgbColor PerceptionColor(RgbColor color, Visibility visibility, LightBand light, unsigned depth, bool background) {
    const bool memory = visibility == Visibility::Remembered;
    eastl::array<unsigned, 3> channels{color.red, color.green, color.blue};
    if (memory) {
        const unsigned value = (channels[0] * LuminanceWeights[0] + channels[1] * LuminanceWeights[1]
            + channels[2] * LuminanceWeights[2]) / 100;
        for (unsigned i = 0; i < 3; ++i) { channels[i] = MemoryOffsets[i] + value * MemoryScales[i] / 100; }
    } else {
        for (auto& value : channels) {
            switch (light) {
            case LightBand::Dark: value = (background ? 0 : DarkForeground) + value / DarkDivisor; break;
            case LightBand::Dim: value = value * DimPercent / 100; break;
            case LightBand::Ordinary:
            case LightBand::Bright: break;
            }
        }
    }
    const unsigned strength = DepthStrength(depth, background, memory);
    for (unsigned i = 0; i < 3; ++i) {
        channels[i] = (channels[i] * (100 - strength) + DepthColor[i] * strength) / 100;
    }
    return {static_cast<unsigned char>(channels[0]), static_cast<unsigned char>(channels[1]),
        static_cast<unsigned char>(channels[2])};
}

PerceptionTransform MakePerceptionTransform(Visibility visibility, LightBand light, unsigned depth, bool background) {
    const bool memory = visibility == Visibility::Remembered;
    PerceptionTransform result;
    eastl::array<float, 4>* rows[] = {&result.red, &result.green, &result.blue};
    const float strength = float(DepthStrength(depth, background, memory)) / 100.0f;
    for (unsigned i = 0; i < 3; ++i) {
        auto& row = *rows[i];
        if (memory) {
            for (unsigned j = 0; j < 3; ++j) {
                row[j] = float(LuminanceWeights[j]) / 100.0f * float(MemoryScales[i]) / 100.0f;
            }
            row[3] = float(MemoryOffsets[i]) / 255.0f;
        } else {
            switch (light) {
            case LightBand::Dark:
                row[i] = 1.0f / DarkDivisor;
                row[3] = background ? 0.0f : float(DarkForeground) / 255.0f;
                break;
            case LightBand::Dim: row[i] = float(DimPercent) / 100.0f; break;
            case LightBand::Ordinary:
            case LightBand::Bright: break;
            }
        }
        for (auto& coefficient : row) { coefficient *= 1.0f - strength; }
        row[3] += float(DepthColor[i]) / 255.0f * strength;
    }
    return result;
}

}
