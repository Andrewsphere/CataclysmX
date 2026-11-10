#include <cstdlib>
#include <cstring>
#include <bit>

#include "core/world.h"
#include "core/lighting.h"
#include "core/perception.h"
#include "core/slot_map.h"
#include "core/save_database.h"
#include "items/item_system.h"
#include "items/item_snapshot.h"
#include "worldgen/worldgen.h"

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/optional.h>
#include <EASTL/sort.h>
#include <EASTL/utility.h>
#include <ankerl/unordered_dense.h>

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
    return left.x < right.x || (left.x == right.x
        && (left.z < right.z || (left.z == right.z && left.y < right.y)));
}

bool ValidChunk(ChunkCoord position) {
    return position.y >= world_min_y && position.y <= world_max_y;
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

bool ValidCircle(TileCircleRegion region) {
    const auto x = static_cast<std::int64_t>(region.center.x);
    const auto z = static_cast<std::int64_t>(region.center.z);
    const auto radius = static_cast<std::int64_t>(region.radiusTiles);
    return radius >= 0 && radius <= maxCircleResidencyRadius
        && x - radius >= -world_coordinate_limit && x + radius <= world_coordinate_limit
        && z - radius >= -world_coordinate_limit && z + radius <= world_coordinate_limit;
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
    eastl::optional<TileCircleRegion> circle;
};

using Archive = ChunkArchive;

std::uint64_t CheckedAdd(std::uint64_t left, std::uint64_t right) {
    Require(right <= eastl::numeric_limits<std::uint64_t>::max() - left);
    return left + right;
}

SaveResult<void> SaveFailure(SaveError error, const char* message, Diagnostics* diagnostics) {
    if (diagnostics) {
        diagnostics->Add(message);
    }
    return eastl::unexpected(error);
}

// Validate before Restore: it must never discover a bad owner after creating keys.
// Append IDs to a shared list so the caller can also reject cross-chunk aliases.
bool ValidateArchive(const ContentCatalog& content, const Archive& saved,
    PersistentId nextId, Vector<PersistentId>& ids) {
    if (!ValidRegion({saved.position, 0}) || !ValidChunk(saved.position)) {
        return false;
    }
    for (Tile tile : saved.tiles) {
        if (!content.TryGetTerrain(tile.terrain)) {
            return false;
        }
    }
    const auto addId = [&ids, nextId](PersistentId id) {
        if (id == 0 || id >= nextId) {
            return false;
        }
        ids.push_back(id);
        return true;
    };
    Vector<TileCoord> positions;
    for (const SavedActor& actor : saved.actors) {
        if (!ValidLightEmission(actor.light) || actor.speed == 0 || (actor.parked && !actor.player) || !addId(actor.id)
            || !ValidPosition(actor.position) || ChunkOf(actor.position) != saved.position
            || (!actor.parked && Occurrences(positions, actor.position))) {
            return false;
        }
        if (!actor.parked) { positions.push_back(actor.position); }
    }
    positions.clear();
    for (const SavedObject& object : saved.objects) {
        if (!ValidLightEmission(object.light) || object.kind == ObjectKind::None
            || !ValidObjectAppearance({object.kind, object.facing, object.enabled, object.broken})
            || object.updateInterval == 0 || object.remainingTicks > object.updateInterval
            || !addId(object.id) || !ValidPosition(object.position)
            || ChunkOf(object.position) != saved.position || Occurrences(positions, object.position)) {
            return false;
        }
        positions.push_back(object.position);
    }
    for (std::size_t index = 0; index < saved.items.size(); ++index) {
        const SavedItem& item = saved.items[index];
        const ItemDefinition* definition = content.TryGetItem(item.type);
        if (!addId(item.id) || !definition
            || (item.location != LocationKind::item && item.installation.value)) {
            return false;
        }
        switch (item.location) {
        case LocationKind::ground:
            if (item.owner != 0 || !ValidPosition(item.position)
                || ChunkOf(item.position) != saved.position) {
                return false;
            }
            break;
        case LocationKind::actor:
            if (eastl::none_of(saved.actors.begin(), saved.actors.end(),
                    [&item](const SavedActor& owner) { return owner.id == item.owner; })) {
                return false;
            }
            break;
        case LocationKind::object:
            if (eastl::none_of(saved.objects.begin(), saved.objects.end(),
                    [&item](const SavedObject& owner) { return owner.id == item.owner; })) {
                return false;
            }
            break;
        case LocationKind::item:
            if (eastl::none_of(saved.items.begin(), saved.items.begin() + index,
                    [&item](const SavedItem& owner) {
                         return owner.id == item.owner;
                    })) {
                return false;
            }
            break;
        default: return false;
        }
    }
    return ValidateItemSnapshot(content, saved).has_value();
}

bool UniqueIds(Vector<PersistentId>& ids) {
    eastl::sort(ids.begin(), ids.end());
    return eastl::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

void CollectArchiveIds(const Archive& archive, Vector<PersistentId>& ids) {
    for (const SavedActor& actor : archive.actors) { ids.push_back(actor.id); }
    for (const SavedObject& object : archive.objects) { ids.push_back(object.id); }
    for (const SavedItem& item : archive.items) { ids.push_back(item.id); }
}

bool ValidMemoryChunk(const ContentCatalog& content, const MemoryChunk& chunk) {
    if (!ValidChunk(chunk.position) || !ValidRegion({chunk.position, 0})) { return false; }
    bool observed = false;
    for (TerrainId terrain : chunk.terrain) {
        if (terrain.value && !content.TryGetTerrain(terrain)) { return false; }
        observed = observed || terrain.value != 0;
    }
    if (!observed) { return false; }
    int previousIndex = -1;
    for (const RememberedObject& object : chunk.objects) {
        if (object.tileIndex <= previousIndex || !chunk.terrain[object.tileIndex].value
            || object.appearance.kind == ObjectKind::None
            || !ValidObjectAppearance(object.appearance)) { return false; }
        previousIndex = object.tileIndex;
    }
    return true;
}

bool ValidMemories(const ContentCatalog& content, const Vector<PlayerMemory>& memories,
    PersistentId nextId) {
    PersistentId previousPlayer = 0;
    for (const PlayerMemory& memory : memories) {
        if (memory.player <= previousPlayer || memory.player >= nextId || memory.chunks.empty()) { return false; }
        previousPlayer = memory.player;
        for (std::size_t c = 0; c < memory.chunks.size(); ++c) {
            const MemoryChunk& chunk = memory.chunks[c];
            if (!ValidMemoryChunk(content, chunk)
                || (c && !ChunkLess(memory.chunks[c-1].position, chunk.position))) { return false; }
        }
    }
    return true;
}

}

ChunkCoord ChunkOf(TileCoord position) {
    return {FloorChunk(position.x), FloorChunk(position.z), position.y};
}

int TileIndex(TileCoord position) {
    const int x = (position.x % chunk_size + chunk_size) % chunk_size;
    const int z = (position.z % chunk_size + chunk_size) % chunk_size;
    return z * chunk_size + x;
}

bool ValidPosition(TileCoord position) {
    return position.x >= -world_coordinate_limit && position.x <= world_coordinate_limit
        && position.z >= -world_coordinate_limit && position.z <= world_coordinate_limit
        && position.y >= world_min_y && position.y <= world_max_y;
}

bool Passable(const ContentCatalog& content, TerrainId terrain) {
    return content.GetTerrain(terrain).passable;
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

ItemLocation ItemLocation::Inside(ItemHandle item, ItemCapabilityId installation) {
    ItemLocation result;
    result.kind = LocationKind::item;
    result.item = item;
    result.installation = installation;
    return result;
}

ItemLocation ItemLocation::Stored(ObjectHandle object) {
    ItemLocation result;
    result.kind = LocationKind::object;
    result.object = object;
    return result;
}

class World::Impl {
    friend class World;
    struct ColumnHash {
        std::uint64_t operator()(ColumnCoord position) const {
            const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.x)) << 32)
                | static_cast<std::uint32_t>(position.z);
            return ankerl::unordered_dense::hash<std::uint64_t>{}(key);
        }
    };
    struct ChunkColumn {
        eastl::array<Chunk, world_layer_count> layers;
    };
    ankerl::unordered_dense::segmented_map<ColumnCoord, ChunkColumn, ColumnHash> columns_;
public:
    using DemandMap = ankerl::unordered_dense::map<ColumnCoord, bool, ColumnHash>;
    struct ChunkHash {
        std::uint64_t operator()(ChunkCoord position) const {
            const auto horizontal = ColumnHash{}({position.x, position.z});
            return ankerl::unordered_dense::hash<std::uint64_t>{}(
                horizontal ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.y))
                    * 0x9e3779b97f4a7c15ULL));
        }
    };
    using ArchiveMap = ankerl::unordered_dense::segmented_map<ChunkCoord, Archive, ChunkHash>;
    const ContentCatalog& content;
    TerrainId floor;
    TerrainId wall;
    std::uint32_t seed;
    std::uint32_t generationVersion = world_generation_version;
    // A bounded, disposable planning cache; never part of authoritative state.
    eastl::optional<RegionPlan> generationPlan;
    PersistentId next_id = 1;
    dod::slot_map64<Actor> actors;
    dod::slot_map64<Item> items;
    dod::slot_map64<InteractiveObject> objects;
    dod::slot_map64<Request> requests;
    ArchiveMap archives; // Authoritative, unloaded, unsaved values; never bounded.
    // Clean, disposable FIFO values; pending archives are never subject to eviction.
    ArchiveMap warmArchives;
    Vector<ChunkCoord> warmOrder; // FIFO, independent of dense-map relocation on erase.
    static constexpr std::size_t maxWarmEntries = 256;
    static constexpr std::size_t maxWarmBytes = 8 * 1024 * 1024;
    std::size_t warmBytes = 0;
    ankerl::unordered_dense::set<ChunkCoord, ChunkHash> dirtyChunks; // Includes pending unloaded archives.
    ankerl::unordered_dense::map<PersistentId, TileCoord> playerPositions; // Survives unload and archive eviction.
    ItemSystem itemSystem;
    eastl::unique_ptr<SaveDatabase> database;
    Vector<TileCoord> gravityColumns; // X/Z tile columns, normalized Y.
    unsigned gravityDepth = 0;
    bool settlingGravity = false;
    std::uint8_t skyLight = defaultSkyLight;
    using MemoryMap = ankerl::unordered_dense::segmented_map<ChunkCoord, MemoryChunk, ChunkHash>;
    ankerl::unordered_dense::map<PersistentId, MemoryMap> memories;
    mutable LightingField lighting;
    mutable bool lightingDirty = true;
    ankerl::unordered_dense::map<ChunkCoord, std::uint64_t, ChunkHash> lightingInputs;
    std::uint64_t nextLightingInputRevision = 0;
    std::uint64_t revision = 1;
    std::uint64_t mapRevision = 1;
    std::uint64_t geometryRevision = 1;
    std::uint64_t appearanceRevision = 1;
    mutable std::uint64_t lightingRevision = 1;
    bool demandDirty = true;
    VisionProfile vision;
    mutable PerceptionStats perceptionStats;
    mutable OpticalField optics;
    mutable std::uint64_t opticsRevision = 0;
    mutable ankerl::unordered_dense::set<ChunkCoord, ChunkHash> opticalChanges;
    struct LightSamples {
        ChunkCoord position{};
        std::uint64_t revision = 0;
        eastl::array<std::uint8_t, chunk_size * chunk_size> levels;
        std::uint8_t minimum = 255;
    };
    mutable Vector<LightSamples> lightSamples;
    ankerl::unordered_dense::map<ChunkCoord, std::uint64_t, ChunkHash> appearanceChanges;
    struct FootprintChunk {
        ChunkCoord position;
        eastl::array<std::uint64_t, 4> bits{};
    };
    struct ObserverCache {
        PersistentId player = 0;
        VisibilityMask geometry;
        VisibilityMask visible;
        Vector<FootprintChunk> footprint;
        ankerl::unordered_dense::map<ChunkCoord, eastl::array<std::uint64_t, 4>, ChunkHash> learnedFootprint;
        VisibilityWorkspace workspace;
        std::uint64_t geometryRevision = 0;
        std::uint64_t lightingRevision = 0;
        VisionProfile vision;
        std::uint64_t filterRevision = 0;
        std::uint64_t observedFilterRevision = 0;
        std::uint64_t observedAppearanceRevision = 0;
    };
    mutable Vector<ObserverCache> observers;

    static bool TouchesPerception(ChunkCoord chunk, const VisibilityMask& mask, int margin = 0) {
        return chunk.y >= mask.minimumY - margin && chunk.y <= mask.maximumY + margin
            && chunk.x * chunk_size <= mask.origin.x + mask.radius + margin
            && (chunk.x + 1) * chunk_size > mask.origin.x - mask.radius - margin
            && chunk.z * chunk_size <= mask.origin.z + mask.radius + margin
            && (chunk.z + 1) * chunk_size > mask.origin.z - mask.radius - margin;
    }

    void MarkGeometryDirty(ChunkCoord position) {
        ++geometryRevision;
        opticalChanges.insert(position);
        for (auto& observer : observers) {
            if (TouchesPerception(position, observer.geometry)) { observer.geometryRevision = 0; }
        }
    }

    void MarkLightingDirty(ChunkCoord position) {
        lightingDirty = true;
        const auto inputRevision = ++nextLightingInputRevision;
        Require(inputRevision != 0);
        lightingInputs.insert_or_assign(position, inputRevision);
    }

    void EnsureLighting(const World& world, ChunkCoord position) const {
        const auto before = lighting.GetWorkStats();
        lighting.EnsureChunk(world, position);
        const auto& after = lighting.GetWorkStats();
        perceptionStats.lightingOutputChunks += after.outputChunks - before.outputChunks;
        perceptionStats.lightingSkyDirectChunks += after.skyDirectChunks - before.skyDirectChunks;
        perceptionStats.lightingSkyFirstChunks += after.skyFirstChunks - before.skyFirstChunks;
        perceptionStats.lightingSourcePatches += after.sourcePatches - before.sourcePatches;
    }

    ObserverCache& PerceptionFor(const World& world, const Actor& actor) const {
        auto found = eastl::find_if(observers.begin(), observers.end(), [&](const ObserverCache& cache) {
            return cache.player == actor.id;
        });
        if (found == observers.end()) {
            observers.push_back({});
            found = observers.end() - 1;
            found->player = actor.id;
        }
        auto& cache = *found;
        const bool geometryChanged = cache.geometryRevision == 0
            || cache.geometry.origin != actor.position || cache.geometry.radius != vision.range;
        if (geometryChanged) {
            if (opticsRevision != geometryRevision) {
                if (opticsRevision == 0) { optics.Rebuild(world); }
                else { optics.Refresh(world, {opticalChanges.values().data(), opticalChanges.size()}); }
                opticalChanges.clear();
                ++perceptionStats.opticalRebuilds;
                opticsRevision = geometryRevision;
            }
            BuildVisibilityMask(world, actor.position, vision.range, cache.geometry, perceptionStats, &optics, &cache.workspace);
            cache.geometryRevision = geometryRevision;
        }
        // Resolve pending lighting before comparing the light generation. Geometry
        // and light have different lifetimes, including const-only projections.
        world.GetLightLevel(actor.position);
        if (geometryChanged || cache.lightingRevision == 0 || cache.vision != vision) {
            ++perceptionStats.filterRebuilds;
            lightSamples.resize(ChunkCount());
            cache.visible = cache.geometry;
            cache.footprint.clear();
            const auto observerLight = lighting.Get(actor.position);
            int squaredRanges[4];
            for (int i = 0; i < 4; ++i) {
                const int range = VisionRange(vision, static_cast<LightBand>(i), observerLight);
                squaredRanges[i] = range * range;
            }
            std::size_t sampleIndex = 0;
            for (const auto& column : columns_) {
                for (const Chunk& chunk : column.second.layers) {
                    const auto chunkIndex = sampleIndex++;
                    const auto coord = chunk.position;
                    if (coord.y < cache.geometry.minimumY || coord.y > cache.geometry.maximumY
                        || coord.x * chunk_size > actor.position.x + vision.range
                        || (coord.x + 1) * chunk_size <= actor.position.x - vision.range
                        || coord.z * chunk_size > actor.position.z + vision.range
                        || (coord.z + 1) * chunk_size <= actor.position.z - vision.range) { continue; }
                    if (!cache.geometry.visibleLayers[coord.y - cache.geometry.minimumY]) { continue; }
                    // Prepare publishes invalidation tokens before output materializes.
                    EnsureLighting(world, coord);
                    auto& samples = lightSamples[chunkIndex];
                    const auto chunkRevision = lighting.GetChunkRevision(coord);
                    if (samples.position != coord || samples.revision != chunkRevision) {
                        const auto levels = lighting.GetChunkLevels(coord);
                        Require(levels.size() == chunk_size * chunk_size);
                        samples.position = coord;
                        samples.revision = chunkRevision;
                        samples.minimum = 255;
                        for (int i = 0; i < chunk_size * chunk_size; ++i) {
                            samples.levels[i] = levels[i];
                            samples.minimum = eastl::min(samples.minimum, levels[i]);
                        }
                    }
                    FootprintChunk footprint{coord, {}};
                    if (cache.geometry.clearLayers[coord.y - cache.geometry.minimumY] && samples.minimum >= 64) {
                        const int chunkX = coord.x * chunk_size;
                        const int chunkZ = coord.z * chunk_size;
                        for (int z = chunkZ; z < chunkZ + chunk_size; ++z) {
                            const auto row = cache.geometry.ClearRow(z, coord.y);
                            const int first = eastl::max(chunkX, row.first);
                            const int last = eastl::min(chunkX + chunk_size - 1, row.second);
                            if (first > last) { continue; }
                            const int low = (z - chunkZ) * chunk_size + first - chunkX;
                            const int high = (z - chunkZ) * chunk_size + last - chunkX;
                            for (int word = low / 64; word <= high / 64; ++word) {
                                const int a = word == low / 64 ? low % 64 : 0;
                                const int b = word == high / 64 ? high % 64 + 1 : 64;
                                footprint.bits[word] |= (b == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << b) - 1)
                                    & (~std::uint64_t{0} << a);
                            }
                        }
                        if (footprint.bits[0] || footprint.bits[1] || footprint.bits[2] || footprint.bits[3]) {
                            cache.footprint.push_back(footprint);
                        }
                        continue;
                    }
                    for (int index = 0; index < chunk_size * chunk_size; ++index) {
                        const TileCoord p{coord.x * chunk_size + index % chunk_size,
                            coord.z * chunk_size + index / chunk_size, coord.y};
                        const auto geometry = cache.geometry.Get(p);
                        if (!(geometry & 1)) { continue; }
                        ++perceptionStats.filterTilesSampled;
                        const auto light = (geometry & 2)
                            ? TerrainLightLevel(world, actor.position, p) : samples.levels[index];
                        const int dx = p.x - actor.position.x;
                        const int dz = p.z - actor.position.z;
                        if (dx*dx + dz*dz > squaredRanges[static_cast<int>(ClassifyLight(light))]) {
                            cache.visible.MaterializeLayer(coord.y);
                            cache.visible.cells[cache.visible.Index(p)] = 0;
                        } else {
                            footprint.bits[index / 64] |= std::uint64_t{1} << (index % 64);
                        }
                    }
                    if (footprint.bits[0] || footprint.bits[1] || footprint.bits[2] || footprint.bits[3]) {
                        cache.footprint.push_back(footprint);
                    }
                }
            }
            cache.lightingRevision = lightingRevision;
            cache.vision = vision;
            ++cache.filterRevision;
        }
        return cache;
    }

    // Keep callbacks and compound destruction inside a single spatial boundary.
    struct GravityBatch {
        World& world;
        explicit GravityBatch(World& value) : world(value) { ++world.impl_->gravityDepth; }
        ~GravityBatch() {
            --world.impl_->gravityDepth;
            world.FlushGravity();
        }
    };

    void QueueGravity(TileCoord position) {
        position.y = 0;
        if (eastl::find(gravityColumns.begin(), gravityColumns.end(), position) == gravityColumns.end()) {
            gravityColumns.push_back(position);
        }
    }

    explicit Impl(World& world, const ContentCatalog& catalog, std::uint32_t value)
        : content(catalog), floor(catalog.FindTerrain("floor")),
          wall(catalog.FindTerrain("wall")), seed(value), itemSystem(world) {
        Require(floor.value != 0 && wall.value != 0);
    }

    PersistentId IssueId() {
        Require(next_id != eastl::numeric_limits<PersistentId>::max());
        return next_id++;
    }

    eastl::optional<TileCoord> PlayerPosition(PersistentId id) const {
        const auto found = playerPositions.find(id);
        if (found == playerPositions.end()) { return eastl::nullopt; }
        return found->second;
    }

    void TrackPlayer(PersistentId id, TileCoord position) {
        playerPositions.insert_or_assign(id, position);
    }

    void ForgetPlayer(PersistentId id) {
        Require(playerPositions.erase(id) == 1);
        memories.erase(id);
        observers.erase(eastl::remove_if(observers.begin(), observers.end(),
            [id](const ObserverCache& cache) { return cache.player == id; }), observers.end());
    }

    Vector<PlayerMemory> CaptureMemories() const {
        Vector<PlayerMemory> snapshot;
        snapshot.reserve(memories.size());
        for (const auto& player : memories) {
            PlayerMemory memory{player.first, {}};
            memory.chunks.reserve(player.second.size());
            for (const auto& chunk : player.second) { memory.chunks.push_back(chunk.second); }
            eastl::sort(memory.chunks.begin(), memory.chunks.end(),
                [](const MemoryChunk& a, const MemoryChunk& b) { return ChunkLess(a.position, b.position); });
            snapshot.push_back(eastl::move(memory));
        }
        eastl::sort(snapshot.begin(), snapshot.end(),
            [](const PlayerMemory& a, const PlayerMemory& b) { return a.player < b.player; });
        return snapshot;
    }

    void MarkDirty(ChunkCoord position, bool map = true, bool appearance = false) {
        ++revision;
        if (map) { ++mapRevision; }
        if (appearance) {
            ++appearanceRevision;
            appearanceChanges.insert_or_assign(position, appearanceRevision);
        }
        dirtyChunks.insert(position);
    }

    ChunkCoord RootChunk(ItemLocation location) const {
        for (std::size_t depth = 0; ; ++depth) {
            Require(depth <= items.size());
            switch (location.kind) {
            case LocationKind::ground: return ChunkOf(location.position);
            case LocationKind::actor: return ChunkOf(Live(actors, location.actor)->position);
            case LocationKind::object: return ChunkOf(Live(objects, location.object)->position);
            case LocationKind::item: location = Live(items, location.item)->location; break;
            default: std::abort();
            }
        }
    }

    ItemLocation Root(ItemLocation location) const {
        for (std::size_t depth = 0; location.kind == LocationKind::item; ++depth) {
            Require(depth < 64);
            const Item* parent = Live(items, location.item);
            Require(parent != nullptr);
            location = parent->location;
        }
        return location;
    }

    bool Eligible(ItemHandle handle) const {
        const Item* item = Live(items, handle);
        if (!item) { return false; }
        const ItemLocation root = Root(item->location);
        const Chunk* chunk = GetChunk(RootChunk(root));
        return chunk && chunk->active
            && (root.kind != LocationKind::actor || !Live(actors, root.actor)->parked);
    }

    void Unregister(ItemHandle handle) {
        itemSystem.Detach(handle);
    }

    std::uint64_t Mass(ItemHandle handle) const {
        const Item* item = Live(items, handle);
        Require(item != nullptr);
        std::uint64_t result = content.GetItem(item->type).massGrams;
        for (ItemHandle child : item->contents) { result = CheckedAdd(result, Mass(child)); }
        return result;
    }

    std::uint64_t CarriedMass(ActorHandle handle) const {
        const Actor* actor = Live(actors, handle);
        Require(actor != nullptr);
        std::uint64_t result = 0;
        for (ItemHandle item : actor->inventory) { result = CheckedAdd(result, Mass(item)); }
        return result;
    }

    std::uint64_t Volume(ItemHandle handle, ItemHandle excluded = {}) const {
        const Item* item = Live(items, handle);
        Require(item != nullptr);
        std::uint64_t result = 0;
        for (ItemHandle child : item->contents) {
            const Item* value = Live(items, child);
            Require(value != nullptr);
            if (child != excluded && !value->location.installation.value) {
                result = CheckedAdd(result, content.GetItem(value->type).volumeMl);
            }
        }
        return result;
    }

    std::size_t Height(ItemHandle handle) const {
        const Item* item = Live(items, handle);
        Require(item != nullptr);
        std::size_t height = 1;
        for (ItemHandle child : item->contents) { height = eastl::max(height, 1 + Height(child)); }
        return height;
    }

    ItemHandle Installed(ItemHandle handle, ItemCapabilityId mount, ItemHandle excluded = {}) const {
        for (ItemHandle child : Live(items, handle)->contents) {
            if (child != excluded && Live(items, child)->location.installation == mount) { return child; }
        }
        return {};
    }

    WorldResult<void> Fits(ItemTypeId type, ItemLocation location, ItemHandle moving = {}) const {
        const auto& definition = content.GetItem(type);
        std::size_t depth = 0;
        for (ItemLocation cursor = location; cursor.kind == LocationKind::item;
            cursor = Live(items, cursor.item)->location) { ++depth; }
        if (depth + (moving.value ? Height(moving) : 1) > 64) {
            return eastl::unexpected(WorldError::capacity_exceeded);
        }
        if (location.kind == LocationKind::item) {
            const Item* parent = Live(items, location.item);
            const auto& parentDefinition = content.GetItem(parent->type);
            if (location.installation.value) {
                const auto* behavior = ItemBehaviorConfig(parentDefinition, location.installation);
                if (!behavior || !behavior->AcceptsInstallation(definition)) {
                    return eastl::unexpected(WorldError::incompatible_item);
                }
                if (Installed(location.item, location.installation, moving).value) {
                    return eastl::unexpected(WorldError::occupied);
                }
            } else {
                const auto* container = ItemContainer(parentDefinition);
                if (!container) { return eastl::unexpected(WorldError::not_container); }
                const auto used = Volume(location.item, moving);
                if (used > container->capacityVolumeMl
                    || definition.volumeMl > container->capacityVolumeMl - used) {
                    return eastl::unexpected(WorldError::capacity_exceeded);
                }
            }
        }
        const auto root = Root(location);
        if (root.kind == LocationKind::actor) {
            if (moving.value) {
                const auto oldRoot = Root(Live(items, moving)->location);
                if (oldRoot.kind == LocationKind::actor && oldRoot.actor == root.actor) { return {}; }
            }
            const auto used = CarriedMass(root.actor);
            const auto capacity = Live(actors, root.actor)->carryCapacityGrams;
            const auto mass = moving.value ? Mass(moving) : definition.massGrams;
            if (used > capacity || mass > capacity - used) {
                return eastl::unexpected(WorldError::capacity_exceeded);
            }
        }
        return {};
    }

    Chunk* GetChunk(ChunkCoord position) {
        if (!ValidChunk(position)) { return nullptr; }
        const auto found = columns_.find({position.x, position.z});
        return found == columns_.end() ? nullptr : &found->second.layers[position.y - world_min_y];
    }

    const Chunk* GetChunk(ChunkCoord position) const {
        if (!ValidChunk(position)) { return nullptr; }
        const auto found = columns_.find({position.x, position.z});
        return found == columns_.end() ? nullptr : &found->second.layers[position.y - world_min_y];
    }

    std::size_t ChunkCount() const { return columns_.size() * world_layer_count; }

    void InstallColumn(ColumnCoord position, bool active) {
        const auto [where, inserted] = columns_.try_emplace(position);
        Require(inserted);
        ++perceptionStats.columnsLoaded;
        for (int y = world_min_y; y <= world_max_y; ++y) {
            Chunk& chunk = where->second.layers[y - world_min_y];
            chunk.position = {position.x, position.z, y};
            chunk.active = active;
        }
    }

    static std::size_t WarmCost(const Archive& archive) {
        // DataObject hides recursive allocation capacities. Only cache item-free
        // values, whose entire heap ownership can be measured conservatively.
        return archive.actors.capacity() * sizeof(SavedActor)
            + archive.objects.capacity() * sizeof(SavedObject);
    }

    std::size_t WarmStorageBytes() const {
        // capacity() counts allocated value slots, not bytes or segment count.
        // segmented_vector::reserve allocates whole blocks; capacity() is their
        // count times slots per block. Its pointer vector reserves the requested
        // block count, or doubles on growth. Two pointers per value slot cover
        // that spare capacity even for one-value blocks. Warm storage is only
        // reserved to maxWarmEntries and pop_back/erase never release blocks.
        return sizeof(warmArchives) + sizeof(warmOrder)
            + warmArchives.values().capacity() * (sizeof(ArchiveMap::value_type) + 2 * sizeof(void*))
            + warmArchives.index_bytes() + warmOrder.capacity() * sizeof(ChunkCoord);
    }

    void ForgetWarm(ChunkCoord position) {
        const auto found = warmArchives.find(position);
        if (found != warmArchives.end()) {
            warmBytes -= WarmCost(found->second);
            warmArchives.erase(found);
            EraseOne(warmOrder, position);
        }
    }

    void RetainWarm(Archive&& archive) {
        ForgetWarm(archive.position);
        if (archive.items.capacity() != 0) { return; }
        // Fix outer allocations before budgeting; erasure retains this storage.
        warmArchives.reserve(maxWarmEntries);
        warmOrder.reserve(maxWarmEntries);
        const auto cost = WarmCost(archive);
        Require(WarmStorageBytes() <= maxWarmBytes);
        const auto valueBudget = maxWarmBytes - WarmStorageBytes();
        if (cost > valueBudget) { return; }
        while (!warmArchives.empty() && (warmArchives.size() >= maxWarmEntries
            || cost > valueBudget - warmBytes)) {
            ForgetWarm(warmOrder.front());
        }
        warmBytes += cost;
        const auto position = archive.position;
        Require(warmArchives.try_emplace(position, eastl::move(archive)).second);
        warmOrder.push_back(position);
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
        if (location.kind != LocationKind::item && location.installation.value) {
            return eastl::unexpected(WorldError::invalid_request);
        }
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
            if (!location.installation.value && !ItemContainer(content.GetItem(parent->type))) {
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
            return ItemLocation::Inside(location.item, location.installation);
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

    void Generate(Chunk& value) {
        Require(generationVersion == 1 || generationVersion == 2
            || generationVersion == world_generation_version);
        // Version 3 retains version 2's surface raster. Older worlds acquire
        // the layered extension only outside their original surface layer.
        if (generationVersion != 1 || value.position.y != 0) {
            const auto region = RegionOf({value.position.x * chunk_size, value.position.z * chunk_size});
            if (!generationPlan || generationPlan->region != region) {
                generationPlan = GenerateRegionPlan(content, seed, region);
            }
            value.tiles = GenerateChunk(content, seed, *generationPlan, value.position);
            return;
        }
        Require(generationVersion == 1);
        // Keep version 1 byte-for-byte for unexplored chunks in existing saves.
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
                    !demo && hash % 13U == 0 ? wall : floor, 0};
            }
        }
    }

    Archive Capture(const Chunk& value, Vector<ItemHandle>* capturedItems = nullptr) const {
        Archive saved{value.position, value.tiles, {}, {}, {}};
        Vector<ItemHandle> pending = value.ground_items;
        for (ActorHandle handle : value.actors) {
            const Actor* actor = Live(actors, handle);
            Require(actor != nullptr);
            saved.actors.push_back({actor->id, actor->position, actor->player, actor->turns,
                actor->speed, actor->remainingTicks, actor->parked, actor->carryCapacityGrams, actor->light});
            for (ItemHandle item : actor->inventory) {
                pending.push_back(item);
            }
        }
        for (ObjectHandle handle : value.objects) {
            const InteractiveObject* object = Live(objects, handle);
            Require(object != nullptr);
            saved.objects.push_back({object->id, object->position, object->enabled, object->updates,
                object->updateInterval, object->remainingTicks, object->kind, object->facing,
                object->broken, object->light});
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
            saved.items.push_back({item->id, item->type, item->location.kind,
                item->location.position, owner, item->location.installation, itemSystem.Capture(pending[index])});
            for (ItemHandle child : item->contents) {
                pending.push_back(child);
            }
        }
        if (capturedItems) {
            *capturedItems = eastl::move(pending);
        }
        return saved;
    }

    void Unload(const Chunk& value) {
        Vector<ItemHandle> pending;
        Archive saved = Capture(value, &pending);
        // First capture all values; then retire every runtime key. Do not reset
        // the central slot maps, even when the last chunk is unloaded.
        for (ItemHandle handle : pending) {
            Unregister(handle);
            items.erase(dod::slot_map64<Item>::key{handle.value});
        }
        for (ActorHandle handle : value.actors) {
            actors.erase(dod::slot_map64<Actor>::key{handle.value});
        }
        for (ObjectHandle handle : value.objects) {
            objects.erase(dod::slot_map64<InteractiveObject>::key{handle.value});
        }
        ForgetWarm(value.position);
        Require(archives.try_emplace(value.position, eastl::move(saved)).second);
        MarkDirty(value.position);
    }

    void Restore(Chunk& value, const Archive& saved) {
        value.tiles = saved.tiles;
        for (const SavedActor& actor : saved.actors) {
            const auto key = actors.emplace(Actor{actor.id, actor.position, actor.player, actor.turns,
                {}, actor.speed, actor.remainingTicks, actor.parked, actor.carryCapacityGrams, actor.light});
            value.actors.push_back({key.raw});
            if (actor.player) { TrackPlayer(actor.id, actor.position); }
        }
        for (const SavedObject& object : saved.objects) {
            const auto key = objects.emplace(InteractiveObject{
                object.id, object.position, object.enabled, object.updates, {},
                object.updateInterval, object.remainingTicks, object.kind, object.facing,
                object.broken, object.light});
            value.objects.push_back({key.raw});
        }
        // Parents precede descendants, so every persistent owner is already live.
        for (const SavedItem& item : saved.items) {
            ItemLocation location;
            switch (item.location) {
            case LocationKind::ground: location = ItemLocation::Ground(item.position); break;
            case LocationKind::actor: location = ItemLocation::Carried(FindActor(item.owner)); break;
            case LocationKind::item: location = ItemLocation::Inside(FindItem(item.owner), item.installation); break;
            case LocationKind::object: location = ItemLocation::Stored(FindObject(item.owner)); break;
            }
            Require(Destination(location).has_value());
            Item restored(item.id, item.type, location);
            Require(itemSystem.Initialize(restored, &item.state).has_value());
            const auto key = items.emplace(eastl::move(restored));
            Collection(location).push_back({key.raw});
        }
    }

    SaveResult<void> ValidateSession(const SessionState& session, SaveDatabase* source,
        Diagnostics* diagnostics) const {
        if (session.pacing.ticksPerSecond == 0 || !ValidPosition(session.playerPosition)
            || session.activeRadius < 0
            || session.residentRadius < session.activeRadius
            || session.residentRadius > 8
            || !ValidRegion({ChunkOf(session.playerPosition),
                session.circleResidency ? session.activeRadius : session.residentRadius})
            || (session.circleResidency
                && !ValidCircle({session.playerPosition, session.circleResidency->radiusTiles}))) {
            return SaveFailure(SaveError::InvalidData, "Invalid session pacing or region", diagnostics);
        }
        if (session.players.size() > 32) {
            return SaveFailure(SaveError::InvalidData, "Player roster exceeds limit", diagnostics);
        }
        Vector<SavedPlayer> roster = session.players;
        if (session.player != 0) {
            if (session.player >= next_id) {
                return SaveFailure(SaveError::InvalidData, "Invalid session player ID", diagnostics);
            }
            if (roster.empty()) {
                roster.push_back({session.player, session.playerPosition, {}, 0});
            }
        }
        Vector<PersistentId> playerIds;
        Vector<std::uint64_t> tokens;
        for (const SavedPlayer& entry : roster) {
            if (entry.actor == 0 || entry.actor >= next_id || !ValidPosition(entry.position)
                || (!entry.name.empty() && entry.name.size() > 32)
                || (!session.players.empty() && (entry.name.empty()
                    || std::memchr(entry.name.data(), '\0', entry.name.size())))
                || (session.player != 0 && entry.actor == session.player
                    && (entry.position != session.playerPosition || entry.reconnectToken != 0))
                || (entry.actor != session.player && entry.reconnectToken == 0)) {
                return SaveFailure(SaveError::InvalidData, "Invalid player roster entry", diagnostics);
            }
            playerIds.push_back(entry.actor);
            if (entry.reconnectToken != 0) { tokens.push_back(entry.reconnectToken); }
            const Actor* live = Live(actors, FindActor(entry.actor));
            if (live) {
                if (live->player && live->position == entry.position
                    && (entry.actor != session.player || !live->parked)) { continue; }
                return SaveFailure(SaveError::InvalidData, "Roster player does not match world", diagnostics);
            }
            const ChunkCoord position = ChunkOf(entry.position);
            if (GetChunk(position)) {
                return SaveFailure(SaveError::InvalidData, "Roster player missing from loaded chunk", diagnostics);
            }
            const Archive* archive = nullptr;
            const auto pending = archives.find(position);
            if (pending != archives.end()) { archive = &pending->second; }
            if (!archive) {
                const auto warm = warmArchives.find(position);
                if (warm != warmArchives.end()) { archive = &warm->second; }
            }
            eastl::optional<Archive> disk;
            if (!archive && source) {
                auto saved = source->ReadChunk(position, next_id, diagnostics);
                if (!saved) { return eastl::unexpected(saved.error()); }
                if (*saved) { disk = eastl::move(**saved); archive = &*disk; }
            }
            if (!archive || eastl::none_of(archive->actors.begin(), archive->actors.end(),
                    [&entry, &session](const SavedActor& actor) {
                        return actor.id == entry.actor && actor.player && actor.position == entry.position
                            && (entry.actor != session.player || !actor.parked);
                    })) {
                return SaveFailure(SaveError::InvalidData, "Roster player missing from world", diagnostics);
            }
        }
        if (!UniqueIds(playerIds)) {
            return SaveFailure(SaveError::InvalidData, "Duplicate player IDs", diagnostics);
        }
        eastl::sort(tokens.begin(), tokens.end());
        if (eastl::adjacent_find(tokens.begin(), tokens.end()) != tokens.end()) {
            return SaveFailure(SaveError::InvalidData, "Duplicate reconnect tokens", diagnostics);
        }
        if (session.player != 0 && !session.players.empty()
            && eastl::find(playerIds.begin(), playerIds.end(), session.player) == playerIds.end()) {
            return SaveFailure(SaveError::InvalidData, "Host missing from roster", diagnostics);
        }
        // Disk-only roster actors were checked above; all other known players
        // must also be represented when the session has an authoritative host.
        if (session.player != 0 || !session.players.empty()) {
            for (const auto& player : playerPositions) {
                if (eastl::find(playerIds.begin(), playerIds.end(), player.first) == playerIds.end()) {
                    return SaveFailure(SaveError::InvalidData, "Player missing from roster", diagnostics);
                }
            }
        }
        return {};
    }

    SaveResult<void> Checkpoint(SaveDatabase& target, const SessionState& session,
        Diagnostics* diagnostics) {
        if (auto valid = ValidateSession(session, &target, diagnostics); !valid) {
            return valid;
        }
        Vector<Archive> snapshots;
        snapshots.reserve(eastl::min(dirtyChunks.size(), ChunkCount()));
        for (ChunkCoord position : dirtyChunks) {
            if (const Chunk* chunk = GetChunk(position)) {
                snapshots.push_back(Capture(*chunk));
            }
        }
        // Both live source/destination rows and unloaded archives are replaced in
        // one transaction, so moves never leave an old persistent owner on disk.
        if (auto result = target.Begin(diagnostics); !result) {
            return result;
        }
        for (const auto& entry : archives) {
            const Archive& archive = entry.second;
            if (auto result = target.WriteChunk(archive, diagnostics); !result) {
                target.Rollback();
                return result;
            }
        }
        for (const Archive& snapshot : snapshots) {
            if (auto result = target.WriteChunk(snapshot, diagnostics); !result) {
                target.Rollback();
                return result;
            }
        }
        const SaveSnapshot saveSnapshot{seed, generationVersion, next_id, session, skyLight, CaptureMemories()};
        if (auto result = target.WriteSnapshot(saveSnapshot, diagnostics); !result) {
            target.Rollback();
            return result;
        }
        if (auto result = target.Commit(diagnostics); !result) {
            target.Rollback();
            return result;
        }
        dirtyChunks.clear();
        for (const Archive& snapshot : snapshots) { ForgetWarm(snapshot.position); }
        // Admission order is unspecified; warmOrder records actual FIFO admission.
        for (auto& entry : archives) { RetainWarm(eastl::move(entry.second)); }
        // clear() alone retains the potentially large outer archive allocation.
        ArchiveMap().swap(archives);
        return {};
    }
};

World::World(const ContentCatalog& content, std::uint32_t seed)
    : impl_(eastl::make_unique<Impl>(*this, content, seed)) {}
World::~World() = default;

WorldResult<LoadRequest> World::RequestChunks(ChunkRegion region, LoadMode mode) {
    if (!ValidRegion(region)) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    if (!ValidMode(mode)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    region.center.y = 0;
    impl_->demandDirty = true;
    return LoadRequest{impl_->requests.emplace(Request{region, mode, eastl::nullopt}).raw};
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
    region.center.y = 0;
    if (!request->circle && request->region.center == region.center && request->region.radius == region.radius
        && request->mode == mode) { return {}; }
    impl_->demandDirty = true;
    *request = {region, mode, eastl::nullopt};
    return {};
}

WorldResult<LoadRequest> World::RequestCircle(TileCircleRegion region, LoadMode mode) {
    if (!ValidCircle(region)) { return eastl::unexpected(WorldError::invalid_region); }
    if (!ValidMode(mode)) { return eastl::unexpected(WorldError::invalid_request); }
    region.center.y = 0;
    impl_->demandDirty = true;
    return LoadRequest{impl_->requests.emplace(Request{{}, mode, region}).raw};
}

WorldResult<void> World::UpdateCircleRequest(LoadRequest handle, TileCircleRegion region, LoadMode mode) {
    Request* request = Live(impl_->requests, handle);
    if (!request) { return eastl::unexpected(WorldError::invalid_request); }
    if (!ValidCircle(region)) { return eastl::unexpected(WorldError::invalid_region); }
    if (!ValidMode(mode)) { return eastl::unexpected(WorldError::invalid_request); }
    region.center.y = 0;
    if (request->circle && request->circle->center == region.center
        && request->circle->radiusTiles == region.radiusTiles && request->mode == mode) { return {}; }
    *request = {{}, mode, region};
    impl_->demandDirty = true;
    return {};
}

WorldResult<void> World::ReleaseRequest(LoadRequest handle) {
    if (!Live(impl_->requests, handle)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    impl_->requests.erase(dod::slot_map64<Request>::key{handle.value});
    impl_->demandDirty = true;
    return {};
}

SaveResult<void> World::ReconcileChunks(Diagnostics* diagnostics) {
    if (!impl_->demandDirty) { return {}; }
    Impl::DemandMap demand;
    for (const Request& request : impl_->requests) {
        if (request.circle) {
            const auto circle = *request.circle;
            const auto first = ChunkOf({circle.center.x - circle.radiusTiles, circle.center.z - circle.radiusTiles});
            const auto last = ChunkOf({circle.center.x + circle.radiusTiles, circle.center.z + circle.radiusTiles});
            for (int x = first.x; x <= last.x; ++x) {
                for (int z = first.z; z <= last.z; ++z) {
                    const int nearestX = eastl::clamp(circle.center.x, x * chunk_size, (x + 1) * chunk_size - 1);
                    const int nearestZ = eastl::clamp(circle.center.z, z * chunk_size, (z + 1) * chunk_size - 1);
                    const int dx = nearestX - circle.center.x;
                    const int dz = nearestZ - circle.center.z;
                    if (dx * dx + dz * dz <= circle.radiusTiles * circle.radiusTiles) {
                        bool& active = demand[{x, z}];
                        active = active || request.mode == LoadMode::active;
                    }
                }
            }
            continue;
        }
        const ChunkRegion region = request.region;
        for (int x = region.center.x - region.radius; x <= region.center.x + region.radius; ++x) {
            for (int z = region.center.z - region.radius; z <= region.center.z + region.radius; ++z) {
                bool& active = demand[{x, z}];
                active = active || request.mode == LoadMode::active;
            }
        }
    }
    bool sameColumns = demand.size() == impl_->columns_.size();
    bool sameActivity = sameColumns;
    for (const auto& entry : demand) {
        const auto current = impl_->columns_.find(entry.first);
        if (current == impl_->columns_.end()) { sameColumns = false; sameActivity = false; }
        else if (current->second.layers.front().active != entry.second) { sameActivity = false; }
    }
    if (sameActivity) {
        impl_->demandDirty = false;
        return {};
    }
    ++impl_->perceptionStats.demandReconciliations;
    if (sameColumns) {
        // Skip archive work, but retain the structural path's gravity boundary:
        // newly active disk-restored entities may still be suspended in air.
        Impl::GravityBatch batch(*this);
        for (auto& column : impl_->columns_) {
            const bool active = demand.at(column.first);
            for (Chunk& chunk : column.second.layers) {
                if (chunk.active != active) { ++impl_->mapRevision; ++impl_->revision; }
                chunk.active = active;
            }
        }
        SettleGravity();
        impl_->demandDirty = false;
        return {};
    }
    Impl::ArchiveMap staged;
    if (impl_->database) {
        for (const auto& entry : demand) {
            for (int y = world_min_y; y <= world_max_y; ++y) {
                const ChunkCoord position{entry.first.x, entry.first.z, y};
                if (impl_->GetChunk(position)
                    || impl_->archives.contains(position) || impl_->warmArchives.contains(position)) {
                    continue;
                }
                auto saved = impl_->database->ReadChunk(position, impl_->next_id, diagnostics);
                if (!saved) {
                    if (diagnostics) { diagnostics->Add("Reconciling world chunks"); }
                    return eastl::unexpected(saved.error());
                }
                if (*saved) {
                    // ReadChunk fully validates external terrain, ownership,
                    // state, local IDs and bounds before returning a value.
                    if ((**saved).position != position) {
                        return SaveFailure(SaveError::InvalidData, "Invalid disk chunk archive", diagnostics);
                    }
                    Require(staged.try_emplace(position, eastl::move(**saved)).second);
                }
            }
        }
    }
    if (!staged.empty()) {
        Vector<PersistentId> ids;
        ids.reserve(impl_->actors.size() + impl_->items.size() + impl_->objects.size());
        // Include live entities about to unload. Trusted Capture values need
        // only ID collection here, not another tile/item-tree validation.
        for (const Actor& actor : impl_->actors) { ids.push_back(actor.id); }
        for (const Item& item : impl_->items) { ids.push_back(item.id); }
        for (const InteractiveObject& object : impl_->objects) { ids.push_back(object.id); }
        for (const auto& entry : impl_->archives) { CollectArchiveIds(entry.second, ids); }
        for (const auto& entry : impl_->warmArchives) { CollectArchiveIds(entry.second, ids); }
        for (const auto& entry : staged) { CollectArchiveIds(entry.second, ids); }
        if (!UniqueIds(ids)) {
            return SaveFailure(SaveError::InvalidData, "Duplicate persistent ID in incoming chunks", diagnostics);
        }
    }
    // No live state, activity flags, archives, or keys change before this point.
    Impl::GravityBatch batch(*this);
    for (auto current = impl_->columns_.begin(); current != impl_->columns_.end();) {
        const auto wanted = demand.find(current->first);
        const bool remove = wanted == demand.end();
        for (Chunk& chunk : current->second.layers) {
            const auto position = chunk.position;
            if (remove) {
                impl_->lightingDirty = true;
                impl_->MarkGeometryDirty(position);
                impl_->lightingInputs.erase(position);
                chunk.active = false;
                impl_->Unload(chunk);
            } else {
                if (chunk.active != wanted->second) { ++impl_->mapRevision; ++impl_->revision; }
                chunk.active = wanted->second;
            }
        }
        // Erasure relocates the last dense value; revisit the returned iterator.
        if (remove) {
            current = impl_->columns_.erase(current);
            ++impl_->perceptionStats.columnsUnloaded;
        } else { ++current; }
    }
    const auto incomingArchive = [&](ChunkCoord position) -> const Archive* {
        const auto pending = impl_->archives.find(position);
        if (pending != impl_->archives.end()) { return &pending->second; }
        const auto warm = impl_->warmArchives.find(position);
        if (warm != impl_->warmArchives.end()) { return &warm->second; }
        const auto disk = staged.find(position);
        if (disk != staged.end()) { return &disk->second; }
        return nullptr;
    };
    Vector<ChunkCoord> incoming;
    for (const auto& entry : demand) {
        if (impl_->columns_.contains(entry.first)) { continue; }
        impl_->InstallColumn(entry.first, entry.second);
        for (int y = world_min_y; y <= world_max_y; ++y) {
            const ChunkCoord position{entry.first.x, entry.first.z, y};
            incoming.push_back(position);
            impl_->MarkLightingDirty(position);
            impl_->MarkGeometryDirty(position);
            ++impl_->mapRevision;
            ++impl_->revision;
            Chunk* chunk = impl_->GetChunk(position);
            if (const Archive* saved = incomingArchive(position)) { chunk->tiles = saved->tiles; }
            else { impl_->Generate(*chunk); impl_->MarkDirty(position); }
        }
    }
    // Publish complete terrain/residency before restoration or mutation callbacks.
    Vector<ItemHandle> restoredItems;
    for (ChunkCoord position : incoming) {
        if (const Archive* saved = incomingArchive(position)) {
            impl_->Restore(*impl_->GetChunk(position), *saved);
            for (const SavedItem& item : saved->items) { restoredItems.push_back(impl_->FindItem(item.id)); }
        }
    }
    for (ChunkCoord position : incoming) {
        impl_->archives.erase(position);
        impl_->ForgetWarm(position);
    }
    for (ItemHandle handle : restoredItems) { impl_->itemSystem.Attach(handle); }
    SettleGravity();
    impl_->demandDirty = false;
    return {};
}

SaveResult<void> World::CreateSave(const char* path, const SessionState& session,
    Diagnostics* diagnostics) {
    if (impl_->database) {
        return SaveFailure(SaveError::InvalidState, "World already has an attached save", diagnostics);
    }
    if (!CheckInvariants()) {
        return SaveFailure(SaveError::InvalidData, "Invalid world state", diagnostics);
    }
    if (auto result = impl_->ValidateSession(session, nullptr, diagnostics); !result) {
        return result;
    }
    auto database = eastl::make_unique<SaveDatabase>(impl_->content);
    if (auto result = database->Create(path, diagnostics); !result) {
        return result;
    }
    if (auto result = impl_->Checkpoint(*database, session, diagnostics); !result) {
        return result;
    }
    impl_->database = eastl::move(database);
    return {};
}

SaveResult<SessionState> World::LoadSave(const char* path, Diagnostics* diagnostics) {
    if (impl_->database || !impl_->requests.empty() || !impl_->actors.empty()
        || !impl_->items.empty() || !impl_->objects.empty() || impl_->ChunkCount() != 0
        || !impl_->archives.empty() || !impl_->warmArchives.empty() || !impl_->dirtyChunks.empty()
        || !impl_->playerPositions.empty() || impl_->next_id != 1) {
        SaveFailure(SaveError::InvalidState, "Loading requires a pristine world", diagnostics);
        return eastl::unexpected(SaveError::InvalidState);
    }
    auto database = eastl::make_unique<SaveDatabase>(impl_->content);
    auto snapshot = database->Open(path, diagnostics);
    if (!snapshot) {
        if (diagnostics) { diagnostics->Add("Loading world save"); }
        return eastl::unexpected(snapshot.error());
    }
    const bool supported = snapshot->generationVersion == 1
        || snapshot->generationVersion == 2
        || snapshot->generationVersion == world_generation_version;
    if (!supported || snapshot->nextId == 0) {
        const SaveError error = !supported
            ? SaveError::UnsupportedVersion : SaveError::InvalidData;
        SaveFailure(error, "Invalid world generation version or next persistent ID", diagnostics);
        return eastl::unexpected(error);
    }
    if (!ValidMemories(impl_->content, snapshot->memories, snapshot->nextId)) {
        SaveFailure(SaveError::InvalidData, "Invalid player memory snapshot", diagnostics);
        return eastl::unexpected(SaveError::InvalidData);
    }
    // Validate on temporary state; even seed/ID installation waits for success.
    Impl candidate(*this, impl_->content, snapshot->seed);
    candidate.next_id = snapshot->nextId;
    if (auto result = candidate.ValidateSession(snapshot->session, database.get(), diagnostics); !result) {
        return eastl::unexpected(result.error());
    }
    impl_->seed = snapshot->seed;
    impl_->generationVersion = snapshot->generationVersion;
    impl_->generationPlan.reset();
    impl_->next_id = snapshot->nextId;
    impl_->skyLight = snapshot->skyLight;
    for (PlayerMemory& player : snapshot->memories) {
        auto& chunks = impl_->memories.try_emplace(player.player).first->second;
        for (MemoryChunk& chunk : player.chunks) {
            const auto position = chunk.position;
            chunks.try_emplace(position, eastl::move(chunk));
        }
    }
    impl_->lightingDirty = true;
    ++impl_->revision;
    ++impl_->mapRevision;
    for (const SavedPlayer& player : snapshot->session.players) {
        impl_->TrackPlayer(player.actor, player.position);
    }
    if (snapshot->session.player != 0 && snapshot->session.players.empty()) {
        impl_->TrackPlayer(snapshot->session.player, snapshot->session.playerPosition);
    }
    impl_->database = eastl::move(database);
    return snapshot->session;
}

SaveResult<void> World::Save(const SessionState& session, Diagnostics* diagnostics) {
    if (!impl_->database) {
        return SaveFailure(SaveError::NotOpen, "World has no attached save", diagnostics);
    }
    if (!CheckInvariants()) {
        return SaveFailure(SaveError::InvalidData, "Invalid world state", diagnostics);
    }
    return impl_->Checkpoint(*impl_->database, session, diagnostics);
}

bool World::HasSave() const { return impl_->database != nullptr; }
const ContentCatalog& World::GetContent() const { return impl_->content; }
std::uint32_t World::GetSeed() const { return impl_->seed; }
std::uint32_t World::GetGenerationVersion() const { return impl_->generationVersion; }

std::uint64_t World::GetRevision() const { return impl_->revision; }
std::uint64_t World::GetMapRevision() const { return impl_->mapRevision; }
VisionProfile World::GetVisionProfile() const { return impl_->vision; }
WorldResult<void> World::SetVisionProfile(VisionProfile vision) {
    if (!ValidVisionProfile(vision)) { return eastl::unexpected(WorldError::invalid_request); }
    if (impl_->vision != vision) { impl_->vision = vision; ++impl_->revision; ++impl_->mapRevision; }
    return {};
}
PerceptionStats World::GetPerceptionStats() const { return impl_->perceptionStats; }
void World::ResetPerceptionStats() const { impl_->perceptionStats = {}; }
void World::RecordPointLos(bool voxel, std::uint64_t count) const {
    if (voxel) { impl_->perceptionStats.traversedVoxels += count; }
    else { impl_->perceptionStats.pointLosCalls += count; }
}
bool World::CanPlayerSee(ActorHandle observer, TileCoord target) const {
    if (!ValidPosition(target)) { return false; }
    const auto* mask = GetPlayerVisibility(observer);
    return mask && mask->Contains(target);
}
const VisibilityMask* World::GetPlayerVisibility(ActorHandle observer) const {
    const Actor* actor = GetActor(observer);
    if (!actor || !actor->player || actor->parked || !IsActive(actor->position)) { return nullptr; }
    return &impl_->PerceptionFor(*this, *actor).visible;
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
    result.reserve(impl_->ChunkCount());
    for (const auto& column : impl_->columns_) {
        for (const Chunk& chunk : column.second.layers) { result.push_back(chunk.position); }
    }
    return result;
}

std::size_t World::ArchivedChunkCount() const { return impl_->archives.size(); }
const Actor* World::GetActor(ActorHandle handle) const { return Live(impl_->actors, handle); }
const Item* World::GetItem(ItemHandle handle) const { return Live(impl_->items, handle); }
const InteractiveObject* World::GetObject(ObjectHandle handle) const { return Live(impl_->objects, handle); }
ActorHandle World::FindActor(PersistentId id) const { return impl_->FindActor(id); }
eastl::optional<TileCoord> World::FindPlayerPosition(PersistentId id) const {
    return impl_->PlayerPosition(id);
}
ItemHandle World::FindItem(PersistentId id) const { return impl_->FindItem(id); }
ObjectHandle World::FindObject(PersistentId id) const { return impl_->FindObject(id); }

ActorHandle World::ActorAt(TileCoord position) const {
    const Chunk* value = ValidPosition(position) ? GetChunk(ChunkOf(position)) : nullptr;
    if (value) {
        for (ActorHandle handle : value->actors) {
            const Actor* candidate = GetActor(handle);
            Require(candidate != nullptr);
            if (!candidate->parked && candidate->position == position) {
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

void World::SettleGravity() {
    for (const auto& column : impl_->columns_) {
        if (!column.second.layers.front().active) { continue; }
        for (const Chunk& chunk : column.second.layers) {
            for (ActorHandle handle : chunk.actors) {
                const Actor* actor = GetActor(handle);
                if (!actor->parked) { impl_->QueueGravity(actor->position); }
            }
            for (ItemHandle handle : chunk.ground_items) {
                impl_->QueueGravity(GetItem(handle)->location.position);
            }
        }
    }
    FlushGravity();
}

void World::FlushGravity() {
    if (impl_->gravityDepth != 0 || impl_->settlingGravity
        || impl_->itemSystem.callbackDepth_ != 0 || impl_->itemSystem.draining_) { return; }
    impl_->settlingGravity = true;
    struct Falling {
        int y;
        PersistentId id;
        ActorHandle actor;
        ItemHandle item;
    };
    while (!impl_->gravityColumns.empty()) {
        Vector<TileCoord> columns;
        columns.swap(impl_->gravityColumns);
        Vector<Falling> falling;
        for (TileCoord column : columns) {
            for (int y = world_min_y; y <= world_max_y; ++y) {
                column.y = y;
                const Chunk* chunk = GetChunk(ChunkOf(column));
                if (!chunk) { continue; }
                for (ActorHandle handle : chunk->actors) {
                    const Actor* actor = GetActor(handle);
                    if (!actor->parked && actor->position == column) {
                        falling.push_back({y, actor->id, handle, {}});
                    }
                }
                for (ItemHandle handle : chunk->ground_items) {
                    const Item* item = GetItem(handle);
                    if (item->location.position == column) {
                        falling.push_back({y, item->id, {}, handle});
                    }
                }
            }
        }
        eastl::sort(falling.begin(), falling.end(), [](const Falling& left, const Falling& right) {
            return left.y < right.y || (left.y == right.y && left.id < right.id);
        });
        for (const Falling& entry : falling) {
            TileCoord position;
            if (entry.actor.value) {
                const Actor* actor = GetActor(entry.actor);
                if (!actor || actor->parked) { continue; }
                position = actor->position;
            } else {
                const Item* item = GetItem(entry.item);
                if (!item || item->location.kind != LocationKind::ground) { continue; }
                position = item->location.position;
            }
            TileCoord destination = position;
            while (destination.y > world_min_y) {
                const Tile* current = GetTile(destination);
                if (!current || !impl_->content.GetTerrain(current->terrain).openSpace) { break; }
                TileCoord below = destination;
                --below.y;
                const Tile* tile = GetTile(below);
                if (!tile || !Passable(impl_->content, tile->terrain) || ActorAt(below).value) { break; }
                destination = below;
            }
            if (destination == position) { continue; }
            // Public mutation helpers retain indexes, player tracking, dirty
            // chunks and item-tree notifications. The guard prevents recursion.
            if (entry.actor.value) {
                Require(MoveActor(entry.actor, destination).has_value());
            } else {
                Require(TransferItem(entry.item, ItemLocation::Ground(destination)).has_value());
            }
        }
    }
    impl_->settlingGravity = false;
}

WorldResult<void> World::SetTile(TileCoord position, Tile value) {
    Impl::GravityBatch batch(*this);
    if (auto result = impl_->Available(position); !result) {
        return result;
    }
    if (!impl_->content.TryGetTerrain(value.terrain)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    const Tile previous = *GetTile(position);
    if (previous == value) { return {}; }
    const auto& before = impl_->content.GetTerrain(previous.terrain);
    const auto& after = impl_->content.GetTerrain(value.terrain);
    if (before.blocksSight != after.blocksSight || before.openSpace != after.openSpace
        || before.stairs != after.stairs) { impl_->MarkGeometryDirty(ChunkOf(position)); }
    if (before.blocksSight != after.blocksSight || before.openSpace != after.openSpace
        || before.stairs != after.stairs || before.light != after.light) { impl_->MarkLightingDirty(ChunkOf(position)); }
    impl_->GetChunk(ChunkOf(position))->tiles[TileIndex(position)] = value;
    impl_->MarkDirty(ChunkOf(position), true, true);
    impl_->QueueGravity(position);
    return {};
}

WorldResult<ActorHandle> World::CreateActor(TileCoord position, bool player) {
    Impl::GravityBatch batch(*this);
    if (auto result = impl_->Available(position); !result) {
        return eastl::unexpected(result.error());
    }
    if (!Passable(impl_->content, GetTile(position)->terrain)) {
        return eastl::unexpected(WorldError::blocked);
    }
    if (ActorAt(position).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    const auto key = impl_->actors.emplace(Actor{impl_->IssueId(), position, player, 0, {}});
    const ActorHandle handle{key.raw};
    impl_->GetChunk(ChunkOf(position))->actors.push_back(handle);
    if (player) { impl_->TrackPlayer(GetActor(handle)->id, position); }
    impl_->MarkDirty(ChunkOf(position));
    impl_->QueueGravity(position);
    return handle;
}

WorldResult<void> World::MoveActor(ActorHandle handle, TileCoord destination) {
    Impl::GravityBatch batch(*this);
    Actor* value = Live(impl_->actors, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    if (auto result = impl_->Available(destination); !result) {
        return result;
    }
    if (value->position == destination) {
        impl_->QueueGravity(destination);
        return {};
    }
    if (!Passable(impl_->content, GetTile(destination)->terrain)) {
        return eastl::unexpected(WorldError::blocked);
    }
    if (!value->parked && ActorAt(destination).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    impl_->QueueGravity(value->position);
    impl_->QueueGravity(destination);
    const ChunkCoord previous = ChunkOf(value->position);
    const ChunkCoord next = ChunkOf(destination);
    if (previous != next) {
        EraseOne(impl_->GetChunk(previous)->actors, handle);
        impl_->GetChunk(next)->actors.push_back(handle);
    }
    value->position = destination;
    if (value->light.intensity && !value->parked) {
        impl_->MarkLightingDirty(previous);
        if (next != previous) { impl_->MarkLightingDirty(next); }
    }
    if (value->player) { impl_->TrackPlayer(value->id, destination); }
    impl_->MarkDirty(previous);
    impl_->MarkDirty(next);
    return {};
}

WorldResult<void> World::SetActorParked(ActorHandle handle, bool parked) {
    Impl::GravityBatch batch(*this);
    Actor* value = Live(impl_->actors, handle);
    if (!value) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!value->player) { return eastl::unexpected(WorldError::invalid_request); }
    if (value->parked == parked) { return {}; }
    if (!parked) {
        if (!Passable(impl_->content, GetTile(value->position)->terrain)) {
            return eastl::unexpected(WorldError::blocked);
        }
        if (ActorAt(value->position).value != 0) {
            return eastl::unexpected(WorldError::occupied);
        }
    }
    value->parked = parked;
    if (value->light.intensity) { impl_->MarkLightingDirty(ChunkOf(value->position)); }
    impl_->MarkDirty(ChunkOf(value->position));
    impl_->QueueGravity(value->position);
    return {};
}

WorldResult<void> World::ParkActors(eastl::span<const ActorHandle> actors) {
    for (ActorHandle handle : actors) {
        const Actor* value = GetActor(handle);
        if (!value) { return eastl::unexpected(WorldError::invalid_handle); }
        if (!value->player) { return eastl::unexpected(WorldError::invalid_request); }
    }
    Impl::GravityBatch batch(*this);
    for (ActorHandle handle : actors) {
        Require(SetActorParked(handle, true).has_value());
    }
    return {};
}

WorldResult<void> World::DestroyActor(ActorHandle handle) {
    Impl::GravityBatch batch(*this);
    Actor* value = Live(impl_->actors, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const TileCoord position = value->position;
    impl_->QueueGravity(position);
    while (!value->inventory.empty()) {
        Require(TransferItem(value->inventory.back(), ItemLocation::Ground(position)).has_value());
        value = Live(impl_->actors, handle);
        Require(value != nullptr);
    }
    EraseOne(impl_->GetChunk(ChunkOf(position))->actors, handle);
    if (value->player) { impl_->ForgetPlayer(value->id); }
    if (value->light.intensity && !value->parked) { impl_->MarkLightingDirty(ChunkOf(position)); }
    impl_->actors.erase(dod::slot_map64<Actor>::key{handle.value});
    impl_->MarkDirty(ChunkOf(position));
    return {};
}

WorldResult<ItemHandle> World::CreateItem(ItemTypeId type, ItemLocation location) {
    Impl::GravityBatch batch(*this);
    const ItemDefinition* definition = impl_->content.TryGetItem(type);
    if (!definition) { return eastl::unexpected(WorldError::invalid_request); }
    const auto destination = impl_->Destination(location);
    if (!destination) {
        return eastl::unexpected(destination.error());
    }
    if (auto fits = impl_->Fits(type, *destination); !fits) { return eastl::unexpected(fits.error()); }
    Item created(impl_->IssueId(), type, *destination);
    Require(impl_->itemSystem.Initialize(created, nullptr).has_value());
    const auto key = impl_->items.emplace(eastl::move(created));
    const ItemHandle handle{key.raw};
    impl_->Collection(*destination).push_back(handle);
    impl_->itemSystem.Attach(handle);
    impl_->MarkDirty(impl_->RootChunk(*destination), destination->kind == LocationKind::ground);
    const auto root = impl_->Root(*destination);
    if (root.kind == LocationKind::ground) { impl_->QueueGravity(root.position); }
    if (destination->kind == LocationKind::item) { impl_->itemSystem.Notify({ItemChangeKind::ContentsChanged, destination->item, destination->installation}); }
    return handle;
}

WorldResult<void> World::TransferItem(ItemHandle handle, ItemLocation location) {
    Impl::GravityBatch batch(*this);
    Item* value = Live(impl_->items, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const auto destination = impl_->Destination(location, handle);
    if (!destination) {
        return eastl::unexpected(destination.error());
    }
    if (value->location == *destination) {
        const auto root = impl_->Root(*destination);
        if (root.kind == LocationKind::ground) { impl_->QueueGravity(root.position); }
        return {};
    }
    if (auto fits = impl_->Fits(value->type, *destination, handle); !fits) { return fits; }
    impl_->MarkDirty(impl_->RootChunk(value->location), value->location.kind == LocationKind::ground);
    impl_->MarkDirty(impl_->RootChunk(*destination), destination->kind == LocationKind::ground);
    const ItemLocation old = value->location;
    const auto oldRoot = impl_->Root(old);
    const auto newRoot = impl_->Root(*destination);
    if (oldRoot.kind == LocationKind::ground) { impl_->QueueGravity(oldRoot.position); }
    if (newRoot.kind == LocationKind::ground) { impl_->QueueGravity(newRoot.position); }
    Vector<ItemHandle> moved{handle};
    for (std::size_t i = 0; i < moved.size(); ++i) {
        for (ItemHandle child : GetItem(moved[i])->contents) { moved.push_back(child); }
    }
    EraseOne(impl_->Collection(value->location), handle);
    impl_->Collection(*destination).push_back(handle);
    value->location = *destination;
    impl_->itemSystem.BeginCallback();
    for (ItemHandle current : moved) { impl_->itemSystem.Notify({ItemChangeKind::Moved, current, {}}); }
    if (old.kind == LocationKind::item) { impl_->itemSystem.Notify({ItemChangeKind::ContentsChanged, old.item, old.installation}); }
    if (destination->kind == LocationKind::item) { impl_->itemSystem.Notify({ItemChangeKind::ContentsChanged, destination->item, destination->installation}); }
    impl_->itemSystem.EndCallback();
    return {};
}

WorldResult<void> World::DestroyItem(ItemHandle handle) {
    Impl::GravityBatch batch(*this);
    const Item* value = GetItem(handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    impl_->MarkDirty(impl_->RootChunk(value->location), value->location.kind == LocationKind::ground);
    const ItemLocation old = value->location;
    const auto root = impl_->Root(old);
    if (root.kind == LocationKind::ground) { impl_->QueueGravity(root.position); }
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
        impl_->Unregister(current);
        impl_->items.erase(dod::slot_map64<Item>::key{current.value});
    }
    if (old.kind == LocationKind::item) { impl_->itemSystem.Notify({ItemChangeKind::ContentsChanged, old.item, old.installation}); }
    return {};
}

WorldResult<void> World::SetActorCarryCapacity(ActorHandle handle, std::uint64_t grams) {
    Actor* actor = Live(impl_->actors, handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    if (impl_->CarriedMass(handle) > grams) { return eastl::unexpected(WorldError::capacity_exceeded); }
    actor->carryCapacityGrams = grams;
    impl_->MarkDirty(ChunkOf(actor->position), false);
    return {};
}

std::uint64_t World::GetItemMass(ItemHandle handle) const {
    return GetItem(handle) ? impl_->Mass(handle) : 0;
}

std::uint64_t World::GetCarriedMass(ActorHandle handle) const {
    return GetActor(handle) ? impl_->CarriedMass(handle) : 0;
}

std::uint64_t World::GetContainerUsedVolume(ItemHandle handle) const {
    return GetItem(handle) ? impl_->Volume(handle) : 0;
}

bool World::CanAccessItem(ActorHandle actorHandle, ItemHandle itemHandle) const {
    const Actor* actor = GetActor(actorHandle);
    const Item* item = GetItem(itemHandle);
    if (!actor || actor->parked || !IsActive(actor->position) || !item) { return false; }
    const ItemLocation root = impl_->Root(item->location);
    return (root.kind == LocationKind::actor && root.actor == actorHandle)
        || (root.kind == LocationKind::ground && root.position == actor->position);
}

Vector<ItemActionInfo> World::QueryItemActions(ActorHandle actor, ItemHandle handle) const {
    return impl_->itemSystem.QueryActions(actor, handle);
}

ItemUseResult World::UseItem(ActorHandle actor, ItemHandle handle, const ItemUseRequest& request) {
    Impl::GravityBatch batch(*this);
    return impl_->itemSystem.Use(actor, handle, request);
}

WorldResult<void> World::SetItemEnergy(ItemHandle handle, ItemCapabilityId capability, std::uint32_t energy) {
    Impl::GravityBatch batch(*this);
    return impl_->itemSystem.SetEnergy({handle, capability}, energy);
}

eastl::optional<Tick> World::NextItemEventIn() const {
    return impl_->itemSystem.NextEventIn();
}

void World::ProcessItemEvents() {
    Impl::GravityBatch batch(*this);
    impl_->itemSystem.ProcessEvents();
}

std::size_t World::ActiveItemCount() const { return impl_->itemSystem.ScheduledItemCount(); }

Item::Item() = default;
Item::Item(PersistentId identity, ItemTypeId itemType, ItemLocation itemLocation)
    : id(identity), type(itemType), location(itemLocation) {}
Item::~Item() = default;
Item::Item(Item&&) noexcept = default;
Item& Item::operator=(Item&&) noexcept = default;
Item* World::MutableItem(ItemHandle item) { return Live(impl_->items, item); }
void World::MarkItemDirty(ItemHandle handle) {
    if (const auto* item = GetItem(handle)) { impl_->MarkDirty(impl_->RootChunk(item->location), false); }
}
bool World::ItemEligible(ItemHandle item) const { return impl_->Eligible(item); }
WorldResult<void> World::CheckItemTransfer(ItemHandle handle, ItemLocation location) const {
    const auto* item = GetItem(handle);
    if (!item) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto destination = impl_->Destination(location, handle);
    if (!destination) { return eastl::unexpected(destination.error()); }
    if (item->location == *destination) { return {}; }
    return impl_->Fits(item->type, *destination, handle);
}
DataObject World::CaptureItemState(ItemHandle item) const { return impl_->itemSystem.Capture(item); }
eastl::optional<ItemEnergyView> World::GetItemEnergy(ItemHandle item, ItemCapabilityId capability) const {
    return impl_->itemSystem.Energy(item, capability);
}
ItemPresentation World::DescribeItem(ActorHandle actor, ItemHandle item) const { return impl_->itemSystem.Describe(actor, item); }

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
    impl_->MarkDirty(ChunkOf(position), true, true);
    return handle;
}

WorldResult<void> World::DestroyObject(ObjectHandle handle) {
    Impl::GravityBatch batch(*this);
    InteractiveObject* value = Live(impl_->objects, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const TileCoord position = value->position;
    impl_->QueueGravity(position);
    while (!value->contents.empty()) {
        Require(TransferItem(value->contents.back(), ItemLocation::Ground(position)).has_value());
        value = Live(impl_->objects, handle);
        Require(value != nullptr);
    }
    EraseOne(impl_->GetChunk(ChunkOf(position))->objects, handle);
    if (value->enabled && value->light.intensity) { impl_->MarkLightingDirty(ChunkOf(position)); }
    impl_->objects.erase(dod::slot_map64<InteractiveObject>::key{handle.value});
    impl_->MarkDirty(ChunkOf(position), true, true);
    return {};
}

WorldResult<void> World::SetObjectEnabled(ObjectHandle handle, bool enabled) {
    InteractiveObject* value = Live(impl_->objects, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    if (value->enabled == enabled) { return {}; }
    if (value->light.intensity) { impl_->MarkLightingDirty(ChunkOf(value->position)); }
    value->enabled = enabled;
    impl_->MarkDirty(ChunkOf(value->position), true, true);
    return {};
}

ObjectAppearance DescribeObject(const InteractiveObject& object) {
    return {object.kind, object.facing, object.enabled, object.broken};
}

WorldResult<void> World::SetObjectAppearance(ObjectHandle handle, ObjectKind kind,
    ObjectFacing facing, bool broken) {
    InteractiveObject* object = Live(impl_->objects, handle);
    if (!object) { return eastl::unexpected(WorldError::invalid_handle); }
    if (kind == ObjectKind::None || !ValidObjectAppearance({kind, facing, object->enabled, broken})) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    object->kind = kind;
    object->facing = facing;
    object->broken = broken;
    impl_->MarkDirty(ChunkOf(object->position), true, true);
    return {};
}

WorldResult<void> World::SetObjectLight(ObjectHandle handle, LightEmission light) {
    InteractiveObject* object = Live(impl_->objects, handle);
    if (!object) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!ValidLightEmission(light)) { return eastl::unexpected(WorldError::invalid_request); }
    if (object->light == light) { return {}; }
    object->light = light;
    if (object->enabled) { impl_->MarkLightingDirty(ChunkOf(object->position)); }
    impl_->MarkDirty(ChunkOf(object->position));
    return {};
}

WorldResult<void> World::SetActorLight(ActorHandle handle, LightEmission light) {
    Actor* actor = Live(impl_->actors, handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!ValidLightEmission(light)) { return eastl::unexpected(WorldError::invalid_request); }
    if (actor->light == light) { return {}; }
    actor->light = light;
    if (!actor->parked) { impl_->MarkLightingDirty(ChunkOf(actor->position)); }
    impl_->MarkDirty(ChunkOf(actor->position));
    return {};
}

void World::SetSkyLight(std::uint8_t level) {
    if (impl_->skyLight != level) {
        impl_->skyLight = level;
        impl_->lightingDirty = true;
        ++impl_->mapRevision;
        ++impl_->revision;
    }
}
std::uint8_t World::GetSkyLight() const { return impl_->skyLight; }
std::uint64_t World::GetLightingInputRevision(ChunkCoord position) const {
    const auto found = impl_->lightingInputs.find(position);
    return found != impl_->lightingInputs.end() ? found->second : 0;
}
std::uint8_t World::GetLightLevel(TileCoord position) const {
    if (!ValidPosition(position)) { return 0; }
    if (impl_->lightingDirty) {
        impl_->lighting.Prepare(*this);
        for (auto& observer : impl_->observers) {
            for (const auto changed : impl_->lighting.ChangedChunks()) {
                // Opaque endpoints sample adjacent space, including their upper face.
                if (Impl::TouchesPerception(changed, observer.geometry, 1)) {
                    observer.lightingRevision = 0;
                    break;
                }
            }
        }
        ++impl_->perceptionStats.lightingRebuilds;
        ++impl_->lightingRevision;
        impl_->lightingDirty = false;
    }
    if (!GetChunk(ChunkOf(position))) { return 0; }
    impl_->EnsureLighting(*this, ChunkOf(position));
    return impl_->lighting.Get(position);
}

eastl::span<const std::uint8_t> World::GetChunkLightLevels(ChunkCoord position) const {
    if (!GetChunk(position)) { return {}; }
    // Edge chunks may start outside the horizontal coordinate contract.
    GetLightLevel({eastl::clamp(position.x * chunk_size, -world_coordinate_limit, world_coordinate_limit),
        eastl::clamp(position.z * chunk_size, -world_coordinate_limit, world_coordinate_limit), position.y});
    return impl_->lighting.GetChunkLevels(position);
}

std::uint8_t World::GetIncidentLightLevel(TileCoord position, int face) const {
    if (!ValidPosition(position) || face < 0 || face >= 6) { return 0; }
    GetLightLevel(position);
    if (!GetChunk(ChunkOf(position))) { return 0; }
    return impl_->lighting.GetIncident(position, face);
}

Vector<PlayerMemory> World::GetPlayerMemories() const { return impl_->CaptureMemories(); }

RememberedTile World::GetRememberedTile(PersistentId player, TileCoord position) const {
    if (!ValidPosition(position)) { return {}; }
    const auto memory = impl_->memories.find(player);
    if (memory == impl_->memories.end()) { return {}; }
    const auto coord = ChunkOf(position);
    const auto chunk = memory->second.find(coord);
    if (chunk == memory->second.end()) { return {}; }
    const int index = TileIndex(position);
    RememberedTile result{chunk->second.terrain[index], {}};
    const auto object = eastl::lower_bound(chunk->second.objects.begin(), chunk->second.objects.end(), index,
        [](const RememberedObject& value, int key) { return value.tileIndex < key; });
    if (object != chunk->second.objects.end() && object->tileIndex == index) { result.object = object->appearance; }
    return result;
}

void World::UpdatePlayerObservations() {
    for (ActorHandle handle : ActiveActors()) {
        const Actor& actor = *GetActor(handle);
        if (!actor.player || actor.parked) { continue; }
        auto& cache = impl_->PerceptionFor(*this, actor);
        if (cache.observedFilterRevision == cache.filterRevision
            && cache.observedAppearanceRevision == impl_->appearanceRevision) { continue; }
        ++impl_->perceptionStats.observationScans;
        // No further player-map insertion while borrowing this player's chunk map.
        auto& memory = impl_->memories.try_emplace(actor.id).first->second;
        const bool visibilityChanged = cache.observedFilterRevision != cache.filterRevision;
        for (const auto& footprint : cache.footprint) {
            const auto coord = footprint.position;
            bool appearanceChanged = false;
            if (cache.observedAppearanceRevision != impl_->appearanceRevision) {
                const auto changed = impl_->appearanceChanges.find(coord);
                appearanceChanged = changed != impl_->appearanceChanges.end()
                    && changed->second > cache.observedAppearanceRevision;
            }
            if (!visibilityChanged && !appearanceChanged) { continue; }
            const auto previous = cache.learnedFootprint.find(coord);
            const auto* old = previous != cache.learnedFootprint.end() ? &previous->second : nullptr;
            MemoryChunk* chunk = nullptr;
            const Chunk* liveChunk = nullptr;
            const auto observe = [&](int index) {
                ++impl_->perceptionStats.memoryTilesExamined;
                if (!liveChunk) { liveChunk = impl_->GetChunk(coord); Require(liveChunk != nullptr); }
                const TileCoord p{coord.x * chunk_size + index % chunk_size,
                    coord.z * chunk_size + index / chunk_size, coord.y};
                if (!chunk) {
                    chunk = &memory.try_emplace(coord, MemoryChunk{coord, {}, {}}).first->second;
                }
                bool tileChanged = chunk->terrain[index] != liveChunk->tiles[index].terrain;
                if (tileChanged) { chunk->terrain[index] = liveChunk->tiles[index].terrain; }
                auto object = eastl::lower_bound(chunk->objects.begin(), chunk->objects.end(), index,
                    [](const RememberedObject& value, int key) { return value.tileIndex < key; });
                const InteractiveObject* current = nullptr;
                for (ObjectHandle objectHandle : liveChunk->objects) {
                    const auto* candidate = GetObject(objectHandle);
                    if (candidate->position == p) { current = candidate; break; }
                }
                if (current) {
                    const RememberedObject observed{static_cast<std::uint8_t>(index), DescribeObject(*current)};
                    if (object != chunk->objects.end() && object->tileIndex == index) {
                        if (*object != observed) { *object = observed; tileChanged = true; }
                    } else { chunk->objects.insert(object, observed); tileChanged = true; }
                } else if (object != chunk->objects.end() && object->tileIndex == index) {
                    chunk->objects.erase(object);
                    tileChanged = true;
                }
                if (tileChanged) {
                    ++impl_->perceptionStats.memoryTilesChanged;
                    ++impl_->revision;
                    ++impl_->mapRevision;
                }
            };
            for (int word = 0; word < 4; ++word) {
                auto changed = footprint.bits[word];
                if (!appearanceChanged && old) { changed &= ~(*old)[word]; }
                while (changed) {
                    const int bit = std::countr_zero(changed);
                    observe(word * 64 + bit);
                    changed &= changed - 1;
                }
            }
        }
        cache.learnedFootprint.clear();
        for (const auto& footprint : cache.footprint) {
            cache.learnedFootprint.try_emplace(footprint.position, footprint.bits);
        }
        if (memory.empty()) { impl_->memories.erase(actor.id); }
        cache.observedFilterRevision = cache.filterRevision;
        cache.observedAppearanceRevision = impl_->appearanceRevision;
    }
}

WorldResult<void> World::SetActorSpeed(ActorHandle handle, std::uint32_t speed) {
    Actor* value = Live(impl_->actors, handle);
    if (!value) { return eastl::unexpected(WorldError::invalid_handle); }
    if (speed == 0) { return eastl::unexpected(WorldError::invalid_request); }
    value->speed = speed;
    impl_->MarkDirty(ChunkOf(value->position), false);
    return {};
}

WorldResult<void> World::ScheduleActorAction(ActorHandle handle, std::uint32_t baseCost) {
    Actor* value = Live(impl_->actors, handle);
    if (!value) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!IsActive(value->position) || value->parked) { return eastl::unexpected(WorldError::chunk_unavailable); }
    if (baseCost == 0 || value->remainingTicks != 0) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    Require(value->speed != 0);
    Require(value->turns != eastl::numeric_limits<std::uint64_t>::max());
    value->remainingTicks = ActionTicks(baseCost, value->speed);
    ++value->turns;
    impl_->MarkDirty(ChunkOf(value->position), false);
    return {};
}

WorldResult<void> World::SetObjectUpdateInterval(ObjectHandle handle, std::uint32_t interval) {
    InteractiveObject* value = Live(impl_->objects, handle);
    if (!value) { return eastl::unexpected(WorldError::invalid_handle); }
    if (interval == 0) { return eastl::unexpected(WorldError::invalid_request); }
    value->updateInterval = interval;
    value->remainingTicks = interval;
    impl_->MarkDirty(ChunkOf(value->position), false);
    return {};
}

void World::ElapseTicks(Tick elapsed) {
    Impl::GravityBatch batch(*this);
    if (elapsed == 0) { return; }
    impl_->itemSystem.Elapse(elapsed);
    for (const auto& column : impl_->columns_) {
        if (!column.second.layers.front().active) { continue; }
        for (const Chunk& chunk : column.second.layers) {
            bool modified = false;
            for (ActorHandle handle : chunk.actors) {
                Actor* actor = Live(impl_->actors, handle);
                Require(actor != nullptr);
                if (!actor->parked && actor->remainingTicks != 0) {
                    actor->remainingTicks -= eastl::min(actor->remainingTicks, elapsed);
                    modified = true;
                }
            }
            for (ObjectHandle handle : chunk.objects) {
                InteractiveObject* object = Live(impl_->objects, handle);
                Require(object != nullptr);
                if (object->enabled && object->remainingTicks != 0) {
                    object->remainingTicks -= eastl::min(object->remainingTicks, elapsed);
                    modified = true;
                }
            }
            if (modified) { impl_->MarkDirty(chunk.position, false); }
        }
    }
}

Vector<ActorHandle> World::ActiveActors() const {
    Vector<ActorHandle> result;
    for (const auto& column : impl_->columns_) {
        if (!column.second.layers.front().active) { continue; }
        for (const Chunk& value : column.second.layers) {
            for (ActorHandle handle : value.actors) {
                if (!GetActor(handle)->parked) { result.push_back(handle); }
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
    for (const auto& column : impl_->columns_) {
        if (!column.second.layers.front().active) { continue; }
        for (const Chunk& value : column.second.layers) {
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
    if (!IsActive(value->position) || value->parked) {
        return eastl::unexpected(WorldError::chunk_unavailable);
    }
    Require(value->turns != eastl::numeric_limits<std::uint64_t>::max());
    ++value->turns;
    impl_->MarkDirty(ChunkOf(value->position), false);
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
        value->remainingTicks = value->updateInterval;
        impl_->MarkDirty(ChunkOf(value->position), false);
    }
    return {};
}

bool World::CheckInvariants() const {
    const Impl& state = *impl_;
    for (const auto& player : state.memories) {
        if (player.first == 0 || player.first >= state.next_id || player.second.empty()) { return false; }
        for (const auto& chunk : player.second) {
            if (chunk.first != chunk.second.position || !ValidMemoryChunk(state.content, chunk.second)) { return false; }
        }
    }
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
    for (const auto& column : state.columns_) {
        for (int y = world_min_y; y <= world_max_y; ++y) {
            const Chunk& value = column.second.layers[y - world_min_y];
            if (value.position != ChunkCoord{column.first.x, column.first.z, y}
                || value.active != column.second.layers.front().active
                || !ValidRegion({value.position, 0}) || !ValidChunk(value.position)) {
                return false;
            }
            for (const Tile& tile : value.tiles) {
                if (!state.content.TryGetTerrain(tile.terrain)) { return false; }
            }
            Vector<TileCoord> actor_positions;
            Vector<TileCoord> object_positions;
            for (ActorHandle handle : value.actors) {
                const Actor* actor = this->GetActor(handle);
                if (!actor || !ValidPosition(actor->position) || ChunkOf(actor->position) != value.position
                    || (actor->parked && !actor->player)
                    || (!actor->parked && Occurrences(actor_positions, actor->position) != 0)) {
                    return false;
                }
                if (!actor->parked) { actor_positions.push_back(actor->position); }
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
    }
    for (const auto& entry : state.actors.items()) {
        const Actor& value = entry.second.get();
        const ActorHandle handle{entry.first.raw};
        if (!ValidLightEmission(value.light) || value.speed == 0 || !add_id(value.id) || Occurrences(indexed_actors, handle) != 1
            || (value.player && state.PlayerPosition(value.id) != value.position)) {
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
        if (!ValidLightEmission(value.light) || value.kind == ObjectKind::None
            || !ValidObjectAppearance(DescribeObject(value))
            || value.updateInterval == 0 || value.remainingTicks > value.updateInterval
            || !add_id(value.id) || Occurrences(indexed_objects, handle) != 1) {
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
        const auto* definition = state.content.TryGetItem(value.type);
        if (!add_id(value.id) || !definition) {
            return false;
        }
        for (ItemHandle child : value.contents) {
            const Item* item = this->GetItem(child);
            if (!item || item->location.kind != LocationKind::item || item->location.item != handle
                || !state.Fits(item->type, item->location, child)) {
                return false;
            }
            owned_items.push_back(child);
        }
        // Walk to a valid spatial root, bounding the walk to detect cycles.
        const Item* cursor = &value;
        std::size_t depth = 0;
        while (cursor->location.kind == LocationKind::item) {
            if (++depth >= 64) {
                return false;
            }
            cursor = GetItem(cursor->location.item);
            if (!cursor) {
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
    for (const auto& entry : state.archives) {
        const Archive& saved = entry.second;
        if (entry.first != saved.position) { return false; }
        if (!ValidRegion({saved.position, 0}) || !ValidChunk(saved.position) || state.GetChunk(saved.position)
            || Occurrences(archived_positions, saved.position) != 0) {
            return false;
        }
        archived_positions.push_back(saved.position);
        for (const Tile& tile : saved.tiles) {
            if (!state.content.TryGetTerrain(tile.terrain)) { return false; }
        }
        Vector<TileCoord> actor_positions;
        Vector<TileCoord> object_positions;
        for (const SavedActor& actor : saved.actors) {
            if (actor.speed == 0 || !add_id(actor.id) || !ValidPosition(actor.position)
                || ChunkOf(actor.position) != saved.position
                || (actor.parked && !actor.player)
                || (actor.player && state.PlayerPosition(actor.id) != actor.position)
                || (!actor.parked && Occurrences(actor_positions, actor.position) != 0)) {
                return false;
            }
            if (!actor.parked) { actor_positions.push_back(actor.position); }
        }
        for (const SavedObject& object : saved.objects) {
            if (object.updateInterval == 0 || object.remainingTicks > object.updateInterval
                || !add_id(object.id) || !ValidPosition(object.position)
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
                            return owner.id == value.owner;
                        })) {
                    return false;
                }
                break;
            default: return false;
            }
        }
    }
    for (const auto& player : state.playerPositions) {
        if (player.first == 0 || player.first >= state.next_id || !ValidPosition(player.second)) {
            return false;
        }
    }
    for (const auto& column : state.columns_) {
        for (const Chunk& chunk : column.second.layers) {
            Vector<PersistentId> checkedIds;
            if (!ValidateArchive(state.content, state.Capture(chunk), state.next_id, checkedIds)) { return false; }
        }
    }
    for (const auto& entry : state.archives) {
        const Archive& archive = entry.second;
        Vector<PersistentId> checkedIds;
        if (!ValidateArchive(state.content, archive, state.next_id, checkedIds)) { return false; }
    }
    if (state.warmArchives.size() > Impl::maxWarmEntries
        || state.warmOrder.size() != state.warmArchives.size()) { return false; }
    std::size_t warmBytes = 0;
    for (const auto& entry : state.warmArchives) {
        const Archive& archive = entry.second;
        if (entry.first != archive.position || Occurrences(state.warmOrder, entry.first) != 1) { return false; }
        if (archive.items.capacity() != 0 || state.GetChunk(archive.position)
            || Occurrences(archived_positions, archive.position) != 0
            || !ValidateArchive(state.content, archive, state.next_id, ids)) { return false; }
        archived_positions.push_back(archive.position);
        warmBytes += Impl::WarmCost(archive);
        for (const SavedActor& actor : archive.actors) {
            if (actor.player && state.PlayerPosition(actor.id) != actor.position) { return false; }
        }
    }
    if (warmBytes != state.warmBytes
        || warmBytes + state.WarmStorageBytes() > Impl::maxWarmBytes) { return false; }
    if (!state.itemSystem.CheckInvariants()) { return false; }
    eastl::sort(ids.begin(), ids.end());
    return eastl::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

}
