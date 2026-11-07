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

void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

template<class T>
T take(WorldResult<T> result) {
    require(result.has_value());
    return *result;
}

bool cardinal(int dx, int dz) {
    return (dz == 0 && (dx == -1 || dx == 1))
        || (dx == 0 && (dz == -1 || dz == 1));
}

bool bubble_fits(TileCoord position, int radius) {
    if (!valid_position(position)) {
        return false;
    }
    const ChunkCoord center = chunk_of(position);
    constexpr int minimum = -world_coordinate_limit / chunk_size;
    constexpr int maximum = world_coordinate_limit / chunk_size;
    return center.x >= minimum + radius && center.x <= maximum - radius
        && center.z >= minimum + radius && center.z <= maximum - radius;
}

ItemHandle select_item(const World& world, const Vector<ItemHandle>& items, PersistentId id) {
    ItemHandle selected{};
    PersistentId smallest = eastl::numeric_limits<PersistentId>::max();
    for (ItemHandle handle : items) {
        const Item* item = world.item(handle);
        require(item != nullptr);
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

Outcome movement_failure(WorldError error) {
    switch (error) {
    case WorldError::blocked: return Outcome::blocked;
    case WorldError::occupied: return Outcome::occupied;
    case WorldError::invalid_position:
    case WorldError::chunk_unavailable: return Outcome::unavailable;
    default: std::abort();
    }
}

bool line_visible(const World& world, TileCoord origin, TileCoord target) {
    // Integer Bresenham ray. A blocking target is visible; cells beyond it are
    // not. Unavailable tiles also stop the ray, without loading anything.
    const int dx = std::abs(target.x - origin.x);
    const int dz = -std::abs(target.z - origin.z);
    const int sx = origin.x < target.x ? 1 : -1;
    const int sz = origin.z < target.z ? 1 : -1;
    int error = dx + dz;
    TileCoord cursor = origin;
    for (;;) {
        const Tile* tile = world.tile(cursor);
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

const char* outcome_message(Outcome outcome) {
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
    active_request_ = take(world_.request_chunks({{0, 0}, active_radius_}, LoadMode::active));
    resident_request_ = take(world_.request_chunks({{0, 0}, resident_radius_}, LoadMode::resident));
    world_.reconcile_chunks();
    constexpr TileCoord floors[] = {{0, 0}, {1, 0}, {2, 0}, {5, 5}};
    for (TileCoord position : floors) {
        require(world_.set_tile(position, {}).has_value());
    }
    require(world_.set_tile({3, 0}, {Terrain::closed_door, 0}).has_value());
    require(world_.set_tile({3, 1}, {Terrain::wall, 0}).has_value());
    player_ = take(world_.create_actor({0, 0}, true));
    const ItemHandle backpack = take(world_.create_item(ItemKind::backpack, ItemLocation::ground({0, 0})));
    take(world_.create_item(ItemKind::bandage, ItemLocation::inside(backpack)));
    take(world_.create_item(ItemKind::rock, ItemLocation::ground({1, 0})));
    take(world_.create_object({2, 0}, false));
    take(world_.create_actor({5, 5}));
}

CommandResult Server::send(Command command) {
    const Actor* actor = world_.actor(player_);
    require(actor != nullptr);
    const TileCoord position = actor->position;
    const auto failed = [this](Outcome outcome) { return CommandResult{outcome, false, tick_}; };
    Outcome outcome = Outcome::invalid_command;
    switch (command.kind) {
    case CommandKind::wait:
        outcome = Outcome::waited;
        break;
    case CommandKind::move: {
        if (!cardinal(command.dx, command.dz)) {
            return failed(Outcome::invalid_command);
        }
        const TileCoord destination{position.x + command.dx, position.z + command.dz};
        // Validate the complete future bubble before acquiring any demand.
        if (!bubble_fits(destination, resident_radius_)) {
            return failed(Outcome::unavailable);
        }
        LoadRequest temporary{};
        if (!world_.tile(destination)) {
            temporary = take(world_.request_chunks({chunk_of(destination), 0}, LoadMode::resident));
            world_.reconcile_chunks();
        }
        const auto moved = world_.move_actor(player_, destination);
        if (!moved) {
            if (temporary.value != 0) {
                require(world_.release_request(temporary).has_value());
                world_.reconcile_chunks();
            }
            return failed(movement_failure(moved.error()));
        }
        // The player has entered the destination before any old demand can be
        // removed, preserving its runtime key even with zero-radius bubbles.
        update_bubble();
        if (temporary.value != 0) {
            require(world_.release_request(temporary).has_value());
            world_.reconcile_chunks();
        }
        outcome = Outcome::moved;
        break;
    }
    case CommandKind::pick_up: {
        const ItemHandle item = select_item(world_, world_.items_at(position), command.item);
        if (item.value == 0) {
            return failed(Outcome::nothing_here);
        }
        require(world_.transfer_item(item, ItemLocation::carried(player_)).has_value());
        outcome = Outcome::picked_up;
        break;
    }
    case CommandKind::drop: {
        const ItemHandle item = select_item(world_, actor->inventory, command.item);
        if (item.value == 0) {
            return failed(Outcome::not_carried);
        }
        require(world_.transfer_item(item, ItemLocation::ground(position)).has_value());
        outcome = Outcome::dropped;
        break;
    }
    case CommandKind::interact: {
        if (!cardinal(command.dx, command.dz)) {
            return failed(Outcome::invalid_command);
        }
        const TileCoord destination{position.x + command.dx, position.z + command.dz};
        const Tile* tile = world_.tile(destination);
        if (!tile) {
            return failed(Outcome::unavailable);
        }
        if (tile->terrain == Terrain::closed_door || tile->terrain == Terrain::open_door) {
            const bool closing = tile->terrain == Terrain::open_door;
            if (closing && world_.actor_at(destination).value != 0) {
                return failed(Outcome::occupied);
            }
            Tile changed = *tile;
            changed.terrain = closing ? Terrain::closed_door : Terrain::open_door;
            require(world_.set_tile(destination, changed).has_value());
            outcome = closing ? Outcome::closed : Outcome::opened;
        } else {
            const ObjectHandle object = world_.object_at(destination);
            if (object.value == 0) {
                return failed(Outcome::nothing_here);
            }
            const bool enabled = world_.object(object)->enabled;
            require(world_.set_object_enabled(object, !enabled).has_value());
            outcome = Outcome::toggled;
        }
        break;
    }
    default:
        return failed(Outcome::invalid_command);
    }
    require(tick_ != eastl::numeric_limits<std::uint64_t>::max());
    cataclysmx::advance(world_, player_, rng_state_);
    ++tick_;
    return {outcome, true, tick_};
}

WorldView Server::view(int radius) const {
    // Public presentation input is clamped to [0, 12]. The square, row-major
    // snapshot has circular sight range; out-of-range corners stay empty.
    radius = eastl::clamp(radius, 0, 12);
    const Actor* actor = world_.actor(player_);
    require(actor != nullptr);
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
                || !line_visible(world_, actor->position, position)) {
                continue;
            }
            ViewCell& cell = result.cells[static_cast<std::size_t>(z * result.width + x)];
            cell.visible = true;
            cell.tile = *world_.tile(position);
            if (const Actor* occupant = world_.actor(world_.actor_at(position))) {
                cell.actor = occupant->id;
                cell.player = occupant->player;
            }
            cell.ground_items = static_cast<unsigned int>(world_.items_at(position).size());
            if (const InteractiveObject* object = world_.object(world_.object_at(position))) {
                cell.object = object->id;
                cell.object_enabled = object->enabled;
            }
        }
    }
    for (ItemHandle handle : actor->inventory) {
        const Item* item = world_.item(handle);
        require(item != nullptr);
        result.inventory.push_back({item->id, item->kind, static_cast<unsigned int>(item->contents.size())});
    }
    eastl::sort(result.inventory.begin(), result.inventory.end(),
        [](const InventoryEntry& a, const InventoryEntry& b) { return a.id < b.id; });
    return result;
}

WorldResult<void> Server::set_bubble_radii(int active_radius, int resident_radius) {
    const Actor* actor = world_.actor(player_);
    require(actor != nullptr);
    if (active_radius < 0 || resident_radius < active_radius || resident_radius > 8
        || !bubble_fits(actor->position, resident_radius)) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    active_radius_ = active_radius;
    resident_radius_ = resident_radius;
    update_bubble();
    return {};
}

void Server::update_bubble() {
    const Actor* actor = world_.actor(player_);
    require(actor != nullptr);
    const ChunkCoord center = chunk_of(actor->position);
    require(world_.update_request(active_request_, {center, active_radius_}, LoadMode::active).has_value());
    require(world_.update_request(resident_request_, {center, resident_radius_}, LoadMode::resident).has_value());
    world_.reconcile_chunks();
}

World& Server::world() { return world_; }
const World& Server::world() const { return world_; }
ActorHandle Server::player() const { return player_; }
std::uint64_t Server::tick() const { return tick_; }

}
