#include "core/inventory.h"
#include "core/world.h"
#include <EASTL/array.h>
#include <cstdlib>

namespace cataclysmx {

bool ValidItemStorage(const ItemStorage& storage) {
    switch (storage.kind) {
    case ItemStorageKind::Inventory: return storage.id == 0 && storage.position == TileCoord{};
    case ItemStorageKind::Ground: return storage.id == 0 && ValidPosition(storage.position);
    case ItemStorageKind::Object:
    case ItemStorageKind::Container: return storage.id != 0 && storage.position == TileCoord{};
    }
    return false;
}

const char* InventoryOutcomeMessage(InventoryOutcome outcome) {
    switch (outcome) {
    case InventoryOutcome::Succeeded: return "Item moved.";
    case InventoryOutcome::Inaccessible: return "That storage is out of reach.";
    case InventoryOutcome::InvalidItem: return "That item is no longer there.";
    case InventoryOutcome::InvalidDestination: return "That item cannot be stored there.";
    case InventoryOutcome::NoCapacity: return "Not enough carrying or storage capacity.";
    case InventoryOutcome::NeedsHands: return "You need functioning hands to wield that item.";
    case InventoryOutcome::AlreadyThere: return "That item is already there.";
    case InventoryOutcome::Reserved: return "That item is reserved or installed.";
    case InventoryOutcome::Busy: return "You are still busy.";
    }
    std::abort();
}

ItemStorage World::GetItemStorage(ItemHandle handle) const {
    const auto* item = GetItem(handle);
    if (!item) { return {}; }
    const auto& location = item->location;
    switch (location.kind) {
    case LocationKind::actor: return {};
    case LocationKind::ground: return {ItemStorageKind::Ground, location.position};
    case LocationKind::object: return {ItemStorageKind::Object, {}, GetObject(location.object)->id};
    case LocationKind::item: return {ItemStorageKind::Container, {}, GetItem(location.item)->id};
    }
    std::abort();
}

bool World::CanAccessGroundStorage(ActorHandle handle, TileCoord position) const {
    const auto* actor = GetActor(handle);
    const auto* tile = GetTile(position);
    if (!actor || actor->parked || !IsActive(actor->position) || !tile || actor->position.y != position.y
        || std::abs(actor->position.x - position.x) > 1 || std::abs(actor->position.z - position.z) > 1) { return false; }
    const auto& terrain = GetContent().GetTerrain(tile->terrain);
    if (!terrain.passable || terrain.openSpace || !IsActive(position)
        || !CanPlayerSee(handle, position)) { return false; }
    // Match furniture reach: supported paths inside the immediate 3x3 area,
    // with both side cells clear for a diagonal and an accessible endpoint.
    const auto from = actor->position;
    const auto passable = [this](TileCoord p) {
        const auto* value = GetTile(p);
        return value && IsActive(p) && !BlocksMovement(p)
            && !GetContent().GetTerrain(value->terrain).openSpace;
    };
    if (!passable(from) && from != position) { return false; }
    eastl::array<TileCoord, 9> queue{};
    eastl::array<bool, 9> seen{};
    queue[0] = from; seen[4] = true;
    std::size_t count = 1;
    for (std::size_t i = 0; i < count; ++i) {
        const auto current = queue[i];
        if (current == position) { return true; }
        for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz) { continue; }
            const TileCoord next{current.x + dx, current.z + dz, current.y};
            const int x = next.x - from.x + 1, z = next.z - from.z + 1;
            if (x < 0 || x > 2 || z < 0 || z > 2 || seen[z * 3 + x]) { continue; }
            if ((next != position && !passable(next)) || (dx && dz
                && (!passable({current.x + dx, current.z, current.y})
                    || !passable({current.x, current.z + dz, current.y})))) { continue; }
            if (next == position) { return true; }
            seen[z * 3 + x] = true;
            queue[count++] = next;
        }
    }
    return false;
}

bool World::CanAccessStorage(ActorHandle handle, const ItemStorage& storage) const {
    const auto* actor = GetActor(handle);
    if (!ValidItemStorage(storage) || !actor || actor->parked || !IsActive(actor->position)) { return false; }
    switch (storage.kind) {
    case ItemStorageKind::Inventory: return true;
    case ItemStorageKind::Ground: return CanAccessGroundStorage(handle, storage.position);
    case ItemStorageKind::Object: {
        const auto object = FindObject(storage.id);
        const auto* value = GetObject(object);
        const auto* definition = value ? GetContent().TryGetObject(value->type) : nullptr;
        return definition && definition->behavior == ObjectBehaviorKind::None
            && CanPlayerSee(handle, value->position) && CanAccessObjectStorage(handle, object);
    }
    case ItemStorageKind::Container: {
        const auto item = FindItem(storage.id);
        const auto* value = GetItem(item);
        if (!value || IsReservedItem(item) || GetContent().GetItem(value->type).unfinishedCraft
            || !ItemContainer(GetContent().GetItem(value->type))) { return false; }
        auto root = item;
        for (unsigned depth = 0; depth < 64; ++depth) {
            const auto* current = GetItem(root);
            if (!current || current->location.installation.value) { return false; }
            if (current->location.kind != LocationKind::item) {
                if (current->location.kind == LocationKind::actor && current->location.actor != handle) { return false; }
                return CanAccessStorage(handle, GetItemStorage(root));
            }
            root = current->location.item;
        }
        return false;
    }
    }
    return false;
}

InventoryResult World::TransferInventoryItem(ActorHandle actor, PersistentId id,
    const ItemStorage& source, const ItemStorage& destination) {
    if (!ValidItemStorage(source) || !ValidItemStorage(destination)) { return {InventoryOutcome::InvalidDestination}; }
    const auto handle = FindItem(id);
    const auto* item = GetItem(handle);
    if (!item || GetItemStorage(handle) != source) { return {InventoryOutcome::InvalidItem}; }
    if (IsReservedItem(handle) || item->location.installation.value) { return {InventoryOutcome::Reserved}; }
    if ((item->location.kind == LocationKind::actor && item->location.actor != actor)
        || !CanAccessStorage(actor, source) || !CanAccessStorage(actor, destination)) {
        return {InventoryOutcome::Inaccessible};
    }
    if (source == destination) { return {InventoryOutcome::AlreadyThere}; }
    const auto* owner = GetActor(actor);
    if (GetActorRemainingTicks(actor) || owner->combat.action.kind != ActorActionKind::None) { return {InventoryOutcome::Busy}; }
    ItemLocation location;
    switch (destination.kind) {
    case ItemStorageKind::Inventory: location = ItemLocation::Carried(actor); break;
    case ItemStorageKind::Ground: location = ItemLocation::Ground(destination.position); break;
    case ItemStorageKind::Object: location = ItemLocation::Stored(FindObject(destination.id)); break;
    case ItemStorageKind::Container: location = ItemLocation::Inside(FindItem(destination.id)); break;
    }
    const auto result = TransferItem(handle, location);
    if (!result) { return {result.error() == WorldError::capacity_exceeded ? InventoryOutcome::NoCapacity : InventoryOutcome::InvalidDestination}; }
    return {InventoryOutcome::Succeeded, 100};
}

}
