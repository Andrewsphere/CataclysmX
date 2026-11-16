#pragma once

#include "items/item_types.h"

namespace cataclysmx {

class Server;

// A bounded administrative request, not a gameplay/network command.
inline constexpr std::uint32_t maxDebugGiveItems = 1000;
struct DebugItemGrant {
    ItemTypeId type{};
    std::uint32_t count = 1;
};
enum class DebugGiveOutcome { Given, InvalidItem, InvalidCount, Unavailable, CapacityExceeded };
struct DebugGiveResult {
    DebugGiveOutcome outcome = DebugGiveOutcome::Unavailable;
    std::uint32_t count = 0;
};

// Owner-thread only; targets the local/host player. Preflights the entire count,
// honors carry capacity, creates normal initialized items, and advances no time.
DebugGiveResult DebugGiveItems(Server& server, PersistentId player, DebugItemGrant request);

}
