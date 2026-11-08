#pragma once

#include "core/containers.h"
#include "content/content.h"
#include "core/save_result.h"

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

// Authoritative server state captured in the same transaction as chunk values.
struct SessionState {
    std::uint64_t tick = 0;
    std::uint32_t rngState = 0;
    PersistentId player = 0;
    TileCoord playerPosition{};
    int activeRadius = 1;
    int residentRadius = 2;
    std::uint64_t autosaveInterval = 0;
};
ChunkCoord ChunkOf(TileCoord position);
int TileIndex(TileCoord position);
bool ValidPosition(TileCoord position);

struct Tile {
    TerrainId terrain{};
    std::uint8_t damage = 0;
    bool operator==(const Tile&) const = default;
};
bool Passable(const ContentCatalog& content, TerrainId terrain);

enum class ItemKind : std::uint8_t { rock, backpack, bandage };
const char* item_name(ItemKind kind);
enum class LocationKind : std::uint8_t { ground, actor, item, object };
struct ItemLocation {
    LocationKind kind = LocationKind::ground;
    TileCoord position{};
    ActorHandle actor{};
    ItemHandle item{};
    ObjectHandle object{};

    static ItemLocation Ground(TileCoord position);
    static ItemLocation Carried(ActorHandle actor);
    static ItemLocation Inside(ItemHandle item);
    static ItemLocation Stored(ObjectHandle object);
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
    invalid_request,
    storage_failure
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
    // Content must outlive the World and remain unchanged. The current generator
    // requires definitions named floor and wall.
    explicit World(const ContentCatalog& content, std::uint32_t seed = 42);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    WorldResult<LoadRequest> RequestChunks(ChunkRegion region, LoadMode mode);
    WorldResult<void> UpdateRequest(LoadRequest request, ChunkRegion region, LoadMode mode);
    WorldResult<void> ReleaseRequest(LoadRequest request);
    // Synchronous boundary operation. Overlapping requests combine; active wins.
    SaveResult<void> ReconcileChunks(Diagnostics* diagnostics = nullptr);

    // Create attaches a new database only after a successful first checkpoint.
    // Load requires a pristine World and leaves chunk materialization to demand.
    SaveResult<void> CreateSave(const char* path, const SessionState& session,
        Diagnostics* diagnostics = nullptr);
    SaveResult<SessionState> LoadSave(const char* path, Diagnostics* diagnostics = nullptr);
    SaveResult<void> Save(const SessionState& session, Diagnostics* diagnostics = nullptr);
    bool HasSave() const;
    const ContentCatalog& GetContent() const;

    const Chunk* GetChunk(ChunkCoord position) const;
    const Tile* GetTile(TileCoord position) const;
    bool IsActive(TileCoord position) const;
    Vector<ChunkCoord> LoadedChunks() const;
    std::size_t ArchivedChunkCount() const;

    const Actor* GetActor(ActorHandle handle) const;
    const Item* GetItem(ItemHandle handle) const;
    const InteractiveObject* GetObject(ObjectHandle handle) const;
    ActorHandle FindActor(PersistentId id) const;
    ItemHandle FindItem(PersistentId id) const;
    ObjectHandle FindObject(PersistentId id) const;
    ActorHandle ActorAt(TileCoord position) const;
    Vector<ItemHandle> ItemsAt(TileCoord position) const;
    ObjectHandle ObjectAt(TileCoord position) const;

    WorldResult<void> SetTile(TileCoord position, Tile tile);
    WorldResult<ActorHandle> CreateActor(TileCoord position, bool player = false);
    WorldResult<void> MoveActor(ActorHandle actor, TileCoord destination);
    // Actor destruction drops possessions at its last position.
    WorldResult<void> DestroyActor(ActorHandle actor);
    WorldResult<ItemHandle> CreateItem(ItemKind kind, ItemLocation location);
    WorldResult<void> TransferItem(ItemHandle item, ItemLocation destination);
    WorldResult<void> DestroyItem(ItemHandle item);
    WorldResult<ObjectHandle> CreateObject(TileCoord position, bool enabled = false);
    // Object destruction drops its contents on the ground.
    WorldResult<void> DestroyObject(ObjectHandle object);
    WorldResult<void> SetObjectEnabled(ObjectHandle object, bool enabled);

    // Snapshot lists ordered by persistent ID, independent of storage/load order.
    Vector<ActorHandle> ActiveActors() const;
    Vector<ObjectHandle> ActiveObjects() const;
    WorldResult<void> RecordActorTurn(ActorHandle actor);
    WorldResult<void> RecordObjectUpdate(ObjectHandle object);

    // Useful for headless scenarios: validates ownership and spatial indexes.
    bool CheckInvariants() const;

private:
    class Impl;
    eastl::unique_ptr<Impl> impl_;
};

}
