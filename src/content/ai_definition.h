#pragma once

#include "core/containers.h"

#include <cstdint>

namespace cataclysmx {

struct AiProfileId {
    std::uint32_t value = 0;
    bool operator==(const AiProfileId&) const = default;
};

struct AiProfile {
    String id;
    std::uint32_t sightRange = 12;
    std::uint32_t dimRange = 6;
    std::uint32_t darkRange = 1;
    std::uint32_t scanIntervalTicks = 100;
    std::uint32_t memoryTicks = 1200;
    std::uint32_t searchTicks = 300;
    std::uint32_t candidateBudget = 8;
    std::uint32_t bucketBudget = 16;
    std::uint32_t switchAdvantage = 2;
    bool operator==(const AiProfile&) const = default;
};

bool ValidAiProfile(const AiProfile& profile);

}
