#pragma once

#include "combat/combat_types.h"

namespace cataclysmx {

// Definition-owned melee properties. Damage and penetration use hundredths of HP,
// like DamagePacket. Move modifiers and actor speed are applied by combat.
struct MeleeProfile {
    SmallVector<DamagePacket, 1> damage;
    std::uint32_t attackCost = 100;
    std::uint8_t hands = 1;
    bool operator==(const MeleeProfile&) const = default;
};

}
