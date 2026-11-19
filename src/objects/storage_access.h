#pragma once

#include "core/world_types.h"

namespace cataclysmx {

class World;

// Same path rules as storage, without requiring an object storage compartment.
bool CanReachStation(const World& world, TileCoord from, ObjectHandle object,
    unsigned radius);

// Same-layer bounded reach, radius 0..8. Requires explicit storage capacity and
// active supported terrain. Target furniture may block movement; walls/corners
// still block reach. No residency acquisition or actor-validity checks.
bool CanReachStorage(const World& world, TileCoord from, ObjectHandle object,
    unsigned radius);

}
