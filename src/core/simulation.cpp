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

}

SimulationResult AdvanceUntilInput(World& world, Tick& tick,
    std::uint32_t& rngState, Tick limitTick) {
    Require(limitTick >= tick);
    for (;;) {
        // Completed timed effects precede the input barrier. Their next delay
        // is committed immediately, so resuming this same tick cannot repeat it.
        bool effectsCompleted = world.ProcessObjectEvents();

        const auto dueItemEvent = world.NextItemEventIn();
        effectsCompleted |= dueItemEvent && *dueItemEvent == 0;
        world.ProcessItemEvents();
        // Due effects at the same tick form one observation boundary. This is
        // inside the loop so a long wait cannot skip intermediate observations.
        if (effectsCompleted) { world.UpdatePlayerObservations(); }

        SimulationResult result;
        world.CollectReadyPlayers(result.readyPlayers);
        if (!result.readyPlayers.empty()) {
            result.stop = SimulationStop::InputRequired;
            return result;
        }

        world.ProcessNpcActions(rngState);

        Tick next = std::numeric_limits<Tick>::max();
        bool hasEvents = false;
        if (const auto actorEvent = world.NextActorEventIn()) {
            Require(*actorEvent != 0);
            if (*actorEvent < next) { next = *actorEvent; }
            hasEvents = true;
        }
        if (const auto objectEvent = world.NextObjectEventIn()) {
            Require(*objectEvent != 0);
            if (*objectEvent < next) { next = *objectEvent; }
            hasEvents = true;
        }
        if (const auto itemEvent = world.NextItemEventIn()) {
            Require(*itemEvent != 0);
            if (*itemEvent < next) { next = *itemEvent; }
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
    world.UpdatePlayerObservations();
    Tick tick = 0;
    const SimulationResult result = cataclysmx::AdvanceUntilInput(world, tick, rng_state,
        std::numeric_limits<Tick>::max());
    Require(result.stop == SimulationStop::InputRequired);
}

}
