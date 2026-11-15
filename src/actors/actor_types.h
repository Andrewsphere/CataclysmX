#pragma once

#include <cstdint>

namespace cataclysmx {

// Catalog-local identity, not an entity handle or a persistent numeric ID.
// Zero denotes an untyped actor. Persist the definition's stable string ID.
struct MonsterTypeId {
    std::uint32_t value = 0;
    bool operator==(const MonsterTypeId&) const = default;
};

}
