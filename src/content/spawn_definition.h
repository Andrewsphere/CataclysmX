#pragma once

#include "combat/combat_types.h"
#include "content/ai_definition.h"
#include "core/containers.h"
#include "items/item_types.h"

#include <cstdint>

namespace cataclysmx {

struct LootPoolId {
    std::uint32_t value = 0;
    bool operator==(const LootPoolId&) const = default;
};
struct MonsterTypeId {
    std::uint32_t value = 0;
    bool operator==(const MonsterTypeId&) const = default;
};
struct MonsterGroupId {
    std::uint32_t value = 0;
    bool operator==(const MonsterGroupId&) const = default;
};

// Content limits include every roll of a placement, not just one pool draw.
// Depth/work count pool invocations (including roots); items are bounded apart.
inline constexpr std::uint32_t maxSpawnCount = 1024;
inline constexpr std::uint32_t maxSpawnRolls = 1024;
inline constexpr std::uint32_t maxLootDepth = 16;
inline constexpr std::uint32_t maxLootExpansionWork = 4096;
inline constexpr std::uint32_t maxLootItems = 1024;

struct SpawnCount {
    std::uint32_t min = 1;
    std::uint32_t max = 1;
    bool operator==(const SpawnCount&) const = default;
};

enum class LootEntryKind : std::uint8_t { Item, Pool };
struct LootEntry {
    LootEntryKind kind = LootEntryKind::Item;
    ItemTypeId item{};
    LootPoolId pool{};
    std::uint32_t weight = 1;
    SpawnCount count;
    bool operator==(const LootEntry&) const = default;
};

// One invocation selects one weighted entry. A pool entry repeats independent
// child draws count times; an item entry emits count separate item instances.
struct LootPoolDefinition {
    String id;
    Vector<LootEntry> entries;
    bool operator==(const LootPoolDefinition&) const = default;
};

// Spawn recipe only. The resulting ordinary actor owns its combat/controller
// state; no definition ID or name needs to be added to each runtime actor.
struct MonsterDefinition {
    String id;
    String name;
    AiProfileId aiProfile{};
    BodyKind body = BodyKind::Simple;
    CombatFaction faction = CombatFaction::Undead;
    std::uint32_t speed = 100;
    std::uint32_t attackDamage = 12; // Whole HP, as in CombatState.
    // ratioScale units, 0..10000, divisible by 100 for exact percent conversion.
    std::uint32_t accuracy = 9000;
    bool operator==(const MonsterDefinition&) const = default;
};

struct MonsterGroupEntry {
    MonsterTypeId monster{};
    std::uint32_t weight = 1;
    bool operator==(const MonsterGroupEntry&) const = default;
};
struct MonsterGroupDefinition {
    String id;
    Vector<MonsterGroupEntry> entries;
    bool operator==(const MonsterGroupDefinition&) const = default;
};

}
