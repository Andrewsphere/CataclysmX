#pragma once

#include <cstdlib>
#include <limits>

// All project uses share release-enabled allocation/invariant checks.
#define SLOT_MAP_ASSERT(expression) ((expression) ? (void)0 : std::abort())
#include <slot_map/slot_map.h>
