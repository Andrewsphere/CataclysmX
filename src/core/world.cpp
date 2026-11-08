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

void Require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

int FloorChunk(int coordinate) {
    const int quotient = coordinate / chunk_size;
    return quotient - (coordinate % chunk_size < 0 ? 1 : 0);
}

bool ChunkLess(ChunkCoord left, ChunkCoord right) {
    return left.x < right.x || (left.x == right.x && left.z < right.z);
}

bool ValidRegion(ChunkRegion region) {
    constexpr int minimum = -world_coordinate_limit / chunk_size;
    constexpr int maximum = world_coordinate_limit / chunk_size;
    return region.radius >= 0 && region.radius <= 8
        && region.center.x >= minimum + region.radius
        && region.center.x <= maximum - region.radius
        && region.center.z >= minimum + region.radius
        && region.center.z <= maximum - region.radius;
}

bool ValidMode(LoadMode mode) {
    return mode == LoadMode::resident || mode == LoadMode::active;
}

template<class T>
void EraseOne(Vector<T>& values, T value) {
    const auto found = eastl::find(values.begin(), values.end(), value);
    Require(found != values.end());
    values.erase(found);
}

template<class T>
std::size_t Occurrences(const Vector<T>& values, T value) {
    return static_cast<std::size_t>(eastl::count(values.begin(), values.end(), value));
}

// SlotMap::get assumes a live slot when a version happens to match. In
// particular, never pass unchecked/fabricated external handles straight to get.
template<class T, class Tag>
T* Live(dod::slot_map64<T>& slots, Handle<Tag> handle) {
    const typename dod::slot_map64<T>::key key{handle.value};
    return key.get_tag() == 0 && slots.contains(key) ? slots.get(key) : nullptr;
}

template<class T, class Tag>
const T* Live(const dod::slot_map64<T>& slots, Handle<Tag> handle) {
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

ChunkCoord ChunkOf(TileCoord position) {
    return {FloorChunk(position.x), FloorChunk(position.z)};
}

int TileIndex(TileCoord position) {
    const int x = (position.x % chunk_size + chunk_size) % chunk_size;
    const int z = (position.z % chunk_size + chunk_size) % chunk_size;
    return z * chunk_size + x;
}

bool ValidPosition(TileCoord position) {
    return position.x >= -world_coordinate_limit && position.x <= world_coordinate_limit
        && position.z >= -world_coordinate_limit && position.z <= world_coordinate_limit;
}

bool Passable(Terrain terrain) {
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

ItemLocation ItemLocation::Ground(TileCoord position) {
    ItemLocation result;
    result.position = position;
    return result;
}

ItemLocation ItemLocation::Carried(ActorHandle actor) {
    ItemLocation result;
    result.kind = LocationKind::actor;
    result.actor = actor;
    return result;
}

ItemLocation ItemLocation::Inside(ItemHandle item) {
    ItemLocation result;
    result.kind = LocationKind::item;
    result.item = item;
    return result;
}

ItemLocation ItemLocation::Stored(ObjectHandle object) {
    ItemLocation result;
    result.kind = LocationKind::object;
    result.object = object;
    return result;
}

class World::Impl {
public:
    std::uint32_t seed;
    PersistentId next_id = 1;
    dod::slot_map64<Actor> actors;
    dod::slot_map64<Item> items;
    dod::slot_map64<InteractiveObject> objects;
    dod::slot_map64<Request> requests;
    Vector<Chunk> chunks; // Always lexicographic (x, z).
    Vector<Archive> archives; // Only currently unloaded chunks.

    explicit Impl(std::uint32_t value) : seed(value) {}

    PersistentId IssueId() {
        Require(next_id != eastl::numeric_limits<PersistentId>::max());
        return next_id++;
    }

    Chunk* GetChunk(ChunkCoord position) {
        const auto found = eastl::lower_bound(chunks.begin(), chunks.end(), position,
            [](const Chunk& value, ChunkCoord key) { return ChunkLess(value.position, key); });
        return found != chunks.end() && found->position == position ? &*found : nullptr;
    }

    const Chunk* GetChunk(ChunkCoord position) const {
        const auto found = eastl::lower_bound(chunks.begin(), chunks.end(), position,
            [](const Chunk& value, ChunkCoord key) { return ChunkLess(value.position, key); });
        return found != chunks.end() && found->position == position ? &*found : nullptr;
    }

    WorldResult<void> Available(TileCoord position) const {
        if (!ValidPosition(position)) {
            return eastl::unexpected(WorldError::invalid_position);
        }
        if (!GetChunk(ChunkOf(position))) {
            return eastl::unexpected(WorldError::chunk_unavailable);
        }
        return {};
    }

    ActorHandle FindActor(PersistentId id) const {
        for (const auto& entry : actors.items()) {
            if (entry.second.get().id == id) {
                return {entry.first.raw};
            }
        }
        return {};
    }

    ItemHandle FindItem(PersistentId id) const {
        for (const auto& entry : items.items()) {
            if (entry.second.get().id == id) {
                return {entry.first.raw};
            }
        }
        return {};
    }

    ObjectHandle FindObject(PersistentId id) const {
        for (const auto& entry : objects.items()) {
            if (entry.second.get().id == id) {
                return {entry.first.raw};
            }
        }
        return {};
    }

    WorldResult<ItemLocation> Destination(ItemLocation location, ItemHandle moving = {}) const {
        switch (location.kind) {
        case LocationKind::ground:
            if (auto result = Available(location.position); !result) {
                return eastl::unexpected(result.error());
            }
            return ItemLocation::Ground(location.position);
        case LocationKind::actor:
            if (!Live(actors, location.actor)) {
                return eastl::unexpected(WorldError::invalid_handle);
            }
            return ItemLocation::Carried(location.actor);
        case LocationKind::object:
            if (!Live(objects, location.object)) {
                return eastl::unexpected(WorldError::invalid_handle);
            }
            return ItemLocation::Stored(location.object);
        case LocationKind::item: {
            const Item* parent = Live(items, location.item);
            if (!parent) {
                return eastl::unexpected(WorldError::invalid_handle);
            }
            if (parent->kind != ItemKind::backpack) {
                return eastl::unexpected(WorldError::not_container);
            }
            ItemHandle cursor = location.item;
            for (std::size_t depth = 0; ; ++depth) {
                Require(depth <= items.size());
                if (moving.value != 0 && cursor == moving) {
                    return eastl::unexpected(WorldError::containment_cycle);
                }
                const Item* ancestor = Live(items, cursor);
                Require(ancestor != nullptr);
                if (ancestor->location.kind != LocationKind::item) {
                    break;
                }
                cursor = ancestor->location.item;
            }
            return ItemLocation::Inside(location.item);
        }
        }
        return eastl::unexpected(WorldError::invalid_handle);
    }

    Vector<ItemHandle>& Collection(ItemLocation location) {
        switch (location.kind) {
        case LocationKind::ground: {
            Chunk* owner = GetChunk(ChunkOf(location.position));
            Require(owner != nullptr);
            return owner->ground_items;
        }
        case LocationKind::actor: {
            Actor* owner = Live(actors, location.actor);
            Require(owner != nullptr);
            return owner->inventory;
        }
        case LocationKind::item: {
            Item* owner = Live(items, location.item);
            Require(owner != nullptr);
            return owner->contents;
        }
        case LocationKind::object: {
            InteractiveObject* owner = Live(objects, location.object);
            Require(owner != nullptr);
            return owner->contents;
        }
        }
        std::abort();
    }

    void Generate(Chunk& value) const {
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

    void Unload(const Chunk& value) {
        Archive saved{value.position, value.tiles, {}, {}, {}};
        Vector<ItemHandle> pending = value.ground_items;
        for (ActorHandle handle : value.actors) {
            const Actor* actor = Live(actors, handle);
            Require(actor != nullptr);
            saved.actors.push_back({actor->id, actor->position, actor->player, actor->turns});
            for (ItemHandle item : actor->inventory) {
                pending.push_back(item);
            }
        }
        for (ObjectHandle handle : value.objects) {
            const InteractiveObject* object = Live(objects, handle);
            Require(object != nullptr);
            saved.objects.push_back({object->id, object->position, object->enabled, object->updates});
            for (ItemHandle item : object->contents) {
                pending.push_back(item);
            }
        }
        for (std::size_t index = 0; index < pending.size(); ++index) {
            Require(pending.size() <= items.size());
            const Item* item = Live(items, pending[index]);
            Require(item != nullptr);
            PersistentId owner = 0;
            switch (item->location.kind) {
            case LocationKind::ground: break;
            case LocationKind::actor: owner = Live(actors, item->location.actor)->id; break;
            case LocationKind::item: owner = Live(items, item->location.item)->id; break;
            case LocationKind::object: owner = Live(objects, item->location.object)->id; break;
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

    void Restore(Chunk& value, const Archive& saved) {
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
            case LocationKind::ground: location = ItemLocation::Ground(item.position); break;
            case LocationKind::actor: location = ItemLocation::Carried(FindActor(item.owner)); break;
            case LocationKind::item: location = ItemLocation::Inside(FindItem(item.owner)); break;
            case LocationKind::object: location = ItemLocation::Stored(FindObject(item.owner)); break;
            }
            Require(Destination(location).has_value());
            const auto key = items.emplace(Item{item.id, item.kind, location, {}});
            Collection(location).push_back({key.raw});
        }
    }
};

World::World(std::uint32_t seed) : impl_(eastl::make_unique<Impl>(seed)) {}
World::~World() = default;

WorldResult<LoadRequest> World::RequestChunks(ChunkRegion region, LoadMode mode) {
    if (!ValidRegion(region)) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    if (!ValidMode(mode)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    return LoadRequest{impl_->requests.emplace(Request{region, mode}).raw};
}

WorldResult<void> World::UpdateRequest(LoadRequest handle, ChunkRegion region, LoadMode mode) {
    Request* request = Live(impl_->requests, handle);
    if (!request) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    if (!ValidRegion(region)) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    if (!ValidMode(mode)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    *request = {region, mode};
    return {};
}

WorldResult<void> World::ReleaseRequest(LoadRequest handle) {
    if (!Live(impl_->requests, handle)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    impl_->requests.erase(dod::slot_map64<Request>::key{handle.value});
    return {};
}

void World::ReconcileChunks() {
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
        return ChunkLess(a.position, b.position);
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
            [](const Demand& value, ChunkCoord key) { return ChunkLess(value.position, key); });
        if (wanted == demand.end() || wanted->position != current->position) {
            current->active = false;
            impl_->Unload(*current);
            current = impl_->chunks.erase(current);
        } else {
            current->active = wanted->active;
            ++current;
        }
    }
    for (const Demand entry : demand) {
        if (impl_->GetChunk(entry.position)) {
            continue;
        }
        Chunk value;
        value.position = entry.position;
        value.active = entry.active;
        const auto where = eastl::lower_bound(impl_->chunks.begin(), impl_->chunks.end(), entry.position,
            [](const Chunk& chunk, ChunkCoord key) { return ChunkLess(chunk.position, key); });
        const auto inserted = impl_->chunks.insert(where, eastl::move(value));
        const auto saved = eastl::find_if(impl_->archives.begin(), impl_->archives.end(),
            [entry](const Archive& archive) { return archive.position == entry.position; });
        if (saved == impl_->archives.end()) {
            impl_->Generate(*inserted);
        } else {
            impl_->Restore(*inserted, *saved);
            impl_->archives.erase(saved);
        }
    }
}

const Chunk* World::GetChunk(ChunkCoord position) const { return impl_->GetChunk(position); }

const Tile* World::GetTile(TileCoord position) const {
    if (!ValidPosition(position)) {
        return nullptr;
    }
    const Chunk* value = GetChunk(ChunkOf(position));
    return value ? &value->tiles[TileIndex(position)] : nullptr;
}

bool World::IsActive(TileCoord position) const {
    const Chunk* value = ValidPosition(position) ? GetChunk(ChunkOf(position)) : nullptr;
    return value && value->active;
}

Vector<ChunkCoord> World::LoadedChunks() const {
    Vector<ChunkCoord> result;
    for (const Chunk& value : impl_->chunks) {
        result.push_back(value.position);
    }
    return result;
}

std::size_t World::ArchivedChunkCount() const { return impl_->archives.size(); }
const Actor* World::GetActor(ActorHandle handle) const { return Live(impl_->actors, handle); }
const Item* World::GetItem(ItemHandle handle) const { return Live(impl_->items, handle); }
const InteractiveObject* World::GetObject(ObjectHandle handle) const { return Live(impl_->objects, handle); }
ActorHandle World::FindActor(PersistentId id) const { return impl_->FindActor(id); }
ItemHandle World::FindItem(PersistentId id) const { return impl_->FindItem(id); }
ObjectHandle World::FindObject(PersistentId id) const { return impl_->FindObject(id); }

ActorHandle World::ActorAt(TileCoord position) const {
    const Chunk* value = ValidPosition(position) ? GetChunk(ChunkOf(position)) : nullptr;
    if (value) {
        for (ActorHandle handle : value->actors) {
            const Actor* candidate = GetActor(handle);
            Require(candidate != nullptr);
            if (candidate->position == position) {
                return handle;
            }
        }
    }
    return {};
}

Vector<ItemHandle> World::ItemsAt(TileCoord position) const {
    Vector<ItemHandle> result;
    const Chunk* value = ValidPosition(position) ? GetChunk(ChunkOf(position)) : nullptr;
    if (value) {
        for (ItemHandle handle : value->ground_items) {
            const Item* candidate = GetItem(handle);
            Require(candidate != nullptr);
            if (candidate->location.position == position) {
                result.push_back(handle);
            }
        }
    }
    eastl::sort(result.begin(), result.end(), [this](ItemHandle a, ItemHandle b) {
        return GetItem(a)->id < GetItem(b)->id;
    });
    return result;
}

ObjectHandle World::ObjectAt(TileCoord position) const {
    const Chunk* value = ValidPosition(position) ? GetChunk(ChunkOf(position)) : nullptr;
    if (value) {
        for (ObjectHandle handle : value->objects) {
            const InteractiveObject* candidate = GetObject(handle);
            Require(candidate != nullptr);
            if (candidate->position == position) {
                return handle;
            }
        }
    }
    return {};
}

WorldResult<void> World::SetTile(TileCoord position, Tile value) {
    if (auto result = impl_->Available(position); !result) {
        return result;
    }
    impl_->GetChunk(ChunkOf(position))->tiles[TileIndex(position)] = value;
    return {};
}

WorldResult<ActorHandle> World::CreateActor(TileCoord position, bool player) {
    if (auto result = impl_->Available(position); !result) {
        return eastl::unexpected(result.error());
    }
    if (!Passable(GetTile(position)->terrain)) {
        return eastl::unexpected(WorldError::blocked);
    }
    if (ActorAt(position).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    const auto key = impl_->actors.emplace(Actor{impl_->IssueId(), position, player, 0, {}});
    const ActorHandle handle{key.raw};
    impl_->GetChunk(ChunkOf(position))->actors.push_back(handle);
    return handle;
}

WorldResult<void> World::MoveActor(ActorHandle handle, TileCoord destination) {
    Actor* value = Live(impl_->actors, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    if (auto result = impl_->Available(destination); !result) {
        return result;
    }
    if (value->position == destination) {
        return {};
    }
    if (!Passable(GetTile(destination)->terrain)) {
        return eastl::unexpected(WorldError::blocked);
    }
    if (ActorAt(destination).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    const ChunkCoord previous = ChunkOf(value->position);
    const ChunkCoord next = ChunkOf(destination);
    if (previous != next) {
        EraseOne(impl_->GetChunk(previous)->actors, handle);
        impl_->GetChunk(next)->actors.push_back(handle);
    }
    value->position = destination;
    return {};
}

WorldResult<void> World::DestroyActor(ActorHandle handle) {
    Actor* value = Live(impl_->actors, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const TileCoord position = value->position;
    while (!value->inventory.empty()) {
        Require(TransferItem(value->inventory.back(), ItemLocation::Ground(position)).has_value());
        value = Live(impl_->actors, handle);
        Require(value != nullptr);
    }
    EraseOne(impl_->GetChunk(ChunkOf(position))->actors, handle);
    impl_->actors.erase(dod::slot_map64<Actor>::key{handle.value});
    return {};
}

WorldResult<ItemHandle> World::CreateItem(ItemKind kind, ItemLocation location) {
    const auto destination = impl_->Destination(location);
    if (!destination) {
        return eastl::unexpected(destination.error());
    }
    Require(kind == ItemKind::rock || kind == ItemKind::backpack || kind == ItemKind::bandage);
    const auto key = impl_->items.emplace(Item{impl_->IssueId(), kind, *destination, {}});
    const ItemHandle handle{key.raw};
    impl_->Collection(*destination).push_back(handle);
    return handle;
}

WorldResult<void> World::TransferItem(ItemHandle handle, ItemLocation location) {
    Item* value = Live(impl_->items, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const auto destination = impl_->Destination(location, handle);
    if (!destination) {
        return eastl::unexpected(destination.error());
    }
    if (value->location == *destination) {
        return {};
    }
    EraseOne(impl_->Collection(value->location), handle);
    impl_->Collection(*destination).push_back(handle);
    value->location = *destination;
    return {};
}

WorldResult<void> World::DestroyItem(ItemHandle handle) {
    const Item* value = GetItem(handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    EraseOne(impl_->Collection(value->location), handle);
    Vector<ItemHandle> pending{handle};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const Item* current = GetItem(pending[index]);
        Require(current != nullptr);
        for (ItemHandle child : current->contents) {
            pending.push_back(child);
        }
        Require(pending.size() <= impl_->items.size());
    }
    for (ItemHandle current : pending) {
        impl_->items.erase(dod::slot_map64<Item>::key{current.value});
    }
    return {};
}

WorldResult<ObjectHandle> World::CreateObject(TileCoord position, bool enabled) {
    if (auto result = impl_->Available(position); !result) {
        return eastl::unexpected(result.error());
    }
    if (ObjectAt(position).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    const auto key = impl_->objects.emplace(InteractiveObject{impl_->IssueId(), position, enabled, 0, {}});
    const ObjectHandle handle{key.raw};
    impl_->GetChunk(ChunkOf(position))->objects.push_back(handle);
    return handle;
}

WorldResult<void> World::DestroyObject(ObjectHandle handle) {
    InteractiveObject* value = Live(impl_->objects, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const TileCoord position = value->position;
    while (!value->contents.empty()) {
        Require(TransferItem(value->contents.back(), ItemLocation::Ground(position)).has_value());
        value = Live(impl_->objects, handle);
        Require(value != nullptr);
    }
    EraseOne(impl_->GetChunk(ChunkOf(position))->objects, handle);
    impl_->objects.erase(dod::slot_map64<InteractiveObject>::key{handle.value});
    return {};
}

WorldResult<void> World::SetObjectEnabled(ObjectHandle handle, bool enabled) {
    InteractiveObject* value = Live(impl_->objects, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    value->enabled = enabled;
    return {};
}

Vector<ActorHandle> World::ActiveActors() const {
    Vector<ActorHandle> result;
    for (const Chunk& value : impl_->chunks) {
        if (value.active) {
            for (ActorHandle handle : value.actors) {
                result.push_back(handle);
            }
        }
    }
    eastl::sort(result.begin(), result.end(), [this](ActorHandle a, ActorHandle b) {
        return GetActor(a)->id < GetActor(b)->id;
    });
    return result;
}

Vector<ObjectHandle> World::ActiveObjects() const {
    Vector<ObjectHandle> result;
    for (const Chunk& value : impl_->chunks) {
        if (value.active) {
            for (ObjectHandle handle : value.objects) {
                result.push_back(handle);
            }
        }
    }
    eastl::sort(result.begin(), result.end(), [this](ObjectHandle a, ObjectHandle b) {
        return GetObject(a)->id < GetObject(b)->id;
    });
    return result;
}

WorldResult<void> World::RecordActorTurn(ActorHandle handle) {
    Actor* value = Live(impl_->actors, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    if (!IsActive(value->position)) {
        return eastl::unexpected(WorldError::chunk_unavailable);
    }
    Require(value->turns != eastl::numeric_limits<std::uint64_t>::max());
    ++value->turns;
    return {};
}

WorldResult<void> World::RecordObjectUpdate(ObjectHandle handle) {
    InteractiveObject* value = Live(impl_->objects, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    if (!IsActive(value->position)) {
        return eastl::unexpected(WorldError::chunk_unavailable);
    }
    if (value->enabled) {
        Require(value->updates != eastl::numeric_limits<std::uint64_t>::max());
        ++value->updates;
    }
    return {};
}

bool World::CheckInvariants() const {
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
        if (!ValidRegion({value.position, 0})
            || (index != 0 && !ChunkLess(state.chunks[index - 1].position, value.position))) {
            return false;
        }
        Vector<TileCoord> actor_positions;
        Vector<TileCoord> object_positions;
        for (ActorHandle handle : value.actors) {
            const Actor* actor = this->GetActor(handle);
            if (!actor || !ValidPosition(actor->position) || ChunkOf(actor->position) != value.position
                || Occurrences(actor_positions, actor->position) != 0) {
                return false;
            }
            actor_positions.push_back(actor->position);
            indexed_actors.push_back(handle);
        }
        for (ObjectHandle handle : value.objects) {
            const InteractiveObject* object = this->GetObject(handle);
            if (!object || !ValidPosition(object->position) || ChunkOf(object->position) != value.position
                || Occurrences(object_positions, object->position) != 0) {
                return false;
            }
            object_positions.push_back(object->position);
            indexed_objects.push_back(handle);
        }
        for (ItemHandle handle : value.ground_items) {
            const Item* item = this->GetItem(handle);
            if (!item || item->location.kind != LocationKind::ground
                || !ValidPosition(item->location.position)
                || ChunkOf(item->location.position) != value.position) {
                return false;
            }
            owned_items.push_back(handle);
        }
    }
    for (const auto& entry : state.actors.items()) {
        const Actor& value = entry.second.get();
        const ActorHandle handle{entry.first.raw};
        if (!add_id(value.id) || Occurrences(indexed_actors, handle) != 1) {
            return false;
        }
        for (ItemHandle child : value.inventory) {
            const Item* item = this->GetItem(child);
            if (!item || item->location != ItemLocation::Carried(handle)) {
                return false;
            }
            owned_items.push_back(child);
        }
    }
    for (const auto& entry : state.objects.items()) {
        const InteractiveObject& value = entry.second.get();
        const ObjectHandle handle{entry.first.raw};
        if (!add_id(value.id) || Occurrences(indexed_objects, handle) != 1) {
            return false;
        }
        for (ItemHandle child : value.contents) {
            const Item* item = this->GetItem(child);
            if (!item || item->location != ItemLocation::Stored(handle)) {
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
            const Item* item = this->GetItem(child);
            if (!item || item->location != ItemLocation::Inside(handle)) {
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
            cursor = GetItem(cursor->location.item);
            if (!cursor || cursor->kind != ItemKind::backpack) {
                return false;
            }
        }
        const auto location = state.Destination(cursor->location);
        if (!location || *location != cursor->location) {
            return false;
        }
    }
    if (indexed_actors.size() != state.actors.size() || indexed_objects.size() != state.objects.size()
        || owned_items.size() != state.items.size()) {
        return false;
    }
    for (const auto& entry : state.items.items()) {
        if (Occurrences(owned_items, ItemHandle{entry.first.raw}) != 1) {
            return false;
        }
    }
    Vector<ChunkCoord> archived_positions;
    for (const Archive& saved : state.archives) {
        if (!ValidRegion({saved.position, 0}) || state.GetChunk(saved.position)
            || Occurrences(archived_positions, saved.position) != 0) {
            return false;
        }
        archived_positions.push_back(saved.position);
        Vector<TileCoord> actor_positions;
        Vector<TileCoord> object_positions;
        for (const SavedActor& actor : saved.actors) {
            if (!add_id(actor.id) || !ValidPosition(actor.position)
                || ChunkOf(actor.position) != saved.position
                || Occurrences(actor_positions, actor.position) != 0) {
                return false;
            }
            actor_positions.push_back(actor.position);
        }
        for (const SavedObject& object : saved.objects) {
            if (!add_id(object.id) || !ValidPosition(object.position)
                || ChunkOf(object.position) != saved.position
                || Occurrences(object_positions, object.position) != 0) {
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
                if (value.owner != 0 || !ValidPosition(value.position)
                    || ChunkOf(value.position) != saved.position) {
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
