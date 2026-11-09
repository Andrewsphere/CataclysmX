#pragma once

#include "core/containers.h"
#include "content/content.h"
#include "core/save_result.h"
#include "core/timing.h"
#include "core/world_types.h"
#include "core/item_state.h"
#include "core/world_result.h"

#include <EASTL/array.h>
#include <EASTL/expected.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/unique_ptr.h>

#include <cstdint>

namespace cataclysmx {

// Authoritative server state captured in the same transaction as chunk values.
struct SavedPlayer {
    PersistentId actor = 0;
    TileCoord position{};
    String name;
    std::uint64_t reconnectToken = 0;
};

struct SessionState {
    std::uint64_t tick = 0;
    std::uint32_t rngState = 0;
    PersistentId player = 0;
    TileCoord playerPosition{};
    int activeRadius = 1;
    int residentRadius = 2;
    std::uint64_t autosaveInterval = 0;
    PacingSettings pacing{};
    Vector<SavedPlayer> players;
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

enum class LocationKind : std::uint8_t { ground, actor, item, object };
struct ItemLocation {
    LocationKind kind = LocationKind::ground;
    TileCoord position{};
    ActorHandle actor{};
    ItemHandle item{};
    ObjectHandle object{};
    ItemCapabilityId installation{};

    static ItemLocation Ground(TileCoord position);
    static ItemLocation Carried(ActorHandle actor);
    static ItemLocation Inside(ItemHandle item, ItemCapabilityId installation = {});
    static ItemLocation Stored(ObjectHandle object);
    bool operator==(const ItemLocation&) const = default;
};

struct Actor {
    PersistentId id = 0;
    TileCoord position{};
    bool player = false;
    std::uint64_t turns = 0;
    Vector<ItemHandle> inventory;
    std::uint32_t speed = 100;
    Tick remainingTicks = 0;
    bool parked = false;
    std::uint64_t carryCapacityGrams = 100000;
};
struct Item {
    Item();
    Item(PersistentId id, ItemTypeId type, ItemLocation location);
    ~Item();
    Item(Item&&) noexcept;
    Item& operator=(Item&&) noexcept;
    Item(const Item&) = delete;
    Item& operator=(const Item&) = delete;
    PersistentId id = 0;
    ItemTypeId type{};
    ItemLocation location{};
    Vector<ItemHandle> contents;
private:
    friend class ItemSystem;
    ItemRuntime runtime_;
};
struct InteractiveObject {
    PersistentId id = 0;
    TileCoord position{};
    bool enabled = false;
    std::uint64_t updates = 0;
    Vector<ItemHandle> contents;
    std::uint32_t updateInterval = 100;
    Tick remainingTicks = 100;
};
struct Chunk {
    ChunkCoord position{};
    bool active = false;
    eastl::array<Tile, chunk_size * chunk_size> tiles{};
    Vector<ActorHandle> actors;
    Vector<ItemHandle> ground_items;
    Vector<ObjectHandle> objects;
};

enum class LoadMode { resident, active };
struct ChunkRegion {
    // Horizontal columns through every world layer; center.y is ignored.
    ChunkCoord center{};
    int radius = 0;
};

// All mutations go through World. Returned pointers are read-only borrows:
// reacquire after mutation, and never retain them across unloading/deletion.
class World {
public:
    // Content must outlive the World and remain unchanged. New worlds use layered
    // regional generation; versions 1/2 retain their surface generation. All
    // layers require floor/wall/air terrain roles.
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
    std::uint32_t GetSeed() const;
    std::uint32_t GetGenerationVersion() const;

    const Chunk* GetChunk(ChunkCoord position) const;
    const Tile* GetTile(TileCoord position) const;
    bool IsActive(TileCoord position) const;
    Vector<ChunkCoord> LoadedChunks() const;
    std::size_t ArchivedChunkCount() const;

    const Actor* GetActor(ActorHandle handle) const;
    const Item* GetItem(ItemHandle handle) const;
    const InteractiveObject* GetObject(ObjectHandle handle) const;
    ActorHandle FindActor(PersistentId id) const;
    // Last known position of a player actor, including unloaded roster players.
    eastl::optional<TileCoord> FindPlayerPosition(PersistentId id) const;
    ItemHandle FindItem(PersistentId id) const;
    ObjectHandle FindObject(PersistentId id) const;
    ActorHandle ActorAt(TileCoord position) const;
    Vector<ItemHandle> ItemsAt(TileCoord position) const;
    ObjectHandle ObjectAt(TileCoord position) const;

    WorldResult<void> SetTile(TileCoord position, Tile tile);
    // Bottom-up settlement without damage, RNG, or elapsed time. Spatial edits
    // also settle affected resident columns; parked actors remain frozen.
    void SettleGravity();
    WorldResult<ActorHandle> CreateActor(TileCoord position, bool player = false);
    WorldResult<void> MoveActor(ActorHandle actor, TileCoord destination);
    // Parked players remain owned but do not occupy tiles or advance time.
    // Unparking requires an unoccupied passable tile.
    WorldResult<void> SetActorParked(ActorHandle actor, bool parked);
    // Validate all players first, then park all before settling gravity so
    // stacked players retain their positions regardless of input order.
    WorldResult<void> ParkActors(eastl::span<const ActorHandle> actors);
    // Actor destruction drops possessions at its last position.
    WorldResult<void> DestroyActor(ActorHandle actor);
    WorldResult<ItemHandle> CreateItem(ItemTypeId type, ItemLocation location);
    WorldResult<void> TransferItem(ItemHandle item, ItemLocation destination);
    WorldResult<void> DestroyItem(ItemHandle item);
    WorldResult<void> SetActorCarryCapacity(ActorHandle actor, std::uint64_t grams);
    std::uint64_t GetItemMass(ItemHandle item) const;
    std::uint64_t GetCarriedMass(ActorHandle actor) const;
    std::uint64_t GetContainerUsedVolume(ItemHandle item) const;
    bool CanAccessItem(ActorHandle actor, ItemHandle item) const;
    Vector<ItemActionInfo> QueryItemActions(ActorHandle actor, ItemHandle item) const;
    ItemUseResult UseItem(ActorHandle actor, ItemHandle item, const ItemUseRequest& request);
    WorldResult<void> SetItemEnergy(ItemHandle item, ItemCapabilityId capability, std::uint32_t energy);
    DataObject CaptureItemState(ItemHandle item) const;
    eastl::optional<ItemEnergyView> GetItemEnergy(ItemHandle item, ItemCapabilityId capability) const;
    ItemPresentation DescribeItem(ActorHandle actor, ItemHandle item) const;
    eastl::optional<Tick> NextItemEventIn() const;
    void ProcessItemEvents();
    std::size_t ActiveItemCount() const;
    WorldResult<ObjectHandle> CreateObject(TileCoord position, bool enabled = false);
    // Object destruction drops its contents on the ground.
    WorldResult<void> DestroyObject(ObjectHandle object);
    WorldResult<void> SetObjectEnabled(ObjectHandle object, bool enabled);
    WorldResult<void> SetObjectUpdateInterval(ObjectHandle object, std::uint32_t interval);
    WorldResult<void> SetActorSpeed(ActorHandle actor, std::uint32_t speed);
    WorldResult<void> ScheduleActorAction(ActorHandle actor, std::uint32_t baseCost = 100);
    void ElapseTicks(Tick elapsed);

    // Snapshot lists ordered by persistent ID, independent of storage/load order.
    Vector<ActorHandle> ActiveActors() const;
    Vector<ObjectHandle> ActiveObjects() const;
    WorldResult<void> RecordActorTurn(ActorHandle actor);
    WorldResult<void> RecordObjectUpdate(ObjectHandle object);

    // Useful for headless scenarios: validates ownership and spatial indexes.
    bool CheckInvariants() const;

private:
    friend class ItemSystem;
    friend class ItemReadContext;
    friend class ItemMutationContext;
    Item* MutableItem(ItemHandle item);
    void MarkItemDirty(ItemHandle item);
    bool ItemEligible(ItemHandle item) const;
    WorldResult<void> CheckItemTransfer(ItemHandle item, ItemLocation destination) const;
    void FlushGravity();
    class Impl;
    eastl::unique_ptr<Impl> impl_;
};

}
