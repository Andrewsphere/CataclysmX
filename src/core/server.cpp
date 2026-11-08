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

bool Cardinal(int dx, int dz) {
    return (dz == 0 && (dx == -1 || dx == 1))
        || (dx == 0 && (dz == -1 || dz == 1));
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
    // Integer Bresenham ray. A blocking target is visible; cells beyond it are
    // not. Unavailable tiles also stop the ray, without loading anything.
    const int dx = std::abs(target.x - origin.x);
    const int dz = -std::abs(target.z - origin.z);
    const int sx = origin.x < target.x ? 1 : -1;
    const int sz = origin.z < target.z ? 1 : -1;
    int error = dx + dz;
    TileCoord cursor = origin;
    for (;;) {
        const Tile* tile = world.GetTile(cursor);
        if (!tile) {
            return false;
        }
        if (cursor == target) {
            return true;
        }
        if (cursor != origin && world.GetContent().GetTerrain(tile->terrain).blocksSight) {
            return false;
        }
        const int twice = 2 * error;
        if (twice >= dz) {
            error += dz;
            cursor.x += sx;
        }
        if (twice <= dx) {
            error += dx;
            cursor.z += sz;
        }
    }
}

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
    }
    std::abort();
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
    const ItemHandle backpack = Take(world_.CreateItem(ItemKind::backpack, ItemLocation::Ground({0, 0})));
    Take(world_.CreateItem(ItemKind::bandage, ItemLocation::Inside(backpack)));
    Take(world_.CreateItem(ItemKind::rock, ItemLocation::Ground({1, 0})));
    Take(world_.CreateObject({2, 0}, false));
    Take(world_.CreateActor({5, 5}));
}

CommandResult Server::Send(Command command) {
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    const TileCoord position = actor->position;
    const auto failed = [this](Outcome outcome) { return CommandResult{outcome, false, tick_}; };
    Outcome outcome = Outcome::invalid_command;
    switch (command.kind) {
    case CommandKind::wait:
        outcome = Outcome::waited;
        break;
    case CommandKind::move: {
        if (!Cardinal(command.dx, command.dz)) {
            return failed(Outcome::invalid_command);
        }
        const TileCoord destination{position.x + command.dx, position.z + command.dz};
        // Validate the complete future bubble before acquiring any demand.
        if (!BubbleFits(destination, resident_radius_)) {
            return failed(Outcome::unavailable);
        }
        LoadRequest temporary{};
        const ChunkCoord center = ChunkOf(destination);
        bool complete = true;
        for (int z = center.z - resident_radius_; z <= center.z + resident_radius_; ++z) {
            for (int x = center.x - resident_radius_; x <= center.x + resident_radius_; ++x) {
                if (!world_.GetChunk({x, z})) {
                    complete = false;
                }
            }
        }
        // Preserve the old bubble while staging every chunk the new requests
        // will need. Reconcile failures leave all live world state untouched.
        if (center != bubbleCenter_ || !complete) {
            temporary = Take(world_.RequestChunks({center, resident_radius_}, LoadMode::resident));
            const auto loaded = world_.ReconcileChunks();
            if (!loaded) {
                Require(world_.ReleaseRequest(temporary).has_value());
                return {Outcome::unavailable, false, tick_, loaded.error()};
            }
        }
        const auto moved = world_.MoveActor(player_, destination);
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
        Require(UpdateBubble(active_radius_, resident_radius_).has_value());
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
        Require(world_.TransferItem(item, ItemLocation::Carried(player_)).has_value());
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
    case CommandKind::interact: {
        if (!Cardinal(command.dx, command.dz)) {
            return failed(Outcome::invalid_command);
        }
        const TileCoord destination{position.x + command.dx, position.z + command.dz};
        const Tile* tile = world_.GetTile(destination);
        if (!tile) {
            return failed(Outcome::unavailable);
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
    Require(tick_ != eastl::numeric_limits<std::uint64_t>::max());
    cataclysmx::Advance(world_, player_, rng_state_);
    ++tick_;
    CommandResult result{outcome, true, tick_};
    if (world_.HasSave() && autosaveInterval_ != 0
        && tick_ - lastSaveTick_ >= autosaveInterval_) {
        const auto saved = Save();
        if (!saved) {
            result.storageError = saved.error();
        }
    }
    return result;
}

WorldView Server::View(int radius) const {
    // Public presentation input is clamped to [0, 12]. The square, row-major
    // snapshot has circular sight range; out-of-range corners stay empty.
    radius = eastl::clamp(radius, 0, 12);
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    WorldView result;
    result.player_position = actor->position;
    result.origin = {actor->position.x - radius, actor->position.z - radius};
    result.width = 2 * radius + 1;
    result.height = result.width;
    result.tick = tick_;
    result.cells.resize(static_cast<std::size_t>(result.width * result.height));
    for (int z = 0; z < result.height; ++z) {
        for (int x = 0; x < result.width; ++x) {
            const TileCoord position{result.origin.x + x, result.origin.z + z};
            const int dx = x - radius;
            const int dz = z - radius;
            if (dx * dx + dz * dz > radius * radius
                || !LineVisible(world_, actor->position, position)) {
                continue;
            }
            ViewCell& cell = result.cells[static_cast<std::size_t>(z * result.width + x)];
            cell.visible = true;
            cell.tile = *world_.GetTile(position);
            if (const Actor* occupant = world_.GetActor(world_.ActorAt(position))) {
                cell.actor = occupant->id;
                cell.player = occupant->player;
            }
            cell.ground_items = static_cast<unsigned int>(world_.ItemsAt(position).size());
            if (const InteractiveObject* object = world_.GetObject(world_.ObjectAt(position))) {
                cell.object = object->id;
                cell.object_enabled = object->enabled;
            }
        }
    }
    for (ItemHandle handle : actor->inventory) {
        const Item* item = world_.GetItem(handle);
        Require(item != nullptr);
        result.inventory.push_back({item->id, item->kind, static_cast<unsigned int>(item->contents.size())});
    }
    eastl::sort(result.inventory.begin(), result.inventory.end(),
        [](const InventoryEntry& a, const InventoryEntry& b) { return a.id < b.id; });
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
    if (!UpdateBubble(active_radius, resident_radius, diagnostics)) {
        return eastl::unexpected(WorldError::storage_failure);
    }
    return {};
}

SaveResult<void> Server::UpdateBubble(int activeRadius, int residentRadius,
    Diagnostics* diagnostics) {
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    const ChunkCoord center = ChunkOf(actor->position);
    Require(world_.UpdateRequest(active_request_, {center, activeRadius}, LoadMode::active).has_value());
    Require(world_.UpdateRequest(resident_request_, {center, residentRadius}, LoadMode::resident).has_value());
    const auto reconciled = world_.ReconcileChunks(diagnostics);
    if (!reconciled) {
        Require(world_.UpdateRequest(active_request_, {bubbleCenter_, active_radius_}, LoadMode::active).has_value());
        Require(world_.UpdateRequest(resident_request_, {bubbleCenter_, resident_radius_}, LoadMode::resident).has_value());
        return eastl::unexpected(reconciled.error());
    }
    bubbleCenter_ = center;
    active_radius_ = activeRadius;
    resident_radius_ = residentRadius;
    return {};
}

Server::Server(const ContentCatalog& content, EmptyTag) : world_(content) {}

SessionState Server::Checkpoint() const {
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    return {tick_, rng_state_, actor->id, actor->position,
        active_radius_, resident_radius_, autosaveInterval_};
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
    server->bubbleCenter_ = ChunkOf(session.playerPosition);
    server->active_radius_ = session.activeRadius;
    server->resident_radius_ = session.residentRadius;
    server->active_request_ = Take(server->world_.RequestChunks(
        {server->bubbleCenter_, session.activeRadius}, LoadMode::active));
    server->resident_request_ = Take(server->world_.RequestChunks(
        {server->bubbleCenter_, session.residentRadius}, LoadMode::resident));
    const auto reconciled = server->world_.ReconcileChunks(diagnostics);
    if (!reconciled) {
        return eastl::unexpected(reconciled.error());
    }
    server->player_ = server->world_.FindActor(session.player);
    const Actor* actor = server->world_.GetActor(server->player_);
    if (!actor || !actor->player || actor->position != session.playerPosition) {
        if (diagnostics) {
            diagnostics->Add("Saved player does not match server session");
        }
        return eastl::unexpected(SaveError::InvalidData);
    }
    server->tick_ = session.tick;
    server->rng_state_ = session.rngState;
    server->autosaveInterval_ = session.autosaveInterval;
    server->lastSaveTick_ = session.tick;
    return eastl::move(server);
}

void Server::SetAutosaveInterval(std::uint64_t turns) { autosaveInterval_ = turns; }
std::uint64_t Server::GetAutosaveInterval() const { return autosaveInterval_; }

World& Server::GetWorld() { return world_; }
const World& Server::GetWorld() const { return world_; }
ActorHandle Server::GetPlayer() const { return player_; }
std::uint64_t Server::GetTick() const { return tick_; }

}
