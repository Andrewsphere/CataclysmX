#pragma once

#include "core/containers.h"

#include <EASTL/array.h>
#include <EASTL/expected.h>
#include <EASTL/unique_ptr.h>

#include <cstdint>

namespace cataclysmx {

// Handles are weak, typed, and scoped to the World that issued them.
template<class Tag>
struct Handle {
    std::uint64_t value = 0;
    bool operator==(const Handle&) const = default;
};
using ActorHandle = Handle<struct ActorTag>;
using ItemHandle = Handle<struct ItemTag>;
using ObjectHandle = Handle<struct ObjectTag>;
using LoadRequest = Handle<struct LoadRequestTag>;
using PersistentId = std::uint64_t;

inline constexpr int chunk_size = 16;
inline constexpr int world_coordinate_limit = 1'000'000;

struct TileCoord {
    int x = 0;
    int z = 0;
    bool operator==(const TileCoord&) const = default;
};
struct ChunkCoord {
    int x = 0;
    int z = 0;
    bool operator==(const ChunkCoord&) const = default;
};
ChunkCoord chunk_of(TileCoord position);
int tile_index(TileCoord position);
bool valid_position(TileCoord position);

enum class Terrain : std::uint8_t { floor, wall, closed_door, open_door };
struct Tile {
    Terrain terrain = Terrain::floor;
    std::uint8_t damage = 0;
    bool operator==(const Tile&) const = default;
};
bool passable(Terrain terrain);

enum class ItemKind : std::uint8_t { rock, backpack, bandage };
const char* item_name(ItemKind kind);
enum class LocationKind : std::uint8_t { ground, actor, item, object };
struct ItemLocation {
    LocationKind kind = LocationKind::ground;
    TileCoord position{};
    ActorHandle actor{};
    ItemHandle item{};
    ObjectHandle object{};

    static ItemLocation ground(TileCoord position);
    static ItemLocation carried(ActorHandle actor);
    static ItemLocation inside(ItemHandle item);
    static ItemLocation stored(ObjectHandle object);
    bool operator==(const ItemLocation&) const = default;
};

struct Actor {
    PersistentId id = 0;
    TileCoord position{};
    bool player = false;
    std::uint64_t turns = 0;
    Vector<ItemHandle> inventory;
};
struct Item {
    PersistentId id = 0;
    ItemKind kind = ItemKind::rock;
    ItemLocation location{};
    Vector<ItemHandle> contents;
};
struct InteractiveObject {
    PersistentId id = 0;
    TileCoord position{};
    bool enabled = false;
    std::uint64_t updates = 0;
    Vector<ItemHandle> contents;
};
struct Chunk {
    ChunkCoord position{};
    bool active = false;
    eastl::array<Tile, chunk_size * chunk_size> tiles{};
    Vector<ActorHandle> actors;
    Vector<ItemHandle> ground_items;
    Vector<ObjectHandle> objects;
};

enum class WorldError {
    invalid_position,
    invalid_region,
    invalid_handle,
    chunk_unavailable,
    blocked,
    occupied,
    containment_cycle,
    not_container,
    invalid_request
};
template<class T>
using WorldResult = eastl::expected<T, WorldError>;

enum class LoadMode { resident, active };
struct ChunkRegion {
    ChunkCoord center{};
    int radius = 0;
};

// All mutations go through World. Returned pointers are read-only borrows:
// reacquire after mutation, and never retain them across unloading/deletion.
class World {
public:
    explicit World(std::uint32_t seed = 42);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    WorldResult<LoadRequest> request_chunks(ChunkRegion region, LoadMode mode);
    WorldResult<void> update_request(LoadRequest request, ChunkRegion region, LoadMode mode);
    WorldResult<void> release_request(LoadRequest request);
    // Synchronous boundary operation. Overlapping requests combine; active wins.
    void reconcile_chunks();

    const Chunk* chunk(ChunkCoord position) const;
    const Tile* tile(TileCoord position) const;
    bool active(TileCoord position) const;
    Vector<ChunkCoord> loaded_chunks() const;
    std::size_t archived_chunk_count() const;

    const Actor* actor(ActorHandle handle) const;
    const Item* item(ItemHandle handle) const;
    const InteractiveObject* object(ObjectHandle handle) const;
    ActorHandle find_actor(PersistentId id) const;
    ItemHandle find_item(PersistentId id) const;
    ObjectHandle find_object(PersistentId id) const;
    ActorHandle actor_at(TileCoord position) const;
    Vector<ItemHandle> items_at(TileCoord position) const;
    ObjectHandle object_at(TileCoord position) const;

    WorldResult<void> set_tile(TileCoord position, Tile tile);
    WorldResult<ActorHandle> create_actor(TileCoord position, bool player = false);
    WorldResult<void> move_actor(ActorHandle actor, TileCoord destination);
    // Actor destruction drops possessions at its last position.
    WorldResult<void> destroy_actor(ActorHandle actor);
    WorldResult<ItemHandle> create_item(ItemKind kind, ItemLocation location);
    WorldResult<void> transfer_item(ItemHandle item, ItemLocation destination);
    WorldResult<void> destroy_item(ItemHandle item);
    WorldResult<ObjectHandle> create_object(TileCoord position, bool enabled = false);
    // Object destruction drops its contents on the ground.
    WorldResult<void> destroy_object(ObjectHandle object);
    WorldResult<void> set_object_enabled(ObjectHandle object, bool enabled);

    // Snapshot lists ordered by persistent ID, independent of storage/load order.
    Vector<ActorHandle> active_actors() const;
    Vector<ObjectHandle> active_objects() const;
    WorldResult<void> record_actor_turn(ActorHandle actor);
    WorldResult<void> record_object_update(ObjectHandle object);

    // Useful for headless scenarios: validates ownership and spatial indexes.
    bool check_invariants() const;

private:
    struct Impl;
    eastl::unique_ptr<Impl> impl_;
};

}
