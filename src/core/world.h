#pragma once

#include "core/containers.h"
#include "content/content.h"
#include "core/save_result.h"
#include "core/timing.h"
#include "core/world_types.h"
#include "core/item_state.h"
#include "core/world_result.h"
#include "core/perception_types.h"
#include "core/light_types.h"
#include "actors/actor.h"
#include "actors/ai_controller.h"
#include "objects/interactive_object.h"
#include "objects/object_behavior.h"

#include <EASTL/array.h>
#include <EASTL/expected.h>
#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/unique_ptr.h>

#include <cstdint>

namespace cataclysmx {

struct VisibilityMask;
class CombatSystem;

// Authoritative server state captured in the same transaction as chunk values.
struct SavedPlayer {
    PersistentId actor = 0;
    TileCoord position{};
    String name;
    std::uint64_t reconnectToken = 0;
};

inline constexpr int maxCircleResidencyRadius = 128;
struct CircleResidency {
    int radiusTiles = playerSightRange + maxLightRange + lightSpillDistance;
    bool operator==(const CircleResidency&) const = default;
};

struct SessionState {
    std::uint64_t tick = 0;
    std::uint32_t rngState = 0;
    PersistentId player = 0;
    TileCoord playerPosition{};
    int activeRadius = 1;
    int residentRadius = 5;
    std::uint64_t autosaveInterval = 0;
    PacingSettings pacing{};
    Vector<SavedPlayer> players;
    eastl::optional<CircleResidency> circleResidency;
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
struct Chunk {
    ChunkCoord position{};
    bool active = false;
    eastl::array<Tile, chunk_size * chunk_size> tiles{};
    // Strictly ascending persistent actor ID; independent of dense storage order.
    Vector<ActorHandle> actors;
    // TileIndex-indexed nonparked occupants; parked actors never occupy a tile.
    eastl::array<ActorHandle, chunk_size * chunk_size> actorOccupancy{};
    Vector<ItemHandle> ground_items;
    Vector<ObjectHandle> objects;
};

class World;

// Allocation-free ground-item filter. The range and its iterators borrow World
// storage and expire at any World mutation. Traversal order is unspecified.
class ItemRange {
public:
    class Iterator {
    public:
        ItemHandle operator*() const;
        Iterator& operator++();
        Iterator operator++(int);
        bool operator==(const Iterator&) const = default;

    private:
        friend class ItemRange;
        Iterator(const World* world, const Vector<ItemHandle>* items,
            TileCoord position, std::size_t index);
        void SkipNonmatching();
        const World* world_;
        const Vector<ItemHandle>* items_;
        TileCoord position_;
        std::size_t index_;
    };

    Iterator begin() const;
    Iterator end() const;
    bool Empty() const;

private:
    friend class World;
    ItemRange(const World& world, const Chunk* chunk, TileCoord position);
    const World* world_;
    const Vector<ItemHandle>* items_;
    TileCoord position_;
};

enum class LoadMode { resident, active };
struct ChunkRegion {
    // Horizontal columns through every world layer; center.y is ignored.
    ChunkCoord center{};
    int radius = 0;
};
struct TileCircleRegion {
    TileCoord center{};
    int radiusTiles = 0;
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
    // Tile-space circle, full-height columns, center.y ignored; no implicit halo.
    WorldResult<LoadRequest> RequestCircle(TileCircleRegion region, LoadMode mode);
    WorldResult<void> UpdateCircleRequest(LoadRequest request, TileCircleRegion region, LoadMode mode);
    WorldResult<void> ReleaseRequest(LoadRequest request);
    // Synchronous boundary operation. Overlapping requests combine; active wins.
    SaveResult<void> ReconcileChunks(Diagnostics* diagnostics = nullptr);

    // Create attaches a new database only after a successful first checkpoint.
    // Load requires a pristine World and leaves chunk materialization to demand.
    // Checkpoints source session.tick from GetTick(), ignoring the supplied copy.
    SaveResult<void> CreateSave(const char* path, const SessionState& session,
        Diagnostics* diagnostics = nullptr);
    SaveResult<SessionState> LoadSave(const char* path, Diagnostics* diagnostics = nullptr);
    SaveResult<void> Save(const SessionState& session, Diagnostics* diagnostics = nullptr);
    bool HasSave() const;
    const ContentCatalog& GetContent() const;
    std::uint32_t GetSeed() const;
    std::uint32_t GetGenerationVersion() const;
    // Conservative presentation tokens, owned by mutations (including admin edits).
    std::uint64_t GetRevision() const;
    std::uint64_t GetMapRevision() const;
    // Session-local policy; validation failure leaves the previous profile intact.
    WorldResult<void> SetVisionProfile(VisionProfile vision);
    VisionProfile GetVisionProfile() const;
    // Authoritative terrain perception; cache refreshes never teach memory.
    bool CanPlayerSee(ActorHandle observer, TileCoord target) const;
    // Read-only projection borrow; expires at a World mutation or perception refresh.
    // Null for an unavailable observer. Does not teach memory or acquire residency.
    const VisibilityMask* GetPlayerVisibility(ActorHandle observer) const;
    PerceptionStats GetPerceptionStats() const;
    void ResetPerceptionStats() const;

    const Chunk* GetChunk(ChunkCoord position) const;
    const Tile* GetTile(TileCoord position) const;
    bool IsActive(TileCoord position) const;
    // Unspecified column order; each full-height column is contiguous, ascending Y.
    Vector<ChunkCoord> LoadedChunks() const;
    std::size_t ArchivedChunkCount() const;

    const Actor* GetActor(ActorHandle handle) const;
    // World owns the coordinator; combat rules and action transitions live there.
    CombatSystem& Combat();
    const CombatSystem& Combat() const;
    const Item* GetItem(ItemHandle handle) const;
    const InteractiveObject* GetObject(ObjectHandle handle) const;
    ActorHandle FindActor(PersistentId id) const;
    // Stable chunk-local cursor: first resident actor with ID > after, or {}.
    ActorHandle NextActorInChunk(ChunkCoord position, PersistentId after) const;
    // Last known position of a player actor, including unloaded roster players.
    eastl::optional<TileCoord> FindPlayerPosition(PersistentId id) const;
    ItemHandle FindItem(PersistentId id) const;
    ObjectHandle FindObject(PersistentId id) const;
    ActorHandle ActorAt(TileCoord position) const;
    // Borrowed filter; invalid/unloaded positions are empty. Expires on mutation.
    ItemRange ItemsAt(TileCoord position) const;
    std::size_t CountItemsAt(TileCoord position) const;
    // Explicit snapshot in unspecified order; clears output, retaining capacity.
    void CollectItemsAt(TileCoord position, Vector<ItemHandle>& output) const;
    ObjectHandle ObjectAt(TileCoord position) const;

    WorldResult<void> SetTile(TileCoord position, Tile tile);
    // Bottom-up settlement without damage, RNG, or elapsed time. Spatial edits
    // also settle affected resident columns; parked actors remain frozen.
    void SettleGravity();
    WorldResult<ActorHandle> CreateActor(TileCoord position, bool player = false);
    WorldResult<void> MoveActor(ActorHandle actor, TileCoord destination);
    WorldResult<void> TryActorStep(ActorHandle actor, TileCoord direction, bool activeOnly = false);
    DataObject CaptureActorController(ActorHandle actor) const;
    AiInspection InspectActorAi(ActorHandle actor) const;
    WorldResult<void> SetActorController(ActorHandle actor, AiControllerKind kind);
    WorldResult<void> SetActorAiProfile(ActorHandle actor, eastl::string_view profile);
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
    WorldResult<ObjectHandle> CreateObject(ObjectTypeId type, TileCoord position, bool enabled = true);
    eastl::optional<ObjectStateView> DescribeObjectState(ObjectHandle object) const;
    ObjectUseResult UseObject(ActorHandle actor, ObjectHandle object, ObjectAction action);
    WorldResult<void> SetObjectEnergy(ObjectHandle object, std::uint64_t energy);
    DataObject CaptureObjectState(ObjectHandle object) const;
    // Object destruction drops its contents on the ground.
    WorldResult<void> DestroyObject(ObjectHandle object);
    WorldResult<void> SetObjectEnabled(ObjectHandle object, bool enabled);
    WorldResult<void> SetObjectAppearance(ObjectHandle object, ObjectKind kind,
        ObjectFacing facing = ObjectFacing::North, bool broken = false);
    WorldResult<void> SetObjectLight(ObjectHandle object, LightEmission light);
    WorldResult<void> SetActorLight(ActorHandle actor, LightEmission light);
    void SetSkyLight(std::uint8_t level);
    std::uint8_t GetSkyLight() const;
    // Disposable chunk-local optical/source input token; zero when nonresident.
    // Used by derived lighting to skip unchanged terrain and emitter scans.
    std::uint64_t GetLightingInputRevision(ChunkCoord position) const;
    // Derived caches may refresh; these queries never change player memories.
    std::uint8_t GetLightLevel(TileCoord position) const;
    // Chunk-contiguous projection borrow; expires at the next lighting preparation.
    eastl::span<const std::uint8_t> GetChunkLightLevels(ChunkCoord position) const;
    // Direct incident endpoint light; faces +X,-X,+Z,-Z,+Y,-Y (0..5).
    std::uint8_t GetIncidentLightLevel(TileCoord position, int face) const;
    RememberedTile GetRememberedTile(PersistentId player, TileCoord position) const;
    // Owned canonical save snapshot: players by ID, chunks by (X,Z,Y).
    Vector<PlayerMemory> GetPlayerMemories() const;
    // Explicit post-action/event boundary. Parked/inactive players do not learn.
    void UpdatePlayerObservations();
    WorldResult<void> SetObjectUpdateInterval(ObjectHandle object, std::uint32_t interval);
    WorldResult<void> SetActorSpeed(ActorHandle actor, std::uint32_t speed);
    WorldResult<void> ScheduleActorAction(ActorHandle actor, std::uint32_t baseCost = 100);
    Tick GetTick() const;
    // Valid handles required. Suspended timers report their frozen duration.
    Tick GetActorRemainingTicks(ActorHandle actor) const;
    Tick GetObjectRemainingTicks(ObjectHandle object) const;
    void ElapseTicks(Tick elapsed);

    // Unordered snapshots; clear output while retaining its capacity.
    void CollectActiveActors(Vector<ActorHandle>& output) const;
    void CollectActiveObjects(Vector<ObjectHandle>& output) const;
    void CollectReadyPlayers(Vector<ActorHandle>& output) const;
    void ProcessNpcActions(std::uint32_t& rng);
    bool ProcessObjectEvents();
    eastl::optional<Tick> NextActorEventIn() const;
    eastl::optional<Tick> NextObjectEventIn() const;
    WorldResult<void> RecordActorTurn(ActorHandle actor);
    WorldResult<void> RecordObjectUpdate(ObjectHandle object);

    // Useful for headless scenarios: validates ownership and spatial indexes.
    bool CheckInvariants() const;

private:
    friend class CombatSystem;
    Actor* MutableActor(ActorHandle actor);
    void ActorChanged(ActorHandle actor);
    friend bool LineVisible(const World&, TileCoord, TileCoord);
    void RecordPointLos(bool voxel, std::uint64_t count = 1) const;
    friend class ItemSystem;
    friend class ItemReadContext;
    friend class ItemMutationContext;
    friend class ObjectMutationContext;
    bool CanUseObject(ActorHandle actor, ObjectHandle object) const;
    WorldResult<void> ConsumeObjectInputs(ActorHandle actor, ObjectHandle object, ItemTypeId type, std::uint32_t count);
    WorldResult<void> ProduceObjectOutputs(ObjectHandle object, ItemTypeId type, std::uint32_t count);
    WorldResult<void> CollectObjectContents(ActorHandle actor, ObjectHandle object);
    Item* MutableItem(ItemHandle item);
    void MarkItemDirty(ItemHandle item);
    bool ItemEligible(ItemHandle item) const;
    WorldResult<void> CheckItemTransfer(ItemHandle item, ItemLocation destination) const;
    void FlushGravity();
    class Impl;
    eastl::unique_ptr<Impl> impl_;
};

}
