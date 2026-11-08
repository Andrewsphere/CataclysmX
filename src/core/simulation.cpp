#include <cstdlib>
#include <limits>

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

void ActNpc(World& world, ActorHandle handle, std::uint32_t& rngState) {
    const Actor* actor = world.GetActor(handle);
    Require(actor != nullptr);
    const TileCoord position = actor->position;
    Require(world.ScheduleActorAction(handle).has_value());
    // Unsigned overflow is the specified, portable modulo-2^32 LCG. Even a
    // blocked action consumes its one draw and its full scheduling cost.
    rngState = rngState * 1'664'525U + 1'013'904'223U;
    constexpr TileCoord directions[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    const TileCoord direction = directions[rngState >> 30];
    const TileCoord destination{position.x + direction.x, position.z + direction.z};
    const Tile* tile = world.GetTile(destination);
    if (!world.IsActive(destination) || !tile || !Passable(world.GetContent(), tile->terrain)
        || world.ActorAt(destination).value != 0) {
        return;
    }
    Require(world.MoveActor(handle, destination).has_value());
}

}

SimulationResult AdvanceUntilInput(World& world, Tick& tick,
    std::uint32_t& rngState, Tick limitTick) {
    Require(limitTick >= tick);
    for (;;) {
        // Completed timed effects precede the input barrier. Their next delay
        // is committed immediately, so resuming this same tick cannot repeat it.
        for (ObjectHandle handle : world.ActiveObjects()) {
            const InteractiveObject* object = world.GetObject(handle);
            if (object && object->enabled && object->remainingTicks == 0) {
                Require(world.RecordObjectUpdate(handle).has_value());
            }
        }

        SimulationResult result;
        const Vector<ActorHandle> actors = world.ActiveActors();
        for (ActorHandle handle : actors) {
            const Actor* actor = world.GetActor(handle);
            if (actor && actor->player && actor->remainingTicks == 0) {
                result.readyPlayers.push_back(handle);
            }
        }
        if (!result.readyPlayers.empty()) {
            result.stop = SimulationStop::InputRequired;
            return result;
        }

        for (ActorHandle handle : actors) {
            const Actor* actor = world.GetActor(handle);
            if (actor && !actor->player && actor->remainingTicks == 0) {
                ActNpc(world, handle, rngState);
            }
        }

        Tick next = std::numeric_limits<Tick>::max();
        bool hasEvents = false;
        for (ActorHandle handle : world.ActiveActors()) {
            const Actor* actor = world.GetActor(handle);
            if (!actor) { continue; }
            Require(actor->remainingTicks != 0);
            if (actor->remainingTicks < next) { next = actor->remainingTicks; }
            hasEvents = true;
        }
        for (ObjectHandle handle : world.ActiveObjects()) {
            const InteractiveObject* object = world.GetObject(handle);
            if (!object || !object->enabled) { continue; }
            Require(object->remainingTicks != 0);
            if (object->remainingTicks < next) { next = object->remainingTicks; }
            hasEvents = true;
        }
        if (!hasEvents) { return result; }
        result.nextEventIn = next;
        if (tick == limitTick) {
            result.stop = SimulationStop::LimitReached;
            return result;
        }
        const Tick room = limitTick - tick;
        const Tick elapsed = next < room ? next : room;
        world.ElapseTicks(elapsed);
        tick += elapsed;
    }
}

void Advance(World& world, ActorHandle player, std::uint32_t& rng_state) {
    const Actor* controlled = world.GetActor(player);
    Require(controlled && controlled->player && world.IsActive(controlled->position));
    Require(world.ScheduleActorAction(player).has_value());
    Tick tick = 0;
    const SimulationResult result = cataclysmx::AdvanceUntilInput(world, tick, rng_state,
        std::numeric_limits<Tick>::max());
    Require(result.stop == SimulationStop::InputRequired);
}

}
