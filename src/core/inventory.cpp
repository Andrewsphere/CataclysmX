#include "core/inventory.h"
#include "core/world.h"
#include "combat/combat_system.h"
#include "skills/learning_system.h"
#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <cstdlib>

namespace cataclysmx {
namespace {
bool UsableHand(const Actor& actor, unsigned hand) {
    return actor.combat.body.kind == BodyKind::Detailed
        && Functional(actor.combat.body, hand == 0 ? BodyPart::LeftHand : BodyPart::RightHand);
}
void RefreshWielded(Actor& actor) {
    actor.wieldedItem = actor.equipment.hands[0].item.value
        ? actor.equipment.hands[0].item : actor.equipment.hands[1].item;
}
bool ReservedEquipmentItem(const World& world, ItemHandle handle) {
    for (unsigned depth = 0; depth < 64; ++depth) {
        const auto* item = world.GetItem(handle);
        if (!item) { return false; }
        if (world.IsReservedItem(handle) || item->location.installation.value
            || world.GetContent().GetItem(item->type).unfinishedCraft) { return true; }
        if (item->location.kind != LocationKind::item) { return false; }
        handle = item->location.item;
    }
    return true;
}
InventoryResult EquipmentResult(EquipmentQuery query) {
    switch (query.reason) {
    case CombatRefusal::None: return {InventoryOutcome::Succeeded, query.baseCost};
    case CombatRefusal::Busy: return {InventoryOutcome::Busy};
    case CombatRefusal::MissingCapability: return {InventoryOutcome::NeedsHands};
    case CombatRefusal::MissingSource: return {InventoryOutcome::InvalidItem};
    case CombatRefusal::NeedsFreeHand:
    case CombatRefusal::InvalidGrip:
    case CombatRefusal::InvalidTarget: return {InventoryOutcome::InvalidDestination};
    default: return {InventoryOutcome::Inaccessible};
    }
}
}

EquipmentQuery World::QueryEquipment(ActorHandle handle, const EquipmentRequest& request) const {
    const auto* actor = GetActor(handle);
    if (!actor) { return {CombatRefusal::InvalidActor}; }
    if (!IsActorAlive(handle)) { return {CombatRefusal::Dead}; }
    if (actor->parked || !IsActive(actor->position)) { return {CombatRefusal::Inactive}; }
    if (GetActorRemainingTicks(handle) || actor->combat.action.kind != ActorActionKind::None
        || Learning().Busy(handle)) { return {CombatRefusal::Busy}; }
    if (request.operation > EquipmentOperation::Remove || request.hand > Hand::Right
        || request.grip > GripKind::TwoHanded) { return {CombatRefusal::InvalidTarget}; }
    if (((request.operation == EquipmentOperation::Release || request.operation == EquipmentOperation::Regrip) && request.item)
        || ((request.operation == EquipmentOperation::Wear || request.operation == EquipmentOperation::Remove)
            && (request.hand != Hand::Left || request.grip != GripKind::OneHanded))) { return {CombatRefusal::InvalidTarget}; }
    const unsigned hand = static_cast<unsigned>(request.hand);
    const auto selected = request.operation == EquipmentOperation::Regrip
        || request.operation == EquipmentOperation::Release ? actor->equipment.hands[hand].item : FindItem(request.item);
    const auto* item = GetItem(selected);
    if (!item) { return {CombatRefusal::MissingSource}; }
    if (request.operation == EquipmentOperation::Release) { return {CombatRefusal::None, 100}; }
    const auto& definition = GetContent().GetItem(item->type);
    if (request.operation == EquipmentOperation::Remove) {
        if (eastl::find(actor->equipment.worn.begin(), actor->equipment.worn.end(), selected) == actor->equipment.worn.end()) {
            return {CombatRefusal::MissingSource};
        }
        return {CombatRefusal::None, definition.armor->removeCost};
    }
    if (IsReservedItem(selected) || item->location.installation.value || definition.unfinishedCraft) {
        return {CombatRefusal::MissingSource};
    }
    auto root = selected;
    for (unsigned depth = 0;; ++depth) {
        const auto* current = GetItem(root);
        if (!current || depth >= 64 || current->location.installation.value || IsReservedItem(root)) {
            return {CombatRefusal::MissingSource};
        }
        if (current->location.kind != LocationKind::item) {
            if (current->location != ItemLocation::Carried(handle)) { return {CombatRefusal::MissingSource}; }
            break;
        }
        root = current->location.item;
    }
    const bool worn = eastl::find(actor->equipment.worn.begin(), actor->equipment.worn.end(), selected) != actor->equipment.worn.end();
    if (request.operation == EquipmentOperation::Wear) {
        if (!definition.armor || worn || actor->equipment.hands[0].item == selected
            || actor->equipment.hands[1].item == selected) { return {CombatRefusal::InvalidTarget}; }
        if (eastl::any_of(definition.armor->regions.begin(), definition.armor->regions.end(), [&](const ArmorRegionDefinition& region) {
            return !HasContact(actor->combat.body.kind, region.region);
        })) { return {CombatRefusal::MissingCapability}; }
        for (const auto other : actor->equipment.worn) {
            const auto& armor = *GetContent().GetItem(GetItem(other)->type).armor;
            if (armor.layer == definition.armor->layer && (armor.rigidRegions & definition.armor->rigidRegions)) {
                return {CombatRefusal::InvalidTarget};
            }
        }
        return {CombatRefusal::None, definition.armor->wearCost};
    }
    if (worn) { return {CombatRefusal::InvalidTarget}; }
    const auto& melee = ItemMelee(definition);
    if (eastl::none_of(melee.grips.begin(), melee.grips.end(), [&](const GripDefinition& grip) { return grip.kind == request.grip; })) {
        return {CombatRefusal::InvalidGrip};
    }
    if (!UsableHand(*actor, hand) || (request.grip == GripKind::TwoHanded && !UsableHand(*actor, 1 - hand))) {
        return {CombatRefusal::MissingCapability};
    }
    for (unsigned index = 0; index < 2; ++index) {
        const auto held = actor->equipment.hands[index].item;
        if ((index == hand || request.grip == GripKind::TwoHanded) && held.value && held != selected) {
            return {CombatRefusal::NeedsFreeHand};
        }
        if (request.operation == EquipmentOperation::Hold && held == selected) { return {CombatRefusal::InvalidTarget}; }
    }
    return {CombatRefusal::None, 100};
}

InventoryResult World::HoldItem(ActorHandle actor, ItemHandle item, Hand hand, GripKind grip) {
    if (ReservedEquipmentItem(*this, item)) { return {InventoryOutcome::Reserved}; }
    const auto* value = GetItem(item);
    const auto query = QueryEquipment(actor, {EquipmentOperation::Hold, value ? value->id : 0, hand, grip});
    if (query.reason != CombatRefusal::None) { return EquipmentResult(query); }
    if (!TransferItem(item, ItemLocation::Carried(actor))) { return {InventoryOutcome::NoCapacity}; }
    auto* owner = MutableActor(actor);
    owner->equipment.hands[static_cast<unsigned>(hand)] = {item, grip};
    if (grip == GripKind::TwoHanded) { owner->equipment.hands[1 - static_cast<unsigned>(hand)] = {item, grip}; }
    RefreshWielded(*owner);
    ActorChanged(actor);
    return EquipmentResult(query);
}

InventoryResult World::ReleaseHand(ActorHandle actor, Hand hand) {
    const auto query = QueryEquipment(actor, {EquipmentOperation::Release, 0, hand});
    if (query.reason != CombatRefusal::None) { return EquipmentResult(query); }
    auto* owner = MutableActor(actor);
    const auto item = owner->equipment.hands[static_cast<unsigned>(hand)].item;
    for (auto& held : owner->equipment.hands) { if (held.item == item) { held = {}; } }
    RefreshWielded(*owner);
    ActorChanged(actor);
    return EquipmentResult(query);
}

InventoryResult World::RegripItem(ActorHandle actor, Hand hand, GripKind grip) {
    const auto query = QueryEquipment(actor, {EquipmentOperation::Regrip, 0, hand, grip});
    if (query.reason != CombatRefusal::None) { return EquipmentResult(query); }
    auto* owner = MutableActor(actor);
    const auto item = owner->equipment.hands[static_cast<unsigned>(hand)].item;
    for (auto& held : owner->equipment.hands) { if (held.item == item) { held = {}; } }
    owner->equipment.hands[static_cast<unsigned>(hand)] = {item, grip};
    if (grip == GripKind::TwoHanded) { owner->equipment.hands[1 - static_cast<unsigned>(hand)] = {item, grip}; }
    RefreshWielded(*owner);
    ActorChanged(actor);
    return EquipmentResult(query);
}

InventoryResult World::WearItem(ActorHandle actor, ItemHandle item) {
    if (ReservedEquipmentItem(*this, item)) { return {InventoryOutcome::Reserved}; }
    const auto* value = GetItem(item);
    const auto query = QueryEquipment(actor, {EquipmentOperation::Wear, value ? value->id : 0});
    if (query.reason != CombatRefusal::None) { return EquipmentResult(query); }
    if (!TransferItem(item, ItemLocation::Carried(actor))) { return {InventoryOutcome::NoCapacity}; }
    MutableActor(actor)->equipment.worn.push_back(item);
    ActorChanged(actor);
    return EquipmentResult(query);
}

InventoryResult World::RemoveWornItem(ActorHandle actor, ItemHandle item) {
    const auto* value = GetItem(item);
    const auto query = QueryEquipment(actor, {EquipmentOperation::Remove, value ? value->id : 0});
    if (query.reason != CombatRefusal::None) { return EquipmentResult(query); }
    auto& worn = MutableActor(actor)->equipment.worn;
    worn.erase(eastl::find(worn.begin(), worn.end(), item));
    ActorChanged(actor);
    return EquipmentResult(query);
}

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
    if (!actor || !IsActorAlive(handle) || actor->parked || !IsActive(actor->position) || !tile || actor->position.y != position.y
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
    if (!ValidItemStorage(storage) || !actor || !IsActorAlive(handle) || actor->parked || !IsActive(actor->position)) { return false; }
    switch (storage.kind) {
    case ItemStorageKind::Inventory: return true;
    case ItemStorageKind::Ground: return CanAccessGroundStorage(handle, storage.position);
    case ItemStorageKind::Object: {
        const auto object = FindObject(storage.id);
        const auto* value = GetObject(object);
        const auto* definition = value ? GetContent().TryGetObject(value->type) : nullptr;
        return definition && (definition->behavior == ObjectBehaviorKind::None
                || definition->behavior == ObjectBehaviorKind::Forge)
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
