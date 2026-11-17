#pragma once

#include <EASTL/array.h>
#include <cstddef>
#include <cstdint>

namespace cataclysmx {

enum class DamageType : std::uint8_t { Blunt, Slashing, Piercing, Ballistic, Heat, Chemical, Choking, Count };
inline constexpr std::size_t damage_type_count = static_cast<std::size_t>(DamageType::Count);
using ProtectionValues = eastl::array<std::uint32_t, damage_type_count>;

// Hundredths of HP. A physical contact rounds injury once after all components.
struct DamagePacket {
    DamageType type = DamageType::Blunt;
    std::uint32_t amount = 0;
    std::uint32_t penetration = 0;
    bool operator==(const DamagePacket&) const = default;
};

enum class DefenseKind : std::uint8_t { None, Dodge, Parry, Block };
enum class DefenseMask : std::uint8_t { None = 0, Dodge = 1, Parry = 2, Block = 4, Melee = 7 };
bool AllowsDefense(DefenseMask mask, DefenseKind kind);

}
