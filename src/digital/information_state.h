#pragma once

#include "core/containers.h"
#include "core/timing.h"
#include "core/world_types.h"

namespace cataclysmx {

// Owned by the destination's library; no usable package exists until completion.
// Stable source/worker identities survive saves without retaining entity borrows.
struct InformationTransferState {
    PersistentId worker = 0;
    PersistentId sourceItem = 0;
    PersistentId sourceObject = 0;
    PersistentId hostItem = 0;
    String packageName;
    Tick remainingTicks = 0;
};

}
