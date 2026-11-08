#include "core/pacing.h"

#include <EASTL/sort.h>
#include <EASTL/utility.h>

#include <cstdlib>
#include <limits>

namespace cataclysmx {
namespace {

constexpr std::uint64_t unitsPerTick = 1'000'000;
constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();

std::uint64_t SaturatingAdd(std::uint64_t a, std::uint64_t b) {
    return b > maximum - a ? maximum : a + b;
}

std::uint64_t SaturatingMultiply(std::uint64_t a, std::uint64_t b) {
    return b != 0 && a > maximum / b ? maximum : a * b;
}

std::uint64_t DivideCeiling(std::uint64_t value, std::uint32_t divisor) {
    return value / divisor + (value % divisor != 0 ? 1 : 0);
}

}

bool TickPacer::SetSettings(PacingSettings settings) {
    if (settings.ticksPerSecond == 0) { return false; }
    if (settings.rateLimited != settings_.rateLimited) {
        creditedTicks_ = 0;
        fractionalUnits_ = 0;
    }
    settings_ = settings;
    if (IsIdle()) {
        creditedTicks_ = 0;
        fractionalUnits_ = 0;
    }
    return true;
}

PacingSettings TickPacer::GetSettings() const { return settings_; }

void TickPacer::Synchronize(const Vector<PlayerReadiness>& players) {
    Vector<Deadline> next;
    next.reserve(players.size());
    hasActivity_ = false;
    for (const PlayerReadiness& player : players) {
        if (!player.ready || player.hasCommand) {
            hasActivity_ = true;
            continue;
        }
        if (player.speed == 0) { std::abort(); }
        Deadline deadline{player.actor, player.id,
            ActionTicks(100, player.speed) * unitsPerTick};
        for (const Deadline& previous : deadlines_) {
            if (previous.actor == player.actor && previous.id == player.id) {
                deadline.remainingUnits = previous.remainingUnits;
                break;
            }
        }
        next.push_back(deadline);
    }
    eastl::sort(next.begin(), next.end(), [](const Deadline& a, const Deadline& b) {
        if (a.id != b.id) { return a.id < b.id; }
        return a.actor.value < b.actor.value;
    });
    deadlines_ = eastl::move(next);
    if (IsIdle()) {
        creditedTicks_ = 0;
        fractionalUnits_ = 0;
    }
}

bool TickPacer::IsIdle() const { return settings_.pauseWhenIdle && !hasActivity_; }

void TickPacer::ElapseMicroseconds(std::uint64_t elapsed) {
    if (IsIdle()) { return; }
    const std::uint32_t rate = settings_.ticksPerSecond;
    if (settings_.rateLimited) {
        // Split before multiplying: elapsed * rate may require 96 bits.
        const Tick whole = SaturatingMultiply(elapsed / unitsPerTick, rate);
        const std::uint64_t partial = (elapsed % unitsPerTick) * rate + fractionalUnits_;
        creditedTicks_ = SaturatingAdd(creditedTicks_,
            SaturatingAdd(whole, partial / unitsPerTick));
        fractionalUnits_ = static_cast<std::uint32_t>(partial % unitsPerTick);
    }
    const std::uint64_t elapsedUnits = SaturatingMultiply(elapsed, rate);
    for (Deadline& deadline : deadlines_) {
        deadline.remainingUnits = elapsedUnits >= deadline.remainingUnits
            ? 0 : deadline.remainingUnits - elapsedUnits;
    }
}

Tick TickPacer::AvailableTicks() const {
    return settings_.rateLimited ? creditedTicks_ : maximum;
}

void TickPacer::ConsumeTicks(Tick ticks) {
    if (!settings_.rateLimited) { return; }
    if (ticks > creditedTicks_) { std::abort(); }
    creditedTicks_ -= ticks;
}

eastl::optional<std::uint64_t> TickPacer::NextDeadlineMicroseconds() const {
    if (IsIdle() || deadlines_.empty()) { return {}; }
    std::uint64_t minimum = maximum;
    for (const Deadline& deadline : deadlines_) {
        if (deadline.remainingUnits < minimum) { minimum = deadline.remainingUnits; }
    }
    return DivideCeiling(minimum, settings_.ticksPerSecond);
}

std::uint64_t TickPacer::MicrosecondsUntilTicks(Tick ticks) const {
    if (!settings_.rateLimited || ticks <= creditedTicks_) { return 0; }
    const Tick missing = ticks - creditedTicks_;
    const std::uint32_t rate = settings_.ticksPerSecond;
    // Divide the tick count before multiplying, preserving the exact fraction.
    std::uint64_t wholeSeconds = missing / rate;
    std::uint64_t residualUnits = (missing % rate) * unitsPerTick;
    if (residualUnits < fractionalUnits_) {
        --wholeSeconds; // missing > 0 guarantees a whole second to borrow.
        residualUnits += static_cast<std::uint64_t>(rate) * unitsPerTick;
    }
    residualUnits -= fractionalUnits_;
    return SaturatingAdd(SaturatingMultiply(wholeSeconds, unitsPerTick),
        DivideCeiling(residualUnits, rate));
}

Vector<ActorHandle> TickPacer::ExpiredPlayers() const {
    Vector<ActorHandle> result;
    if (IsIdle()) { return result; }
    for (const Deadline& deadline : deadlines_) {
        if (deadline.remainingUnits == 0) { result.push_back(deadline.actor); }
    }
    return result;
}

void TickPacer::Reset() {
    deadlines_.clear();
    creditedTicks_ = 0;
    fractionalUnits_ = 0;
    hasActivity_ = false;
}

}
