#pragma once

#include "core/world.h"

namespace cataclysmx {

enum class SimulationStop { InputRequired, LimitReached, NoEvents };
struct SimulationResult {
    SimulationStop stop = SimulationStop::NoEvents;
    Vector<ActorHandle> readyPlayers;
    Tick nextEventIn = 0;
};

// Processes due objects, then pauses for ready players before the actor phase.
// NPC actions run in persistent-ID order. Time advances directly to the next
// event, at most through limitTick. No wall clock or input timeout is consulted.
SimulationResult AdvanceUntilInput(World& world, Tick& tick,
    std::uint32_t& rngState, Tick limitTick);

// Single-player convenience: commit a normal wait and advance to the next input
// barrier. Retained for headless callers; one turn now means 100 simulation ticks.
void Advance(World& world, ActorHandle player, std::uint32_t& rng_state);

}
