#pragma once

#include "actors/actor_types.h"
#include "core/containers.h"
#include "core/light_types.h"
#include "core/timing.h"
#include "core/world_types.h"
#include "combat/combat_types.h"
#include "skills/skill_state.h"
#include <EASTL/unique_ptr.h>

namespace cataclysmx {

class AiController;
class World;

// World owns actors. Public fields are inspection values; mutate through World.
class Actor {
public:
    Actor(PersistentId id, TileCoord position, bool player);
    ~Actor();
    Actor(Actor&&) noexcept;
    Actor& operator=(Actor&&) noexcept;
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    PersistentId id = 0;
    TileCoord position{};
    bool player = false;
    std::uint64_t turns = 0;
    Vector<ItemHandle> inventory;
    ItemHandle wieldedItem{};
    std::uint32_t speed = 100;
    ActionDeadline actionDeadline{};
    bool parked = false;
    std::uint64_t carryCapacityGrams = 100000;
    LightEmission light{};
    CombatState combat;
    CombatFeedback combatFeedback;
    // Assigned by CreateMonster; zero for players and legacy/untyped actors.
    MonsterTypeId monsterType{};
    LearningState learning;

private:
    friend class World;
    eastl::unique_ptr<AiController> controller_;
};

}
