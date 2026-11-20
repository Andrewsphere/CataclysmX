#pragma once

#include "core/containers.h"
#include "core/ratio.h"
#include "skills/skill_types.h"
#include <EASTL/array.h>
#include <type_traits>

namespace cataclysmx {
struct SkillDefinition {
    String id;
    String name;
    SkillCategory category = SkillCategory::Noncombat;
    std::uint8_t maxLevel = maximumSkillLevel;
    // Whole XP to earn each next level (index 0 earns level 1).
    eastl::array<std::uint32_t, maximumSkillLevel> xpToNext{
        100, 200, 400, 800, 1200, 1800, 2600, 3600, 4800, 6400};
};
// Compact content records retain full-width SkillId at API boundaries.
// Constructors check representation bounds; loaders validate content semantics.
class SkillRequirement {
public:
    constexpr SkillRequirement() = default;
    constexpr SkillRequirement(SkillId skill, std::uint32_t level)
        : packed_(static_cast<std::uint16_t>(skill.value | (level << 12))) {
        if (skill.value > maxSkillId || level > 15) { std::abort(); }
    }
    constexpr SkillId Skill() const { return {packed_ & maxSkillId}; }
    constexpr std::uint8_t Level() const { return static_cast<std::uint8_t>(packed_ >> 12); }
    bool operator==(const SkillRequirement&) const = default;
private:
    std::uint16_t packed_ = 0;
};
static_assert(sizeof(SkillRequirement) == 2);
static_assert(std::is_trivially_copyable_v<SkillRequirement>);
static_assert(maximumSkillLevel <= 15);

class SkillTraining {
public:
    constexpr SkillTraining() = default;
    constexpr SkillTraining(SkillId skill, std::uint32_t level, std::uint32_t xpPerWork = 100)
        : requirement_(skill, level), xpPerWork_(static_cast<std::uint16_t>(xpPerWork)) {
        if (xpPerWork > UINT16_MAX) { std::abort(); }
    }
    constexpr SkillId Skill() const { return requirement_.Skill(); }
    constexpr std::uint8_t Level() const { return requirement_.Level(); }
    // ratioScale means one XP per base work; zero disables this reward.
    constexpr std::uint16_t XpPerWork() const { return xpPerWork_; }
    bool operator==(const SkillTraining&) const = default;
private:
    SkillRequirement requirement_;
    std::uint16_t xpPerWork_ = 100;
};
static_assert(sizeof(SkillTraining) == 4);
static_assert(std::is_trivially_copyable_v<SkillTraining>);
struct SkillBookBenefit {
    SkillId skill{};
    // Inclusive NEXT levels this benefit can help earn.
    std::uint8_t firstLevel = 1;
    std::uint8_t lastLevel = 5;
    std::uint32_t directXp = 0;
    std::uint32_t theoryXp = 0;
    std::uint32_t multiplier = ratioScale;
    bool operator==(const SkillBookBenefit&) const = default;
};
}
