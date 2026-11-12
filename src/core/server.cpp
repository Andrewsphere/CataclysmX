#include <cstdlib>

#include "core/server.h"

#include "core/simulation.h"
#include "core/perception.h"
#include "core/lighting.h"
#include "combat/combat_system.h"
#include "objects/object_behavior.h"
#include "worldgen/worldgen.h"

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/sort.h>
#include <EASTL/utility.h>

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error Engine must compile without exceptions
#endif

namespace cataclysmx {
namespace {

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

template<class T>
T Take(WorldResult<T> result) {
    Require(result.has_value());
    return *result;
}

bool Cardinal(int dx, int dz, int dy) {
    return (dy == 0 && dz == 0 && (dx == -1 || dx == 1))
        || (dy == 0 && dx == 0 && (dz == -1 || dz == 1))
        || (dx == 0 && dz == 0 && (dy == -1 || dy == 1));
}

ChunkCoord BubbleCenter(TileCoord position) {
    const ChunkCoord chunk = ChunkOf(position);
    return {chunk.x, chunk.z, 0};
}

bool BubbleFits(TileCoord position, int radius) {
    if (!ValidPosition(position)) {
        return false;
    }
    const ChunkCoord center = ChunkOf(position);
    constexpr int minimum = -world_coordinate_limit / chunk_size;
    constexpr int maximum = world_coordinate_limit / chunk_size;
    return center.x >= minimum + radius && center.x <= maximum - radius
        && center.z >= minimum + radius && center.z <= maximum - radius;
}

bool ResidencyFits(TileCoord position, int active, int resident,
    eastl::optional<CircleResidency> circle) {
    if (!BubbleFits(position, active)) { return false; }
    if (!circle) { return BubbleFits(position, resident); }
    const int radius = circle->radiusTiles;
    return radius >= 0 && radius <= maxCircleResidencyRadius
        && position.x >= -world_coordinate_limit + radius
        && position.x <= world_coordinate_limit - radius
        && position.z >= -world_coordinate_limit + radius
        && position.z <= world_coordinate_limit - radius;
}

LoadRequest RequestResident(World& world, TileCoord position, int resident,
    eastl::optional<CircleResidency> circle) {
    return circle ? Take(world.RequestCircle({position, circle->radiusTiles}, LoadMode::resident))
        : Take(world.RequestChunks({BubbleCenter(position), resident}, LoadMode::resident));
}

void UpdateResident(World& world, LoadRequest request, TileCoord position, int resident,
    eastl::optional<CircleResidency> circle) {
    if (circle) {
        Require(world.UpdateCircleRequest(request, {position, circle->radiusTiles}, LoadMode::resident).has_value());
    } else {
        Require(world.UpdateRequest(request, {BubbleCenter(position), resident}, LoadMode::resident).has_value());
    }
}

bool FootprintLoaded(const World& world, TileCoord position, int active, int resident,
    eastl::optional<CircleResidency> circle) {
    const auto center = BubbleCenter(position);
    const int extent = circle ? (circle->radiusTiles + chunk_size - 1) / chunk_size : resident;
    const int radius = eastl::max(active, extent);
    for (int z = center.z - radius; z <= center.z + radius; ++z) {
        for (int x = center.x - radius; x <= center.x + radius; ++x) {
            const bool activeColumn = std::abs(x - center.x) <= active && std::abs(z - center.z) <= active;
            bool residentColumn = true;
            if (circle) {
                const int dx = position.x - eastl::clamp(position.x, x * chunk_size, (x + 1) * chunk_size - 1);
                const int dz = position.z - eastl::clamp(position.z, z * chunk_size, (z + 1) * chunk_size - 1);
                residentColumn = dx * dx + dz * dz <= circle->radiusTiles * circle->radiusTiles;
            } else {
                residentColumn = std::abs(x - center.x) <= resident && std::abs(z - center.z) <= resident;
            }
            // World demand always materializes full-height columns.
            if ((activeColumn || residentColumn) && !world.GetChunk({x, z, 0})) { return false; }
        }
    }
    return true;
}

void ReleaseStaging(World& world, Vector<LoadRequest>& staging) {
    for (LoadRequest request : staging) { Require(world.ReleaseRequest(request).has_value()); }
    staging.clear();
}

SaveResult<void> StageBubble(World& world, TileCoord position, int active, int resident,
    eastl::optional<CircleResidency> circle, Vector<LoadRequest>& staging) {
    if (FootprintLoaded(world, position, active, resident, circle)) { return {}; }
    staging.push_back(RequestResident(world, position, resident, circle));
    if (circle) {
        staging.push_back(Take(world.RequestChunks({BubbleCenter(position), active}, LoadMode::resident)));
    }
    const auto result = world.ReconcileChunks();
    if (!result) { ReleaseStaging(world, staging); }
    return result;
}

template<class Items>
ItemHandle SelectItem(const World& world, const Items& items, PersistentId id) {
    ItemHandle selected{};
    PersistentId smallest = eastl::numeric_limits<PersistentId>::max();
    for (ItemHandle handle : items) {
        const Item* item = world.GetItem(handle);
        Require(item != nullptr);
        if (id != 0 && item->id == id) {
            return handle;
        }
        if (id == 0 && item->id < smallest) {
            selected = handle;
            smallest = item->id;
        }
    }
    return selected;
}

Outcome TransferFailure(WorldError error) {
    switch (error) {
    case WorldError::capacity_exceeded: return Outcome::capacity_exceeded;
    case WorldError::chunk_unavailable: return Outcome::unavailable;
    case WorldError::not_container:
    case WorldError::incompatible_item:
    case WorldError::containment_cycle: return Outcome::invalid_container;
    default: return Outcome::invalid_command;
    }
}

void AppendItemTreeDescription(const World& world, ActorHandle actor, ItemHandle handle,
    Vector<InventoryEntry>& result) {
    const Item* item = world.GetItem(handle);
    Require(item != nullptr);
    const ItemDefinition& definition = world.GetContent().GetItem(item->type);
    InventoryEntry entry{item->id, item->type, static_cast<unsigned int>(item->contents.size())};
    if (item->location.kind == LocationKind::item) {
        entry.parent = world.GetItem(item->location.item)->id;
        entry.installation = item->location.installation;
    }
    entry.massGrams = world.GetItemMass(handle);
    entry.volumeMl = definition.volumeMl;
    if (const auto* container = ItemContainer(definition)) {
        entry.capacityVolumeMl = container->capacityVolumeMl;
        entry.usedVolumeMl = world.GetContainerUsedVolume(handle);
    }
    auto presentation = world.DescribeItem(actor, handle);
    entry.energy = eastl::move(presentation.energy);
    entry.activities = eastl::move(presentation.activities);
    entry.actions = eastl::move(presentation.actions);
    result.push_back(eastl::move(entry));
    // World bounds containment depth; traverse borrows without a handle work list.
    for (ItemHandle child : item->contents) { AppendItemTreeDescription(world, actor, child, result); }
}

template<class Items>
Vector<InventoryEntry> DescribeItems(const World& world, ActorHandle actor, const Items& items) {
    Vector<InventoryEntry> result;
    for (ItemHandle handle : items) { AppendItemTreeDescription(world, actor, handle, result); }
    eastl::sort(result.begin(), result.end(),
        [](const InventoryEntry& a, const InventoryEntry& b) { return a.id < b.id; });
    return result;
}

Outcome MovementFailure(WorldError error) {
    switch (error) {
    case WorldError::blocked: return Outcome::blocked;
    case WorldError::occupied: return Outcome::occupied;
    case WorldError::invalid_position:
    case WorldError::chunk_unavailable: return Outcome::unavailable;
    default: std::abort();
    }
}

bool Ready(const World& world, ActorHandle handle) {
    const auto* actor = world.GetActor(handle);
    return actor && !actor->actionDeadline.suspended
        && actor->combat.action.kind == ActorActionKind::None && world.GetActorRemainingTicks(handle) == 0;
}

Outcome CombatOutcome(AttackOutcome outcome) {
    switch (outcome) {
    case AttackOutcome::Unavailable: return Outcome::unavailable;
    case AttackOutcome::NotReady: return Outcome::not_ready;
    case AttackOutcome::NoTarget: return Outcome::no_target;
    case AttackOutcome::OutOfReach: return Outcome::no_target;
    case AttackOutcome::NoMove: return Outcome::no_combat_move;
    case AttackOutcome::Windup: return Outcome::attack_windup;
    case AttackOutcome::Miss: return Outcome::attack_missed;
    case AttackOutcome::Parried: return Outcome::attack_parried;
    case AttackOutcome::Blocked: return Outcome::attack_blocked;
    case AttackOutcome::Hit: return Outcome::attacked;
    case AttackOutcome::Defeated: return Outcome::target_defeated;
    }
    std::abort();
}

}

bool ValidCommandShape(const Command& command) {
    if (command.style > CombatStyle::Defensive) { return false; }
    switch (command.kind) {
    case CommandKind::attack:
    case CommandKind::combat_style:
        return command.dx == 0 && command.dz == 0 && command.dy == 0;
    case CommandKind::wait: return command.waitCost != 0;
    case CommandKind::move:
    case CommandKind::interact: return Cardinal(command.dx, command.dz, command.dy);
    case CommandKind::pick_up:
    case CommandKind::drop: return true;
    case CommandKind::store_item:
        return command.item != 0 && command.item != command.otherItem;
    case CommandKind::use_item:
        if (command.item == 0 || command.capability.value == 0
            || command.capability.value > maxItemCapabilities
            || !ValidItemActionId(command.itemAction) || command.otherItem == command.item) { return false; }
        // Admission checks the generic argument envelope. The selected behavior's
        // declaration is checked against current authoritative state at execution.
        if (command.otherItem != 0) { return command.dx == 0 && command.dz == 0 && command.dy == 0; }
        return (command.dx == 0 && command.dz == 0 && command.dy == 0)
            || Cardinal(command.dx, command.dz, command.dy);
    }
    return false;
}

const char* OutcomeMessage(Outcome outcome) {
    switch (outcome) {
    case Outcome::waited: return "You wait.";
    case Outcome::moved: return "You move.";
    case Outcome::picked_up: return "You pick up the item.";
    case Outcome::dropped: return "You drop the item.";
    case Outcome::opened: return "You open the door.";
    case Outcome::closed: return "You close the door.";
    case Outcome::toggled: return "You toggle the object.";
    case Outcome::blocked: return "The way is blocked.";
    case Outcome::occupied: return "The tile is occupied.";
    case Outcome::nothing_here: return "There is nothing here.";
    case Outcome::invalid_command: return "Invalid command.";
    case Outcome::not_carried: return "You are not carrying that item.";
    case Outcome::unavailable: return "That location is unavailable.";
    case Outcome::not_ready: return "You are still busy.";
    case Outcome::queued: return "You queue your next action.";
    case Outcome::stale_input: return "Stale input.";
    case Outcome::duplicate_input: return "Duplicate input.";
    case Outcome::used_item: return "You use the item.";
    case Outcome::stored_item: return "You move the item.";
    case Outcome::item_use_failed: return "The item cannot be used.";
    case Outcome::capacity_exceeded: return "Not enough carrying or storage capacity.";
    case Outcome::invalid_container: return "That item cannot be stored there.";
    case Outcome::attacked: return "Your attack hits.";
    case Outcome::attack_missed: return "Your attack misses.";
    case Outcome::attack_parried: return "Your attack is parried.";
    case Outcome::attack_blocked: return "Protection stops your attack.";
    case Outcome::attack_windup: return "You prepare a heavy strike.";
    case Outcome::target_defeated: return "You defeat your target.";
    case Outcome::no_target: return "No perceived hostile is within melee reach.";
    case Outcome::no_combat_move: return "You cannot perform an attack with your usable limbs.";
    case Outcome::combat_style_changed: return "You change your combat style.";
    }
    std::abort();
}

const char* CommandResultMessage(const CommandResult& result) {
    if (result.outcome != Outcome::item_use_failed) { return OutcomeMessage(result.outcome); }
    return result.itemReason.message.fallback.empty() ? ItemUseMessage(result.itemOutcome)
        : result.itemReason.message.fallback.c_str();
}

ContentResult<void> ValidateServerContent(const ContentCatalog& content, Diagnostics* diagnostics) {
    for (const char* id : {"floor", "wall", "closed_door"}) {
        if (content.FindTerrain(id).value == 0) {
            if (diagnostics) {
                String message = "Missing terrain required by the demo: ";
                message += id;
                diagnostics->Add(eastl::string_view(message.data(), message.size()));
            }
            return eastl::unexpected(ContentError::InvalidData);
        }
    }
    if (!content.GetTerrain(content.FindTerrain("floor")).passable) {
        if (diagnostics) { diagnostics->Add("Demo terrain 'floor' must be passable"); }
        return eastl::unexpected(ContentError::InvalidData);
    }
    for (const char* id : {"rock", "backpack", "bandage"}) {
        if (content.FindItem(id).value == 0) {
            if (diagnostics) {
                String message = "Missing item required by the demo: "; message += id;
                diagnostics->Add({message.data(), message.size()});
            }
            return eastl::unexpected(ContentError::InvalidData);
        }
    }
    const auto* backpack = ItemContainer(content.GetItem(content.FindItem("backpack")));
    if (!backpack || backpack->capacityVolumeMl < content.GetItem(content.FindItem("bandage")).volumeMl) {
        if (diagnostics) { diagnostics->Add("Demo backpack must hold the bandage"); }
        return eastl::unexpected(ContentError::InvalidData);
    }
    return ValidateWorldgenContent(content, diagnostics);
}

Server::Server(const ContentCatalog& content, std::uint32_t seed) : world_(content, seed), rng_state_(seed) {
    Require(ValidateServerContent(content).has_value());
    active_request_ = Take(world_.RequestChunks({{0, 0}, active_radius_}, LoadMode::active));
    resident_request_ = RequestResident(world_, {0, 0}, resident_radius_, circleResidency_);
    Require(world_.ReconcileChunks().has_value());
    constexpr TileCoord floors[] = {{0, 0}, {1, 0}, {2, 0}, {5, 5}};
    for (TileCoord position : floors) {
        Require(world_.SetTile(position, {content.FindTerrain("floor"), 0}).has_value());
    }
    Require(world_.SetTile({3, 0}, {content.FindTerrain("closed_door"), 0}).has_value());
    Require(world_.SetTile({3, 1}, {content.FindTerrain("wall"), 0}).has_value());
    player_ = Take(world_.CreateActor({0, 0}, true));
    players_.push_back({world_.GetActor(player_)->id, "Host", 0, {0, 0}, PlayerConnectionState::Active});
    const ItemHandle backpack = Take(world_.CreateItem(content.FindItem("backpack"), ItemLocation::Ground({0, 0})));
    Take(world_.CreateItem(content.FindItem("bandage"), ItemLocation::Inside(backpack)));
    Take(world_.CreateItem(content.FindItem("rock"), ItemLocation::Ground({1, 0})));
    Take(world_.CreateObject({2, 0}, false));
    const auto enemy = Take(world_.CreateActor({5, 5}));
    auto enemyCombat = MakeCombatState(false);
    enemyCombat.faction = CombatFaction::Undead;
    Require(world_.Combat().Configure(enemy, enemyCombat).has_value());
    if (content.FindAiProfile("sighted_zombie").value) {
        Require(world_.SetActorAiProfile(enemy, "sighted_zombie").has_value());
    } else {
        Require(world_.SetActorController(enemy, AiControllerKind::Sighted).has_value());
    }
    // Optional demo equipment follows the original entities, preserving their IDs.
    for (const char* id : {"field_charger", "rechargeable_battery", "door_controller", "utility_controller"}) {
        if (const auto type = content.FindItem(id); type.value != 0) {
            Take(world_.CreateItem(type, ItemLocation::Ground({0, 1})));
        }
    }
    const TerrainId air = content.FindTerrain("air");
    const TerrainId stairs = content.FindTerrain("stairs");
    if (air.value != 0 && stairs.value != 0 && content.GetTerrain(air).openSpace
        && content.GetTerrain(stairs).stairs && content.GetTerrain(stairs).passable) {
        // West of the original demo: stairs connect a basement, ground, and a
        // raised platform. Its hole drops through ground air into the basement.
        // Terrain edits allocate no entity IDs; the extra rock follows all of
        // the original equipment so replay identities remain stable.
        for (int z = 0; z <= 2; ++z) {
            for (int x = -5; x <= -3; ++x) {
                for (int y = -1; y <= 1; ++y) {
                    Require(world_.SetTile({x, z, y}, {content.FindTerrain("floor"), 0}).has_value());
                }
            }
        }
        for (int y = -1; y <= 1; ++y) {
            Require(world_.SetTile({-3, 0, y}, {stairs, 0}).has_value());
        }
        Require(world_.SetTile({-5, 2, -1}, {content.FindTerrain("wall"), 0}).has_value());
        Require(world_.SetTile({-4, 1, 0}, {air, 0}).has_value());
        Require(world_.SetTile({-4, 1, 1}, {air, 0}).has_value());
        Take(world_.CreateItem(content.FindItem("rock"), ItemLocation::Ground({-4, 1, -1})));
        const auto lamp = Take(world_.CreateObject({-5, 0, -1}, true));
        Require(world_.SetObjectAppearance(lamp, ObjectKind::Lamp).has_value());
        Require(world_.SetObjectLight(lamp, {240, 4}).has_value());
        const auto chair = Take(world_.CreateObject({-5, 1, -1}));
        Require(world_.SetObjectAppearance(chair, ObjectKind::Chair, ObjectFacing::East).has_value());
        const auto table = Take(world_.CreateObject({-3, 2, -1}));
        Require(world_.SetObjectAppearance(table, ObjectKind::Table).has_value());
    }
    // Append optional visual test items after all existing demo entities.
    const char* bulbNames[] = {"blinking_lightbulb", "toggle_lightbulb", "single_blink_lightbulb"};
    constexpr TileCoord bulbPositions[] = {{1, 1}, {2, 1}, {1, 2}};
    for (unsigned int index = 0; index < 3; ++index) {
        if (const auto type = content.FindItem(bulbNames[index]); type.value != 0) {
            Require(world_.SetTile(bulbPositions[index], {content.FindTerrain("floor"), 0}).has_value());
            Take(world_.CreateItem(type, ItemLocation::Ground(bulbPositions[index])));
        }
    }
    world_.UpdatePlayerObservations();
    SynchronizePacing();
}

CommandResult Server::Send(Command command) {
    if (!ValidCommandShape(command)) { return {Outcome::invalid_command, false, GetTick()}; }
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    const PlayerSlot* host = FindPlayer(GetHostPlayerId());
    Require(host != nullptr);
    legacySequence_ = eastl::max(legacySequence_, host->lastSequence);
    Require(legacySequence_ != eastl::numeric_limits<std::uint64_t>::max());
    bool otherActive = false;
    for (const PlayerSlot& slot : players_) {
        if (slot.id != GetHostPlayerId() && slot.state == PlayerConnectionState::Active) {
            otherActive = true;
        }
    }
    if (!Ready(world_, player_) || otherActive) {
        // Keep the latest intent, not a chain of key-repeat-generated actions.
        // Destination, item ownership and cost are checked when it becomes due.
        return Submit(GetHostPlayerId(), {++legacySequence_, FindPlayer(GetHostPlayerId())->opportunity, command});
    }
    const CommandResult admission = Submit(GetHostPlayerId(),
        {++legacySequence_, FindPlayer(GetHostPlayerId())->opportunity, command});
    if (admission.outcome != Outcome::queued) { return admission; }
    const auto barrier = cataclysmx::AdvanceUntilInput(world_, rng_state_, GetTick());
    Require(barrier.stop == SimulationStop::InputRequired);
    SessionUpdate resolved;
    Require(ResolveRound(resolved, {}));
    for (const PlayerCommandResult& consumed : resolved.commandResults) {
        if (consumed.player != GetHostPlayerId()) { continue; }
        CommandResult result = consumed.result;
        if (result.advanced) {
            const SessionUpdate update = Update(0);
            result.tick = update.tick;
            result.storageError = update.storageError;
        }
        return result;
    }
    return admission;
}

CommandResult Server::Execute(ActorHandle handle, PlayerSlot& slot, Command command) {
    const Actor* actor = world_.GetActor(handle);
    Require(actor != nullptr);
    const TileCoord position = actor->position;
    const auto failed = [this](Outcome outcome) { return CommandResult{outcome, false, GetTick()}; };
    if (!Ready(world_, handle)) { return failed(Outcome::not_ready); }
    const auto attack = [this, handle](ActorHandle target) {
        const auto result = world_.Combat().Attack(handle, target, rng_state_);
        if (result.committed) { world_.UpdatePlayerObservations(); }
        CommandResult command{CombatOutcome(result.outcome), result.committed, GetTick()};
        command.cost = result.cost;
        return command;
    };
    Outcome outcome = Outcome::invalid_command;
    std::uint32_t baseCost = command.kind == CommandKind::wait ? command.waitCost : ticks_per_turn;
    switch (command.kind) {
    case CommandKind::attack: {
        const auto target = command.target ? world_.FindActor(command.target) : world_.Combat().FindTarget(handle);
        const auto* other = world_.GetActor(target);
        if (!other || !world_.CanPlayerSee(handle, other->position)) { return failed(Outcome::no_target); }
        return attack(target);
    }
    case CommandKind::combat_style:
        if (!world_.Combat().SetStyle(handle, command.style)) { return failed(Outcome::invalid_command); }
        return {Outcome::combat_style_changed, false, GetTick()};
    case CommandKind::wait:
        if (command.waitCost == 0) { return failed(Outcome::invalid_command); }
        outcome = Outcome::waited;
        break;
    case CommandKind::move: {
        if (!Cardinal(command.dx, command.dz, command.dy)) {
            return failed(Outcome::invalid_command);
        }
        const TileCoord destination{position.x + command.dx, position.z + command.dz, position.y + command.dy};
        const auto occupant = world_.ActorAt(destination);
        const auto* other = world_.GetActor(occupant);
        if (other && Hostile(actor->combat.faction, other->combat.faction)) {
            if (!world_.CanPlayerSee(handle, destination)) { return failed(Outcome::occupied); }
            // Bump requests combat against this actor. It does not move or stage
            // a new residency bubble and cannot fall through into movement.
            return attack(occupant);
        }
        baseCost = LocomotionBaseCost(actor->combat.body);
        // Validate the complete future bubble before acquiring any demand.
        if (!ResidencyFits(destination, active_radius_, resident_radius_, circleResidency_)) {
            return failed(Outcome::unavailable);
        }
        Vector<LoadRequest> staging;
        // Preserve the old bubble while staging every chunk the new requests
        // will need. Reconcile failures leave all live world state untouched.
        const auto loaded = StageBubble(world_, destination, active_radius_, resident_radius_, circleResidency_, staging);
        if (!loaded) {
            Require(world_.ReconcileChunks().has_value());
            return {Outcome::unavailable, false, GetTick(), loaded.error()};
        }
        const auto moved = world_.TryActorStep(handle, {command.dx, command.dz, command.dy});
        if (!moved) {
            if (!staging.empty()) {
                ReleaseStaging(world_, staging);
                Require(world_.ReconcileChunks().has_value());
            }
            return failed(MovementFailure(moved.error()));
        }
        // The player has entered the destination before any old demand can be
        // removed, preserving its runtime key even with zero-radius bubbles.
        // The union is resident now; changing activity and releasing temporary
        // demand below cannot require another storage read.
        CommitPlayerBubble(slot);
        ReleaseStaging(world_, staging);
        Require(world_.ReconcileChunks().has_value());
        outcome = Outcome::moved;
        break;
    }
    case CommandKind::pick_up: {
        const ItemHandle item = SelectItem(world_, world_.ItemsAt(position), command.item);
        if (item.value == 0) {
            return failed(Outcome::nothing_here);
        }
        if (const auto moved = world_.TransferItem(item, ItemLocation::Carried(handle)); !moved) {
            return failed(TransferFailure(moved.error()));
        }
        outcome = Outcome::picked_up;
        break;
    }
    case CommandKind::drop: {
        const ItemHandle item = SelectItem(world_, actor->inventory, command.item);
        if (item.value == 0) {
            return failed(Outcome::not_carried);
        }
        Require(world_.TransferItem(item, ItemLocation::Ground(position)).has_value());
        outcome = Outcome::dropped;
        break;
    }
    case CommandKind::use_item: {
        const ItemHandle item = world_.FindItem(command.item);
        const ItemUseResult used = world_.UseItem(handle, item,
            {command.capability, command.itemAction, world_.FindItem(command.otherItem), command.dx, command.dz, command.dy});
        if (used.outcome != ItemUseOutcome::Succeeded) {
            auto result = failed(Outcome::item_use_failed);
            result.itemOutcome = used.outcome;
            result.itemReason = used.reason;
            return result;
        }
        baseCost = used.baseCost;
        outcome = Outcome::used_item;
        break;
    }
    case CommandKind::store_item: {
        const ItemHandle item = world_.FindItem(command.item);
        if (!world_.CanAccessItem(handle, item)) { return failed(Outcome::not_carried); }
        ItemLocation destination = ItemLocation::Carried(handle);
        if (command.otherItem != 0) {
            const ItemHandle container = world_.FindItem(command.otherItem);
            if (!world_.CanAccessItem(handle, container)) { return failed(Outcome::invalid_container); }
            destination = ItemLocation::Inside(container);
        }
        if (const auto moved = world_.TransferItem(item, destination); !moved) {
            return failed(TransferFailure(moved.error()));
        }
        outcome = Outcome::stored_item;
        break;
    }
    case CommandKind::interact: {
        if (!Cardinal(command.dx, command.dz, command.dy)) {
            return failed(Outcome::invalid_command);
        }
        const TileCoord destination{position.x + command.dx, position.z + command.dz, position.y + command.dy};
        const Tile* tile = world_.GetTile(destination);
        if (!tile) {
            return failed(Outcome::unavailable);
        }
        if (command.dy != 0 && !OpenVerticalFace(world_, position, destination)) {
            return failed(Outcome::blocked);
        }
        const TerrainDefinition& terrain = world_.GetContent().GetTerrain(tile->terrain);
        if (terrain.opensTo.value != 0 || terrain.closesTo.value != 0) {
            const bool closing = terrain.closesTo.value != 0;
            if (closing && world_.ActorAt(destination).value != 0) {
                return failed(Outcome::occupied);
            }
            Tile changed = *tile;
            changed.terrain = closing ? terrain.closesTo : terrain.opensTo;
            Require(world_.SetTile(destination, changed).has_value());
            outcome = closing ? Outcome::closed : Outcome::opened;
        } else {
            const ObjectHandle object = world_.ObjectAt(destination);
            if (object.value == 0) {
                return failed(Outcome::nothing_here);
            }
            if (const auto state = world_.DescribeObjectState(object)) {
                ObjectAction action = ObjectAction::Start;
                if (state->running) {
                    action = ObjectAction::Stop;
                } else if (!state->hasJob && !world_.GetObject(object)->contents.empty()) {
                    action = ObjectAction::Collect;
                }
                const ObjectUseResult used = world_.UseObject(handle, object, action);
                if (used.outcome != ObjectUseOutcome::Succeeded) {
                    return failed(used.reason == ObjectUseReason::NoContents
                        || used.reason == ObjectUseReason::NoJob ? Outcome::nothing_here : Outcome::blocked);
                }
                baseCost = used.baseCost;
            } else {
                const bool enabled = world_.GetObject(object)->enabled;
                Require(world_.SetObjectEnabled(object, !enabled).has_value());
            }
            outcome = Outcome::toggled;
        }
        break;
    }
    default:
        return failed(Outcome::invalid_command);
    }
    const Tick cost = ActionTicks(baseCost, world_.GetActor(handle)->speed);
    Require(world_.ScheduleActorAction(handle, baseCost).has_value());
    // Observe the completed action for every participant, even between two
    // commands at the same tick. Snapshot/viewport cadence never grants memory.
    world_.UpdatePlayerObservations();
    CommandResult result{outcome, true, GetTick()};
    result.cost = cost;
    return result;
}

void Server::SynchronizePacing() {
    Vector<PlayerReadiness> players;
    for (const PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const ActorHandle handle = PlayerActor(slot);
        const Actor* actor = world_.GetActor(handle);
        Require(actor && actor->player && !actor->parked && world_.IsActive(actor->position));
        players.push_back({handle, slot.id, actor->speed,
            Ready(world_, handle), slot.pending.has_value()});
    }
    pacer_.Synchronize(players);
}

Server::PlayerSlot* Server::FindPlayer(PersistentId id) {
    for (PlayerSlot& slot : players_) { if (slot.id == id) { return &slot; } }
    return nullptr;
}

const Server::PlayerSlot* Server::FindPlayer(PersistentId id) const {
    for (const PlayerSlot& slot : players_) { if (slot.id == id) { return &slot; } }
    return nullptr;
}

ActorHandle Server::PlayerActor(const PlayerSlot& slot) const {
    return world_.FindActor(slot.id);
}

PersistentId Server::GetHostPlayerId() const { return world_.GetActor(player_)->id; }
std::uint64_t Server::GetRevision() const { return revision_ + world_.GetRevision(); }
PlayerInputContext Server::GetInputContext(PersistentId id) const {
    const auto* slot = FindPlayer(id);
    return slot ? PlayerInputContext{slot->opportunity, slot->lastSequence, GetRevision()}
        : PlayerInputContext{};
}
ViewStats Server::GetViewStats() const { return viewStats_; }
void Server::ResetViewStats() const { viewStats_ = {}; }

Vector<PlayerStatus> Server::GetPlayers() const {
    Vector<PlayerStatus> result;
    for (const PlayerSlot& slot : players_) {
        const Actor* actor = world_.GetActor(PlayerActor(slot));
        const bool ready = slot.state == PlayerConnectionState::Active
            && actor && Ready(world_, PlayerActor(slot));
        result.push_back({slot.id, slot.name, slot.state, ready,
            slot.pending.has_value(), slot.opportunity,
            ready ? pacer_.GraceMicroseconds(PlayerActor(slot)) : 0});
    }
    return result;
}

SessionResult<PlayerJoin> Server::JoinPlayer(eastl::string_view name,
    std::uint64_t reconnectToken, std::uint64_t newToken) {
    if (name.empty() || name.size() > 32) {
        return eastl::unexpected(SessionError::InvalidName);
    }
    for (char character : name) {
        if (character == '\0') { return eastl::unexpected(SessionError::InvalidName); }
    }
    std::size_t online = 0;
    for (const PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Parked) { ++online; }
    }
    if (online >= 4) { return eastl::unexpected(SessionError::Full); }
    if (reconnectToken != 0) {
        for (PlayerSlot& slot : players_) {
            if (slot.token != reconnectToken) { continue; }
            if (slot.state != PlayerConnectionState::Parked) {
                return eastl::unexpected(SessionError::AlreadyConnected);
            }
            const auto position = world_.FindPlayerPosition(slot.id);
            if (!position) { return eastl::unexpected(SessionError::Unavailable); }
            if (!ValidPosition(*position)) {
                return eastl::unexpected(SessionError::Unavailable);
            }
            slot.position = *position;
            slot.center = BubbleCenter(*position);
            slot.residentRequest = Take(world_.RequestChunks(
                {BubbleCenter(slot.position), 0}, LoadMode::resident));
            if (!world_.ReconcileChunks()) {
                Require(world_.ReleaseRequest(slot.residentRequest).has_value());
                slot.residentRequest = {};
                return eastl::unexpected(SessionError::StorageFailure);
            }
            const Actor* actor = world_.GetActor(PlayerActor(slot));
            if (!actor || !actor->player || !actor->parked) {
                Require(world_.ReleaseRequest(slot.residentRequest).has_value());
                slot.residentRequest = {};
                Require(world_.ReconcileChunks().has_value());
                return eastl::unexpected(SessionError::Unavailable);
            }
            slot.state = PlayerConnectionState::Joining;
            slot.name = String(name.data(), name.size());
            ++revision_;
            return PlayerJoin{slot.id, slot.token};
        }
        return eastl::unexpected(SessionError::UnknownToken);
    }
    if (newToken == 0) { return eastl::unexpected(SessionError::InvalidToken); }
    if (players_.size() >= 32) { return eastl::unexpected(SessionError::Full); }
    for (const PlayerSlot& slot : players_) {
        if (slot.token == newToken) { return eastl::unexpected(SessionError::InvalidToken); }
    }
    const TileCoord origin = world_.GetActor(player_)->position;
    TileCoord spawn{};
    bool found = false;
    for (int radius = 0; radius <= 64 && !found; ++radius) {
        for (int dz = -radius; dz <= radius && !found; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (eastl::max(std::abs(dx), std::abs(dz)) != radius) { continue; }
                const TileCoord candidate{origin.x + dx, origin.z + dz, origin.y};
                const Tile* tile = world_.GetTile(candidate);
                if (tile && Passable(world_.GetContent(), tile->terrain)
                    && world_.ActorAt(candidate).value == 0
                    && ResidencyFits(candidate, active_radius_, resident_radius_, circleResidency_)) {
                    spawn = candidate;
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) { return eastl::unexpected(SessionError::Unavailable); }
    const ActorHandle actor = Take(world_.CreateActor(spawn, true));
    Require(world_.SetActorParked(actor, true).has_value());
    PlayerSlot slot;
    slot.id = world_.GetActor(actor)->id;
    slot.name = String(name.data(), name.size());
    slot.token = newToken;
    slot.position = world_.GetActor(actor)->position;
    slot.center = BubbleCenter(slot.position);
    slot.state = PlayerConnectionState::Joining;
    slot.residentRequest = Take(world_.RequestChunks({slot.center, 0}, LoadMode::resident));
    const PlayerJoin joined{slot.id, slot.token};
    players_.push_back(eastl::move(slot));
    ++revision_;
    return joined;
}

SessionResult<void> Server::ActivatePlayer(PersistentId id) {
    PlayerSlot* slot = FindPlayer(id);
    if (!slot) { return eastl::unexpected(SessionError::InvalidPlayer); }
    if (slot->state != PlayerConnectionState::Joining) {
        return eastl::unexpected(SessionError::AlreadyConnected);
    }
    if (!ResidencyFits(slot->position, active_radius_, resident_radius_, circleResidency_)) {
        return eastl::unexpected(SessionError::Unavailable);
    }
    Require(slot->residentRequest.value != 0);
    Vector<LoadRequest> staging;
    const auto rollback = [this, &staging]() {
        ReleaseStaging(world_, staging);
        Require(world_.ReconcileChunks().has_value());
    };
    const auto staged = StageBubble(world_, slot->position, active_radius_, resident_radius_, circleResidency_, staging);
    if (!staged) {
        return eastl::unexpected(SessionError::StorageFailure);
    }
    const ActorHandle actor = PlayerActor(*slot);
    if (!world_.GetActor(actor)) {
        rollback();
        return eastl::unexpected(SessionError::Unavailable);
    }
    TileCoord spawn{};
    bool found = false;
    // At radius zero, the saved tile may be occupied while another tile in
    // the same chunk is free. Rings can also cross a chunk boundary.
    for (int radius = 0; radius <= chunk_size * (resident_radius_ + 1) && !found; ++radius) {
        for (int dz = -radius; dz <= radius && !found; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (eastl::max(std::abs(dx), std::abs(dz)) != radius) { continue; }
                const TileCoord candidate{slot->position.x + dx, slot->position.z + dz, slot->position.y};
                if (!ResidencyFits(candidate, active_radius_, resident_radius_, circleResidency_)) { continue; }
                if (!world_.GetTile(candidate)) {
                    staging.push_back(Take(world_.RequestChunks({BubbleCenter(candidate), 0}, LoadMode::resident)));
                    if (!world_.ReconcileChunks()) {
                        rollback();
                        return eastl::unexpected(SessionError::StorageFailure);
                    }
                }
                const Tile* tile = world_.GetTile(candidate);
                if (tile && Passable(world_.GetContent(), tile->terrain)
                    && world_.ActorAt(candidate).value == 0) {
                    spawn = candidate;
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        rollback();
        return eastl::unexpected(SessionError::Unavailable);
    }
    // Every chunk of the destination bubble is resident before relocating or
    // unparking the actor. Updating the permanent requests cannot need I/O.
    if (!StageBubble(world_, spawn, active_radius_, resident_radius_, circleResidency_, staging)) {
        rollback();
        return eastl::unexpected(SessionError::StorageFailure);
    }
    Require(world_.MoveActor(actor, spawn).has_value());
    Require(world_.SetActorParked(actor, false).has_value());
    slot->state = PlayerConnectionState::Active;
    slot->activeRequest = Take(world_.RequestChunks({BubbleCenter(spawn), active_radius_}, LoadMode::active));
    CommitPlayerBubble(*slot);
    ReleaseStaging(world_, staging);
    Require(world_.ReconcileChunks().has_value());
    world_.UpdatePlayerObservations();
    ++revision_;
    SynchronizePacing();
    return {};
}

SessionResult<void> Server::DisconnectPlayer(PersistentId id) {
    PlayerSlot* slot = FindPlayer(id);
    if (!slot || id == GetHostPlayerId()) {
        return eastl::unexpected(SessionError::InvalidPlayer);
    }
    if (slot->state == PlayerConnectionState::Parked) {
        return eastl::unexpected(SessionError::Unavailable);
    }
    const ActorHandle actor = PlayerActor(*slot);
    if (const auto position = world_.FindPlayerPosition(id)) { slot->position = *position; }
    if (actor.value != 0) { Require(world_.SetActorParked(actor, true).has_value()); }
    slot->pending.reset();
    slot->lastSequence = 0;
    Require(slot->opportunity != eastl::numeric_limits<std::uint64_t>::max());
    ++slot->opportunity;
    slot->state = PlayerConnectionState::Parked;
    if (slot->activeRequest.value) { Require(world_.ReleaseRequest(slot->activeRequest).has_value()); }
    if (slot->residentRequest.value) { Require(world_.ReleaseRequest(slot->residentRequest).has_value()); }
    slot->activeRequest = {};
    slot->residentRequest = {};
    Require(world_.ReconcileChunks().has_value());
    world_.UpdatePlayerObservations();
    ++revision_;
    SynchronizePacing();
    return {};
}

CommandResult Server::Submit(PersistentId id, PlayerInput input) {
    PlayerSlot* slot = FindPlayer(id);
    if (!slot || slot->state != PlayerConnectionState::Active) {
        return {Outcome::unavailable, false, GetTick()};
    }
    if (!ValidCommandShape(input.command)) { return {Outcome::invalid_command, false, GetTick()}; }
    if (input.opportunity != slot->opportunity) { return {Outcome::stale_input, false, GetTick()}; }
    if (input.sequence <= slot->lastSequence) { return {Outcome::duplicate_input, false, GetTick()}; }
    const ActorHandle actor = PlayerActor(*slot);
    // A shorter grace may expire while a different player still holds the
    // round. Its automatic wait is already decided, even though no action can
    // resolve until the entire cohort is satisfied.
    if (!slot->pending && Ready(world_, actor)
        && !pacer_.IsIdle() && pacer_.GraceMicroseconds(actor) == 0) {
        return {Outcome::stale_input, false, GetTick()};
    }
    slot->lastSequence = input.sequence;
    slot->pending = input;
    ++revision_;
    SynchronizePacing();
    return {Outcome::queued, false, GetTick()};
}

bool Server::ResolveRound(SessionUpdate& result, const Vector<ActorHandle>& expired) {
    Vector<PlayerSlot*> cohort;
    for (PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* actor = world_.GetActor(PlayerActor(slot));
        Require(actor);
        if (!Ready(world_, PlayerActor(slot))) { continue; }
        bool timedOut = false;
        for (ActorHandle handle : expired) {
            if (handle == PlayerActor(slot)) { timedOut = true; break; }
        }
        if (!slot.pending && !timedOut) { return false; }
        cohort.push_back(&slot);
    }
    if (cohort.empty()) { return false; }
    eastl::sort(cohort.begin(), cohort.end(), [](const PlayerSlot* a, const PlayerSlot* b) {
        return a->id < b->id;
    });
    for (PlayerSlot* slot : cohort) {
        bool committed = false;
        if (slot->pending) {
            const PlayerInput input = *slot->pending;
            slot->pending.reset();
            CommandResult command = Execute(PlayerActor(*slot), *slot, input.command);
            result.commandResults.push_back({slot->id, input.sequence, input.opportunity, command});
            if (slot->id == GetHostPlayerId()) { result.commandResult = command; }
            if (command.storageError) { result.storageError = command.storageError; }
            committed = command.advanced;
        } else {
            Require(Execute(PlayerActor(*slot), *slot, {}).advanced);
            ++result.skippedActions;
            committed = true;
        }
        Require(slot->opportunity != eastl::numeric_limits<std::uint64_t>::max());
        ++slot->opportunity;
        if (committed) { pacer_.RestartGrace(PlayerActor(*slot)); }
        ++revision_;
    }
    SynchronizePacing();
    return true;
}

bool Server::SetPacing(PacingSettings settings) {
    if (!pacer_.SetSettings(settings)) { return false; }
    SynchronizePacing();
    ++revision_;
    return true;
}

PacingSettings Server::GetPacing() const { return pacer_.GetSettings(); }

eastl::optional<SaveError> Server::Autosave() {
    if (!world_.HasSave() || autosaveInterval_ == 0) { return {}; }
    // Keep the public autosave setting in turns, independent of action count.
    constexpr Tick maximum = eastl::numeric_limits<Tick>::max();
    const Tick interval = autosaveInterval_ > maximum / ticks_per_turn
        ? maximum : autosaveInterval_ * ticks_per_turn;
    if (GetTick() - lastSaveTick_ < interval) { return {}; }
    const auto saved = Save();
    if (!saved) { return saved.error(); }
    return {};
}

SessionUpdate Server::Update(std::uint64_t elapsedMicroseconds) {
    SessionUpdate result;
    std::uint64_t remaining = elapsedMicroseconds;
    constexpr Tick maximum = eastl::numeric_limits<Tick>::max();
    for (;;) {
        SynchronizePacing();
        if (pacer_.IsIdle()) { break; }

        const Tick credit = pacer_.AvailableTicks();
        const Tick before = GetTick();
        const Tick limit = credit > maximum - before ? maximum : before + credit;
        const SimulationResult simulation = cataclysmx::AdvanceUntilInput(world_, rng_state_, limit);
        if (GetTick() != before) { ++revision_; }
        pacer_.ConsumeTicks(GetTick() - before);
        SynchronizePacing();
        if (ResolveRound(result, pacer_.ExpiredPlayers())) { continue; }
        if (pacer_.IsIdle()) { break; }
        if (remaining == 0 || simulation.stop == SimulationStop::NoEvents) { break; }

        std::uint64_t untilEvent = maximum;
        if (simulation.stop == SimulationStop::LimitReached) {
            Require(GetTick() != maximum && simulation.nextEventIn != 0);
            untilEvent = pacer_.MicrosecondsUntilTicks(simulation.nextEventIn);
        }
        if (const auto deadline = pacer_.NextUnexpiredDeadlineMicroseconds()) {
            if (*deadline < untilEvent) { untilEvent = *deadline; }
        }
        const std::uint64_t step = remaining < untilEvent ? remaining : untilEvent;
        Require(step != 0);
        pacer_.ElapseMicroseconds(step);
        ++revision_;
        remaining -= step;
    }
    result.tick = GetTick();
    result.needsInput = Ready(world_, player_);
    if (const auto error = Autosave()) {
        if (!result.storageError) { result.storageError = error; }
    }
    return result;
}

WorldView Server::View(int radius) const {
    return ViewForPlayer(GetHostPlayerId(), radius);
}

WorldView Server::ViewForPlayer(PersistentId id, int radius) const {
    ++viewStats_.requests;
    // Radius is a viewport extent, independent of the fixed gameplay sight range.
    // Queries use current perception and existing memories without learning.
    radius = eastl::clamp(radius, 0, playerSightRange);
    const PlayerSlot* slot = FindPlayer(id);
    if (!slot) { return {}; }
    const ActorHandle player = PlayerActor(*slot);
    const Actor* actor = world_.GetActor(player);
    if (!actor) { return {}; }
    Require(actor != nullptr);
    const auto revision = GetRevision();
    if (slot->cachedView.revision == revision && slot->cachedView.width == radius * 2 + 1) {
        return slot->cachedView;
    }
    ++viewStats_.constructions;
    WorldView result;
    result.player_position = actor->position;
    result.origin = {actor->position.x - radius, actor->position.z - radius, actor->position.y};
    result.width = 2 * radius + 1;
    result.height = result.width;
    result.tick = GetTick();
    result.remainingTicks = world_.GetActorRemainingTicks(player);
    result.combat = world_.Combat().Describe(player);
    result.speed = actor->speed;
    result.pacing = pacer_.GetSettings();
    result.commandQueued = slot->pending.has_value();
    result.playerId = id;
    result.revision = revision;
    result.opportunity = slot->opportunity;
    result.lastInputSequence = slot->lastSequence;
    result.players = GetPlayers();
    const bool observing = slot->state == PlayerConnectionState::Active
        && !actor->parked && world_.IsActive(actor->position);
    if (observing) { result.playerLight = ClassifyLight(world_.GetLightLevel(actor->position)); }
    const bool reuseMap = slot->cachedMapRevision == world_.GetMapRevision()
        && slot->cachedView.origin == result.origin && slot->cachedView.width == result.width
        && slot->cachedObserving == observing;
    if (reuseMap) {
        result.cells = slot->cachedView.cells;
    } else {
        ++viewStats_.mapConstructions;
        const auto* visibility = observing ? world_.GetPlayerVisibility(player) : nullptr;
        const auto observerLight = observing ? world_.GetLightLevel(actor->position) : std::uint8_t{0};
        const auto vision = world_.GetVisionProfile();
        int overlayRanges[4];
        for (int i = 0; i < 4; ++i) {
            const int range = VisionRange(vision, static_cast<LightBand>(i), observerLight);
            overlayRanges[i] = range * range;
        }
        // All World accesses below are read-only. Reuse chunk/lighting borrows
        // across each horizontal run instead of resolving them for every cell.
        eastl::array<const Chunk*, world_layer_count> columnChunks{};
        eastl::array<eastl::span<const std::uint8_t>, world_layer_count> columnLight{};
        eastl::array<bool, world_layer_count> columnLoaded{};
        ChunkCoord cachedColumn{world_coordinate_limit, world_coordinate_limit, 0};
        result.cells.resize(static_cast<std::size_t>(result.width * result.height));
        for (int z = 0; z < result.height; ++z) {
            for (int x = 0; x < result.width; ++x) {
                TileCoord position{result.origin.x + x, result.origin.z + z, result.origin.y};
                const auto column = ChunkOf({position.x, position.z, 0});
                if (column != cachedColumn) {
                    columnLoaded.fill(false);
                    cachedColumn = column;
                }
                ViewCell& cell = result.cells[static_cast<std::size_t>(z * result.width + x)];
                bool liveColumnOpen = observing;
                bool knownColumnOpen = true;
                for (unsigned int depth = 0; position.y >= world_min_y; ++depth, --position.y) {
                    // Live projection can see a lit lower tile through unlit air.
                    // Its geometry never controls the independent remembered path.
                    const int layer = position.y - world_min_y;
                    const Chunk* chunk = nullptr;
                    const Tile* current = nullptr;
                    int tileIndex = 0;
                    if (liveColumnOpen && ValidPosition(position)) {
                        if (!columnLoaded[layer]) {
                            const ChunkCoord coord{column.x, column.z, position.y};
                            columnChunks[layer] = world_.GetChunk(coord);
                            columnLight[layer] = columnChunks[layer] ? world_.GetChunkLightLevels(coord)
                                : eastl::span<const std::uint8_t>{};
                            columnLoaded[layer] = true;
                        }
                        chunk = columnChunks[layer];
                        tileIndex = TileIndex(position);
                        if (chunk) { current = &chunk->tiles[tileIndex]; }
                    }
                    const auto* terrain = current ? &world_.GetContent().GetTerrain(current->terrain) : nullptr;
                    const bool visible = current && visibility && visibility->Contains(position);
                    if (visible) {
                        cell.terrainVisibility = Visibility::Visible;
                        cell.tile = *current;
                        cell.terrainDepth = depth;
                        const auto level = columnLight[layer][tileIndex];
                        const auto spaceLight = ClassifyLight(level);
                        cell.terrainLight = terrain->blocksSight
                            ? ClassifyLight(TerrainLightLevel(world_, actor->position, position)) : spaceLight;
                        const int dx = position.x - actor->position.x;
                        const int dz = position.z - actor->position.z;
                        if (cell.overlayVisibility != Visibility::Visible
                            && dx*dx + dz*dz <= overlayRanges[static_cast<int>(spaceLight)]) {
                            const Actor* occupant = chunk->actors.empty() ? nullptr : world_.GetActor(world_.ActorAt(position));
                            const auto items = chunk->ground_items.empty() ? 0U
                                : static_cast<unsigned int>(world_.CountItemsAt(position));
                            const InteractiveObject* object = chunk->objects.empty() ? nullptr : world_.GetObject(world_.ObjectAt(position));
                            if (occupant || items != 0 || object) {
                                // Memories are a fallback, never an occluder for an
                                // actually visible lower overlay. Nearest live wins.
                                cell.actor = occupant ? occupant->id : 0;
                                cell.player = occupant && occupant->player;
                                cell.ground_items = items;
                                cell.object = object ? object->id : 0;
                                cell.objectAppearance = object ? DescribeObject(*object) : ObjectAppearance{};
                                cell.overlayDepth = depth;
                                cell.overlayVisibility = Visibility::Visible;
                                cell.overlayLight = spaceLight;
                            }
                        }
                        knownColumnOpen = terrain->openSpace;
                    } else if (knownColumnOpen) {
                        const auto remembered = world_.GetRememberedTile(id, position);
                        if (remembered.terrain.value == 0) {
                            knownColumnOpen = false;
                        } else {
                            cell.terrainVisibility = Visibility::Remembered;
                            cell.tile = {remembered.terrain, 0};
                            cell.terrainDepth = depth;
                            cell.terrainLight = LightBand::Dark;
                            if (cell.overlayVisibility == Visibility::Unknown && remembered.object.kind != ObjectKind::None) {
                                cell.objectAppearance = remembered.object;
                                cell.overlayDepth = depth;
                                cell.overlayVisibility = Visibility::Remembered;
                                cell.overlayLight = LightBand::Dark;
                            }
                            knownColumnOpen = world_.GetContent().GetTerrain(remembered.terrain).openSpace;
                        }
                    }
                    liveColumnOpen = terrain && terrain->openSpace;
                    if (!liveColumnOpen && !knownColumnOpen) { break; }
                }
            }
        }
    }
    result.inventory = DescribeItems(world_, player, actor->inventory);
    if (observing) { result.groundItems = DescribeItems(world_, player, world_.ItemsAt(actor->position)); }
    result.carriedMassGrams = world_.GetCarriedMass(player);
    result.carryCapacityGrams = actor->carryCapacityGrams;
    slot->cachedMapRevision = world_.GetMapRevision();
    slot->cachedObserving = observing;
    slot->cachedView = result;
    return result;
}

WorldResult<void> Server::SetBubbleRadii(int active_radius, int resident_radius,
    Diagnostics* diagnostics) {
    return SetResidency(active_radius, resident_radius, {}, diagnostics);
}

WorldResult<void> Server::SetCircleResidency(int activeRadiusChunks, CircleResidency residency,
    Diagnostics* diagnostics) {
    return SetResidency(activeRadiusChunks, eastl::max(resident_radius_, activeRadiusChunks), residency, diagnostics);
}

eastl::optional<CircleResidency> Server::GetCircleResidency() const { return circleResidency_; }
int Server::GetActiveRadius() const { return active_radius_; }
int Server::GetResidentRadius() const { return resident_radius_; }

WorldResult<void> Server::SetResidency(int active_radius, int resident_radius,
    eastl::optional<CircleResidency> circle, Diagnostics* diagnostics) {
    if (active_radius < 0 || resident_radius < active_radius || resident_radius > 8
        || (circle && (circle->radiusTiles < 0 || circle->radiusTiles > maxCircleResidencyRadius))) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    for (const PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* participant = world_.GetActor(PlayerActor(slot));
        Require(participant);
        if (!ResidencyFits(participant->position, active_radius, resident_radius, circle)) {
            return eastl::unexpected(WorldError::invalid_region);
        }
    }
    const int oldActive = active_radius_;
    const int oldResident = resident_radius_;
    const auto oldCircle = circleResidency_;
    for (PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* participant = world_.GetActor(PlayerActor(slot));
        const ChunkCoord center = BubbleCenter(participant->position);
        const LoadRequest active = slot.id == GetHostPlayerId() ? active_request_ : slot.activeRequest;
        const LoadRequest resident = slot.id == GetHostPlayerId() ? resident_request_ : slot.residentRequest;
        Require(world_.UpdateRequest(active, {center, active_radius}, LoadMode::active).has_value());
        UpdateResident(world_, resident, participant->position, resident_radius, circle);
    }
    if (!world_.ReconcileChunks(diagnostics)) {
        for (const PlayerSlot& slot : players_) {
            if (slot.state != PlayerConnectionState::Active) { continue; }
            const LoadRequest active = slot.id == GetHostPlayerId() ? active_request_ : slot.activeRequest;
            const LoadRequest resident = slot.id == GetHostPlayerId() ? resident_request_ : slot.residentRequest;
            Require(world_.UpdateRequest(active, {slot.center, oldActive}, LoadMode::active).has_value());
            UpdateResident(world_, resident, slot.position, oldResident, oldCircle);
        }
        return eastl::unexpected(WorldError::storage_failure);
    }
    for (PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* participant = world_.GetActor(PlayerActor(slot));
        Require(participant);
        slot.center = BubbleCenter(participant->position);
        slot.position = participant->position;
    }
    bubbleCenter_ = FindPlayer(GetHostPlayerId())->center;
    active_radius_ = active_radius;
    resident_radius_ = resident_radius;
    circleResidency_ = circle;
    ++revision_;
    return {};
}

void Server::CommitPlayerBubble(PlayerSlot& slot) {
    const Actor* actor = world_.GetActor(PlayerActor(slot));
    Require(actor != nullptr);
    const ChunkCoord center = BubbleCenter(actor->position);
    LoadRequest active = slot.id == GetHostPlayerId() ? active_request_ : slot.activeRequest;
    LoadRequest resident = slot.id == GetHostPlayerId() ? resident_request_ : slot.residentRequest;
    Require(world_.UpdateRequest(active, {center, active_radius_}, LoadMode::active).has_value());
    UpdateResident(world_, resident, actor->position, resident_radius_, circleResidency_);
    const TileCoord position = actor->position;
    slot.center = center;
    slot.position = position;
    if (slot.id == GetHostPlayerId()) { bubbleCenter_ = center; }
}

Server::Server(const ContentCatalog& content, EmptyTag) : world_(content) {}

SessionState Server::Checkpoint() const {
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    SessionState session{GetTick(), rng_state_, actor->id, actor->position,
        active_radius_, resident_radius_, autosaveInterval_, pacer_.GetSettings()};
    session.circleResidency = circleResidency_;
    for (const PlayerSlot& slot : players_) {
        const auto position = world_.FindPlayerPosition(slot.id);
        Require(position.has_value());
        session.players.push_back({slot.id, *position, slot.name, slot.token});
    }
    return session;
}

SaveResult<void> Server::CreateSave(const char* path, Diagnostics* diagnostics) {
    const auto result = world_.CreateSave(path, Checkpoint(), diagnostics);
    if (result) {
        lastSaveTick_ = GetTick();
    }
    return result;
}

SaveResult<void> Server::Save(Diagnostics* diagnostics) {
    const auto result = world_.Save(Checkpoint(), diagnostics);
    if (result) {
        lastSaveTick_ = GetTick();
    }
    return result;
}

SaveResult<eastl::unique_ptr<Server>> Server::LoadSave(const ContentCatalog& content,
    const char* path, Diagnostics* diagnostics) {
    eastl::unique_ptr<Server> server(new Server(content, EmptyTag{}));
    const auto loaded = server->world_.LoadSave(path, diagnostics);
    if (!loaded) {
        if (diagnostics) { diagnostics->Add("Loading server world"); }
        return eastl::unexpected(loaded.error());
    }
    const SessionState& session = *loaded;
    if (session.player == 0 || session.activeRadius < 0
        || session.residentRadius < session.activeRadius || session.residentRadius > 8
        || !ResidencyFits(session.playerPosition, session.activeRadius, session.residentRadius, session.circleResidency)) {
        if (diagnostics) {
            diagnostics->Add("Invalid server session");
        }
        return eastl::unexpected(SaveError::InvalidData);
    }
    server->bubbleCenter_ = BubbleCenter(session.playerPosition);
    server->active_radius_ = session.activeRadius;
    server->resident_radius_ = session.residentRadius;
    server->circleResidency_ = session.circleResidency;
    server->active_request_ = Take(server->world_.RequestChunks(
        {server->bubbleCenter_, session.activeRadius}, LoadMode::active));
    server->resident_request_ = RequestResident(server->world_, session.playerPosition,
        session.residentRadius, session.circleResidency);
    // Materialize the whole roster together: reconciliation settles gravity,
    // and guests may support each other or the host across saved layers.
    Vector<LoadRequest> staging;
    for (const SavedPlayer& saved : session.players) {
        // Guests return parked and only need their saved column. Their future
        // active/resident footprint is validated when activation is requested.
        if (!ValidPosition(saved.position)) {
            if (diagnostics) { diagnostics->Add("Saved player position exceeds world limits"); }
            return eastl::unexpected(SaveError::InvalidData);
        }
        if (saved.actor == session.player) { continue; }
        const auto request = server->world_.RequestChunks(
            {BubbleCenter(saved.position), 0}, LoadMode::resident);
        if (!request) { return eastl::unexpected(SaveError::InvalidData); }
        staging.push_back(*request);
    }
    const auto reconciled = server->world_.ReconcileChunks(diagnostics);
    if (!reconciled) {
        return eastl::unexpected(reconciled.error());
    }
    server->player_ = server->world_.FindActor(session.player);
    const Actor* actor = server->world_.GetActor(server->player_);
    if (!actor || !actor->player || actor->parked || actor->position != session.playerPosition) {
        if (diagnostics) {
            diagnostics->Add("Saved player does not match server session");
        }
        return eastl::unexpected(SaveError::InvalidData);
    }
    Vector<ActorHandle> guests;
    if (session.players.empty()) {
        server->players_.push_back({session.player, "Host", 0, session.playerPosition,
            PlayerConnectionState::Active, {}, {}, server->bubbleCenter_});
    } else {
        for (const SavedPlayer& saved : session.players) {
            PlayerSlot slot;
            slot.id = saved.actor;
            slot.name = saved.name;
            slot.token = saved.reconnectToken;
            slot.position = saved.position;
            slot.center = BubbleCenter(saved.position);
            slot.state = saved.actor == session.player
                ? PlayerConnectionState::Active : PlayerConnectionState::Parked;
            const ActorHandle handle = server->world_.FindActor(slot.id);
            const Actor* value = server->world_.GetActor(handle);
            if (!value || !value->player || value->position != slot.position) {
                if (diagnostics) { diagnostics->Add("Saved player does not match roster"); }
                return eastl::unexpected(SaveError::InvalidData);
            }
            if (slot.state == PlayerConnectionState::Parked) {
                guests.push_back(handle);
            }
            server->players_.push_back(eastl::move(slot));
        }
    }
    // Validate every saved position before removing any guest support. Parking
    // is gravity-deferred so an upper guest cannot fall before it is parked.
    Require(server->world_.ParkActors({guests.data(), guests.size()}).has_value());
    for (LoadRequest request : staging) {
        Require(server->world_.ReleaseRequest(request).has_value());
    }
    const auto released = server->world_.ReconcileChunks(diagnostics);
    if (!released) { return eastl::unexpected(released.error()); }
    for (PlayerSlot& slot : server->players_) {
        const auto position = server->world_.FindPlayerPosition(slot.id);
        Require(position.has_value());
        slot.position = *position;
        slot.center = BubbleCenter(*position);
    }
    server->bubbleCenter_ = BubbleCenter(server->world_.GetActor(server->player_)->position);
    eastl::sort(server->players_.begin(), server->players_.end(),
        [](const PlayerSlot& a, const PlayerSlot& b) { return a.id < b.id; });
    server->rng_state_ = session.rngState;
    server->autosaveInterval_ = session.autosaveInterval;
    server->lastSaveTick_ = session.tick;
    Require(server->pacer_.SetSettings(session.pacing));
    server->pacer_.Reset();
    server->SynchronizePacing();
    return eastl::move(server);
}

void Server::SetAutosaveInterval(std::uint64_t turns) { autosaveInterval_ = turns; }
std::uint64_t Server::GetAutosaveInterval() const { return autosaveInterval_; }

World& Server::GetWorld() { return world_; }
const World& Server::GetWorld() const { return world_; }
ActorHandle Server::GetPlayer() const { return player_; }
std::uint64_t Server::GetTick() const { return world_.GetTick(); }

}
