#pragma once

#include "core/timing.h"

#include <EASTL/array.h>
#include <cstddef>
#include <cstdint>

namespace cataclysmx {

// Logical time: one turn is one game second, regardless of PacingSettings.
inline constexpr Tick body_ticks_per_day = Tick{ticks_per_turn} * 60 * 60 * 24;
inline constexpr std::uint32_t body_hp_scale = 100;

enum class BodyKind : std::uint8_t { Detailed, Simple };
enum class BodyPart : std::uint8_t {
    Head, Torso, LeftHand, RightHand, LeftLeg, RightLeg,
    AttackApparatus, Locomotion, Core, Count
};
inline constexpr std::size_t body_part_count = static_cast<std::size_t>(BodyPart::Count);

struct InjuryProfile {
    Tick mendingDuration = 7 * body_ticks_per_day;
    Tick healingInterval = body_ticks_per_day / 24; // One regular HP per game hour.
    Tick maxHealingInterval = body_ticks_per_day; // One maximum HP per game day.
    std::uint32_t mendedHp = 40; // Whole HP; 60 is also a valid recovery policy.
    std::uint32_t maxLossConversionPercent = 10; // Injury converted to max loss, 0..100%.
    std::uint32_t maxLossCapPercent = 50; // Accumulated loss cap, bounded policy: 0..50%.
    bool regeneration = true; // False freezes all recovery, including mending.
    bool operator==(const InjuryProfile&) const = default;
};

// HP and maximum loss are hundredths of HP. Subsequent whole-HP injury to a
// broken detailed extremity adds maxLossConversionPercent hundredths per
// injury HP (10 by default), up to maxLossCapPercent of its unreduced maximum.
// Counters accrue only in their eligible phase; unused time at full health is
// discarded. These explicit fields are the complete persistence state.
struct BodyPartState {
    std::uint32_t hp = 0;
    std::uint32_t maxLoss = 0;
    Tick mendingElapsed = 0;
    Tick healingElapsed = 0;
    Tick maxHealingElapsed = 0;
    bool broken = false;
    bool operator==(const BodyPartState&) const = default;
};

struct BodyState {
    BodyKind kind = BodyKind::Detailed; // Independent of controller/allegiance.
    eastl::array<BodyPartState, body_part_count> parts{};
    bool operator==(const BodyState&) const = default;
};

struct InjurySummary {
    BodyPart affectedPart = BodyPart::Head;
    bool transferredToCore = false;
    bool lostCapability = false;
    bool broken = false; // Newly broken by this injury.
    bool defeated = false; // Simple Core only; detailed/player defeat is deferred.
};

BodyState MakeBody(BodyKind kind);
bool HasPart(BodyKind kind, BodyPart part);
bool Functional(const BodyState& body, BodyPart part);
// Ordinary step cost: 100 healthy, 150 one disabled detailed leg, 200 crawling.
std::uint32_t LocomotionBaseCost(const BodyState& body);
// Detailed parts and simple Core have 100 HP; simple attached zones have 60.
// Requires a valid BodyPart (not Count). Both maximum queries return hundredths.
std::uint32_t UnreducedMax(BodyPart part);
std::uint32_t EffectiveMax(const BodyState& body, BodyPart part); // Hundredths.
bool ValidateProfile(const InjuryProfile& profile);
bool ValidateBody(const BodyState& body, const InjuryProfile& profile);

// Injury is whole HP AFTER protection. Zero (including rounded sub-1 injury)
// is ignored. Crossing zero never applies overkill/max loss. A simple attached
// zone already at zero redirects the entire subsequent hit to Core.
// Runtime calls require validated state/profile and a present target part.
InjurySummary ApplyInjury(BodyState& body, BodyPart part,
    std::uint32_t integerInjury, const InjuryProfile& profile);

// Caller supplies a frozen logical elapsed duration at its activation boundary.
// Recovery is subdivision-independent, with bounded work even for UINT64_MAX.
// Maximum restoration runs only at full regular HP; each maximum increment
// then leaves ordinary healing to fill that newly available capacity.
void ElapseBody(BodyState& body, const InjuryProfile& profile, Tick elapsed);

}
