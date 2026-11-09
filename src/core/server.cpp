#include <cstdlib>

#include "core/server.h"

#include "core/simulation.h"
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

bool OpenVerticalFace(const World& world, TileCoord first, TileCoord second) {
    const Tile* a = world.GetTile(first);
    const Tile* b = world.GetTile(second);
    if (!a || !b) { return false; }
    const auto& upper = world.GetContent().GetTerrain((first.y > second.y ? a : b)->terrain);
    const auto& lower = world.GetContent().GetTerrain((first.y > second.y ? b : a)->terrain);
    return upper.openSpace || (upper.stairs && lower.stairs);
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

ItemHandle SelectItem(const World& world, const Vector<ItemHandle>& items, PersistentId id) {
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

Vector<InventoryEntry> DescribeItems(const World& world, ActorHandle actor,
    Vector<ItemHandle> pending) {
    Vector<InventoryEntry> result;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const ItemHandle handle = pending[i];
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
        pending.insert(pending.end(), item->contents.begin(), item->contents.end());
    }
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

bool LineVisible(const World& world, TileCoord origin, TileCoord target) {
    // Center-to-center rational voxel DDA. Crossing times are odd/(2*delta);
    // compare by integer cross products, never floating point. At a tie every
    // touched voxel and every vertical edge of that supercover must be open.
    // A blocking target itself is visible, but its bottom face is checked first.
    const int delta[] = {std::abs(target.x - origin.x), std::abs(target.z - origin.z),
        std::abs(target.y - origin.y)};
    const int step[] = {target.x > origin.x ? 1 : -1, target.z > origin.z ? 1 : -1,
        target.y > origin.y ? 1 : -1};
    int numerator[] = {1, 1, 1};
    TileCoord cursor = origin;
    for (;;) {
        const Tile* tile = world.GetTile(cursor);
        if (!tile) { return false; }
        if (cursor == target) { return true; }
        if (cursor != origin && world.GetContent().GetTerrain(tile->terrain).blocksSight) {
            return false;
        }
        int axis = -1;
        for (int i = 0; i < 3; ++i) {
            if (delta[i] != 0 && (axis < 0
                || numerator[i] * delta[axis] < numerator[axis] * delta[i])) { axis = i; }
        }
        Require(axis >= 0);
        unsigned int mask = 0;
        for (int i = 0; i < 3; ++i) {
            if (delta[i] != 0 && numerator[i] * delta[axis] == numerator[axis] * delta[i]) {
                mask |= 1U << i;
            }
        }
        for (unsigned int subset = 1; subset <= 7; ++subset) {
            if ((subset & mask) != subset) { continue; }
            const TileCoord touched{cursor.x + ((subset & 1) ? step[0] : 0),
                cursor.z + ((subset & 2) ? step[1] : 0),
                cursor.y + ((subset & 4) ? step[2] : 0)};
            const Tile* touchedTile = world.GetTile(touched);
            if (!touchedTile) { return false; }
            if ((mask & 4) != 0) {
                TileCoord other = touched;
                other.y += (subset & 4) ? -step[2] : step[2];
                if (!OpenVerticalFace(world, touched, other)) { return false; }
            }
            if (touched != target && world.GetContent().GetTerrain(touchedTile->terrain).blocksSight) {
                return false;
            }
        }
        // Include the vertical face at the unshifted horizontal corner too.
        if ((mask & 4) && !OpenVerticalFace(world, cursor,
            {cursor.x, cursor.z, cursor.y + step[2]})) { return false; }
        if (mask & 1) { cursor.x += step[0]; numerator[0] += 2; }
        if (mask & 2) { cursor.z += step[1]; numerator[1] += 2; }
        if (mask & 4) { cursor.y += step[2]; numerator[2] += 2; }
    }
}

}

bool ValidCommandShape(const Command& command) {
    switch (command.kind) {
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
    resident_request_ = Take(world_.RequestChunks({{0, 0}, resident_radius_}, LoadMode::resident));
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
    Take(world_.CreateActor({5, 5}));
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
    }
    SynchronizePacing();
}

CommandResult Server::Send(Command command) {
    if (!ValidCommandShape(command)) { return {Outcome::invalid_command, false, tick_}; }
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
    if (actor->remainingTicks != 0 || otherActive) {
        // Keep the latest intent, not a chain of key-repeat-generated actions.
        // Destination, item ownership and cost are checked when it becomes due.
        return Submit(GetHostPlayerId(), {++legacySequence_, FindPlayer(GetHostPlayerId())->opportunity, command});
    }
    const CommandResult admission = Submit(GetHostPlayerId(),
        {++legacySequence_, FindPlayer(GetHostPlayerId())->opportunity, command});
    if (admission.outcome != Outcome::queued) { return admission; }
    const auto barrier = cataclysmx::AdvanceUntilInput(world_, tick_, rng_state_, tick_);
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
    const auto failed = [this](Outcome outcome) { return CommandResult{outcome, false, tick_}; };
    if (actor->remainingTicks != 0) { return failed(Outcome::not_ready); }
    Outcome outcome = Outcome::invalid_command;
    std::uint32_t baseCost = command.kind == CommandKind::wait ? command.waitCost : ticks_per_turn;
    switch (command.kind) {
    case CommandKind::wait:
        if (command.waitCost == 0) { return failed(Outcome::invalid_command); }
        outcome = Outcome::waited;
        break;
    case CommandKind::move: {
        if (!Cardinal(command.dx, command.dz, command.dy)) {
            return failed(Outcome::invalid_command);
        }
        const TileCoord destination{position.x + command.dx, position.z + command.dz, position.y + command.dy};
        // Validate the complete future bubble before acquiring any demand.
        if (!BubbleFits(destination, resident_radius_)) {
            return failed(Outcome::unavailable);
        }
        LoadRequest temporary{};
        const ChunkCoord center = BubbleCenter(destination);
        bool complete = true;
        for (int z = center.z - resident_radius_; z <= center.z + resident_radius_; ++z) {
            for (int x = center.x - resident_radius_; x <= center.x + resident_radius_; ++x) {
                for (int y = world_min_y; y <= world_max_y; ++y) {
                    if (!world_.GetChunk({x, z, y})) { complete = false; }
                }
            }
        }
        // Preserve the old bubble while staging every chunk the new requests
        // will need. Reconcile failures leave all live world state untouched.
        if (center != slot.center || !complete) {
            temporary = Take(world_.RequestChunks({center, resident_radius_}, LoadMode::resident));
            const auto loaded = world_.ReconcileChunks();
            if (!loaded) {
                Require(world_.ReleaseRequest(temporary).has_value());
                return {Outcome::unavailable, false, tick_, loaded.error()};
            }
        }
        if (command.dy != 0) {
            const Tile* source = world_.GetTile(position);
            const Tile* target = world_.GetTile(destination);
            if (!source || !target || !world_.GetContent().GetTerrain(source->terrain).stairs
                || !world_.GetContent().GetTerrain(target->terrain).stairs) {
                if (temporary.value != 0) {
                    Require(world_.ReleaseRequest(temporary).has_value());
                    Require(world_.ReconcileChunks().has_value());
                }
                return failed(Outcome::blocked);
            }
        }
        const auto moved = world_.MoveActor(handle, destination);
        if (!moved) {
            if (temporary.value != 0) {
                Require(world_.ReleaseRequest(temporary).has_value());
                Require(world_.ReconcileChunks().has_value());
            }
            return failed(MovementFailure(moved.error()));
        }
        // The player has entered the destination before any old demand can be
        // removed, preserving its runtime key even with zero-radius bubbles.
        // The union is resident now; changing activity and releasing temporary
        // demand below cannot require another storage read.
        Require(UpdatePlayerBubble(slot, active_radius_, resident_radius_).has_value());
        if (temporary.value != 0) {
            Require(world_.ReleaseRequest(temporary).has_value());
            Require(world_.ReconcileChunks().has_value());
        }
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
            const bool enabled = world_.GetObject(object)->enabled;
            Require(world_.SetObjectEnabled(object, !enabled).has_value());
            outcome = Outcome::toggled;
        }
        break;
    }
    default:
        return failed(Outcome::invalid_command);
    }
    const Tick cost = ActionTicks(baseCost, world_.GetActor(handle)->speed);
    Require(world_.ScheduleActorAction(handle, baseCost).has_value());
    CommandResult result{outcome, true, tick_};
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
            actor->remainingTicks == 0, slot.pending.has_value()});
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
std::uint64_t Server::GetRevision() const { return revision_; }

Vector<PlayerStatus> Server::GetPlayers() const {
    Vector<PlayerStatus> result;
    for (const PlayerSlot& slot : players_) {
        const Actor* actor = world_.GetActor(PlayerActor(slot));
        const bool ready = slot.state == PlayerConnectionState::Active
            && actor && actor->remainingTicks == 0;
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
                    && BubbleFits(candidate, resident_radius_)) {
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
    const ChunkCoord center = BubbleCenter(slot->position);
    if (!BubbleFits(slot->position, resident_radius_)) {
        return eastl::unexpected(SessionError::Unavailable);
    }
    Require(slot->residentRequest.value != 0);
    Require(world_.UpdateRequest(slot->residentRequest,
        {center, resident_radius_}, LoadMode::resident).has_value());
    slot->activeRequest = Take(world_.RequestChunks({center, active_radius_}, LoadMode::active));
    const auto rollback = [this, slot, center](LoadRequest temporary) {
        if (temporary.value != 0) { Require(world_.ReleaseRequest(temporary).has_value()); }
        Require(world_.ReleaseRequest(slot->activeRequest).has_value());
        slot->activeRequest = {};
        Require(world_.UpdateRequest(slot->residentRequest,
            {center, 0}, LoadMode::resident).has_value());
        Require(world_.ReconcileChunks().has_value());
    };
    const auto staged = world_.ReconcileChunks();
    if (!staged) {
        rollback({});
        return eastl::unexpected(SessionError::StorageFailure);
    }
    const ActorHandle actor = PlayerActor(*slot);
    if (!world_.GetActor(actor)) {
        rollback({});
        return eastl::unexpected(SessionError::Unavailable);
    }
    TileCoord spawn{};
    bool found = false;
    LoadRequest temporary{};
    ChunkCoord temporaryCenter{};
    // At radius zero, the saved tile may be occupied while another tile in
    // the same chunk is free. Rings can also cross a chunk boundary.
    for (int radius = 0; radius <= chunk_size * (resident_radius_ + 1) && !found; ++radius) {
        for (int dz = -radius; dz <= radius && !found; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (eastl::max(std::abs(dx), std::abs(dz)) != radius) { continue; }
                const TileCoord candidate{slot->position.x + dx, slot->position.z + dz, slot->position.y};
                if (!BubbleFits(candidate, resident_radius_)) { continue; }
                const ChunkCoord candidateCenter = BubbleCenter(candidate);
                if (candidateCenter != center &&
                    (!temporary.value || candidateCenter != temporaryCenter)) {
                    if (temporary.value) {
                        Require(world_.ReleaseRequest(temporary).has_value());
                        temporary = {};
                    }
                    temporary = Take(world_.RequestChunks(
                        {candidateCenter, resident_radius_}, LoadMode::resident));
                    temporaryCenter = candidateCenter;
                    if (!world_.ReconcileChunks()) {
                        rollback(temporary);
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
        rollback(temporary);
        return eastl::unexpected(SessionError::Unavailable);
    }
    // Every chunk of the destination bubble is resident before relocating or
    // unparking the actor. Updating the permanent requests cannot need I/O.
    if (BubbleCenter(spawn) != center && !temporary.value) {
        std::abort();
    }
    Require(world_.MoveActor(actor, spawn).has_value());
    Require(world_.SetActorParked(actor, false).has_value());
    slot->state = PlayerConnectionState::Active;
    Require(UpdatePlayerBubble(*slot, active_radius_, resident_radius_).has_value());
    if (temporary.value) {
        Require(world_.ReleaseRequest(temporary).has_value());
        Require(world_.ReconcileChunks().has_value());
    }
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
    ++revision_;
    SynchronizePacing();
    return {};
}

CommandResult Server::Submit(PersistentId id, PlayerInput input) {
    PlayerSlot* slot = FindPlayer(id);
    if (!slot || slot->state != PlayerConnectionState::Active) {
        return {Outcome::unavailable, false, tick_};
    }
    if (!ValidCommandShape(input.command)) { return {Outcome::invalid_command, false, tick_}; }
    if (input.opportunity != slot->opportunity) { return {Outcome::stale_input, false, tick_}; }
    if (input.sequence <= slot->lastSequence) { return {Outcome::duplicate_input, false, tick_}; }
    const ActorHandle actor = PlayerActor(*slot);
    // A shorter grace may expire while a different player still holds the
    // round. Its automatic wait is already decided, even though no action can
    // resolve until the entire cohort is satisfied.
    if (!slot->pending && world_.GetActor(actor)->remainingTicks == 0
        && !pacer_.IsIdle() && pacer_.GraceMicroseconds(actor) == 0) {
        return {Outcome::stale_input, false, tick_};
    }
    slot->lastSequence = input.sequence;
    slot->pending = input;
    ++revision_;
    SynchronizePacing();
    return {Outcome::queued, false, tick_};
}

bool Server::ResolveRound(SessionUpdate& result, const Vector<ActorHandle>& expired) {
    Vector<PlayerSlot*> cohort;
    for (PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* actor = world_.GetActor(PlayerActor(slot));
        Require(actor);
        if (actor->remainingTicks != 0) { continue; }
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
    if (tick_ - lastSaveTick_ < interval) { return {}; }
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
        const Tick limit = credit > maximum - tick_ ? maximum : tick_ + credit;
        const Tick before = tick_;
        const SimulationResult simulation = cataclysmx::AdvanceUntilInput(world_, tick_, rng_state_, limit);
        if (tick_ != before) { ++revision_; }
        pacer_.ConsumeTicks(tick_ - before);
        SynchronizePacing();
        if (ResolveRound(result, pacer_.ExpiredPlayers())) { continue; }
        if (pacer_.IsIdle()) { break; }
        if (remaining == 0 || simulation.stop == SimulationStop::NoEvents) { break; }

        std::uint64_t untilEvent = maximum;
        if (simulation.stop == SimulationStop::LimitReached) {
            Require(tick_ != maximum && simulation.nextEventIn != 0);
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
    result.tick = tick_;
    result.needsInput = world_.GetActor(player_)->remainingTicks == 0;
    if (const auto error = Autosave()) {
        if (!result.storageError) { result.storageError = error; }
    }
    return result;
}

WorldView Server::View(int radius) const {
    return ViewForPlayer(GetHostPlayerId(), radius);
}

WorldView Server::ViewForPlayer(PersistentId id, int radius) const {
    // Public presentation input is clamped to [0, 12]. The square, row-major
    // snapshot has spherical 3D sight range; out-of-range candidates stay empty.
    radius = eastl::clamp(radius, 0, 12);
    const PlayerSlot* slot = FindPlayer(id);
    if (!slot) { return {}; }
    const ActorHandle player = PlayerActor(*slot);
    const Actor* actor = world_.GetActor(player);
    if (!actor) { return {}; }
    Require(actor != nullptr);
    WorldView result;
    result.player_position = actor->position;
    result.origin = {actor->position.x - radius, actor->position.z - radius, actor->position.y};
    result.width = 2 * radius + 1;
    result.height = result.width;
    result.tick = tick_;
    result.remainingTicks = actor->remainingTicks;
    result.speed = actor->speed;
    result.pacing = pacer_.GetSettings();
    result.commandQueued = slot->pending.has_value();
    result.playerId = id;
    result.revision = revision_;
    result.opportunity = slot->opportunity;
    result.lastInputSequence = slot->lastSequence;
    result.players = GetPlayers();
    result.cells.resize(static_cast<std::size_t>(result.width * result.height));
    for (int z = 0; z < result.height; ++z) {
        for (int x = 0; x < result.width; ++x) {
            TileCoord position{result.origin.x + x, result.origin.z + z, result.origin.y};
            const int dx = x - radius;
            const int dz = z - radius;
            if (dx * dx + dz * dz > radius * radius) {
                continue;
            }
            ViewCell& cell = result.cells[static_cast<std::size_t>(z * result.width + x)];
            bool overlayFound = false;
            for (unsigned int depth = 0; position.y >= world_min_y; ++depth, --position.y) {
                if (dx * dx + dz * dz + static_cast<int>(depth * depth) > radius * radius) { break; }
                const Tile* tile = world_.GetTile(position);
                // Projection follows only the air column, but each candidate
                // must have its own straight LOS from the player (no bent ray).
                if (!tile) { break; }
                const bool visible = LineVisible(world_, actor->position, position);
                if (visible) {
                    cell.visible = true;
                    cell.tile = *tile;
                    cell.terrainDepth = depth;
                    if (!overlayFound) {
                        if (const Actor* occupant = world_.GetActor(world_.ActorAt(position))) {
                            cell.actor = occupant->id;
                            cell.player = occupant->player;
                        }
                        cell.ground_items = static_cast<unsigned int>(world_.ItemsAt(position).size());
                        if (const InteractiveObject* object = world_.GetObject(world_.ObjectAt(position))) {
                            cell.object = object->id;
                            cell.object_enabled = object->enabled;
                        }
                        overlayFound = cell.actor != 0 || cell.ground_items != 0 || cell.object != 0;
                        if (overlayFound) { cell.overlayDepth = depth; }
                    }
                }
                if (!world_.GetContent().GetTerrain(tile->terrain).openSpace) { break; }
            }
        }
    }
    result.inventory = DescribeItems(world_, player, actor->inventory);
    result.groundItems = DescribeItems(world_, player, world_.ItemsAt(actor->position));
    result.carriedMassGrams = world_.GetCarriedMass(player);
    result.carryCapacityGrams = actor->carryCapacityGrams;
    return result;
}

WorldResult<void> Server::SetBubbleRadii(int active_radius, int resident_radius,
    Diagnostics* diagnostics) {
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    if (active_radius < 0 || resident_radius < active_radius || resident_radius > 8
        || !BubbleFits(actor->position, resident_radius)) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    for (const PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* participant = world_.GetActor(PlayerActor(slot));
        Require(participant);
        if (!BubbleFits(participant->position, resident_radius)) {
            return eastl::unexpected(WorldError::invalid_region);
        }
    }
    const int oldActive = active_radius_;
    const int oldResident = resident_radius_;
    for (PlayerSlot& slot : players_) {
        if (slot.state != PlayerConnectionState::Active) { continue; }
        const Actor* participant = world_.GetActor(PlayerActor(slot));
        const ChunkCoord center = BubbleCenter(participant->position);
        const LoadRequest active = slot.id == GetHostPlayerId() ? active_request_ : slot.activeRequest;
        const LoadRequest resident = slot.id == GetHostPlayerId() ? resident_request_ : slot.residentRequest;
        Require(world_.UpdateRequest(active, {center, active_radius}, LoadMode::active).has_value());
        Require(world_.UpdateRequest(resident, {center, resident_radius}, LoadMode::resident).has_value());
    }
    if (!world_.ReconcileChunks(diagnostics)) {
        for (const PlayerSlot& slot : players_) {
            if (slot.state != PlayerConnectionState::Active) { continue; }
            const LoadRequest active = slot.id == GetHostPlayerId() ? active_request_ : slot.activeRequest;
            const LoadRequest resident = slot.id == GetHostPlayerId() ? resident_request_ : slot.residentRequest;
            Require(world_.UpdateRequest(active, {slot.center, oldActive}, LoadMode::active).has_value());
            Require(world_.UpdateRequest(resident, {slot.center, oldResident}, LoadMode::resident).has_value());
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
    ++revision_;
    return {};
}

SaveResult<void> Server::UpdatePlayerBubble(PlayerSlot& slot, int activeRadius, int residentRadius,
    Diagnostics* diagnostics) {
    const Actor* actor = world_.GetActor(PlayerActor(slot));
    Require(actor != nullptr);
    const ChunkCoord center = BubbleCenter(actor->position);
    LoadRequest active = slot.id == GetHostPlayerId() ? active_request_ : slot.activeRequest;
    LoadRequest resident = slot.id == GetHostPlayerId() ? resident_request_ : slot.residentRequest;
    Require(world_.UpdateRequest(active, {center, activeRadius}, LoadMode::active).has_value());
    Require(world_.UpdateRequest(resident, {center, residentRadius}, LoadMode::resident).has_value());
    const TileCoord position = actor->position;
    const auto reconciled = world_.ReconcileChunks(diagnostics);
    if (!reconciled) {
        Require(world_.UpdateRequest(active, {slot.center, active_radius_}, LoadMode::active).has_value());
        Require(world_.UpdateRequest(resident, {slot.center, resident_radius_}, LoadMode::resident).has_value());
        return eastl::unexpected(reconciled.error());
    }
    slot.center = center;
    slot.position = position;
    if (slot.id == GetHostPlayerId()) { bubbleCenter_ = center; }
    active_radius_ = activeRadius;
    resident_radius_ = residentRadius;
    return {};
}

Server::Server(const ContentCatalog& content, EmptyTag) : world_(content) {}

SessionState Server::Checkpoint() const {
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    SessionState session{tick_, rng_state_, actor->id, actor->position,
        active_radius_, resident_radius_, autosaveInterval_, pacer_.GetSettings()};
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
        lastSaveTick_ = tick_;
    }
    return result;
}

SaveResult<void> Server::Save(Diagnostics* diagnostics) {
    const auto result = world_.Save(Checkpoint(), diagnostics);
    if (result) {
        lastSaveTick_ = tick_;
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
        || !BubbleFits(session.playerPosition, session.residentRadius)) {
        if (diagnostics) {
            diagnostics->Add("Invalid server session");
        }
        return eastl::unexpected(SaveError::InvalidData);
    }
    server->bubbleCenter_ = BubbleCenter(session.playerPosition);
    server->active_radius_ = session.activeRadius;
    server->resident_radius_ = session.residentRadius;
    server->active_request_ = Take(server->world_.RequestChunks(
        {server->bubbleCenter_, session.activeRadius}, LoadMode::active));
    server->resident_request_ = Take(server->world_.RequestChunks(
        {server->bubbleCenter_, session.residentRadius}, LoadMode::resident));
    // Materialize the whole roster together: reconciliation settles gravity,
    // and guests may support each other or the host across saved layers.
    Vector<LoadRequest> staging;
    for (const SavedPlayer& saved : session.players) {
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
    server->tick_ = session.tick;
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
std::uint64_t Server::GetTick() const { return tick_; }

}
