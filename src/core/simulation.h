#pragma once

#include "core/world.h"

namespace cataclysmx {

enum class SimulationStop { InputRequired, LimitReached, NoEvents };
struct SimulationResult {
    SimulationStop stop = SimulationStop::NoEvents;
    Vector<ActorHandle> readyPlayers;
    Tick nextEventIn = 0;
};

// Processes due objects and items, then pauses for ready players before NPCs.
// World owns reusable, persistent-ID-ordered due batches. Time advances to the next
// event, at most through limitTick. No wall clock or input timeout is consulted.
SimulationResult AdvanceUntilInput(World& world, std::uint32_t& rngState, Tick limitTick);
// Compatibility overload: tick is an output mirror of World::GetTick().
SimulationResult AdvanceUntilInput(World& world, Tick& tick,
    std::uint32_t& rngState, Tick limitTick);

// Single-player convenience: commit a normal wait and advance to the next input
// barrier. Retained for headless callers; one turn now means 100 simulation ticks.
void Advance(World& world, ActorHandle player, std::uint32_t& rng_state);

}
