#pragma once

#include "core/timing.h"

#include <cstdint>

namespace cataclysmx {

enum class ObjectMoveKind : std::uint8_t { Push, Pull };
enum class ObjectMoveOutcome : std::uint8_t {
    Moved, Unavailable, NotReady, OutOfReach, Immovable, Blocked, Occupied
};
struct ObjectMoveResult {
    ObjectMoveOutcome outcome = ObjectMoveOutcome::Unavailable;
    // Base action cost, before actor-speed scheduling; zero on refusal.
    Tick cost = 0;
    bool operator==(const ObjectMoveResult&) const = default;
};

}
