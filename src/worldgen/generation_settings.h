#pragma once

#include "core/world_types.h"

#include <cstdint>

namespace cataclysmx {

inline constexpr std::uint32_t world_generation_version = 8;
inline constexpr int maxStartSafetyRadius = 64;

enum class PopulationMode : std::uint8_t { Urban, Authored };

// New-world policy, installed before materialization and saved with the world.
// Safety only filters initial hostile placements, never existing/restored actors.
struct GenerationSettings {
    PopulationMode population = PopulationMode::Urban;
    TileCoord start{};
    int safetyRadius = 0;
    bool operator==(const GenerationSettings&) const = default;
};

}
