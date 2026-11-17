#pragma once

#include "combat/damage.h"
#include "core/ratio.h"
#include "core/timing.h"
#include <EASTL/span.h>

namespace cataclysmx {

// Ratings use 100 units per skill level. Invalid inputs/overflow are invariants.
std::int32_t AttackRating100(int melee, int handling, std::int32_t bonus100 = 0,
    std::int32_t penalty100 = 0);
std::int32_t DodgeRating100(int dodge, std::int32_t bonus100 = 0,
    std::int32_t penalty100 = 0);
std::uint32_t DefenseChance(DefenseKind defense, std::int32_t attacker100,
    std::int32_t defender100);

enum class StrikeDrawOutcome : std::uint8_t { Miss, DefenseSucceeded, Contact };
StrikeDrawOutcome ResolveStrikeDraw(std::uint32_t draw, std::uint32_t intrinsicMiss,
    std::uint32_t defenseChance);

// Same full-period uint32 LCG as legacy melee, rejection sampling removes modulo
// bias. bound==1 consumes no draw; bound==0 is an invariant violation.
std::uint32_t DrawBelow(std::uint32_t& rng, std::uint32_t bound);

std::uint32_t SkillEfficiencyBonus(int effectiveSkill);
std::uint32_t DiscountStaminaCost(std::uint32_t baseCost, int effectiveSkill);
std::uint32_t IncreaseBlockCapacity(std::uint32_t baseCapacity, int effectiveSkill);
std::uint32_t CombatTrainingRatio(int opponentRank, int learnedRank);
Tick ArtStudyDuration(Tick baseDuration, unsigned learnedSurplus);
std::uint32_t LayerEncumbrance(std::uint32_t intrinsic, std::uint32_t nonexemptCount);

// One packet per DamageType, any order. Output is aligned with input. Shared
// budget is allocated by largest remainder, ties by DamageType. No allocations.
// Output must not overlap packet storage. Empty/zero damage absorbs nothing.
std::uint64_t AllocateBlockAbsorption(eastl::span<const DamagePacket> packets,
    std::uint32_t capacity, eastl::span<std::uint32_t> absorbed);

struct CombatTrainingBudget {
    std::uint64_t remainingWork = 0;
    bool operator==(const CombatTrainingBudget&) const = default;
};
// Sequential equal shares, including integer remainder; caller supplies remaining
// planned opportunities, not damage component count. Invalidated shares expire.
std::uint64_t TakeTrainingShare(CombatTrainingBudget& budget, std::uint32_t remainingStrikes);

}
