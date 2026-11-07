#pragma once

#include "core/world.h"

namespace cataclysmx {

// One turn of the current active snapshot, in persistent-ID order. The player
// must be live and active. NPCs consume one explicit RNG draw each, even when
// blocked; resident-only and archived entities do not advance or catch up.
void advance(World& world, ActorHandle player, std::uint32_t& rng_state);

}
