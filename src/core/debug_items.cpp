#include "core/debug_items.h"

#include "core/server.h"
#include <cstdlib>

namespace cataclysmx {

DebugGiveResult DebugGiveItems(Server& server, PersistentId player, DebugItemGrant request) {
    auto& world = server.GetWorld();
    const auto handle = server.GetPlayer();
    const auto* actor = world.GetActor(handle);
    if (!actor || !player || actor->id != player || actor->parked || !world.IsActive(actor->position)) {
        return {DebugGiveOutcome::Unavailable};
    }
    if (!request.count || request.count > maxDebugGiveItems) { return {DebugGiveOutcome::InvalidCount}; }
    const auto* definition = world.GetContent().TryGetItem(request.type);
    if (!definition || definition->unfinishedCraft) { return {DebugGiveOutcome::InvalidItem}; }
    const auto capacity = actor->carryCapacityGrams;
    const auto used = world.GetCarriedMass(handle);
    const auto added = static_cast<std::uint64_t>(definition->massGrams) * request.count;
    if (used > capacity || added > capacity - used) { return {DebugGiveOutcome::CapacityExceeded}; }
    // Type, owner, and complete mass are validated before creating any instances.
    // Fresh root items have no children, and no simulation runs between creations.
    for (std::uint32_t i = 0; i < request.count; ++i) {
        if (!world.CreateItem(request.type, ItemLocation::Carried(handle))) { std::abort(); }
    }
    return {DebugGiveOutcome::Given, request.count};
}

}
