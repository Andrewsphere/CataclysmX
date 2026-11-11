#pragma once

#include <cstdint>
#include <cstdlib>
#include <limits>

namespace cataclysmx {

using Tick = std::uint64_t;
inline constexpr std::uint32_t ticks_per_turn = 100;

// Absolute simulation time never wraps. Exhausting the clock is a broken
// invariant, including a deadline that cannot be represented at the current tick.
inline Tick CheckedTickAdd(Tick tick, Tick duration) {
    if (duration > std::numeric_limits<Tick>::max() - tick) { std::abort(); }
    return tick + duration;
}

struct ActionDeadline {
    Tick dueTick = 0;
    Tick suspendedTicks = 0;
    bool suspended = true;

    Tick Remaining(Tick now) const {
        return suspended ? suspendedTicks : dueTick > now ? dueTick - now : 0;
    }
    void Set(Tick now, Tick duration) {
        if (suspended) { suspendedTicks = duration; }
        else { dueTick = CheckedTickAdd(now, duration); }
    }
    void SetRunning(Tick now, bool running) {
        if (running == !suspended) { return; }
        if (running) { dueTick = CheckedTickAdd(now, suspendedTicks); }
        else { suspendedTicks = Remaining(now); }
        suspended = !running;
    }
};

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
