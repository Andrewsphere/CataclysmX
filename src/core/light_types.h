#pragma once

#include <cstdint>

namespace cataclysmx {

inline constexpr int maxLightRange = 12;
inline constexpr int lightSpillDistance = 2;
inline constexpr int skySpillDistance = 12; // Compile-time tuning: 1..15; attenuation scales with this reach.
inline constexpr int playerSightRange = 64;
inline constexpr std::uint8_t defaultSkyLight = 192;

enum class LightBand : std::uint8_t { Dark, Dim, Ordinary, Bright };

struct LightEmission {
    std::uint8_t intensity = 0;
    std::uint8_t range = 0;
    bool operator==(const LightEmission&) const = default;
};

constexpr bool ValidLightEmission(LightEmission emission) {
    return emission.range <= maxLightRange
        && ((emission.intensity == 0) == (emission.range == 0));
}

constexpr LightBand ClassifyLight(std::uint8_t level) {
    if (level < 16) { return LightBand::Dark; }
    if (level < 64) { return LightBand::Dim; }
    if (level < 192) { return LightBand::Ordinary; }
    return LightBand::Bright;
}

const char* LightBandName(LightBand band);

}
