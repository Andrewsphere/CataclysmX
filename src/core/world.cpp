#include <cstdlib>

#include "core/world.h"
#include "core/slot_map.h"

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/sort.h>
#include <EASTL/utility.h>

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

int floor_chunk(int coordinate) {
    const int quotient = coordinate / chunk_size;
    return quotient - (coordinate % chunk_size < 0 ? 1 : 0);
}

bool chunk_less(ChunkCoord left, ChunkCoord right) {
    return left.x < right.x || (left.x == right.x && left.z < right.z);
}

bool valid_region(ChunkRegion region) {
    constexpr int minimum = -world_coordinate_limit / chunk_size;
    constexpr int maximum = world_coordinate_limit / chunk_size;
    return region.radius >= 0 && region.radius <= 8
        && region.center.x >= minimum + region.radius
        && region.center.x <= maximum - region.radius
        && region.center.z >= minimum + region.radius
        && region.center.z <= maximum - region.radius;
}

bool valid_mode(LoadMode mode) {
    return mode == LoadMode::resident || mode == LoadMode::active;
}

template<class T>
void erase_one(Vector<T>& values, T value) {
    const auto found = eastl::find(values.begin(), values.end(), value);
    require(found != values.end());
    values.erase(found);
}

template<class T>
std::size_t occurrences(const Vector<T>& values, T value) {
    return static_cast<std::size_t>(eastl::count(values.begin(), values.end(), value));
}

// SlotMap::get assumes a live slot when a version happens to match. In
// particular, never pass unchecked/fabricated external handles straight to get.
template<class T, class Tag>
T* live(dod::slot_map64<T>& slots, Handle<Tag> handle) {
    const typename dod::slot_map64<T>::key key{handle.value};
    return key.get_tag() == 0 && slots.contains(key) ? slots.get(key) : nullptr;
}

template<class T, class Tag>
const T* live(const dod::slot_map64<T>& slots, Handle<Tag> handle) {
    const typename dod::slot_map64<T>::key key{handle.value};
    return key.get_tag() == 0 && slots.contains(key) ? slots.get(key) : nullptr;
}

struct Request {
    ChunkRegion region;
    LoadMode mode;
};

struct SavedActor {
    PersistentId id;
    TileCoord position;
    bool player;
    std::uint64_t turns;
};

struct SavedObject {
    PersistentId id;
    TileCoord position;
    bool enabled;
    std::uint64_t updates;
};

struct SavedItem {
    PersistentId id;
    ItemKind kind;
    LocationKind location;
    TileCoord position;
    PersistentId owner;
};

struct Archive {
    ChunkCoord position;
    eastl::array<Tile, chunk_size * chunk_size> tiles;
    Vector<SavedActor> actors;
    Vector<SavedObject> objects;
    // Breadth-first, preserving each owner's collection order. No runtime keys.
    Vector<SavedItem> items;
};

struct Demand {
    ChunkCoord position;
    bool active;
};

}

ChunkCoord chunk_of(TileCoord position) {
    return {floor_chunk(position.x), floor_chunk(position.z)};
}

int tile_index(TileCoord position) {
    const int x = (position.x % chunk_size + chunk_size) % chunk_size;
    const int z = (position.z % chunk_size + chunk_size) % chunk_size;
    return z * chunk_size + x;
}

bool valid_position(TileCoord position) {
    return position.x >= -world_coordinate_limit && position.x <= world_coordinate_limit
        && position.z >= -world_coordinate_limit && position.z <= world_coordinate_limit;
}

bool passable(Terrain terrain) {
    return terrain == Terrain::floor || terrain == Terrain::open_door;
}

const char* item_name(ItemKind kind) {
    switch (kind) {
    case ItemKind::rock: return "rock";
    case ItemKind::backpack: return "backpack";
    case ItemKind::bandage: return "bandage";
    }
    std::abort();
}

ItemLocation ItemLocation::ground(TileCoord position) {
    ItemLocation result;
    result.position = position;
    return result;
}

ItemLocation ItemLocation::carried(ActorHandle actor) {
    ItemLocation result;
    result.kind = LocationKind::actor;
    result.actor = actor;
    return result;
}

ItemLocation ItemLocation::inside(ItemHandle item) {
    ItemLocation result;
    result.kind = LocationKind::item;
    result.item = item;
    return result;
}

ItemLocation ItemLocation::stored(ObjectHandle object) {
    ItemLocation result;
    result.kind = LocationKind::object;
    result.object = object;
    return result;
}

struct World::Impl {
    std::uint32_t seed;
    PersistentId next_id = 1;
    dod::slot_map64<Actor> actors;
    dod::slot_map64<Item> items;
    dod::slot_map64<InteractiveObject> objects;
    dod::slot_map64<Request> requests;
    Vector<Chunk> chunks; // Always lexicographic (x, z).
    Vector<Archive> archives; // Only currently unloaded chunks.

    explicit Impl(std::uint32_t value) : seed(value) {}

    PersistentId issue_id() {
        require(next_id != eastl::numeric_limits<PersistentId>::max());
        return next_id++;
    }

    Chunk* chunk(ChunkCoord position) {
        const auto found = eastl::lower_bound(chunks.begin(), chunks.end(), position,
            [](const Chunk& value, ChunkCoord key) { return chunk_less(value.position, key); });
        return found != chunks.end() && found->position == position ? &*found : nullptr;
    }

    const Chunk* chunk(ChunkCoord position) const {
        const auto found = eastl::lower_bound(chunks.begin(), chunks.end(), position,
            [](const Chunk& value, ChunkCoord key) { return chunk_less(value.position, key); });
        return found != chunks.end() && found->position == position ? &*found : nullptr;
    }

    WorldResult<void> available(TileCoord position) const {
        if (!valid_position(position)) {
            return eastl::unexpected(WorldError::invalid_position);
        }
        if (!chunk(chunk_of(position))) {
            return eastl::unexpected(WorldError::chunk_unavailable);
        }
        return {};
    }

    ActorHandle find_actor(PersistentId id) const {
        for (const auto& entry : actors.items()) {
            if (entry.second.get().id == id) {
                return {entry.first.raw};
            }
        }
        return {};
    }

    ItemHandle find_item(PersistentId id) const {
        for (const auto& entry : items.items()) {
            if (entry.second.get().id == id) {
                return {entry.first.raw};
            }
        }
        return {};
    }

    ObjectHandle find_object(PersistentId id) const {
        for (const auto& entry : objects.items()) {
            if (entry.second.get().id == id) {
                return {entry.first.raw};
            }
        }
        return {};
    }

    WorldResult<ItemLocation> destination(ItemLocation location, ItemHandle moving = {}) const {
        switch (location.kind) {
        case LocationKind::ground:
            if (auto result = available(location.position); !result) {
                return eastl::unexpected(result.error());
            }
            return ItemLocation::ground(location.position);
        case LocationKind::actor:
            if (!live(actors, location.actor)) {
                return eastl::unexpected(WorldError::invalid_handle);
            }
            return ItemLocation::carried(location.actor);
        case LocationKind::object:
            if (!live(objects, location.object)) {
                return eastl::unexpected(WorldError::invalid_handle);
            }
            return ItemLocation::stored(location.object);
        case LocationKind::item: {
            const Item* parent = live(items, location.item);
            if (!parent) {
                return eastl::unexpected(WorldError::invalid_handle);
            }
            if (parent->kind != ItemKind::backpack) {
                return eastl::unexpected(WorldError::not_container);
            }
            ItemHandle cursor = location.item;
            for (std::size_t depth = 0; ; ++depth) {
                require(depth <= items.size());
                if (moving.value != 0 && cursor == moving) {
                    return eastl::unexpected(WorldError::containment_cycle);
                }
                const Item* ancestor = live(items, cursor);
                require(ancestor != nullptr);
                if (ancestor->location.kind != LocationKind::item) {
                    break;
                }
                cursor = ancestor->location.item;
            }
            return ItemLocation::inside(location.item);
        }
        }
        return eastl::unexpected(WorldError::invalid_handle);
    }

    Vector<ItemHandle>& collection(ItemLocation location) {
        switch (location.kind) {
        case LocationKind::ground: {
            Chunk* owner = chunk(chunk_of(location.position));
            require(owner != nullptr);
            return owner->ground_items;
        }
        case LocationKind::actor: {
            Actor* owner = live(actors, location.actor);
            require(owner != nullptr);
            return owner->inventory;
        }
        case LocationKind::item: {
            Item* owner = live(items, location.item);
            require(owner != nullptr);
            return owner->contents;
        }
        case LocationKind::object: {
            InteractiveObject* owner = live(objects, location.object);
            require(owner != nullptr);
            return owner->contents;
        }
        }
        std::abort();
    }

    void generate(Chunk& value) const {
        for (int z = 0; z < chunk_size; ++z) {
            for (int x = 0; x < chunk_size; ++x) {
                const int global_x = value.position.x * chunk_size + x;
                const int global_z = value.position.z * chunk_size + z;
                // Unsigned coordinate mixing is portable, with no RNG state or IDs.
                std::uint32_t hash = seed ^ (static_cast<std::uint32_t>(global_x) * 0x9e3779b9U)
                    ^ (static_cast<std::uint32_t>(global_z) * 0x85ebca6bU);
                hash ^= hash >> 16;
                hash *= 0x7feb352dU;
                hash ^= hash >> 15;
                const bool demo = global_x >= -32 && global_x <= 32
                    && global_z >= -32 && global_z <= 32;
                value.tiles[z * chunk_size + x] = {
                    !demo && hash % 13U == 0 ? Terrain::wall : Terrain::floor, 0};
            }
        }
    }

    void unload(const Chunk& value) {
        Archive saved{value.position, value.tiles, {}, {}, {}};
        Vector<ItemHandle> pending = value.ground_items;
        for (ActorHandle handle : value.actors) {
            const Actor* actor = live(actors, handle);
            require(actor != nullptr);
            saved.actors.push_back({actor->id, actor->position, actor->player, actor->turns});
            for (ItemHandle item : actor->inventory) {
                pending.push_back(item);
            }
        }
        for (ObjectHandle handle : value.objects) {
            const InteractiveObject* object = live(objects, handle);
            require(object != nullptr);
            saved.objects.push_back({object->id, object->position, object->enabled, object->updates});
            for (ItemHandle item : object->contents) {
                pending.push_back(item);
            }
        }
        for (std::size_t index = 0; index < pending.size(); ++index) {
            require(pending.size() <= items.size());
            const Item* item = live(items, pending[index]);
            require(item != nullptr);
            PersistentId owner = 0;
            switch (item->location.kind) {
            case LocationKind::ground: break;
            case LocationKind::actor: owner = live(actors, item->location.actor)->id; break;
            case LocationKind::item: owner = live(items, item->location.item)->id; break;
            case LocationKind::object: owner = live(objects, item->location.object)->id; break;
            }
            saved.items.push_back({item->id, item->kind, item->location.kind,
                item->location.position, owner});
            for (ItemHandle child : item->contents) {
                pending.push_back(child);
            }
        }
        // First capture all values; then retire every runtime key. Do not reset
        // the central slot maps, even when the last chunk is unloaded.
        for (ItemHandle handle : pending) {
            items.erase(dod::slot_map64<Item>::key{handle.value});
        }
        for (ActorHandle handle : value.actors) {
            actors.erase(dod::slot_map64<Actor>::key{handle.value});
        }
        for (ObjectHandle handle : value.objects) {
            objects.erase(dod::slot_map64<InteractiveObject>::key{handle.value});
        }
        archives.push_back(eastl::move(saved));
    }

    void restore(Chunk& value, const Archive& saved) {
        value.tiles = saved.tiles;
        for (const SavedActor& actor : saved.actors) {
            const auto key = actors.emplace(Actor{actor.id, actor.position, actor.player, actor.turns, {}});
            value.actors.push_back({key.raw});
        }
        for (const SavedObject& object : saved.objects) {
            const auto key = objects.emplace(InteractiveObject{
                object.id, object.position, object.enabled, object.updates, {}});
            value.objects.push_back({key.raw});
        }
        // Parents precede descendants, so every persistent owner is already live.
        for (const SavedItem& item : saved.items) {
            ItemLocation location;
            switch (item.location) {
            case LocationKind::ground: location = ItemLocation::ground(item.position); break;
            case LocationKind::actor: location = ItemLocation::carried(find_actor(item.owner)); break;
            case LocationKind::item: location = ItemLocation::inside(find_item(item.owner)); break;
            case LocationKind::object: location = ItemLocation::stored(find_object(item.owner)); break;
            }
            require(destination(location).has_value());
            const auto key = items.emplace(Item{item.id, item.kind, location, {}});
            collection(location).push_back({key.raw});
        }
    }
};

World::World(std::uint32_t seed) : impl_(eastl::make_unique<Impl>(seed)) {}
World::~World() = default;

WorldResult<LoadRequest> World::request_chunks(ChunkRegion region, LoadMode mode) {
    if (!valid_region(region)) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    if (!valid_mode(mode)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    return LoadRequest{impl_->requests.emplace(Request{region, mode}).raw};
}

WorldResult<void> World::update_request(LoadRequest handle, ChunkRegion region, LoadMode mode) {
    Request* request = live(impl_->requests, handle);
    if (!request) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    if (!valid_region(region)) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    if (!valid_mode(mode)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    *request = {region, mode};
    return {};
}

WorldResult<void> World::release_request(LoadRequest handle) {
    if (!live(impl_->requests, handle)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    impl_->requests.erase(dod::slot_map64<Request>::key{handle.value});
    return {};
}

void World::reconcile_chunks() {
    Vector<Demand> demand;
    for (const Request& request : impl_->requests) {
        const ChunkRegion region = request.region;
        for (int x = region.center.x - region.radius; x <= region.center.x + region.radius; ++x) {
            for (int z = region.center.z - region.radius; z <= region.center.z + region.radius; ++z) {
                demand.push_back({{x, z}, request.mode == LoadMode::active});
            }
        }
    }
    eastl::sort(demand.begin(), demand.end(), [](const Demand& a, const Demand& b) {
        return chunk_less(a.position, b.position);
    });
    std::size_t count = 0;
    for (const Demand entry : demand) {
        if (count != 0 && demand[count - 1].position == entry.position) {
            demand[count - 1].active = demand[count - 1].active || entry.active;
        } else {
            demand[count++] = entry;
        }
    }
    demand.resize(count);
    for (auto current = impl_->chunks.begin(); current != impl_->chunks.end();) {
        const auto wanted = eastl::lower_bound(demand.begin(), demand.end(), current->position,
            [](const Demand& value, ChunkCoord key) { return chunk_less(value.position, key); });
        if (wanted == demand.end() || wanted->position != current->position) {
            current->active = false;
            impl_->unload(*current);
            current = impl_->chunks.erase(current);
        } else {
            current->active = wanted->active;
            ++current;
        }
    }
    for (const Demand entry : demand) {
        if (impl_->chunk(entry.position)) {
            continue;
        }
        Chunk value;
        value.position = entry.position;
        value.active = entry.active;
        const auto where = eastl::lower_bound(impl_->chunks.begin(), impl_->chunks.end(), entry.position,
            [](const Chunk& chunk, ChunkCoord key) { return chunk_less(chunk.position, key); });
        const auto inserted = impl_->chunks.insert(where, eastl::move(value));
        const auto saved = eastl::find_if(impl_->archives.begin(), impl_->archives.end(),
            [entry](const Archive& archive) { return archive.position == entry.position; });
        if (saved == impl_->archives.end()) {
            impl_->generate(*inserted);
        } else {
            impl_->restore(*inserted, *saved);
            impl_->archives.erase(saved);
        }
    }
}

const Chunk* World::chunk(ChunkCoord position) const { return impl_->chunk(position); }

const Tile* World::tile(TileCoord position) const {
    if (!valid_position(position)) {
        return nullptr;
    }
    const Chunk* value = chunk(chunk_of(position));
    return value ? &value->tiles[tile_index(position)] : nullptr;
}

bool World::active(TileCoord position) const {
    const Chunk* value = valid_position(position) ? chunk(chunk_of(position)) : nullptr;
    return value && value->active;
}

Vector<ChunkCoord> World::loaded_chunks() const {
    Vector<ChunkCoord> result;
    for (const Chunk& value : impl_->chunks) {
        result.push_back(value.position);
    }
    return result;
}

std::size_t World::archived_chunk_count() const { return impl_->archives.size(); }
const Actor* World::actor(ActorHandle handle) const { return live(impl_->actors, handle); }
const Item* World::item(ItemHandle handle) const { return live(impl_->items, handle); }
const InteractiveObject* World::object(ObjectHandle handle) const { return live(impl_->objects, handle); }
ActorHandle World::find_actor(PersistentId id) const { return impl_->find_actor(id); }
ItemHandle World::find_item(PersistentId id) const { return impl_->find_item(id); }
ObjectHandle World::find_object(PersistentId id) const { return impl_->find_object(id); }

ActorHandle World::actor_at(TileCoord position) const {
    const Chunk* value = valid_position(position) ? chunk(chunk_of(position)) : nullptr;
    if (value) {
        for (ActorHandle handle : value->actors) {
            const Actor* candidate = actor(handle);
            require(candidate != nullptr);
            if (candidate->position == position) {
                return handle;
            }
        }
    }
    return {};
}

Vector<ItemHandle> World::items_at(TileCoord position) const {
    Vector<ItemHandle> result;
    const Chunk* value = valid_position(position) ? chunk(chunk_of(position)) : nullptr;
    if (value) {
        for (ItemHandle handle : value->ground_items) {
            const Item* candidate = item(handle);
            require(candidate != nullptr);
            if (candidate->location.position == position) {
                result.push_back(handle);
            }
        }
    }
    eastl::sort(result.begin(), result.end(), [this](ItemHandle a, ItemHandle b) {
        return item(a)->id < item(b)->id;
    });
    return result;
}

ObjectHandle World::object_at(TileCoord position) const {
    const Chunk* value = valid_position(position) ? chunk(chunk_of(position)) : nullptr;
    if (value) {
        for (ObjectHandle handle : value->objects) {
            const InteractiveObject* candidate = object(handle);
            require(candidate != nullptr);
            if (candidate->position == position) {
                return handle;
            }
        }
    }
    return {};
}

WorldResult<void> World::set_tile(TileCoord position, Tile value) {
    if (auto result = impl_->available(position); !result) {
        return result;
    }
    impl_->chunk(chunk_of(position))->tiles[tile_index(position)] = value;
    return {};
}

WorldResult<ActorHandle> World::create_actor(TileCoord position, bool player) {
    if (auto result = impl_->available(position); !result) {
        return eastl::unexpected(result.error());
    }
    if (!passable(tile(position)->terrain)) {
        return eastl::unexpected(WorldError::blocked);
    }
    if (actor_at(position).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    const auto key = impl_->actors.emplace(Actor{impl_->issue_id(), position, player, 0, {}});
    const ActorHandle handle{key.raw};
    impl_->chunk(chunk_of(position))->actors.push_back(handle);
    return handle;
}

WorldResult<void> World::move_actor(ActorHandle handle, TileCoord destination) {
    Actor* value = live(impl_->actors, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    if (auto result = impl_->available(destination); !result) {
        return result;
    }
    if (value->position == destination) {
        return {};
    }
    if (!passable(tile(destination)->terrain)) {
        return eastl::unexpected(WorldError::blocked);
    }
    if (actor_at(destination).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    const ChunkCoord previous = chunk_of(value->position);
    const ChunkCoord next = chunk_of(destination);
    if (previous != next) {
        erase_one(impl_->chunk(previous)->actors, handle);
        impl_->chunk(next)->actors.push_back(handle);
    }
    value->position = destination;
    return {};
}

WorldResult<void> World::destroy_actor(ActorHandle handle) {
    Actor* value = live(impl_->actors, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const TileCoord position = value->position;
    while (!value->inventory.empty()) {
        require(transfer_item(value->inventory.back(), ItemLocation::ground(position)).has_value());
        value = live(impl_->actors, handle);
        require(value != nullptr);
    }
    erase_one(impl_->chunk(chunk_of(position))->actors, handle);
    impl_->actors.erase(dod::slot_map64<Actor>::key{handle.value});
    return {};
}

WorldResult<ItemHandle> World::create_item(ItemKind kind, ItemLocation location) {
    const auto destination = impl_->destination(location);
    if (!destination) {
        return eastl::unexpected(destination.error());
    }
    require(kind == ItemKind::rock || kind == ItemKind::backpack || kind == ItemKind::bandage);
    const auto key = impl_->items.emplace(Item{impl_->issue_id(), kind, *destination, {}});
    const ItemHandle handle{key.raw};
    impl_->collection(*destination).push_back(handle);
    return handle;
}

WorldResult<void> World::transfer_item(ItemHandle handle, ItemLocation location) {
    Item* value = live(impl_->items, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const auto destination = impl_->destination(location, handle);
    if (!destination) {
        return eastl::unexpected(destination.error());
    }
    if (value->location == *destination) {
        return {};
    }
    erase_one(impl_->collection(value->location), handle);
    impl_->collection(*destination).push_back(handle);
    value->location = *destination;
    return {};
}

WorldResult<void> World::destroy_item(ItemHandle handle) {
    const Item* value = item(handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    erase_one(impl_->collection(value->location), handle);
    Vector<ItemHandle> pending{handle};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const Item* current = item(pending[index]);
        require(current != nullptr);
        for (ItemHandle child : current->contents) {
            pending.push_back(child);
        }
        require(pending.size() <= impl_->items.size());
    }
    for (ItemHandle current : pending) {
        impl_->items.erase(dod::slot_map64<Item>::key{current.value});
    }
    return {};
}

WorldResult<ObjectHandle> World::create_object(TileCoord position, bool enabled) {
    if (auto result = impl_->available(position); !result) {
        return eastl::unexpected(result.error());
    }
    if (object_at(position).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    const auto key = impl_->objects.emplace(InteractiveObject{impl_->issue_id(), position, enabled, 0, {}});
    const ObjectHandle handle{key.raw};
    impl_->chunk(chunk_of(position))->objects.push_back(handle);
    return handle;
}

WorldResult<void> World::destroy_object(ObjectHandle handle) {
    InteractiveObject* value = live(impl_->objects, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const TileCoord position = value->position;
    while (!value->contents.empty()) {
        require(transfer_item(value->contents.back(), ItemLocation::ground(position)).has_value());
        value = live(impl_->objects, handle);
        require(value != nullptr);
    }
    erase_one(impl_->chunk(chunk_of(position))->objects, handle);
    impl_->objects.erase(dod::slot_map64<InteractiveObject>::key{handle.value});
    return {};
}

WorldResult<void> World::set_object_enabled(ObjectHandle handle, bool enabled) {
    InteractiveObject* value = live(impl_->objects, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    value->enabled = enabled;
    return {};
}

Vector<ActorHandle> World::active_actors() const {
    Vector<ActorHandle> result;
    for (const Chunk& value : impl_->chunks) {
        if (value.active) {
            for (ActorHandle handle : value.actors) {
                result.push_back(handle);
            }
        }
    }
    eastl::sort(result.begin(), result.end(), [this](ActorHandle a, ActorHandle b) {
        return actor(a)->id < actor(b)->id;
    });
    return result;
}

Vector<ObjectHandle> World::active_objects() const {
    Vector<ObjectHandle> result;
    for (const Chunk& value : impl_->chunks) {
        if (value.active) {
            for (ObjectHandle handle : value.objects) {
                result.push_back(handle);
            }
        }
    }
    eastl::sort(result.begin(), result.end(), [this](ObjectHandle a, ObjectHandle b) {
        return object(a)->id < object(b)->id;
    });
    return result;
}

WorldResult<void> World::record_actor_turn(ActorHandle handle) {
    Actor* value = live(impl_->actors, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    if (!active(value->position)) {
        return eastl::unexpected(WorldError::chunk_unavailable);
    }
    require(value->turns != eastl::numeric_limits<std::uint64_t>::max());
    ++value->turns;
    return {};
}

WorldResult<void> World::record_object_update(ObjectHandle handle) {
    InteractiveObject* value = live(impl_->objects, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    if (!active(value->position)) {
        return eastl::unexpected(WorldError::chunk_unavailable);
    }
    if (value->enabled) {
        require(value->updates != eastl::numeric_limits<std::uint64_t>::max());
        ++value->updates;
    }
    return {};
}

bool World::check_invariants() const {
    const Impl& state = *impl_;
    Vector<PersistentId> ids;
    Vector<ActorHandle> indexed_actors;
    Vector<ObjectHandle> indexed_objects;
    Vector<ItemHandle> owned_items;
    const auto add_id = [&ids, &state](PersistentId id) {
        if (id == 0 || id >= state.next_id) {
            return false;
        }
        ids.push_back(id);
        return true;
    };
    for (std::size_t index = 0; index < state.chunks.size(); ++index) {
        const Chunk& value = state.chunks[index];
        if (!valid_region({value.position, 0})
            || (index != 0 && !chunk_less(state.chunks[index - 1].position, value.position))) {
            return false;
        }
        Vector<TileCoord> actor_positions;
        Vector<TileCoord> object_positions;
        for (ActorHandle handle : value.actors) {
            const Actor* actor = this->actor(handle);
            if (!actor || !valid_position(actor->position) || chunk_of(actor->position) != value.position
                || occurrences(actor_positions, actor->position) != 0) {
                return false;
            }
            actor_positions.push_back(actor->position);
            indexed_actors.push_back(handle);
        }
        for (ObjectHandle handle : value.objects) {
            const InteractiveObject* object = this->object(handle);
            if (!object || !valid_position(object->position) || chunk_of(object->position) != value.position
                || occurrences(object_positions, object->position) != 0) {
                return false;
            }
            object_positions.push_back(object->position);
            indexed_objects.push_back(handle);
        }
        for (ItemHandle handle : value.ground_items) {
            const Item* item = this->item(handle);
            if (!item || item->location.kind != LocationKind::ground
                || !valid_position(item->location.position)
                || chunk_of(item->location.position) != value.position) {
                return false;
            }
            owned_items.push_back(handle);
        }
    }
    for (const auto& entry : state.actors.items()) {
        const Actor& value = entry.second.get();
        const ActorHandle handle{entry.first.raw};
        if (!add_id(value.id) || occurrences(indexed_actors, handle) != 1) {
            return false;
        }
        for (ItemHandle child : value.inventory) {
            const Item* item = this->item(child);
            if (!item || item->location != ItemLocation::carried(handle)) {
                return false;
            }
            owned_items.push_back(child);
        }
    }
    for (const auto& entry : state.objects.items()) {
        const InteractiveObject& value = entry.second.get();
        const ObjectHandle handle{entry.first.raw};
        if (!add_id(value.id) || occurrences(indexed_objects, handle) != 1) {
            return false;
        }
        for (ItemHandle child : value.contents) {
            const Item* item = this->item(child);
            if (!item || item->location != ItemLocation::stored(handle)) {
                return false;
            }
            owned_items.push_back(child);
        }
    }
    for (const auto& entry : state.items.items()) {
        const Item& value = entry.second.get();
        const ItemHandle handle{entry.first.raw};
        if (!add_id(value.id) || (value.kind != ItemKind::backpack && !value.contents.empty())) {
            return false;
        }
        for (ItemHandle child : value.contents) {
            const Item* item = this->item(child);
            if (!item || item->location != ItemLocation::inside(handle)) {
                return false;
            }
            owned_items.push_back(child);
        }
        // Walk to a valid spatial root, bounding the walk to detect cycles.
        const Item* cursor = &value;
        std::size_t depth = 0;
        while (cursor->location.kind == LocationKind::item) {
            if (++depth > state.items.size()) {
                return false;
            }
            cursor = item(cursor->location.item);
            if (!cursor || cursor->kind != ItemKind::backpack) {
                return false;
            }
        }
        const auto location = state.destination(cursor->location);
        if (!location || *location != cursor->location) {
            return false;
        }
    }
    if (indexed_actors.size() != state.actors.size() || indexed_objects.size() != state.objects.size()
        || owned_items.size() != state.items.size()) {
        return false;
    }
    for (const auto& entry : state.items.items()) {
        if (occurrences(owned_items, ItemHandle{entry.first.raw}) != 1) {
            return false;
        }
    }
    Vector<ChunkCoord> archived_positions;
    for (const Archive& saved : state.archives) {
        if (!valid_region({saved.position, 0}) || state.chunk(saved.position)
            || occurrences(archived_positions, saved.position) != 0) {
            return false;
        }
        archived_positions.push_back(saved.position);
        Vector<TileCoord> actor_positions;
        Vector<TileCoord> object_positions;
        for (const SavedActor& actor : saved.actors) {
            if (!add_id(actor.id) || !valid_position(actor.position)
                || chunk_of(actor.position) != saved.position
                || occurrences(actor_positions, actor.position) != 0) {
                return false;
            }
            actor_positions.push_back(actor.position);
        }
        for (const SavedObject& object : saved.objects) {
            if (!add_id(object.id) || !valid_position(object.position)
                || chunk_of(object.position) != saved.position
                || occurrences(object_positions, object.position) != 0) {
                return false;
            }
            object_positions.push_back(object.position);
        }
        for (std::size_t index = 0; index < saved.items.size(); ++index) {
            const SavedItem& value = saved.items[index];
            if (!add_id(value.id)) {
                return false;
            }
            switch (value.location) {
            case LocationKind::ground:
                if (value.owner != 0 || !valid_position(value.position)
                    || chunk_of(value.position) != saved.position) {
                    return false;
                }
                break;
            case LocationKind::actor:
                if (eastl::none_of(saved.actors.begin(), saved.actors.end(),
                        [&value](const SavedActor& owner) { return owner.id == value.owner; })) {
                    return false;
                }
                break;
            case LocationKind::object:
                if (eastl::none_of(saved.objects.begin(), saved.objects.end(),
                        [&value](const SavedObject& owner) { return owner.id == value.owner; })) {
                    return false;
                }
                break;
            case LocationKind::item:
                // Parents must precede children. This also excludes cycles and
                // guarantees every archived tree terminates at a spatial root.
                if (eastl::none_of(saved.items.begin(), saved.items.begin() + index,
                        [&value](const SavedItem& owner) {
                            return owner.id == value.owner && owner.kind == ItemKind::backpack;
                        })) {
                    return false;
                }
                break;
            default: return false;
            }
        }
    }
    eastl::sort(ids.begin(), ids.end());
    return eastl::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

}
