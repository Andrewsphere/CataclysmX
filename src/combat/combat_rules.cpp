#include "combat/combat_rules.h"
#include "combat/character_combat.h"

#include <cstdlib>
#include <limits>

namespace cataclysmx {
namespace {

void Require(bool condition) {
    if (!condition) { std::abort(); }
}

std::int32_t CheckedRating(std::int64_t value) {
    Require(value >= std::numeric_limits<std::int32_t>::min()
        && value <= std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(value);
}

std::uint32_t CheckedUnsigned(std::uint64_t value) {
    Require(value <= std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(value);
}

}

bool AllowsDefense(DefenseMask mask, DefenseKind kind) {
    const auto bits = static_cast<unsigned>(mask);
    Require((bits & ~static_cast<unsigned>(DefenseMask::Melee)) == 0);
    switch (kind) {
    case DefenseKind::None: return false;
    case DefenseKind::Dodge: return (bits & static_cast<unsigned>(DefenseMask::Dodge)) != 0;
    case DefenseKind::Parry: return (bits & static_cast<unsigned>(DefenseMask::Parry)) != 0;
    case DefenseKind::Block: return (bits & static_cast<unsigned>(DefenseMask::Block)) != 0;
    }
    std::abort();
}

std::int32_t AttackRating100(int melee, int handling, std::int32_t bonus100,
    std::int32_t penalty100) {
    return CheckedRating(std::int64_t{40} * melee + std::int64_t{60} * handling
        + bonus100 - penalty100);
}

std::int32_t DodgeRating100(int dodge, std::int32_t bonus100, std::int32_t penalty100) {
    return CheckedRating(std::int64_t{100} * dodge + bonus100 - penalty100);
}

std::uint32_t DefenseChance(DefenseKind defense, std::int32_t attacker100,
    std::int32_t defender100) {
    std::int64_t base = 0;
    switch (defense) {
    case DefenseKind::None: return 0;
    case DefenseKind::Dodge:
    case DefenseKind::Parry: base = 4000; break;
    case DefenseKind::Block: base = 6000; break;
    default: std::abort();
    }
    const auto chance = base + 5 * (std::int64_t{defender100} - attacker100);
    if (chance < 500) { return 500; }
    if (chance > 9000) { return 9000; }
    return static_cast<std::uint32_t>(chance);
}

StrikeDrawOutcome ResolveStrikeDraw(std::uint32_t draw, std::uint32_t intrinsicMiss,
    std::uint32_t defenseChance) {
    Require(draw < ratioScale && ValidUnitRatio(intrinsicMiss) && ValidUnitRatio(defenseChance));
    if (draw < intrinsicMiss) { return StrikeDrawOutcome::Miss; }
    const auto defenseBand = (ratioScale - intrinsicMiss) * defenseChance / ratioScale;
    if (draw < intrinsicMiss + defenseBand) { return StrikeDrawOutcome::DefenseSucceeded; }
    return StrikeDrawOutcome::Contact;
}

std::uint32_t DrawBelow(std::uint32_t& rng, std::uint32_t bound) {
    Require(bound != 0);
    if (bound == 1) { return 0; }
    // Reject the low remainder of the 2^32 range. Arithmetic wraps as uint32.
    const std::uint32_t threshold = (std::uint32_t{0} - bound) % bound;
    for (;;) {
        rng = rng * 1'664'525U + 1'013'904'223U;
        if (rng >= threshold) { return rng % bound; }
    }
}

std::uint32_t SkillEfficiencyBonus(int effectiveSkill) {
    if (effectiveSkill <= 0) { return 0; }
    if (effectiveSkill >= 10) { return 2000; }
    return static_cast<std::uint32_t>(effectiveSkill) * 200;
}

std::uint32_t DiscountStaminaCost(std::uint32_t baseCost, int effectiveSkill) {
    const auto numerator = std::uint64_t{baseCost} * (ratioScale - SkillEfficiencyBonus(effectiveSkill));
    return CheckedUnsigned((numerator + ratioScale - 1) / ratioScale);
}

std::uint32_t IncreaseBlockCapacity(std::uint32_t baseCapacity, int effectiveSkill) {
    return CheckedUnsigned(std::uint64_t{baseCapacity}
        * (ratioScale + SkillEfficiencyBonus(effectiveSkill)) / ratioScale);
}

std::uint32_t CombatTrainingRatio(int opponentRank, int learnedRank) {
    const auto difference = std::int64_t{opponentRank} - learnedRank;
    if (difference >= 0) { return ratioScale; }
    if (difference == -1) { return 5000; }
    if (difference == -2) { return 1000; }
    return 0;
}

Tick ArtStudyDuration(Tick baseDuration, unsigned learnedSurplus) {
    Require(baseDuration != 0);
    const std::uint64_t divisor = ratioScale + std::uint64_t{2000} * learnedSurplus;
    // Avoid baseDuration*ratioScale overflow, even for maximum Tick. The
    // remainder product fits uint64 for every 32-bit unsigned surplus.
    const auto whole = baseDuration / divisor;
    const auto remainder = baseDuration % divisor;
    return whole * ratioScale + (remainder * ratioScale + divisor - 1) / divisor;
}

std::uint32_t LayerEncumbrance(std::uint32_t intrinsic, std::uint32_t nonexemptCount) {
    const auto extra = nonexemptCount > 1 ? nonexemptCount - 1 : 0;
    return CheckedUnsigned(std::uint64_t{intrinsic} + std::uint64_t{5} * extra);
}

std::uint64_t AllocateBlockAbsorption(eastl::span<const DamagePacket> packets,
    std::uint32_t capacity, eastl::span<std::uint32_t> absorbed) {
    Require(packets.size() <= damage_type_count && absorbed.size() == packets.size());
    std::uint32_t seen = 0;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        const auto& packet = packets[i];
        Require(packet.type < DamageType::Count);
        const auto bit = std::uint32_t{1} << static_cast<unsigned>(packet.type);
        Require((seen & bit) == 0);
        seen |= bit;
        total += packet.amount;
        absorbed[i] = 0;
    }
    if (total == 0 || capacity == 0) { return 0; }
    const std::uint64_t budget = total < capacity ? total : capacity;
    eastl::array<std::uint64_t, damage_type_count> remainders{};
    eastl::array<bool, damage_type_count> awarded{};
    std::uint64_t assigned = 0;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        const auto product = budget * packets[i].amount;
        absorbed[i] = static_cast<std::uint32_t>(product / total);
        remainders[i] = product % total;
        assigned += absorbed[i];
    }
    while (assigned < budget) {
        std::size_t best = packets.size();
        for (std::size_t i = 0; i < packets.size(); ++i) {
            if (awarded[i] || remainders[i] == 0) { continue; }
            if (best == packets.size() || remainders[i] > remainders[best]
                || (remainders[i] == remainders[best] && packets[i].type < packets[best].type)) {
                best = i;
            }
        }
        Require(best != packets.size());
        ++absorbed[best];
        awarded[best] = true;
        ++assigned;
    }
    return budget;
}

std::uint64_t TakeTrainingShare(CombatTrainingBudget& budget, std::uint32_t remainingStrikes) {
    Require(remainingStrikes != 0);
    const auto share = budget.remainingWork / remainingStrikes;
    budget.remainingWork -= share;
    return share;
}

eastl::unique_ptr<CharacterCombatState> CloneCharacterCombat(const CharacterCombatState* state) {
    if (!state) { return {}; }
    return eastl::unique_ptr<CharacterCombatState>(new CharacterCombatState(*state));
}

bool EqualCharacterCombat(const CharacterCombatState* a, const CharacterCombatState* b) {
    if (!a || !b) { return a == b; }
    return *a == *b;
}

}
