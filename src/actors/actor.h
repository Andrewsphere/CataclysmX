#pragma once

#include "core/containers.h"
#include "core/light_types.h"
#include "core/timing.h"
#include "core/world_types.h"
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
    std::uint32_t speed = 100;
    Tick remainingTicks = 0;
    bool parked = false;
    std::uint64_t carryCapacityGrams = 100000;
    LightEmission light{};

private:
    friend class World;
    eastl::unique_ptr<AiController> controller_;
};

}
