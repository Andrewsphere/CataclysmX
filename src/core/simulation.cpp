#include <cstdlib>

#include "core/simulation.h"

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error Engine must compile without exceptions
#endif

namespace cataclysmx {
namespace {

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

struct ActorSnapshot {
    ActorHandle handle;
    TileCoord position;
};

}

void Advance(World& world, ActorHandle player, std::uint32_t& rng_state) {
    const Actor* controlled = world.GetActor(player);
    Require(controlled != nullptr);
    Require(controlled->player && world.IsActive(controlled->position));

    Vector<ActorSnapshot> actors;
    for (ActorHandle handle : world.ActiveActors()) {
        const Actor* actor = world.GetActor(handle);
        Require(actor != nullptr);
        actors.push_back({handle, actor->position});
    }
    const Vector<ObjectHandle> objects = world.ActiveObjects();

    constexpr TileCoord directions[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    for (const ActorSnapshot& actor : actors) {
        Require(world.RecordActorTurn(actor.handle).has_value());
        if (actor.handle == player) {
            continue;
        }
        // Unsigned overflow is the specified, portable modulo-2^32 LCG.
        rng_state = rng_state * 1'664'525U + 1'013'904'223U;
        const TileCoord direction = directions[rng_state >> 30];
        const TileCoord destination{actor.position.x + direction.x, actor.position.z + direction.z};
        const Tile* tile = world.GetTile(destination);
        if (!world.IsActive(destination) || !tile || !Passable(world.GetContent(), tile->terrain)
            || world.ActorAt(destination).value != 0) {
            continue;
        }
        Require(world.MoveActor(actor.handle, destination).has_value());
    }
    for (ObjectHandle handle : objects) {
        Require(world.RecordObjectUpdate(handle).has_value());
    }
}

}
