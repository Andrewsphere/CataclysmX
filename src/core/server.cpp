#include <cstdlib>

#include "core/server.h"

#include "core/simulation.h"

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/sort.h>

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
        if (cursor != origin && (tile->terrain == Terrain::wall
                || tile->terrain == Terrain::closed_door)) {
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

Server::Server(std::uint32_t seed) : world_(seed), rng_state_(seed) {
    active_request_ = Take(world_.RequestChunks({{0, 0}, active_radius_}, LoadMode::active));
    resident_request_ = Take(world_.RequestChunks({{0, 0}, resident_radius_}, LoadMode::resident));
    world_.ReconcileChunks();
    constexpr TileCoord floors[] = {{0, 0}, {1, 0}, {2, 0}, {5, 5}};
    for (TileCoord position : floors) {
        Require(world_.SetTile(position, {}).has_value());
    }
    Require(world_.SetTile({3, 0}, {Terrain::closed_door, 0}).has_value());
    Require(world_.SetTile({3, 1}, {Terrain::wall, 0}).has_value());
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
        if (!world_.GetTile(destination)) {
            temporary = Take(world_.RequestChunks({ChunkOf(destination), 0}, LoadMode::resident));
            world_.ReconcileChunks();
        }
        const auto moved = world_.MoveActor(player_, destination);
        if (!moved) {
            if (temporary.value != 0) {
                Require(world_.ReleaseRequest(temporary).has_value());
                world_.ReconcileChunks();
            }
            return failed(MovementFailure(moved.error()));
        }
        // The player has entered the destination before any old demand can be
        // removed, preserving its runtime key even with zero-radius bubbles.
        UpdateBubble();
        if (temporary.value != 0) {
            Require(world_.ReleaseRequest(temporary).has_value());
            world_.ReconcileChunks();
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
        if (tile->terrain == Terrain::closed_door || tile->terrain == Terrain::open_door) {
            const bool closing = tile->terrain == Terrain::open_door;
            if (closing && world_.ActorAt(destination).value != 0) {
                return failed(Outcome::occupied);
            }
            Tile changed = *tile;
            changed.terrain = closing ? Terrain::closed_door : Terrain::open_door;
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
    return {outcome, true, tick_};
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

WorldResult<void> Server::SetBubbleRadii(int active_radius, int resident_radius) {
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    if (active_radius < 0 || resident_radius < active_radius || resident_radius > 8
        || !BubbleFits(actor->position, resident_radius)) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    active_radius_ = active_radius;
    resident_radius_ = resident_radius;
    UpdateBubble();
    return {};
}

void Server::UpdateBubble() {
    const Actor* actor = world_.GetActor(player_);
    Require(actor != nullptr);
    const ChunkCoord center = ChunkOf(actor->position);
    Require(world_.UpdateRequest(active_request_, {center, active_radius_}, LoadMode::active).has_value());
    Require(world_.UpdateRequest(resident_request_, {center, resident_radius_}, LoadMode::resident).has_value());
    world_.ReconcileChunks();
}

World& Server::GetWorld() { return world_; }
const World& Server::GetWorld() const { return world_; }
ActorHandle Server::GetPlayer() const { return player_; }
std::uint64_t Server::GetTick() const { return tick_; }

}
