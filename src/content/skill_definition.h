#pragma once

#include "core/containers.h"
#include "core/ratio.h"
#include "skills/skill_types.h"
#include <EASTL/array.h>

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
struct SkillRequirement {
    SkillId skill{};
    std::uint8_t level = 0;
    bool operator==(const SkillRequirement&) const = default;
};
struct SkillTraining {
    SkillId skill{};
    std::uint8_t level = 0;
    // ratioScale means one XP per base work; zero disables this reward.
    std::uint32_t xpPerWork = 100;
    bool operator==(const SkillTraining&) const = default;
};
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
