#pragma once

#include <cstdint>

namespace cataclysmx {

// Catalog-local IDs. Zero is absent; saves and ordering use definition names.
struct CombatStyleId {
    std::uint32_t value = 0;
    bool operator==(const CombatStyleId&) const = default;
};
struct CombatMoveId {
    std::uint32_t value = 0;
    bool operator==(const CombatMoveId&) const = default;
};
struct CombatArtId {
    std::uint32_t value = 0;
    bool operator==(const CombatArtId&) const = default;
};
struct AbilityId {
    std::uint32_t value = 0;
    bool operator==(const AbilityId&) const = default;
};

}
