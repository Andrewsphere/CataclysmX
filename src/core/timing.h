#pragma once

#include <cstdint>

namespace cataclysmx {

using Tick = std::uint64_t;
inline constexpr std::uint32_t ticks_per_turn = 100;

// Simulation units are fixed. This rate only maps ticks to real time.
struct PacingSettings {
    std::uint32_t ticksPerSecond = 100;
    bool rateLimited = false;
    bool pauseWhenIdle = true;
    bool operator==(const PacingSettings&) const = default;
};

// Both inputs must be positive. A lower speed value means shorter actions.
constexpr Tick ActionTicks(std::uint32_t baseCost, std::uint32_t speed) {
    const Tick product = static_cast<Tick>(baseCost) * speed;
    return product / ticks_per_turn + (product % ticks_per_turn != 0 ? 1 : 0);
}

}
