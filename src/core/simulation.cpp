#include <cstdlib>

#include "core/simulation.h"

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error Engine must compile without exceptions
#endif

namespace cataclysmx {
namespace {

void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

struct ActorSnapshot {
    ActorHandle handle;
    TileCoord position;
};

}

void advance(World& world, ActorHandle player, std::uint32_t& rng_state) {
    const Actor* controlled = world.actor(player);
    require(controlled != nullptr);
    require(controlled->player && world.active(controlled->position));

    Vector<ActorSnapshot> actors;
    for (ActorHandle handle : world.active_actors()) {
        const Actor* actor = world.actor(handle);
        require(actor != nullptr);
        actors.push_back({handle, actor->position});
    }
    const Vector<ObjectHandle> objects = world.active_objects();

    constexpr TileCoord directions[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    for (const ActorSnapshot& actor : actors) {
        require(world.record_actor_turn(actor.handle).has_value());
        if (actor.handle == player) {
            continue;
        }
        // Unsigned overflow is the specified, portable modulo-2^32 LCG.
        rng_state = rng_state * 1'664'525U + 1'013'904'223U;
        const TileCoord direction = directions[rng_state >> 30];
        const TileCoord destination{actor.position.x + direction.x, actor.position.z + direction.z};
        const Tile* tile = world.tile(destination);
        if (!world.active(destination) || !tile || !passable(tile->terrain)
            || world.actor_at(destination).value != 0) {
            continue;
        }
        require(world.move_actor(actor.handle, destination).has_value());
    }
    for (ObjectHandle handle : objects) {
        require(world.record_object_update(handle).has_value());
    }
}

}
