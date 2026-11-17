#pragma once

#include "combat/damage.h"
#include "combat/equipment_types.h"
#include "skills/skill_types.h"

namespace cataclysmx {

// Definition-owned melee properties. Damage and penetration use hundredths of HP,
// like DamagePacket. Move modifiers and actor speed are applied by combat.
struct MeleeProfile {
    SmallVector<DamagePacket, 1> damage;
    std::uint32_t attackCost = 100;
    std::uint8_t hands = 1;
    // Appended expansion fields. Loader converts legacy hands into one grip;
    // zero handlingSkill resolves to the catalog's explicit improvised fallback.
    SkillId handlingSkill{};
    SmallVector<GripDefinition, 1> grips;
    std::uint32_t blockCapacity = 0;
    DefenseMask allowedResponses = DefenseMask::None;
    bool operator==(const MeleeProfile&) const = default;
};

}
