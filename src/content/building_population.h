#pragma once

#include "content/object_definition.h"
#include "content/spawn_definition.h"
#include "core/ratio.h"
#include "core/world_types.h"

namespace cataclysmx {

struct PatternFurniture {
    TileCoord position{}; // Local x,z,y, before building rotation.
    ObjectTypeId type{};
    ObjectFacing facing = ObjectFacing::North;
    bool operator==(const PatternFurniture&) const = default;
};

enum class LootDestination : std::uint8_t { Ground, Furniture };
struct PatternLoot {
    TileCoord position{};
    LootPoolId pool{};
    std::uint32_t chance = ratioScale; // One placement gate, 0..ratioScale.
    std::uint32_t rolls = 1; // Independent pool draws after a successful gate.
    LootDestination destination = LootDestination::Ground;
    bool operator==(const PatternLoot&) const = default;
};

struct PatternMonster {
    TileCoord position{};
    MonsterGroupId group{};
    std::uint32_t chance = ratioScale; // At most one actor per marker.
    bool operator==(const PatternMonster&) const = default;
};

struct PatternPopulation {
    Vector<PatternFurniture> furniture;
    Vector<PatternLoot> loot;
    Vector<PatternMonster> monsters;
    bool operator==(const PatternPopulation&) const = default;
};

}
