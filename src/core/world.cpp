#include <cstdlib>
#include <cstring>
#include <bit>
#include <initializer_list>

#include "core/world.h"
#include "actors/movement.h"
#include "core/lighting.h"
#include "core/perception.h"
#include "core/slot_map.h"
#include "core/save_database.h"
#include "combat/combat_system.h"
#include "combat/combat_persistence.h"
#include "combat/combat_runtime.h"
#include "items/item_system.h"
#include "items/item_snapshot.h"
#include "crafting/crafting_system.h"
#include "skills/learning_system.h"
#include "worldgen/worldgen.h"
#include "objects/storage_access.h"
#include "power/power_system.h"

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
    LoadReason reason = LoadReason::Other;
};

bool RequestContains(const Request& request, ColumnCoord column) {
    if (!request.circle) {
        return std::abs(column.x - request.region.center.x) <= request.region.radius
            && std::abs(column.z - request.region.center.z) <= request.region.radius;
    }
    const auto circle = *request.circle;
    const auto nearestX = eastl::clamp(circle.center.x, column.x * chunk_size, (column.x + 1) * chunk_size - 1);
    const auto nearestZ = eastl::clamp(circle.center.z, column.z * chunk_size, (column.z + 1) * chunk_size - 1);
    const auto dx = std::int64_t{nearestX} - circle.center.x;
    const auto dz = std::int64_t{nearestZ} - circle.center.z;
    return dx * dx + dz * dz <= std::int64_t{circle.radiusTiles} * circle.radiusTiles;
}

bool CarriedPowerItem(const World& world, ActorHandle actor, ItemHandle handle) {
    const auto* item = world.GetItem(handle);
    if (!item || world.IsReservedItem(handle)) { return false; }
    while (item->location.kind == LocationKind::item) {
        if (item->location.installation.value) { return false; }
        handle = item->location.item;
        item = world.GetItem(handle);
        if (!item || world.IsReservedItem(handle)) { return false; }
    }
    return item->location.kind == LocationKind::actor && item->location.actor == actor;
}

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

eastl::optional<PowerDeviceKind> PowerRole(const ObjectDefinition* definition) {
    if (!definition) { return eastl::nullopt; }
    switch (definition->behavior) {
    case ObjectBehaviorKind::Generator: return PowerDeviceKind::Generator;
    case ObjectBehaviorKind::Solar: return PowerDeviceKind::Solar;
    case ObjectBehaviorKind::GridBattery: return PowerDeviceKind::Battery;
    case ObjectBehaviorKind::CraftingMachine:
        if (definition->machine.gridPowered) { return PowerDeviceKind::Machine; }
        break;
    case ObjectBehaviorKind::None:
    case ObjectBehaviorKind::Forge: break;
    }
    return eastl::nullopt;
}

bool MatchesPowerObject(const PowerSystem& power, const ContentCatalog& content, PersistentId id,
    TileCoord position, ObjectTypeId type, bool enabled, bool broken, const ObjectStateView& view, Tick now) {
    const auto kind = PowerRole(content.TryGetObject(type));
    const auto device = power.FindDevice(id);
    if (kind.has_value() != device.has_value()) { return false; }
    if (!kind) { return true; }
    const bool hold = enabled && !broken && view.runRequested && *kind != PowerDeviceKind::Battery
        && (*kind == PowerDeviceKind::Generator || *kind == PowerDeviceKind::Solar || view.hasJob);
    if (device->position != position || device->kind != *kind || device->workshopHold != hold) { return false; }
    if ((*kind == PowerDeviceKind::Generator || *kind == PowerDeviceKind::Solar)
        && (view.generatorIntervalStart > now || (device->chargeProcessed
            && (!view.generatorIntervalInitialized || device->chargeInterval > view.generatorIntervalStart)))) { return false; }
    if (*kind == PowerDeviceKind::Solar && view.generatorOutputRemaining
        > SolarIntervalAllowance(view.generatorIntervalStart, content.GetObject(type).solar.peakOutputPerTick)) { return false; }
    return true;
}

// Behavior state was already validated by ReadChunk/ValidateArchive. Read only
// index facts; no behavior construction or DataObject allocation is needed here.
bool ValidPowerArchive(const PowerSystem& power, const ContentCatalog& content, const Archive& saved, Tick now) {
    std::size_t found = 0;
    for (const auto& object : saved.objects) {
        ObjectStateView view;
        view.runRequested = object.state.GetBoolean("run_requested").value_or(false);
        view.hasJob = object.state.GetBoolean("has_job").value_or(false);
        view.generatorIntervalStart = object.state.GetUnsigned("generator_interval_start").value_or(0);
        view.generatorIntervalInitialized = object.state.GetBoolean("generator_interval_initialized").value_or(false);
        const auto* definition = content.TryGetObject(object.type);
        if (definition && definition->behavior == ObjectBehaviorKind::Solar) {
            view.generatorIntervalStart = object.state.GetUnsigned("interval_start").value_or(0);
            view.generatorIntervalInitialized = object.state.GetBoolean("interval_initialized").value_or(false);
            view.generatorOutputRemaining = object.state.GetUnsigned("output_remaining").value_or(0);
        }
        if (!MatchesPowerObject(power, content, object.id, object.position, object.type,
                object.enabled, object.broken, view, now)) { return false; }
        if (power.FindDevice(object.id)) { ++found; }
    }
    std::size_t expected = 0;
    for (const auto& device : power.Devices()) { if (ChunkOf(device.position) == saved.position) { ++expected; } }
    return found == expected;
}

// Relative archive timers are untrusted, including resident-only snapshots.
// Validate without adding: Restore/activation must not reach CheckedTickAdd
// with a duration that cannot be represented at the current World clock.
SaveResult<void> ValidateArchiveDeadlines(const Archive& saved, const ContentCatalog& content,
    Tick tick, Diagnostics* diagnostics) {
    const Tick room = eastl::numeric_limits<Tick>::max() - tick;
    for (const SavedActor& actor : saved.actors) {
        const auto controllerContent = ValidateAiControllerContent(actor.controller, content, diagnostics);
        if (!controllerContent) {
            return eastl::unexpected(controllerContent.error() == StateError::UnsupportedVersion
                ? SaveError::UnsupportedVersion : SaveError::InvalidData);
        }
        const auto combat = ReadCombatState(actor.combat, content, diagnostics);
        if (!combat) { return eastl::unexpected(SaveError::InvalidData); }
        const auto learning = ReadLearningState(actor.learning, content, diagnostics);
        if (!learning || learning->study.remaining > room) { return eastl::unexpected(SaveError::InvalidData); }
        const Tick recovery = combat->action.recoveryTicks;
        if (actor.remainingTicks > room || recovery > room - actor.remainingTicks) {
            return SaveFailure(SaveError::InvalidData, "Saved actor delay exceeds simulation clock headroom", diagnostics);
        }
    }
    for (const SavedObject& object : saved.objects) {
        if (!object.type.value && object.remainingTicks > room) {
            return SaveFailure(SaveError::InvalidData, "Saved object delay exceeds simulation clock headroom", diagnostics);
        }
    }
    for (const SavedItem& item : saved.items) {
        const auto* definition = content.TryGetItem(item.type);
        if (!definition || !definition->unfinishedCraft) { continue; }
        const auto* payload = item.state.Find("crafting_job");
        if (!payload) { return SaveFailure(SaveError::InvalidData, "Missing saved crafting job", diagnostics); }
        const auto state = ReadCraftingState(*payload);
        if (!state || state->remaining > room) {
            return SaveFailure(SaveError::InvalidData, "Invalid saved crafting delay or simulation clock headroom", diagnostics);
        }
    }
    return {};
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
    for (const auto& item : saved.items) { if (!content.TryGetItem(item.type)) { return false; } }
    for (const SavedActor& actor : saved.actors) {
        if (!actor.monsterType.empty()
            && (actor.player || !content.FindMonster(actor.monsterType).value)) { return false; }
        const auto controller = ReadAiController(actor.controller);
        if (!controller || (actor.player && *controller)) { return false; }
        if (!ValidateAiControllerContent(actor.controller, content)) { return false; }
        const auto combat = ReadCombatState(actor.combat, content);
        if (!combat || !ReadCharacterCombat(actor.characterCombat, content)) { return false; }
        const auto combatVersion = actor.combat.GetUnsigned("version");
        if ((!combatVersion || *combatVersion < 3) && combat->action.kind == ActorActionKind::MeleeWindup
            && combat->action.weapon != actor.wieldedItem) { return false; }
        const auto equipment = ReadEquipment(actor.equipment);
        if (!equipment) { return false; }
        Vector<PersistentId> equipped;
        for (unsigned hand = 0; hand < 2; ++hand) {
            const auto held = equipment->hands[hand];
            if (!held.item) { continue; }
            const auto found = eastl::find_if(saved.items.begin(), saved.items.end(), [&](const SavedItem& item) {
                return item.id == held.item && item.location == LocationKind::actor && item.owner == actor.id;
            });
            if (found == saved.items.end()) { return false; }
            const auto& grips = ItemMelee(content.GetItem(found->type)).grips;
            if (eastl::none_of(grips.begin(), grips.end(), [&](const GripDefinition& grip) { return grip.kind == held.grip; })) { return false; }
            const auto other = equipment->hands[1 - hand];
            if ((held.grip == GripKind::TwoHanded) != (other.item == held.item)) { return false; }
            if (held.grip == GripKind::TwoHanded && other.grip != held.grip) { return false; }
            if (!hand || equipment->hands[0].item != held.item) { equipped.push_back(held.item); }
        }
        for (const auto worn : equipment->worn) {
            if (eastl::find(equipped.begin(), equipped.end(), worn) != equipped.end()) { return false; }
            const auto found = eastl::find_if(saved.items.begin(), saved.items.end(), [&](const SavedItem& item) {
                return item.id == worn && item.location == LocationKind::actor && item.owner == actor.id;
            });
            if (found == saved.items.end() || !content.GetItem(found->type).armor) { return false; }
            const auto& armor = *content.GetItem(found->type).armor;
            for (const auto previous : equipment->worn) {
                if (previous == worn) { break; }
                const auto item = eastl::find_if(saved.items.begin(), saved.items.end(), [&](const SavedItem& value) { return value.id == previous; });
                const auto& other = *content.GetItem(item->type).armor;
                if (armor.layer == other.layer && (armor.rigidRegions & other.rigidRegions)) { return false; }
            }
            equipped.push_back(worn);
        }
        const auto learning = ReadLearningState(actor.learning, content);
        if (!learning || (learning->study.active && (combat->action.kind != ActorActionKind::None
            || actor.remainingTicks != learning->study.remaining))) { return false; }
        if (learning->study.active) {
            for (const auto& item : saved.items) {
                const auto* definition = content.TryGetItem(item.type);
                if (!definition || !definition->unfinishedCraft) { continue; }
                const auto* payload = item.state.Find("crafting_job");
                if (!payload) { return false; }
                const auto job = ReadCraftingState(*payload);
                if (!job || job->worker == actor.id) { return false; }
            }
        }
        if (actor.wieldedItem && eastl::none_of(saved.items.begin(), saved.items.end(), [&actor](const SavedItem& item) {
            return item.id == actor.wieldedItem && item.location == LocationKind::actor && item.owner == actor.id;
        })) { return false; }
        if (!ValidLightEmission(actor.light) || actor.speed == 0 || (actor.parked && !actor.player) || !addId(actor.id)
            || !ValidPosition(actor.position) || ChunkOf(actor.position) != saved.position
            || (!actor.parked && Occurrences(positions, actor.position))) {
            return false;
        }
        if (!actor.parked) { positions.push_back(actor.position); }
    }
    positions.clear();
    for (const SavedObject& object : saved.objects) {
        if (object.type.value) {
            const auto* definition = content.TryGetObject(object.type);
            if (!definition || !ValidateObjectBehaviorState(*definition, object.state)) { return false; }
        } else if (object.state.Type() != DataType::Object || object.state.Size() != 0) { return false; }
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
    if (!ValidateItemSnapshot(content, saved)) { return false; }
    const auto volume = [&](auto&& self, const SavedItem& item) -> std::uint64_t {
        const auto& definition = content.GetItem(item.type);
        if (!definition.unfinishedCraft) { return definition.volumeMl; }
        std::uint64_t result = 0;
        for (const auto& child : saved.items) {
            if (child.location == LocationKind::item && child.owner == item.id && !child.installation.value) {
                const auto amount = self(self, child);
                if (amount > UINT64_MAX - result) { return UINT64_MAX; }
                result += amount;
            }
        }
        return result;
    };
    for (const auto& object : saved.objects) {
        const auto* definition = content.TryGetObject(object.type);
        if (!definition) { continue; }
        if (definition->blocksMovement) {
            for (const auto& actor : saved.actors) {
                if (!actor.parked && actor.position == object.position) { return false; }
            }
        }
        if (!definition->capacityVolumeMl) { continue; }
        std::uint64_t used = 0;
        for (const auto& item : saved.items) {
            if (item.location != LocationKind::object || item.owner != object.id || item.id == object.equipment) { continue; }
            const auto amount = volume(volume, item);
            if (amount > *definition->capacityVolumeMl - used) { return false; }
            used += amount;
        }
    }
    return true;
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
            || !ValidObjectAppearance(object.appearance)
            || (object.appearance.type.value && !content.TryGetObject(object.appearance.type))) { return false; }
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
    World& owner;
    const ContentCatalog& content;
    TerrainId floor;
    TerrainId wall;
    std::uint32_t seed;
    std::uint32_t generationVersion = world_generation_version;
    // A bounded, disposable planning cache; never part of authoritative state.
    eastl::optional<RegionPlan> generationPlan;
    ChunkPlacements generationPlacements;
    Vector<GeneratedBuildingPower> generationPower;
    Vector<BuildingPowerRegistration> pendingPowerBuildings;
    Vector<ChunkCoord> incomingChunks;
    Vector<ChunkCoord> newChunks;
    PersistentId next_id = 1;
    Tick tick = 0;
    dod::slot_map64<Actor> actors;
    ankerl::unordered_dense::map<PersistentId, ActorHandle> actorIds;
    dod::slot_map64<Item> items;
    dod::slot_map64<InteractiveObject> objects;
    dod::slot_map64<Request> requests;
    struct DueActor { PersistentId id; ActorHandle handle; };
    struct DueObject { PersistentId id; ObjectHandle handle; };
    Vector<DueActor> dueActors;
    Vector<DueObject> dueObjects;

    void RefreshSchedule(Actor& actor) {
        const Chunk* chunk = GetChunk(ChunkOf(actor.position));
        actor.actionDeadline.SetRunning(tick, actor.combat.life == ActorLifeState::Alive && chunk && chunk->active && !actor.parked
            && (actor.player || actor.controller_));
        if (actor.actionDeadline.suspended) { craftingSystem.ReleaseWorkerStations(actor.id); }
    }
    void RefreshSchedule(InteractiveObject& object) {
        const Chunk* chunk = GetChunk(ChunkOf(object.position));
        object.updateDeadline.SetRunning(tick, chunk && chunk->active && object.enabled && !object.type.value);
        if (!chunk || !chunk->active || !object.enabled || object.broken) { craftingSystem.ReleaseStation(object.id); }
    }
    void SetChunkActive(Chunk& chunk, bool active) {
        chunk.active = active;
        for (auto handle : chunk.actors) { RefreshSchedule(*Live(actors, handle)); }
        for (auto handle : chunk.objects) { RefreshSchedule(*Live(objects, handle)); }
        if (!active) { craftingSystem.ReleaseUnavailableStations(); }
    }
    SaveResult<void> ValidateActivation(const Chunk& chunk, Diagnostics* diagnostics) const {
        const Tick room = eastl::numeric_limits<Tick>::max() - tick;
        for (ActorHandle handle : chunk.actors) {
            const Actor* actor = Live(actors, handle);
            Require(actor != nullptr);
            if (!actor->parked && actor->learning.study.active && actor->learning.study.remaining > room) {
                return SaveFailure(SaveError::InvalidData, "Suspended study exceeds simulation clock headroom", diagnostics);
            }
            if (actor->actionDeadline.suspended && !actor->parked && (actor->player || actor->controller_)
                && (actor->actionDeadline.suspendedTicks > room
                    || actor->combat.action.recoveryTicks > room - actor->actionDeadline.suspendedTicks)) {
                return SaveFailure(SaveError::InvalidData, "Suspended actor delay exceeds simulation clock headroom", diagnostics);
            }
        }
        for (ObjectHandle handle : chunk.objects) {
            const InteractiveObject* object = Live(objects, handle);
            Require(object != nullptr);
            if (object->updateDeadline.suspended && !object->type.value && object->enabled
                && object->updateDeadline.suspendedTicks > room) {
                return SaveFailure(SaveError::InvalidData, "Suspended object delay exceeds simulation clock headroom", diagnostics);
            }
        }
        return {};
    }
    bool processingNpcs = false;
    bool processingObjects = false;
    bool objectCallback = false;
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
    CraftingSystem craftingSystem;
    LearningSystem learningSystem;
    PowerSystem powerSystem;
    Vector<ColumnCoord> workshopDemand;
    Vector<ColumnCoord> playerPowerDemand;
    Vector<ColumnCoord> powerDemandScratch;
    static PowerBounds ConsumerArea(TileCoord position, int radius) {
        return {{eastl::max(-world_coordinate_limit, position.x - radius),
                    eastl::max(-world_coordinate_limit, position.z - radius), position.y},
                {eastl::min(world_coordinate_limit, position.x + radius),
                    eastl::min(world_coordinate_limit, position.z + radius), position.y}};
    }
    Vector<PowerBounds> powerPlayerScratch;
    void RefreshPowerDemand() {
        powerSystem.CollectWorkshopDemand(powerDemandScratch);
        if (workshopDemand != powerDemandScratch) {
            workshopDemand.swap(powerDemandScratch);
            demandDirty = true;
        }
        powerPlayerScratch.clear();
        for (const auto& actor : actors) {
            if (actor.player && !actor.parked && actor.combat.life == ActorLifeState::Alive) {
                powerPlayerScratch.push_back(ConsumerArea(actor.position, craftingSystem.ConsumerRadius(actor.id)));
            }
        }
        powerSystem.CollectConsumerDemand(powerPlayerScratch, powerDemandScratch);
        if (playerPowerDemand != powerDemandScratch) {
            playerPowerDemand.swap(powerDemandScratch);
            demandDirty = true;
        }
    }
    Vector<ItemHandle> wiringMaterials;
    bool GatherWiringMaterials(ActorHandle actor, ItemTypeId type, std::uint32_t count) {
        wiringMaterials.clear();
        for (const auto& entry : items.items()) {
            const ItemHandle handle{entry.first.raw};
            const auto& item = entry.second.get();
            if (item.type == type && item.contents.empty() && CarriedPowerItem(owner, actor, handle)) {
                wiringMaterials.push_back(handle);
            }
        }
        if (wiringMaterials.size() < count) { return false; }
        eastl::sort(wiringMaterials.begin(), wiringMaterials.end(), [this](auto a, auto b) {
            return owner.GetItem(a)->id < owner.GetItem(b)->id;
        });
        wiringMaterials.resize(count);
        return true;
    }
    struct RecipeCredit { PersistentId actor; CraftingRecipeId recipe; std::uint64_t points; };
    // A failed disk lookup must not lose a completed job's award. Retried before
    // loading/saving; those fallible boundaries expose the original storage error.
    Vector<RecipeCredit> pendingRecipeCredits;
    unsigned craftingMutationDepth = 0;
    CombatSystem combatSystem;
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
        // Solar supply follows geometry, not lighting. All geometry mutations
        // (including overhead objects and residency changes) wake one power pass.
        powerSystem.Invalidate();
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
            int ranges[4];
            for (int i = 0; i < 4; ++i) {
                const int range = VisionRange(vision, static_cast<LightBand>(i), observerLight);
                ranges[i] = range;
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
                        if (!WithinSightRange(dx*dx + dz*dz, ranges[static_cast<int>(ClassifyLight(light))],
                            p.y == actor.position.y)) {
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

    struct ObjectCallbackScope {
        World& world;
        GravityBatch gravity;
        bool active = true;
        explicit ObjectCallbackScope(World& value) : world(value), gravity(value) {
            Require(!world.impl_->objectCallback);
            world.impl_->objectCallback = true;
            world.impl_->itemSystem.BeginCallback();
        }
        void End() {
            Require(active && world.impl_->objectCallback);
            active = false;
            world.impl_->objectCallback = false;
            world.impl_->itemSystem.EndCallback();
        }
        ~ObjectCallbackScope() { if (active) { End(); } }
    };

    // Preflight belongs to the caller. No callbacks observe partial relocation.
    void RelocateObject(World& world, ObjectHandle handle, TileCoord destination) {
        auto* object = Live(objects, handle);
        craftingSystem.ReleaseStation(object->id);
        const ChunkCoord previous = ChunkOf(object->position), next = ChunkOf(destination);
        QueueGravity(object->position);
        QueueGravity(destination);
        if (previous != next) {
            EraseOne(GetChunk(previous)->objects, handle);
            GetChunk(next)->objects.push_back(handle);
        }
        object->position = destination;
        RefreshSchedule(*object);
        powerSystem.SyncObject(handle);
        MarkDirty(previous, true, true);
        MarkDirty(next, true, true);
        MarkGeometryDirty(previous);
        MarkGeometryDirty(next);
        MarkLightingDirty(previous);
        MarkLightingDirty(next);
        Vector<ItemHandle> moved = object->contents;
        for (std::size_t i = 0; i < moved.size(); ++i) {
            const auto* item = world.GetItem(moved[i]);
            moved.insert(moved.end(), item->contents.begin(), item->contents.end());
        }
        for (ItemHandle item : moved) { itemSystem.Notify({ItemChangeKind::Moved, item, {}}); }
    }

    void RelocatePair(World& world, ActorHandle actorHandle, ObjectHandle objectHandle,
        TileCoord actorDestination, TileCoord objectDestination) {
        GravityBatch gravity(world);
        itemSystem.BeginCallback();
        auto* actor = Live(actors, actorHandle);
        const ChunkCoord previous = ChunkOf(actor->position), next = ChunkOf(actorDestination);
        QueueGravity(actor->position);
        QueueGravity(actorDestination);
        VacateActorTile(actorHandle);
        if (previous != next) {
            EraseOne(GetChunk(previous)->actors, actorHandle);
            InsertChunkActor(*GetChunk(next), actorHandle);
        }
        actor->position = actorDestination;
        OccupyActorTile(actorHandle);
        RefreshSchedule(*actor);
        if (actor->player) { TrackPlayer(actor->id, actorDestination); }
        MarkDirty(previous);
        MarkDirty(next);
        if (actor->light.intensity) { MarkLightingDirty(previous); MarkLightingDirty(next); }
        RelocateObject(world, objectHandle, objectDestination);
        itemSystem.EndCallback();
    }

    void QueueGravity(TileCoord position) {
        position.y = 0;
        if (eastl::find(gravityColumns.begin(), gravityColumns.end(), position) == gravityColumns.end()) {
            gravityColumns.push_back(position);
        }
    }

    explicit Impl(World& world, const ContentCatalog& catalog, std::uint32_t value)
        : owner(world), content(catalog), floor(catalog.FindTerrain("floor")),
          wall(catalog.FindTerrain("wall")), seed(value), itemSystem(world), craftingSystem(world), learningSystem(world), powerSystem(world, value), combatSystem(world) {
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
                result = CheckedAdd(result, ExternalVolume(child));
            }
        }
        return result;
    }

    std::uint64_t ExternalVolume(ItemHandle handle) const {
        const auto& definition = content.GetItem(Live(items, handle)->type);
        return definition.unfinishedCraft ? Volume(handle) : definition.volumeMl;
    }

    std::uint64_t ObjectVolume(ObjectHandle handle) const {
        std::uint64_t result = 0;
        for (ItemHandle child : Live(objects, handle)->contents) {
            if (child == Live(objects, handle)->equipment) { continue; }
            result = CheckedAdd(result, ExternalVolume(child));
        }
        return result;
    }

    bool ObjectOutputsFit(ObjectHandle handle, ItemTypeId type, std::uint32_t count) const {
        const auto* definition = content.TryGetObject(Live(objects, handle)->type);
        if (!definition || !definition->capacityVolumeMl) { return true; }
        const auto used = ObjectVolume(handle);
        const auto volume = std::uint64_t{content.GetItem(type).volumeMl} * count;
        return used <= *definition->capacityVolumeMl && volume <= *definition->capacityVolumeMl - used;
    }

    eastl::optional<Tick> ObjectEventDelay(ObjectHandle handle) const {
        const auto* object = Live(objects, handle);
        if (!object->enabled || !GetChunk(ChunkOf(object->position))->active) { return eastl::nullopt; }
        const auto delay = object->behavior_ ? object->behavior_->NextEventIn()
            : object->type.value ? eastl::optional<Tick>{} : eastl::optional<Tick>{object->updateDeadline.Remaining(tick)};
        const auto* definition = content.TryGetObject(object->type);
        if (delay && *delay == 0 && definition && definition->behavior == ObjectBehaviorKind::CraftingMachine
            && !ObjectOutputsFit(handle, definition->machine.output, definition->machine.outputCount)) {
            return eastl::nullopt;
        }
        return delay;
    }

    // Only unfinished crafts have contents-dependent external volume. Project a
    // transfer through those wrappers without mutating either ownership tree.
    std::uint64_t ProjectedVolume(ItemHandle handle, ItemHandle moving,
        ItemLocation destination, std::uint64_t insertedVolume) const {
        const auto* item = Live(items, handle);
        const auto& definition = content.GetItem(item->type);
        if (!definition.unfinishedCraft) { return definition.volumeMl; }
        std::uint64_t result = 0;
        for (ItemHandle child : item->contents) {
            if (child != moving && !Live(items, child)->location.installation.value) {
                result = CheckedAdd(result, ProjectedVolume(child, moving, destination, insertedVolume));
            }
        }
        if (destination.kind == LocationKind::item && destination.item == handle
            && !destination.installation.value) { result = CheckedAdd(result, insertedVolume); }
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
                    || (moving.value ? ExternalVolume(moving) : definition.volumeMl) > container->capacityVolumeMl - used) {
                    return eastl::unexpected(WorldError::capacity_exceeded);
                }
            }
        }
        const auto root = Root(location);
        if (root.kind == LocationKind::object) {
            const auto* object = Live(objects, root.object);
            const auto* objectDefinition = content.TryGetObject(object->type);
            if (objectDefinition && objectDefinition->capacityVolumeMl && (!moving.value || moving != object->equipment)) {
                const auto inserted = moving.value ? ExternalVolume(moving)
                    : (definition.unfinishedCraft ? 0 : definition.volumeMl);
                std::uint64_t used = 0;
                for (ItemHandle child : object->contents) {
                    if (child != moving && child != object->equipment) {
                        used = CheckedAdd(used, ProjectedVolume(child, moving, location, inserted));
                    }
                }
                if (location.kind == LocationKind::object) { used = CheckedAdd(used, inserted); }
                if (used > *objectDefinition->capacityVolumeMl) {
                    return eastl::unexpected(WorldError::capacity_exceeded);
                }
            }
        }
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
        // Eligible snapshots have no allocated DataObject trees. Account for
        // retained entity-vector capacity even when a vector is empty.
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
        // Recursive DataObject allocations are opaque. Keep only snapshots
        // without actors (including their owned identity strings and controller
        // envelopes) or typed behavior payloads.
        if (archive.items.capacity() != 0 || !archive.actors.empty()
            || eastl::any_of(archive.objects.begin(), archive.objects.end(),
                [](const SavedObject& object) { return object.type.value != 0; })) { return; }
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
        const auto found = actorIds.find(id);
        return found == actorIds.end() ? ActorHandle{} : found->second;
    }

    void InsertChunkActor(Chunk& chunk, ActorHandle handle) {
        const Actor* actor = Live(actors, handle);
        Require(actor != nullptr);
        const auto found = eastl::lower_bound(chunk.actors.begin(), chunk.actors.end(), actor->id,
            [this](ActorHandle candidate, PersistentId id) {
                return Live(actors, candidate)->id < id;
            });
        chunk.actors.insert(found, handle);
    }

    void OccupyActorTile(ActorHandle handle) {
        const Actor* actor = Live(actors, handle);
        Require(actor != nullptr);
        if (actor->parked || actor->combat.life == ActorLifeState::Dead) { return; }
        ActorHandle& occupant = GetChunk(ChunkOf(actor->position))->actorOccupancy[TileIndex(actor->position)];
        Require(occupant == ActorHandle{});
        occupant = handle;
    }

    void VacateActorTile(ActorHandle handle) {
        const Actor* actor = Live(actors, handle);
        Require(actor != nullptr);
        if (actor->parked || actor->combat.life == ActorLifeState::Dead) { return; }
        ActorHandle& occupant = GetChunk(ChunkOf(actor->position))->actorOccupancy[TileIndex(actor->position)];
        Require(occupant == handle);
        occupant = {};
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
            Chunk* currentChunk = GetChunk(ChunkOf(location.position));
            Require(currentChunk != nullptr);
            return currentChunk->ground_items;
        }
        case LocationKind::actor: {
            Actor* currentActor = Live(actors, location.actor);
            Require(currentActor != nullptr);
            return currentActor->inventory;
        }
        case LocationKind::item: {
            Item* currentItem = Live(items, location.item);
            Require(currentItem != nullptr);
            return currentItem->contents;
        }
        case LocationKind::object: {
            InteractiveObject* currentObject = Live(objects, location.object);
            Require(currentObject != nullptr);
            return currentObject->contents;
        }
        }
        std::abort();
    }

    void Generate(Chunk& value) {
        Require(generationVersion >= 1 && generationVersion <= world_generation_version);
        // Version 3 retains version 2's surface raster. Older worlds acquire
        // the layered extension only outside their original surface layer.
        if (generationVersion != 1 || value.position.y != 0) {
            const auto region = RegionOf({value.position.x * chunk_size, value.position.z * chunk_size});
            if (!generationPlan || generationPlan->region != region) {
                generationPlan = GenerateRegionPlan(content, seed, region, generationVersion == 1 ? 2 : generationVersion);
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
                actor->speed, actor->actionDeadline.Remaining(tick), actor->parked, actor->carryCapacityGrams, actor->light,
                CaptureAiController(actor->controller_.get()), CaptureCombatState(actor->combat, content),
                actor->monsterType.value ? content.GetMonster(actor->monsterType).id : String{},
                actor->wieldedItem.value ? Live(items, actor->wieldedItem)->id : 0,
                CaptureLearningState(actor->learning, content), CaptureEquipment(owner, *actor),
                CaptureCharacterCombat(actor->characterCombat.get(), content)});
            for (ItemHandle item : actor->inventory) {
                pending.push_back(item);
            }
        }
        for (ObjectHandle handle : value.objects) {
            const InteractiveObject* object = Live(objects, handle);
            Require(object != nullptr);
            saved.objects.push_back({object->id, object->position, object->enabled, object->updates,
                object->updateInterval, object->updateDeadline.Remaining(tick), object->kind, object->facing,
                object->broken, object->light, object->type,
                object->behavior_ ? object->behavior_->SaveState() : DataObject{},
                object->equipment.value ? Live(items, object->equipment)->id : 0});
            for (ItemHandle item : object->contents) {
                pending.push_back(item);
            }
        }
        for (std::size_t index = 0; index < pending.size(); ++index) {
            Require(pending.size() <= items.size());
            const Item* item = Live(items, pending[index]);
            Require(item != nullptr);
            PersistentId ownerId = 0;
            switch (item->location.kind) {
            case LocationKind::ground: break;
            case LocationKind::actor: ownerId = Live(actors, item->location.actor)->id; break;
            case LocationKind::item: ownerId = Live(items, item->location.item)->id; break;
            case LocationKind::object: ownerId = Live(objects, item->location.object)->id; break;
            }
            saved.items.push_back({item->id, item->type, item->location.kind,
                item->location.position, ownerId, item->location.installation, itemSystem.Capture(pending[index])});
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
        for (auto actor : value.actors) { craftingSystem.ReleaseWorkerStations(Live(actors, actor)->id); }
        for (auto object : value.objects) { craftingSystem.ReleaseStation(Live(objects, object)->id); }
        Vector<ItemHandle> pending;
        Archive saved = Capture(value, &pending);
        // First capture all values; then retire every runtime key. Do not reset
        // the central slot maps, even when the last chunk is unloaded.
        for (ItemHandle handle : pending) {
            Unregister(handle);
            items.erase(dod::slot_map64<Item>::key{handle.value});
        }
        for (ActorHandle handle : value.actors) {
            VacateActorTile(handle);
            Require(actorIds.erase(Live(actors, handle)->id) == 1);
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
            Actor restored(actor.id, actor.position, actor.player);
            restored.monsterType = actor.monsterType.empty() ? MonsterTypeId{} : content.FindMonster(actor.monsterType);
            Require(actor.monsterType.empty() || (!actor.player && restored.monsterType.value));
            restored.turns = actor.turns;
            restored.speed = actor.speed;
            restored.actionDeadline.Set(tick, actor.remainingTicks);
            restored.parked = actor.parked;
            restored.carryCapacityGrams = actor.carryCapacityGrams;
            restored.light = actor.light;
            const auto combat = ReadCombatState(actor.combat, content);
            Require(combat.has_value());
            restored.combat = *combat;
            const auto combatVersion = actor.combat.GetUnsigned("version");
            if ((!combatVersion || *combatVersion < 3) && restored.combat.life == ActorLifeState::Alive) {
                // RAM archives can still contain the original envelope. Disk
                // migration has its own remainingTicks-aware normalization and
                // returns v3; never infer migration from legacyAccuracyOverride.
                Tick lock = 0;
                switch (restored.combat.action.kind) {
                case ActorActionKind::MeleeWindup:
                    lock = CheckedTickAdd(actor.remainingTicks, restored.combat.action.recoveryTicks);
                    break;
                case ActorActionKind::Recovery:
                case ActorActionKind::DisruptionRecovery: lock = actor.remainingTicks; break;
                default: break;
                }
                restored.combat.combatLockRemaining = eastl::max(restored.combat.combatLockRemaining, lock);
            }
            if (restored.combat.life == ActorLifeState::Dead) {
                restored.parked = restored.player;
                restored.combat.action = {};
                restored.actionDeadline.Set(tick, 0);
            }
            auto character = ReadCharacterCombat(actor.characterCombat, content);
            Require(character.has_value());
            restored.characterCombat = eastl::move(*character);
            auto learning = ReadLearningState(actor.learning, content);
            Require(learning.has_value());
            restored.learning = eastl::move(*learning);
            if (restored.combat.life == ActorLifeState::Dead) {
                restored.learning.study = {};
                if (restored.characterCombat) { restored.characterCombat->manuals.clear(); }
            }
            auto controller = ReadAiController(actor.controller);
            Require(controller.has_value());
            restored.controller_ = eastl::move(*controller);
            RefreshSchedule(restored);
            const auto key = actors.emplace(eastl::move(restored));
            const ActorHandle handle{key.raw};
            Require(actorIds.try_emplace(actor.id, handle).second);
            InsertChunkActor(value, handle);
            OccupyActorTile(handle);
            if (actor.player) { TrackPlayer(actor.id, actor.position); }
        }
        for (const SavedObject& object : saved.objects) {
            InteractiveObject restored(object.id, object.position, object.enabled);
            restored.updates = object.updates;
            restored.updateInterval = object.updateInterval;
            restored.updateDeadline.Set(tick, object.remainingTicks);
            restored.kind = object.kind;
            restored.facing = object.facing;
            restored.broken = object.broken;
            restored.light = object.light;
            restored.type = object.type;
            if (object.type.value) {
                auto behavior = MakeObjectBehavior(content.GetObject(object.type), &object.state);
                Require(behavior.has_value());
                restored.behavior_ = eastl::move(*behavior);
            }
            RefreshSchedule(restored);
            const auto key = objects.emplace(eastl::move(restored));
            value.objects.push_back({key.raw});
            powerSystem.SyncObject({key.raw});
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
        for (const auto& object : saved.objects) {
            Live(objects, FindObject(object.id))->equipment = FindItem(object.equipment);
        }
        for (const auto& actor : saved.actors) {
            const auto handle = FindActor(actor.id);
            auto* restored = Live(actors, handle);
            const auto equipment = ReadEquipment(actor.equipment);
            Require(equipment.has_value());
            for (unsigned hand = 0; hand < 2; ++hand) {
                restored->equipment.hands[hand] = {FindItem(equipment->hands[hand].item), equipment->hands[hand].grip};
            }
            for (const auto worn : equipment->worn) { restored->equipment.worn.push_back(FindItem(worn)); }
            if (actor.equipment.Size() == 0 && actor.wieldedItem) {
                const auto item = FindItem(actor.wieldedItem);
                const auto& grips = ItemMelee(content.GetItem(Live(items, item)->type)).grips;
                Require(!grips.empty());
                restored->equipment.hands[0] = {item, grips.front().kind};
                if (grips.front().kind == GripKind::TwoHanded) { restored->equipment.hands[1] = restored->equipment.hands[0]; }
            }
            restored->wieldedItem = restored->equipment.hands[0].item.value
                ? restored->equipment.hands[0].item : restored->equipment.hands[1].item;
            const auto combatVersion = actor.combat.GetUnsigned("version");
            auto& action = restored->combat.action;
            if ((!combatVersion || *combatVersion < 3) && action.kind == ActorActionKind::MeleeWindup && action.weapon) {
                // Catalog-only migration cannot know the legacy weapon's grip.
                // Bind its already-paid commitment after equipment identities
                // exist; never reconstruct an expanded v3 commitment this way.
                const auto weapon = FindItem(action.weapon);
                Require(weapon.value && action.weapon == actor.wieldedItem);
                action.sources = {};
                action.sources.primaryKind = AttackSourceKind::HeldItem;
                bool bound = false;
                for (unsigned hand = 0; hand < 2; ++hand) {
                    const auto held = restored->equipment.hands[hand];
                    if (held.item != weapon) { continue; }
                    if (!bound) { action.sources.primaryHand = static_cast<Hand>(hand); }
                    bound = true;
                    action.sources.hands[hand] = {action.weapon, held.grip, true};
                    action.sources.functionalHands = static_cast<HandMask>(
                        static_cast<unsigned>(action.sources.functionalHands) | (1U << hand));
                }
                Require(bound);
            }
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
        Vector<PersistentId> sessionIds;
        Vector<std::uint64_t> tokens;
        for (const SavedPlayer& entry : roster) {
            if (entry.actor == 0 || entry.actor >= next_id || entry.session >= next_id || !ValidPosition(entry.position)
                || (!entry.name.empty() && entry.name.size() > 32)
                || (!session.players.empty() && (entry.name.empty()
                    || std::memchr(entry.name.data(), '\0', entry.name.size())))
                || (session.player != 0 && entry.actor == session.player
                    && (entry.position != session.playerPosition || entry.reconnectToken != 0))
                || (entry.actor != session.player && entry.reconnectToken == 0)) {
                return SaveFailure(SaveError::InvalidData, "Invalid player roster entry", diagnostics);
            }
            playerIds.push_back(entry.actor);
            sessionIds.push_back(entry.session ? entry.session : entry.actor);
            if (entry.reconnectToken != 0) { tokens.push_back(entry.reconnectToken); }
            const Actor* live = Live(actors, FindActor(entry.actor));
            if (live) {
                if (live->player && live->position == entry.position
                    && (entry.actor != session.player || !live->parked || live->combat.life == ActorLifeState::Dead)) { continue; }
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
                    [&entry, &session, this](const SavedActor& actor) {
                        const auto combat = ReadCombatState(actor.combat, content);
                        return actor.id == entry.actor && actor.player && actor.position == entry.position
                            && combat && (entry.actor != session.player || !actor.parked || combat->life == ActorLifeState::Dead);
                    })) {
                return SaveFailure(SaveError::InvalidData, "Roster player missing from world", diagnostics);
            }
        }
        if (!UniqueIds(playerIds) || !UniqueIds(sessionIds)) {
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
                    const auto* actor = Live(actors, FindActor(player.first));
                    if (actor && actor->combat.life == ActorLifeState::Dead && actor->combat.corpse) { continue; }
                    const auto position = ChunkOf(player.second);
                    const Archive* saved = nullptr;
                    if (const auto found = archives.find(position); found != archives.end()) { saved = &found->second; }
                    if (!saved) {
                        if (const auto found = warmArchives.find(position); found != warmArchives.end()) { saved = &found->second; }
                    }
                    eastl::optional<Archive> disk;
                    if (!saved && source) {
                        auto result = source->ReadChunk(position, next_id, diagnostics);
                        if (!result) { return eastl::unexpected(result.error()); }
                        if (*result) { disk = eastl::move(**result); saved = &*disk; }
                    }
                    if (saved && eastl::any_of(saved->actors.begin(), saved->actors.end(), [&](const SavedActor& value) {
                        const auto combat = ReadCombatState(value.combat, content);
                        return value.id == player.first && combat && combat->life == ActorLifeState::Dead && combat->corpse;
                    })) { continue; }
                    return SaveFailure(SaveError::InvalidData, "Player missing from roster", diagnostics);
                }
            }
        }
        return {};
    }

    SaveResult<void> ApplyRecipeCredit(World& world, RecipeCredit credit, Diagnostics* diagnostics) {
        if (const auto handle = FindActor(credit.actor); handle.value) {
            auto* actor = Live(actors, handle);
            if (actor->combat.life == ActorLifeState::Dead) { return {}; }
            cataclysmx::LearnRecipe(actor->learning, content, credit.recipe, credit.points);
            world.ActorChanged(handle);
            return {};
        }
        const auto apply = [&](Archive& archive) -> SaveResult<bool> {
            for (auto& actor : archive.actors) {
                if (actor.id != credit.actor) { continue; }
                const auto combat = ReadCombatState(actor.combat, content, diagnostics);
                if (!combat) { return eastl::unexpected(SaveError::InvalidData); }
                if (combat->life == ActorLifeState::Dead) { return true; }
                auto learning = ReadLearningState(actor.learning, content, diagnostics);
                if (!learning) { return eastl::unexpected(SaveError::InvalidData); }
                cataclysmx::LearnRecipe(*learning, content, credit.recipe, credit.points);
                actor.learning = CaptureLearningState(*learning, content);
                MarkDirty(archive.position, false);
                return true;
            }
            return false;
        };
        for (auto& entry : archives) {
            auto result = apply(entry.second);
            if (!result) { return eastl::unexpected(result.error()); }
            if (*result) { return {}; }
        }
        // Warm archives never contain actors. Disk lookup works for NPCs too;
        // playerPositions alone cannot locate arbitrary historical contributors.
        if (!database) { return {}; }
        auto position = database->FindActorChunk(credit.actor, diagnostics);
        if (!position) { return eastl::unexpected(position.error()); }
        if (!*position) { return {}; } // Destroyed/nonexistent actor.
        // An old source row may still mention a moved or destroyed actor. Current
        // RAM ownership supersedes it, even when that actor is absent from RAM.
        if (GetChunk(**position) || archives.contains(**position) || warmArchives.contains(**position)) { return {}; }
        auto saved = database->ReadChunk(**position, next_id, diagnostics);
        if (!saved) { return eastl::unexpected(saved.error()); }
        if (!*saved) { return SaveFailure(SaveError::InvalidData, "Recipe contributor chunk disappeared", diagnostics); }
        auto result = apply(**saved);
        if (!result) { return eastl::unexpected(result.error()); }
        if (!*result) { return SaveFailure(SaveError::InvalidData, "Recipe contributor disappeared", diagnostics); }
        Require(archives.try_emplace(**position, eastl::move(**saved)).second);
        return {};
    }

    SaveResult<void> FlushRecipeCredits(World& world, Diagnostics* diagnostics) {
        while (!pendingRecipeCredits.empty()) {
            const auto result = ApplyRecipeCredit(world, pendingRecipeCredits.front(), diagnostics);
            if (!result) { return result; }
            pendingRecipeCredits.erase(pendingRecipeCredits.begin());
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
        SaveSnapshot saveSnapshot{seed, generationVersion, next_id, session, skyLight, CaptureMemories()};
        saveSnapshot.session.tick = tick;
        saveSnapshot.power = powerSystem.Capture();
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

WorldResult<LoadRequest> World::RequestChunks(ChunkRegion region, LoadMode mode, LoadReason reason) {
    if (!ValidRegion(region)) {
        return eastl::unexpected(WorldError::invalid_region);
    }
    if (!ValidMode(mode) || (reason != LoadReason::Other && reason != LoadReason::Player)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    region.center.y = 0;
    impl_->demandDirty = true;
    return LoadRequest{impl_->requests.emplace(Request{region, mode, eastl::nullopt, reason}).raw};
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
    *request = {region, mode, eastl::nullopt, request->reason};
    return {};
}

WorldResult<LoadRequest> World::RequestCircle(TileCircleRegion region, LoadMode mode, LoadReason reason) {
    if (!ValidCircle(region)) { return eastl::unexpected(WorldError::invalid_region); }
    if (!ValidMode(mode) || (reason != LoadReason::Other && reason != LoadReason::Player)) { return eastl::unexpected(WorldError::invalid_request); }
    region.center.y = 0;
    impl_->demandDirty = true;
    return LoadRequest{impl_->requests.emplace(Request{{}, mode, region, reason}).raw};
}

WorldResult<void> World::UpdateCircleRequest(LoadRequest handle, TileCircleRegion region, LoadMode mode) {
    Request* request = Live(impl_->requests, handle);
    if (!request) { return eastl::unexpected(WorldError::invalid_request); }
    if (!ValidCircle(region)) { return eastl::unexpected(WorldError::invalid_region); }
    if (!ValidMode(mode)) { return eastl::unexpected(WorldError::invalid_request); }
    region.center.y = 0;
    if (request->circle && request->circle->center == region.center
        && request->circle->radiusTiles == region.radiusTiles && request->mode == mode) { return {}; }
    *request = {{}, mode, region, request->reason};
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
    for (;;) {
        if (auto result = ReconcileChunksPass(diagnostics); !result) { return result; }
        // Materialization, population or gravity may have exposed another source
        // or changed a player's grid. Finish this closure before returning to input.
        impl_->RefreshPowerDemand();
        if (!impl_->demandDirty) { return {}; }
    }
}

SaveResult<void> World::ReconcileChunksPass(Diagnostics* diagnostics) {
    if (auto result = impl_->FlushRecipeCredits(*this, diagnostics); !result) { return result; }
    impl_->RefreshPowerDemand();
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
    for (const auto column : impl_->workshopDemand) { demand[column] = true; }
    for (const auto column : impl_->playerPowerDemand) { demand[column] = true; }
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
    // Preflight every live timer that will resume, before either reconciliation
    // path can change any activity flags. Time may have advanced while resident.
    for (const auto& entry : demand) {
        if (!entry.second) { continue; }
        const auto current = impl_->columns_.find(entry.first);
        if (current == impl_->columns_.end() || current->second.layers.front().active) { continue; }
        for (const Chunk& chunk : current->second.layers) {
            if (auto valid = impl_->ValidateActivation(chunk, diagnostics); !valid) { return valid; }
        }
    }
    if (sameColumns) {
        ++impl_->perceptionStats.demandReconciliations;
        // Skip archive work, but retain the structural path's gravity boundary:
        // newly active disk-restored entities may still be suspended in air.
        Impl::GravityBatch batch(*this);
        for (auto& column : impl_->columns_) {
            const bool active = demand.at(column.first);
            for (Chunk& chunk : column.second.layers) {
                if (chunk.active != active) { ++impl_->mapRevision; ++impl_->revision; }
                impl_->SetChunkActive(chunk, active);
            }
        }
        SettleGravity();
        impl_->demandDirty = false;
        return {};
    }
    Impl::ArchiveMap staged;
    const auto incomingArchive = [&](ChunkCoord position) -> const Archive* {
        const auto pending = impl_->archives.find(position);
        if (pending != impl_->archives.end()) { return &pending->second; }
        const auto warm = impl_->warmArchives.find(position);
        if (warm != impl_->warmArchives.end()) { return &warm->second; }
        const auto disk = staged.find(position);
        if (disk != staged.end()) { return &disk->second; }
        return nullptr;
    };
    auto& incoming = impl_->incomingChunks;
    auto& buildings = impl_->pendingPowerBuildings;
    const auto stageBuildings = [&]() -> SaveResult<void> {
        incoming.clear(); buildings.clear();
        for (const auto& entry : demand) {
            if (impl_->columns_.contains(entry.first)) { continue; }
            for (int y = world_min_y; y <= world_max_y; ++y) { incoming.push_back({entry.first.x, entry.first.z, y}); }
        }
        eastl::sort(incoming.begin(), incoming.end(), ChunkLess);
        if (impl_->generationVersion < 7) { return {}; }
        for (const auto position : incoming) {
            if (position.y != 0) { continue; }
            const auto region = RegionOf({position.x * chunk_size, position.z * chunk_size});
            if (!impl_->generationPlan || impl_->generationPlan->region != region) {
                impl_->generationPlan = GenerateRegionPlan(impl_->content, impl_->seed, region, impl_->generationVersion);
            }
            GenerateBuildingPower(impl_->content, *impl_->generationPlan, position, impl_->generationPower);
            for (const auto& building : impl_->generationPower) { buildings.push_back({building.origin, building.bounds}); }
        }
        if (!impl_->powerSystem.ValidateBuildings({buildings.data(), buildings.size()})) {
            return SaveFailure(SaveError::InvalidData, "Invalid generated electrical coverage or grid ID exhaustion", diagnostics);
        }
        return {};
    };
    // Include source dependencies of players about to be restored before the
    // mutation boundary, so bad source archives cannot fail a later live pass.
    Vector<PowerBounds> stagedConsumers;
    struct IncomingConsumer { PersistentId worker; TileCoord position; std::size_t area; };
    Vector<IncomingConsumer> incomingConsumers;
    Vector<ColumnCoord> stagedSources;
    for (;;) {
        if (impl_->database) {
            for (const auto& entry : demand) {
                for (int y = world_min_y; y <= world_max_y; ++y) {
                    const ChunkCoord position{entry.first.x, entry.first.z, y};
                    if (impl_->GetChunk(position) || incomingArchive(position)) { continue; }
                    auto saved = impl_->database->ReadChunk(position, impl_->next_id, diagnostics);
                    if (!saved) {
                        if (diagnostics) { diagnostics->Add("Reconciling world chunks"); }
                        return eastl::unexpected(saved.error());
                    }
                    if (*saved) {
                        if ((**saved).position != position) {
                            return SaveFailure(SaveError::InvalidData, "Invalid disk chunk archive", diagnostics);
                        }
                        Require(staged.try_emplace(position, eastl::move(**saved)).second);
                    }
                }
            }
        }
        if (auto result = stageBuildings(); !result) { return result; }
        stagedConsumers.clear();
        incomingConsumers.clear();
        for (const auto& actor : impl_->actors) {
            if (!actor.player || actor.parked || actor.combat.life != ActorLifeState::Alive) { continue; }
            incomingConsumers.push_back({actor.id, actor.position, stagedConsumers.size()});
            stagedConsumers.push_back(Impl::ConsumerArea(actor.position,
                impl_->craftingSystem.ConsumerRadius(actor.id)));
        }
        for (const auto& device : impl_->powerSystem.Devices()) {
            if (device.workshopHold) { stagedConsumers.push_back({device.position, device.position}); }
        }
        for (const auto& entry : demand) {
            if (impl_->columns_.contains(entry.first)) { continue; }
            for (int y = world_min_y; y <= world_max_y; ++y) {
                const auto* saved = incomingArchive({entry.first.x, entry.first.z, y});
                if (!saved) { continue; }
                for (const auto& actor : saved->actors) {
                    if (!actor.player || actor.parked) { continue; }
                    const auto combat = ReadCombatState(actor.combat, impl_->content, diagnostics);
                    if (!combat) { return eastl::unexpected(SaveError::InvalidData); }
                    if (combat->life == ActorLifeState::Alive) {
                        incomingConsumers.push_back({actor.id, actor.position, stagedConsumers.size()});
                        stagedConsumers.push_back(Impl::ConsumerArea(actor.position,
                            impl_->craftingSystem.ConsumerRadius(actor.id)));
                    }
                }
            }
        }
        // Incoming jobs are not attached yet. Include their snapshotted reach
        // before any player, activity flag or generated coverage is published.
        for (const auto& entry : demand) {
            if (impl_->columns_.contains(entry.first)) { continue; }
            for (int y = world_min_y; y <= world_max_y; ++y) {
                const auto* saved = incomingArchive({entry.first.x, entry.first.z, y});
                if (!saved) { continue; }
                for (const auto& item : saved->items) {
                    if (!impl_->content.GetItem(item.type).unfinishedCraft) { continue; }
                    const auto* payload = item.state.Find("crafting_job");
                    if (!payload) { return SaveFailure(SaveError::InvalidData, "Missing incoming crafting job", diagnostics); }
                    const auto job = ReadCraftingState(*payload);
                    if (!job) { return SaveFailure(SaveError::InvalidData, "Invalid incoming crafting job", diagnostics); }
                    if (!job->worker) { continue; }
                    for (const auto& consumer : incomingConsumers) {
                        if (consumer.worker != job->worker) { continue; }
                        const auto area = Impl::ConsumerArea(consumer.position, job->settings.nearbyRadius);
                        auto& current = stagedConsumers[consumer.area];
                        current.min.x = eastl::min(current.min.x, area.min.x);
                        current.min.z = eastl::min(current.min.z, area.min.z);
                        current.max.x = eastl::max(current.max.x, area.max.x);
                        current.max.z = eastl::max(current.max.z, area.max.z);
                    }
                }
            }
        }
        impl_->powerSystem.CollectConsumerDemand(stagedConsumers, stagedSources);
        // Planned generated coverage can connect an incoming player to indexed
        // sources even before that building's first global registration commits.
        for (const auto& building : buildings) {
            if (eastl::none_of(stagedConsumers.begin(), stagedConsumers.end(), [&](PowerBounds area) {
                    return PowerOverlaps(building.bounds, area);
                })) { continue; }
            for (const auto& device : impl_->powerSystem.Devices()) {
                if (device.kind == PowerDeviceKind::Machine || !PowerContains(building.bounds, device.position)) { continue; }
                const auto chunk = ChunkOf(device.position);
                stagedSources.push_back({chunk.x, chunk.z});
            }
        }
        bool expanded = false;
        for (const auto column : stagedSources) {
            const auto entry = demand.try_emplace(column, true);
            expanded |= entry.second || !entry.first->second;
            entry.first->second = true;
        }
        if (!expanded) { break; }
    }
    // The closure may activate a previously resident source column.
    for (const auto& entry : demand) {
        const auto current = impl_->columns_.find(entry.first);
        if (!entry.second || current == impl_->columns_.end() || current->second.layers.front().active) { continue; }
        for (const auto& chunk : current->second.layers) {
            if (auto valid = impl_->ValidateActivation(chunk, diagnostics); !valid) { return valid; }
        }
    }
    for (const auto& entry : demand) {
        if (impl_->columns_.contains(entry.first)) { continue; }
        for (int y = world_min_y; y <= world_max_y; ++y) {
            if (const Archive* saved = incomingArchive({entry.first.x, entry.first.z, y})) {
                if (auto valid = ValidateArchiveDeadlines(*saved, impl_->content, impl_->tick, diagnostics); !valid) { return valid; }
                if (!ValidPowerArchive(impl_->powerSystem, impl_->content, *saved, impl_->tick)) {
                    return SaveFailure(SaveError::InvalidData, "Incoming archive differs from the global power equipment index", diagnostics);
                }
            } else {
                for (const auto& device : impl_->powerSystem.Devices()) {
                    if (ChunkOf(device.position) == ChunkCoord{entry.first.x, entry.first.z, y}) {
                        return SaveFailure(SaveError::InvalidData, "Missing archive for indexed power equipment", diagnostics);
                    }
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
    // All subsequent registrations are guaranteed by the whole-batch preflight.
    for (const auto& building : buildings) {
        Require(impl_->powerSystem.RegisterBuilding(building.origin, building.bounds).has_value());
    }
    ++impl_->perceptionStats.demandReconciliations;
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
                impl_->SetChunkActive(chunk, false);
                impl_->Unload(chunk);
            } else {
                if (chunk.active != wanted->second) { ++impl_->mapRevision; ++impl_->revision; }
                impl_->SetChunkActive(chunk, wanted->second);
            }
        }
        // Erasure relocates the last dense value; revisit the returned iterator.
        if (remove) {
            current = impl_->columns_.erase(current);
            ++impl_->perceptionStats.columnsUnloaded;
        } else { ++current; }
    }
    auto& newChunks = impl_->newChunks;
    newChunks.clear();
    for (const auto& entry : demand) {
        if (impl_->columns_.contains(entry.first)) { continue; }
        impl_->InstallColumn(entry.first, entry.second);
        for (int y = world_min_y; y <= world_max_y; ++y) {
            const ChunkCoord position{entry.first.x, entry.first.z, y};
            impl_->MarkLightingDirty(position);
            impl_->MarkGeometryDirty(position);
            ++impl_->mapRevision;
            ++impl_->revision;
            Chunk* chunk = impl_->GetChunk(position);
            if (const Archive* saved = incomingArchive(position)) { chunk->tiles = saved->tiles; }
            else {
                impl_->Generate(*chunk);
                impl_->MarkDirty(position);
                newChunks.push_back(position);
            }
        }
    }
    // Publish complete terrain/residency before restoration or mutation callbacks.
    eastl::sort(incoming.begin(), incoming.end(), ChunkLess);
    eastl::sort(newChunks.begin(), newChunks.end(), ChunkLess);
    impl_->itemSystem.BeginCallback();
    Vector<ItemHandle> restoredItems;
    for (ChunkCoord position : incoming) {
        if (const Archive* saved = incomingArchive(position)) {
            impl_->Restore(*impl_->GetChunk(position), *saved);
            for (const SavedItem& item : saved->items) { restoredItems.push_back(impl_->FindItem(item.id)); }
        }
    }
    // Existing empty snapshots deliberately never enter this population pass.
    // New actors start ready (zero deadline); typed objects keep the legacy
    // timer suspended and machine/item work uses relative durations. Population
    // therefore needs no nonzero absolute deadline, even at maximum tick.
    if (impl_->generationVersion >= 4) {
        Vector<TileCoord> placedFurniture;
        for (ChunkCoord position : newChunks) {
            const auto region = RegionOf({position.x * chunk_size, position.z * chunk_size});
            if (!impl_->generationPlan || impl_->generationPlan->region != region) {
                impl_->generationPlan = GenerateRegionPlan(impl_->content, impl_->seed, region, impl_->generationVersion);
            }
            auto& placements = impl_->generationPlacements;
            GenerateChunkPlacements(impl_->content, impl_->seed, *impl_->generationPlan, position, placements);
            placedFurniture.clear();
            for (const auto& furniture : placements.furniture) {
                const auto& definition = impl_->content.GetObject(furniture.type);
                const auto& terrain = impl_->content.GetTerrain(GetTile(furniture.position)->terrain);
                if (ObjectAt(furniture.position).value || !terrain.passable || terrain.openSpace
                    || (definition.blocksMovement && ActorAt(furniture.position).value)) { continue; }
                const auto created = CreateObject(furniture.type, furniture.position);
                Require(created.has_value());
                Require(SetObjectAppearance(*created, definition.kind, furniture.facing).has_value());
                placedFurniture.push_back(furniture.position);
            }
            for (const auto& item : placements.items) {
                ItemLocation location = ItemLocation::Ground(item.position);
                if (item.destination == LootDestination::Furniture) {
                    if (eastl::find(placedFurniture.begin(), placedFurniture.end(), item.position) == placedFurniture.end()) { continue; }
                    location = ItemLocation::Stored(ObjectAt(item.position));
                }
                Require(CreateItem(item.type, location).has_value());
            }
            for (const auto& monster : placements.monsters) {
                if (ActorAt(monster.position).value || BlocksMovement(monster.position)) { continue; }
                Require(CreateMonster(monster.type, monster.position).has_value());
            }
        }
    }
    for (ChunkCoord position : incoming) {
        impl_->archives.erase(position);
        impl_->ForgetWarm(position);
    }
    for (ItemHandle handle : restoredItems) { impl_->itemSystem.Attach(handle); }
    impl_->itemSystem.EndCallback();
    Vector<ActorHandle> terminal;
    for (const auto& entry : impl_->actors.items()) {
        const auto& body = entry.second.get().combat.body;
        if (entry.second.get().combat.life == ActorLifeState::Alive
            && (body.kind == BodyKind::Simple ? !Functional(body, BodyPart::Core)
                : !Functional(body, BodyPart::Head) || !Functional(body, BodyPart::Torso))) {
            terminal.push_back({entry.first.raw});
        }
    }
    for (const auto actor : terminal) { MarkActorDead(actor); }
    FinalizeActorDeaths();
    SettleGravity();
    incoming.clear();
    newChunks.clear();
    impl_->demandDirty = false;
    return {};
}

SaveResult<void> World::CreateSave(const char* path, const SessionState& session,
    Diagnostics* diagnostics) {
    FinalizeActorDeaths();
    if (auto result = impl_->FlushRecipeCredits(*this, diagnostics); !result) { return result; }
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
        || !impl_->playerPositions.empty() || impl_->next_id != 1 || impl_->tick != 0
        || !impl_->powerSystem.IsPristine()) {
        SaveFailure(SaveError::InvalidState, "Loading requires a pristine world", diagnostics);
        return eastl::unexpected(SaveError::InvalidState);
    }
    auto database = eastl::make_unique<SaveDatabase>(impl_->content);
    auto snapshot = database->Open(path, diagnostics);
    if (!snapshot) {
        if (diagnostics) { diagnostics->Add("Loading world save"); }
        return eastl::unexpected(snapshot.error());
    }
    const bool supported = snapshot->generationVersion >= 1
        && snapshot->generationVersion <= world_generation_version;
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
    impl_->tick = snapshot->session.tick;
    impl_->generationVersion = snapshot->generationVersion;
    impl_->generationPlan.reset();
    impl_->next_id = snapshot->nextId;
    impl_->skyLight = snapshot->skyLight;
    Require(impl_->powerSystem.Restore(snapshot->power, snapshot->session.tick).has_value());
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
    FinalizeActorDeaths();
    if (!impl_->database) {
        return SaveFailure(SaveError::NotOpen, "World has no attached save", diagnostics);
    }
    if (auto result = impl_->FlushRecipeCredits(*this, diagnostics); !result) { return result; }
    if (!CheckInvariants()) {
        return SaveFailure(SaveError::InvalidData, "Invalid world state", diagnostics);
    }
    return impl_->Checkpoint(*impl_->database, session, diagnostics);
}

bool World::HasSave() const { return impl_->database != nullptr; }
PowerSystem& World::Power() { return impl_->powerSystem; }
const PowerSystem& World::Power() const { return impl_->powerSystem; }
void World::PowerChanged() {
    ++impl_->revision;
    ++impl_->mapRevision;
    impl_->demandDirty = true;
}

const char* PowerActionMessage(PowerActionOutcome outcome) {
    switch (outcome) {
    case PowerActionOutcome::Succeeded: return "Electrical installation complete";
    case PowerActionOutcome::InvalidArea: return "Choose one layer and a rectangle within the wiring size limit";
    case PowerActionOutcome::OutOfReach: return "Electrical installation is out of reach";
    case PowerActionOutcome::MissingMaterials: return "Missing wiring materials";
    case PowerActionOutcome::InvalidEquipment: return "Carry a suitable wiring kit or placeable electrical device";
    case PowerActionOutcome::Occupied: return "The installation tile is occupied";
    case PowerActionOutcome::Unavailable: return "Electrical installation is unavailable";
    case PowerActionOutcome::NoNewCoverage: return "This area is already wired";
    }
    std::abort();
}

PowerActionResult World::WireGrid(ActorHandle actorHandle, ItemHandle kit, PowerBounds bounds) {
    const auto* actor = GetActor(actorHandle);
    if (!actor || !IsActorAlive(actorHandle) || actor->parked || !IsActive(actor->position)
        || actor->actionDeadline.Remaining(impl_->tick) || actor->combat.action.kind != ActorActionKind::None
        || impl_->craftingSystem.Busy(actorHandle) || impl_->learningSystem.Busy(actorHandle)) { return {}; }
    const auto* item = GetItem(kit);
    if (!item || !CarriedPowerItem(*this, actorHandle, kit)) { return {PowerActionOutcome::InvalidEquipment}; }
    const auto& definition = impl_->content.GetItem(item->type);
    if (!definition.wiringMaterial.value) { return {PowerActionOutcome::InvalidEquipment}; }
    const auto quote = impl_->powerSystem.Quote(bounds);
    if (!quote || bounds.min.y != actor->position.y) { return {PowerActionOutcome::InvalidArea}; }
    const auto settings = impl_->powerSystem.Settings();
    for (const auto corner : {bounds.min, bounds.max}) {
        if (std::abs(corner.x - actor->position.x) > settings.maxWiringWidth
            || std::abs(corner.z - actor->position.z) > settings.maxWiringDepth) { return {PowerActionOutcome::OutOfReach}; }
    }
    for (int x = bounds.min.x; x <= bounds.max.x; ++x) {
        for (int z = bounds.min.z; z <= bounds.max.z; ++z) {
            if (!GetTile({x, z, bounds.min.y})) { return {PowerActionOutcome::Unavailable}; }
        }
    }
    if (!quote->newTiles && quote->mergedGrids < 2) { return {PowerActionOutcome::NoNewCoverage}; }
    const auto materialCount = (quote->wiringUnits + definition.wiringTilesPerMaterial - 1) / definition.wiringTilesPerMaterial;
    if (!impl_->GatherWiringMaterials(actorHandle, definition.wiringMaterial, materialCount)) {
        return {PowerActionOutcome::MissingMaterials};
    }
    const auto cost = eastl::max<std::uint32_t>(1, quote->newTiles) * ticks_per_turn;
    if (ActionTicks(cost, actor->speed) > UINT64_MAX - impl_->tick) { return {}; }
    Impl::GravityBatch batch(*this);
    BeginCraftingMutation();
    const auto extended = impl_->powerSystem.ExtendBounds(bounds);
    Require(extended.has_value());
    for (const auto material : impl_->wiringMaterials) { Require(DestroyItem(material).has_value()); }
    EndCraftingMutation();
    return {PowerActionOutcome::Succeeded, cost};
}

PowerActionResult World::PlacePowerObject(ActorHandle actorHandle, ItemHandle equipment,
    ItemHandle wiringKit, TileCoord position) {
    const auto* actor = GetActor(actorHandle);
    if (!actor || !IsActorAlive(actorHandle) || actor->parked || !IsActive(actor->position)
        || actor->actionDeadline.Remaining(impl_->tick) || actor->combat.action.kind != ActorActionKind::None
        || impl_->craftingSystem.Busy(actorHandle) || impl_->learningSystem.Busy(actorHandle)) { return {}; }
    const auto* item = GetItem(equipment);
    if (!item || !CarriedPowerItem(*this, actorHandle, equipment) || !item->contents.empty()) {
        return {PowerActionOutcome::InvalidEquipment};
    }
    const auto& definition = impl_->content.GetItem(item->type);
    const auto* placed = impl_->content.TryGetObject(definition.placedObject);
    if (!placed || (placed->behavior != ObjectBehaviorKind::GridBattery
        && placed->behavior != ObjectBehaviorKind::Generator
        && placed->behavior != ObjectBehaviorKind::Solar
        && !(placed->behavior == ObjectBehaviorKind::CraftingMachine && placed->machine.gridPowered))) {
        return {PowerActionOutcome::InvalidEquipment};
    }
    if (!ValidPosition(position) || !CanAccessGroundStorage(actorHandle, position)) { return {PowerActionOutcome::OutOfReach}; }
    if (ObjectAt(position).value || (placed->blocksMovement && ActorAt(position).value)) { return {PowerActionOutcome::Occupied}; }
    const bool battery = placed->behavior == ObjectBehaviorKind::GridBattery;
    impl_->wiringMaterials.clear();
    if (battery) {
        const auto* kit = GetItem(wiringKit);
        if (!kit || !CarriedPowerItem(*this, actorHandle, wiringKit) || equipment == wiringKit) {
            return {PowerActionOutcome::InvalidEquipment};
        }
        const auto& kitDefinition = impl_->content.GetItem(kit->type);
        if (!kitDefinition.wiringMaterial.value || kitDefinition.wiringMaterial == item->type) {
            return {PowerActionOutcome::InvalidEquipment};
        }
        const auto count = (4U + kitDefinition.wiringTilesPerMaterial - 1) / kitDefinition.wiringTilesPerMaterial;
        if (!impl_->GatherWiringMaterials(actorHandle, kitDefinition.wiringMaterial, count)) {
            return {PowerActionOutcome::MissingMaterials};
        }
    }
    constexpr auto cost = ticks_per_turn * 4;
    if (ActionTicks(cost, actor->speed) > UINT64_MAX - impl_->tick) { return {}; }
    Impl::GravityBatch batch(*this);
    BeginCraftingMutation();
    const auto object = CreateObject(definition.placedObject, position);
    Require(object.has_value());
    if (battery) { Require(TransferItem(equipment, ItemLocation::Stored(*object)).has_value()); }
    else { Require(DestroyItem(equipment).has_value()); }
    for (const auto material : impl_->wiringMaterials) { Require(DestroyItem(material).has_value()); }
    impl_->powerSystem.SyncObject(*object);
    EndCraftingMutation();
    return {PowerActionOutcome::Succeeded, cost};
}

const char* StationActionMessage(StationActionOutcome outcome) {
    switch (outcome) {
    case StationActionOutcome::Placed: return "Workshop station placed";
    case StationActionOutcome::Packed: return "Workshop station packed";
    case StationActionOutcome::OutOfReach: return "Workshop station is out of reach";
    case StationActionOutcome::Occupied: return "The station tile is occupied";
    case StationActionOutcome::InvalidEquipment: return "Carry the station's empty packing item";
    case StationActionOutcome::StationBusy: return "Workshop station is in use";
    case StationActionOutcome::NotEmpty: return "Empty the station before packing it";
    case StationActionOutcome::Broken: return "Workshop station is broken";
    case StationActionOutcome::FixedFixture: return "This station is a fixed fixture";
    case StationActionOutcome::Unavailable: return "Workshop action is unavailable";
    case StationActionOutcome::FuelLoaded: return "Forge fuel loaded";
    case StationActionOutcome::Ignited: return "Forge ignited";
    case StationActionOutcome::Extinguished: return "Forge extinguished";
    case StationActionOutcome::FuelDiscarded: return "Cold forge fuel discarded";
    case StationActionOutcome::ColdForge: return "Ignite the forge first";
    case StationActionOutcome::InsufficientFuel: return "Insufficient forge fuel";
    case StationActionOutcome::NoCapacity: return "The whole fuel charge must fit";
    case StationActionOutcome::InvalidFuel: return "Carry an accepted, unreserved fuel item";
    case StationActionOutcome::Disabled: return "Enable the forge first";
    case StationActionOutcome::AlreadyReady: return "Forge is already ready";
    case StationActionOutcome::AlreadyCold: return "Forge is already cold";
    case StationActionOutcome::MustExtinguish: return "Extinguish the forge first";
    case StationActionOutcome::HasFuel: return "Discard the cold forge fuel before packing";
    }
    return "Invalid workshop action";
}

PersistentId World::StationWorker(ObjectHandle handle) const {
    const auto* object = GetObject(handle);
    return object ? impl_->craftingSystem.StationWorker(object->id) : 0;
}

WorldResult<StationActionResult> World::PlaceStation(ActorHandle actorHandle, ItemHandle equipment, TileCoord position) {
    const auto* actor = GetActor(actorHandle);
    const auto* item = GetItem(equipment);
    if (!actor || !item) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!ValidPosition(position)) { return eastl::unexpected(WorldError::invalid_request); }
    if (!IsActorAlive(actorHandle) || actor->parked || !IsActive(actor->position)
        || actor->actionDeadline.Remaining(impl_->tick) || actor->combat.action.kind != ActorActionKind::None
        || impl_->craftingSystem.Busy(actorHandle) || impl_->learningSystem.Busy(actorHandle)
        || ActionTicks(400, actor->speed) > UINT64_MAX - impl_->tick) { return StationActionResult{}; }
    const auto& definition = impl_->content.GetItem(item->type);
    const auto* station = impl_->content.TryGetObject(definition.placedObject);
    if (!station || station->packingItem != item->type || !CarriedPowerItem(*this, actorHandle, equipment)
        || !item->contents.empty()) { return StationActionResult{StationActionOutcome::InvalidEquipment}; }
    if (!CanAccessGroundStorage(actorHandle, position)) { return StationActionResult{StationActionOutcome::OutOfReach}; }
    if (ObjectAt(position).value || (station->blocksMovement && ActorAt(position).value)) {
        return StationActionResult{StationActionOutcome::Occupied};
    }
    Impl::GravityBatch batch(*this); BeginCraftingMutation();
    Require(CreateTypedObject(definition.placedObject, position, true, equipment).has_value());
    EndCraftingMutation();
    return StationActionResult{StationActionOutcome::Placed, 400};
}

StationActionOutcome World::QueryPackStation(ActorHandle actorHandle, ObjectHandle handle) const {
    const auto* actor = GetActor(actorHandle);
    const auto* object = GetObject(handle);
    if (!actor || !object || !IsActorAlive(actorHandle) || actor->parked || !IsActive(actor->position)
        || actor->actionDeadline.Remaining(impl_->tick) || actor->combat.action.kind != ActorActionKind::None
        || impl_->craftingSystem.Busy(actorHandle) || impl_->learningSystem.Busy(actorHandle)
        || ActionTicks(400, actor->speed) > UINT64_MAX - impl_->tick) { return StationActionOutcome::Unavailable; }
    if (!CanReachStation(*this, actor->position, handle, 1)) { return StationActionOutcome::OutOfReach; }
    const auto* definition = impl_->content.TryGetObject(object->type);
    if (!definition || !definition->packingItem.value) { return StationActionOutcome::FixedFixture; }
    if (object->broken) { return StationActionOutcome::Broken; }
    if (StationWorker(handle)) { return StationActionOutcome::StationBusy; }
    if (definition->behavior == ObjectBehaviorKind::Forge) {
        const auto state = DescribeObjectState(handle)->forge;
        if (state.ready) { return StationActionOutcome::MustExtinguish; }
        if (state.fuel) { return StationActionOutcome::HasFuel; }
    }
    const auto* equipment = GetItem(object->equipment);
    if (!equipment || equipment->type != definition->packingItem || !equipment->contents.empty()
        || equipment->location != ItemLocation::Stored(handle)) { return StationActionOutcome::InvalidEquipment; }
    if (object->contents.size() != 1) { return StationActionOutcome::NotEmpty; }
    return StationActionOutcome::Packed;
}

WorldResult<StationActionResult> World::PackStation(ActorHandle actor, ObjectHandle handle) {
    if (!GetActor(actor) || !GetObject(handle)) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto outcome = QueryPackStation(actor, handle);
    if (outcome != StationActionOutcome::Packed) { return StationActionResult{outcome}; }
    const auto equipment = GetObject(handle)->equipment;
    const auto position = GetObject(handle)->position;
    Impl::GravityBatch batch(*this); BeginCraftingMutation();
    Live(impl_->objects, handle)->equipment = {};
    const auto carried = TransferItem(equipment, ItemLocation::Carried(actor));
    if (!carried) {
        Require(carried.error() == WorldError::capacity_exceeded);
        Require(TransferItem(equipment, ItemLocation::Ground(position)).has_value());
    }
    Require(DestroyObject(handle).has_value());
    EndCraftingMutation();
    return StationActionResult{StationActionOutcome::Packed, 400};
}

void World::CollectStations(ActorHandle actorHandle, Vector<StationView>& output) const {
    output.clear();
    const auto* actor = GetActor(actorHandle);
    if (!actor || !IsActorAlive(actorHandle) || actor->parked || !IsActive(actor->position)) { return; }
    const int radius = eastl::max<int>(1, GetCraftingSettings().nearbyRadius);
    for (int z = -radius; z <= radius; ++z) { for (int x = -radius; x <= radius; ++x) {
        const auto handle = ObjectAt({actor->position.x + x, actor->position.z + z, actor->position.y});
        const auto* object = GetObject(handle);
        const auto* definition = object ? impl_->content.TryGetObject(object->type) : nullptr;
        if (!definition || (definition->toolTraits.Empty() && !definition->packingItem.value)
            || !CanReachStation(*this, actor->position, handle, radius)) { continue; }
        output.push_back({object->id, object->type, object->position, object->enabled, object->broken,
            StationWorker(handle), QueryPackStation(actorHandle, handle),
            DescribeObjectState(handle)->forge});
    } }
    eastl::sort(output.begin(), output.end(), [](const StationView& a, const StationView& b) { return a.id < b.id; });
}

StationActionOutcome World::QueryForgeAction(ActorHandle actorHandle, ObjectHandle handle,
    ForgeAction action, ItemHandle fuel) const {
    const auto* actor = GetActor(actorHandle);
    const auto* object = GetObject(handle);
    const auto* definition = object ? impl_->content.TryGetObject(object->type) : nullptr;
    const Tick cost = action == ForgeAction::Ignite ? 400 : 100;
    if (!actor || !object || !definition || definition->behavior != ObjectBehaviorKind::Forge
        || action > ForgeAction::DiscardFuel || (action != ForgeAction::LoadFuel && fuel.value)
        || !IsActorAlive(actorHandle) || actor->parked || !IsActive(actor->position)
        || actor->actionDeadline.Remaining(impl_->tick) || actor->combat.action.kind != ActorActionKind::None
        || impl_->craftingSystem.Busy(actorHandle) || impl_->learningSystem.Busy(actorHandle)
        || ActionTicks(cost, actor->speed) > UINT64_MAX - impl_->tick) { return StationActionOutcome::Unavailable; }
    if (!CanReachStation(*this, actor->position, handle, 1)) { return StationActionOutcome::OutOfReach; }
    if (object->broken) { return StationActionOutcome::Broken; }
    if (!object->enabled) { return StationActionOutcome::Disabled; }
    const auto state = DescribeObjectState(handle)->forge;
    switch (action) {
    case ForgeAction::LoadFuel: {
        const auto* item = GetItem(fuel);
        if (!item || !CarriedPowerItem(*this, actorHandle, fuel) || !item->contents.empty()) {
            return StationActionOutcome::InvalidFuel;
        }
        for (const auto& entry : definition->forge.fuels) {
            if (entry.item != item->type) { continue; }
            return entry.heat <= definition->forge.fuelCapacity - state.fuel
                ? StationActionOutcome::FuelLoaded : StationActionOutcome::NoCapacity;
        }
        return StationActionOutcome::InvalidFuel;
    }
    case ForgeAction::Ignite:
        if (state.ready) { return StationActionOutcome::AlreadyReady; }
        return state.fuel > definition->forge.ignitionCost
            ? StationActionOutcome::Ignited : StationActionOutcome::InsufficientFuel;
    case ForgeAction::Extinguish:
        return state.ready ? StationActionOutcome::Extinguished : StationActionOutcome::AlreadyCold;
    case ForgeAction::DiscardFuel:
        if (state.ready) { return StationActionOutcome::MustExtinguish; }
        return state.fuel ? StationActionOutcome::FuelDiscarded : StationActionOutcome::AlreadyCold;
    }
    return StationActionOutcome::Unavailable;
}

WorldResult<StationActionResult> World::UseForge(ActorHandle actor, ObjectHandle handle,
    ForgeAction action, ItemHandle fuel) {
    const auto* object = GetObject(handle);
    if (!GetActor(actor) || !object || (fuel.value && !GetItem(fuel))) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const auto* definition = impl_->content.TryGetObject(object->type);
    if (!definition || definition->behavior != ObjectBehaviorKind::Forge
        || action > ForgeAction::DiscardFuel || (action != ForgeAction::LoadFuel && fuel.value)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    const auto outcome = QueryForgeAction(actor, handle, action, fuel);
    if (outcome != StationActionOutcome::FuelLoaded && outcome != StationActionOutcome::Ignited
        && outcome != StationActionOutcome::Extinguished && outcome != StationActionOutcome::FuelDiscarded) {
        return StationActionResult{outcome};
    }
    auto state = DescribeObjectState(handle)->forge;
    Impl::GravityBatch batch(*this); BeginCraftingMutation();
    switch (action) {
    case ForgeAction::LoadFuel: {
        const auto type = GetItem(fuel)->type;
        for (const auto& entry : definition->forge.fuels) {
            if (entry.item == type) { state.fuel += entry.heat; break; }
        }
        Require(DestroyItem(fuel).has_value());
        break;
    }
    case ForgeAction::Ignite:
        state.fuel -= definition->forge.ignitionCost; state.ready = true; break;
    case ForgeAction::Extinguish: state.ready = false; break;
    case ForgeAction::DiscardFuel: state.fuel = 0; break;
    }
    // Fuel destruction can mutate item storage; reacquire the object by handle.
    Require(Live(impl_->objects, handle)->behavior_->SetForgeState(state).has_value());
    impl_->MarkDirty(ChunkOf(GetObject(handle)->position), false);
    EndCraftingMutation();
    return StationActionResult{outcome, action == ForgeAction::Ignite ? Tick{400} : Tick{100}};
}

WorldResult<void> World::SpendForgeHeat(ObjectHandle handle, std::uint64_t heat) {
    auto* object = Live(impl_->objects, handle);
    if (!object) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto* definition = impl_->content.TryGetObject(object->type);
    if (!definition || definition->behavior != ObjectBehaviorKind::Forge || !object->enabled || object->broken) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    auto state = object->behavior_->Describe().forge;
    if (!state.ready || !state.fuel || heat > state.fuel) { return eastl::unexpected(WorldError::invalid_request); }
    state.fuel -= heat;
    if (!state.fuel) { state.ready = false; }
    Require(object->behavior_->SetForgeState(state).has_value());
    impl_->MarkDirty(ChunkOf(object->position), false);
    return {};
}

bool World::CanInspectPower(ActorHandle actorHandle) const {
    const auto* actor = GetActor(actorHandle);
    if (!actor || !IsActorAlive(actorHandle) || actor->parked || !IsActive(actor->position)) { return false; }
    const auto hasMeter = [&](auto&& self, ItemHandle handle) -> bool {
        const auto* item = GetItem(handle);
        if (!item || item->location.installation.value || IsReservedItem(handle)) { return false; }
        if (impl_->content.GetItem(item->type).gridMeter) { return true; }
        for (const auto child : item->contents) { if (self(self, child)) { return true; } }
        return false;
    };
    for (const auto handle : actor->inventory) { if (hasMeter(hasMeter, handle)) { return true; } }
    return false;
}

void World::CollectChunkResidencyDebug(Vector<ChunkResidencyDebug>& output,
    ChunkResidencyCounters& counters) const {
    output.clear(); counters = {};
    Vector<PowerColumnDebug> machines;
    impl_->powerSystem.CollectWorkshopDebug(machines);
    for (const auto& column : impl_->columns_) {
        ChunkResidencyDebug entry;
        entry.column = column.first;
        entry.active = column.second.layers.front().active;
        for (const auto& request : impl_->requests) {
            if (!RequestContains(request, column.first)) { continue; }
            if (request.reason == LoadReason::Player) {
                if (request.mode == LoadMode::active) { ++entry.playerActiveRequests; }
                else { ++entry.playerResidentRequests; }
            } else if (request.mode == LoadMode::active) { ++entry.otherActiveRequests; }
            else { ++entry.otherResidentRequests; }
        }
        if (eastl::find(impl_->playerPowerDemand.begin(), impl_->playerPowerDemand.end(), column.first)
            != impl_->playerPowerDemand.end()) { ++entry.playerActiveRequests; }
        for (const auto& machine : machines) {
            if (machine.column == column.first) {
                entry.machineOwners = machine.ownerHolds;
                entry.machineDependencies = machine.sourceDependencies;
                break;
            }
        }
        const bool player = entry.playerActiveRequests || entry.playerResidentRequests;
        const bool machine = entry.machineOwners || entry.machineDependencies;
        ++counters.loadedColumns;
        counters.activeColumns += entry.active;
        counters.playerColumns += player;
        counters.machineColumns += machine;
        counters.machineOnlyColumns += machine && !player;
        counters.sharedColumns += machine && player;
        output.push_back(entry);
    }
    eastl::sort(output.begin(), output.end(), [](const auto& a, const auto& b) {
        return a.column.x != b.column.x ? a.column.x < b.column.x : a.column.z < b.column.z;
    });
    counters.loadedChunks = std::uint64_t{counters.loadedColumns} * world_layer_count;
    counters.archivedChunks = ArchivedChunkCount();
}
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
CombatSystem& World::Combat() { return impl_->combatSystem; }
const CombatSystem& World::Combat() const { return impl_->combatSystem; }
Actor* World::MutableActor(ActorHandle handle) { return Live(impl_->actors, handle); }
void World::ActorChanged(ActorHandle handle) {
    const auto* actor = GetActor(handle);
    Require(actor != nullptr);
    impl_->MarkDirty(ChunkOf(actor->position), false);
}
const Item* World::GetItem(ItemHandle handle) const { return Live(impl_->items, handle); }
const InteractiveObject* World::GetObject(ObjectHandle handle) const { return Live(impl_->objects, handle); }
ActorHandle World::FindActor(PersistentId id) const { return impl_->FindActor(id); }

ActorHandle World::NextActorInChunk(ChunkCoord position, PersistentId after) const {
    const Chunk* chunk = GetChunk(position);
    if (!chunk) { return {}; }
    const auto found = eastl::upper_bound(chunk->actors.begin(), chunk->actors.end(), after,
        [this](PersistentId id, ActorHandle candidate) { return id < GetActor(candidate)->id; });
    return found == chunk->actors.end() ? ActorHandle{} : *found;
}
eastl::optional<TileCoord> World::FindPlayerPosition(PersistentId id) const {
    return impl_->PlayerPosition(id);
}
ItemHandle World::FindItem(PersistentId id) const { return impl_->FindItem(id); }
ObjectHandle World::FindObject(PersistentId id) const { return impl_->FindObject(id); }

ActorHandle World::ActorAt(TileCoord position) const {
    const Chunk* value = ValidPosition(position) ? GetChunk(ChunkOf(position)) : nullptr;
    return value ? value->actorOccupancy[TileIndex(position)] : ActorHandle{};
}

ItemRange::ItemRange(const World& world, const Chunk* chunk, TileCoord position)
    : world_(&world), items_(chunk ? &chunk->ground_items : nullptr), position_(position) {}

ItemRange::Iterator::Iterator(const World* world, const Vector<ItemHandle>* items,
    TileCoord position, std::size_t index)
    : world_(world), items_(items), position_(position), index_(index) {
    SkipNonmatching();
}

void ItemRange::Iterator::SkipNonmatching() {
    while (items_ && index_ < items_->size()) {
        const Item* item = world_->GetItem((*items_)[index_]);
        Require(item != nullptr);
        if (item->location.position == position_) { break; }
        ++index_;
    }
}

ItemHandle ItemRange::Iterator::operator*() const {
    Require(items_ && index_ < items_->size());
    return (*items_)[index_];
}

ItemRange::Iterator& ItemRange::Iterator::operator++() {
    Require(items_ && index_ < items_->size());
    ++index_;
    SkipNonmatching();
    return *this;
}

ItemRange::Iterator ItemRange::Iterator::operator++(int) {
    Iterator previous = *this;
    ++*this;
    return previous;
}

ItemRange::Iterator ItemRange::begin() const { return {world_, items_, position_, 0}; }
ItemRange::Iterator ItemRange::end() const {
    return {world_, items_, position_, items_ ? items_->size() : 0};
}
bool ItemRange::Empty() const { return begin() == end(); }

ItemRange World::ItemsAt(TileCoord position) const {
    const Chunk* value = ValidPosition(position) ? GetChunk(ChunkOf(position)) : nullptr;
    return ItemRange(*this, value, position);
}

std::size_t World::CountItemsAt(TileCoord position) const {
    std::size_t result = 0;
    for ([[maybe_unused]] ItemHandle handle : ItemsAt(position)) { ++result; }
    return result;
}

void World::CollectItemsAt(TileCoord position, Vector<ItemHandle>& output) const {
    output.clear();
    for (ItemHandle handle : ItemsAt(position)) { output.push_back(handle); }
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
                if (!tile || !Passable(impl_->content, tile->terrain) || ActorAt(below).value
                    || (entry.actor.value && BlocksMovement(below))) { break; }
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
    if (BlocksMovement(position)) {
        return eastl::unexpected(WorldError::blocked);
    }
    if (ActorAt(position).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    const auto key = impl_->actors.emplace(Actor(impl_->IssueId(), position, player));
    const ActorHandle handle{key.raw};
    Require(impl_->actorIds.try_emplace(GetActor(handle)->id, handle).second);
    MutableActor(handle)->combat.styleId = impl_->content.FindCombatStyle("basic_balanced");
    Require(MutableActor(handle)->combat.styleId.value != 0);
    impl_->InsertChunkActor(*impl_->GetChunk(ChunkOf(position)), handle);
    impl_->OccupyActorTile(handle);
    impl_->RefreshSchedule(*Live(impl_->actors, handle));
    if (player) { impl_->TrackPlayer(GetActor(handle)->id, position); }
    impl_->MarkDirty(ChunkOf(position));
    impl_->QueueGravity(position);
    return handle;
}

WorldResult<ActorHandle> World::CreateMonster(MonsterTypeId type, TileCoord position) {
    const auto* definition = impl_->content.TryGetMonster(type);
    if (!definition) { return eastl::unexpected(WorldError::invalid_request); }
    const auto* profile = impl_->content.TryGetAiProfile(definition->aiProfile);
    if (!profile) { return eastl::unexpected(WorldError::invalid_request); }
    Impl::GravityBatch batch(*this);
    const auto created = CreateActor(position, false);
    if (!created) { return created; }
    auto* actor = Live(impl_->actors, *created);
    actor->combat = MakeCombatState(false);
    actor->combat.body = MakeBody(definition->body);
    actor->combat.faction = definition->faction;
    actor->combat.attackDamage = definition->attackDamage;
    actor->combat.accuracy = static_cast<std::uint8_t>(definition->accuracy / 100);
    actor->combat.styleId = definition->combatStyle;
    actor->combat.authoredTrainingRanks = definition->authoredTrainingRanks;
    actor->combat.offensiveTrainingRank = definition->offensiveTrainingRank;
    actor->combat.defensiveTrainingRank = definition->defensiveTrainingRank;
    actor->combat.attackRating100 = definition->attackRating100;
    actor->combat.defenseRating100 = definition->defenseRating100;
    actor->combat.inexhaustible = definition->inexhaustible;
    actor->combat.reach = definition->reach;
    actor->speed = definition->speed;
    actor->controller_ = MakeSightedAiController(profile->id);
    actor->monsterType = type;
    impl_->RefreshSchedule(*actor);
    return created;
}

DataObject World::CaptureActorController(ActorHandle handle) const {
    const Actor* actor = GetActor(handle);
    return actor ? CaptureAiController(actor->controller_.get()) : DataObject{};
}

AiInspection World::InspectActorAi(ActorHandle handle) const {
    const Actor* actor = GetActor(handle);
    return actor && actor->controller_ ? actor->controller_->Inspect() : AiInspection{};
}

WorldResult<void> World::SetActorAiProfile(ActorHandle handle, eastl::string_view profile) {
    Actor* actor = Live(impl_->actors, handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    if (actor->player || !impl_->content.FindAiProfile(profile).value) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    actor->controller_ = MakeSightedAiController(profile);
    impl_->RefreshSchedule(*actor);
    impl_->MarkDirty(ChunkOf(actor->position), false);
    return {};
}

WorldResult<void> World::SetActorController(ActorHandle handle, AiControllerKind kind) {
    Actor* actor = Live(impl_->actors, handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    if (actor->player && kind != AiControllerKind::None) { return eastl::unexpected(WorldError::invalid_request); }
    auto controller = MakeAiController(kind);
    if (!controller) { return eastl::unexpected(WorldError::invalid_request); }
    actor->controller_ = eastl::move(*controller);
    impl_->RefreshSchedule(*actor);
    impl_->MarkDirty(ChunkOf(actor->position), false);
    return {};
}

WorldResult<void> World::TryActorStep(ActorHandle handle, TileCoord direction, bool activeOnly) {
    const Actor* actor = GetActor(handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!IsActorAlive(handle) || actor->parked || !IsActive(actor->position)) { return eastl::unexpected(WorldError::chunk_unavailable); }
    // Explicit comparisons also avoid overflow for fabricated direction values.
    const bool cardinal = (direction == TileCoord{1, 0, 0}) || (direction == TileCoord{-1, 0, 0})
        || (direction == TileCoord{0, 1, 0}) || (direction == TileCoord{0, -1, 0})
        || (direction == TileCoord{0, 0, 1}) || (direction == TileCoord{0, 0, -1});
    const bool diagonal = direction.y == 0 && (direction.x == -1 || direction.x == 1)
        && (direction.z == -1 || direction.z == 1);
    if (!cardinal && !diagonal) { return eastl::unexpected(WorldError::invalid_request); }
    const TileCoord source = actor->position;
    const TileCoord target{source.x + direction.x, source.z + direction.z, source.y + direction.y};
    if (const auto available = impl_->Available(target); !available) { return available; }
    if (activeOnly && !IsActive(target)) { return eastl::unexpected(WorldError::chunk_unavailable); }
    if (direction.y != 0 && (!impl_->content.GetTerrain(GetTile(source)->terrain).stairs
        || !impl_->content.GetTerrain(GetTile(target)->terrain).stairs)) {
        return eastl::unexpected(WorldError::blocked);
    }
    return MoveActor(handle, target);
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
    if (BlocksMovement(destination)) {
        return eastl::unexpected(WorldError::blocked);
    }
    if (!value->parked && ActorAt(destination).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    impl_->QueueGravity(value->position);
    impl_->QueueGravity(destination);
    const ChunkCoord previous = ChunkOf(value->position);
    const ChunkCoord next = ChunkOf(destination);
    impl_->VacateActorTile(handle);
    if (previous != next) {
        EraseOne(impl_->GetChunk(previous)->actors, handle);
        impl_->InsertChunkActor(*impl_->GetChunk(next), handle);
    }
    value->position = destination;
    impl_->OccupyActorTile(handle);
    impl_->RefreshSchedule(*value);
    if (value->light.intensity && !value->parked) {
        impl_->MarkLightingDirty(previous);
        if (next != previous) { impl_->MarkLightingDirty(next); }
    }
    if (value->player) { impl_->TrackPlayer(value->id, destination); }
    impl_->MarkDirty(previous);
    impl_->MarkDirty(next);
    // A pending activity cannot accompany an authoritative displacement intact.
    // Ordinary steps have no pending activity; gravity and external moves do.
    if (!value->parked) { impl_->combatSystem.Disrupt(handle, {DisruptionCause::ForcedMovement}); }
    return {};
}

WorldResult<void> World::SetActorParked(ActorHandle handle, bool parked) {
    Impl::GravityBatch batch(*this);
    Actor* value = Live(impl_->actors, handle);
    if (!value) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!value->player) { return eastl::unexpected(WorldError::invalid_request); }
    if (!parked && !IsActorAlive(handle)) { return eastl::unexpected(WorldError::invalid_request); }
    if (value->parked == parked) { return {}; }
    if (!parked) {
        if (BlocksMovement(value->position)) {
            return eastl::unexpected(WorldError::blocked);
        }
        if (ActorAt(value->position).value != 0) {
            return eastl::unexpected(WorldError::occupied);
        }
    }
    impl_->VacateActorTile(handle);
    value->parked = parked;
    impl_->OccupyActorTile(handle);
    impl_->RefreshSchedule(*value);
    if (parked) { impl_->craftingSystem.ReleaseUnavailableStations(); }
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
    impl_->VacateActorTile(handle);
    if (value->player) { impl_->ForgetPlayer(value->id); }
    impl_->craftingSystem.ReleaseWorkerStations(value->id);
    if (value->light.intensity && !value->parked) { impl_->MarkLightingDirty(ChunkOf(position)); }
    Require(impl_->actorIds.erase(value->id) == 1);
    impl_->actors.erase(dod::slot_map64<Actor>::key{handle.value});
    impl_->MarkDirty(ChunkOf(position));
    return {};
}

bool World::IsActorAlive(ActorHandle handle) const {
    const auto* actor = GetActor(handle);
    return actor && actor->combat.life == ActorLifeState::Alive;
}

void World::MarkActorDead(ActorHandle handle, PersistentId killer) {
    auto* actor = MutableActor(handle);
    if (!actor || (actor->combat.life == ActorLifeState::Dead && actor->combat.corpse)) { return; }
    const bool newlyDead = actor->combat.life != ActorLifeState::Dead;
    const auto position = actor->position;
    const auto id = actor->id;
    // Release funded work without completing it or destroying the job. Keep the
    // paid remainder on its wrapper for a future living worker to resume.
    Vector<ItemHandle> jobs;
    for (const auto& entry : impl_->items.items()) {
        const ItemHandle item{entry.first.raw};
        const auto job = InspectCrafting(item);
        if (job && job->worker == id) { jobs.push_back(item); }
    }
    for (const auto item : jobs) {
        auto data = CaptureItemState(item);
        auto state = ReadCraftingState(*data.Find("crafting_job"));
        Require(state.has_value());
        state->worker = 0; state->pauseRequested = false; state->status = CraftingStatus::WorkerUnavailable;
        state->stations = {};
        data.Set("crafting_job", WriteCraftingState(*state));
        DetachCrafting(item);
        Require(impl_->itemSystem.Initialize(*MutableItem(item), &data).has_value());
        MarkItemDirty(item);
    }
    actor = MutableActor(handle);
    impl_->VacateActorTile(handle);
    actor->combat.life = ActorLifeState::Dead;
    actor->combat.action = {};
    actor->learning.study = {};
    if (actor->characterCombat) {
        actor->characterCombat->manuals.clear();
    }
    actor->actionDeadline.Set(impl_->tick, 0);
    actor->actionDeadline.SetRunning(impl_->tick, false);
    if (actor->player) { actor->parked = true; }
    impl_->MarkLightingDirty(ChunkOf(position));
    ActorChanged(handle);
    if (newlyDead) {
        CombatEvent event;
        event.kind = CombatEventKind::ActorDied;
        event.tick = GetTick(); event.actor = id; event.opponent = killer;
        Combat().EmitEvent(event);
    }
}

void World::FinalizeActorDeaths() {
    Impl::GravityBatch batch(*this);
    Vector<PersistentId> pending;
    for (const auto& actor : impl_->actors) {
        if (actor.combat.life == ActorLifeState::Dead && !actor.combat.corpse) { pending.push_back(actor.id); }
    }
    eastl::sort(pending.begin(), pending.end());
    for (const auto id : pending) {
        const auto handle = FindActor(id);
        // Legacy readers may have marked terminal life before items (and their
        // crafting-worker assignments) existed. Complete cleanup after restore.
        MarkActorDead(handle);
        const auto position = GetActor(handle)->position;
        const bool player = GetActor(handle)->player;
        const auto created = CreateItem(GetContent().CorpseItem(), ItemLocation::Ground(position));
        Require(created.has_value());
        const auto corpse = *created;
        MutableActor(handle)->combat.corpse = GetItem(corpse)->id;
        // Terminal ownership transfer bypasses capacity and reservation checks.
        // Root identity and all nested contents survive intact.
        MutableActor(handle)->equipment = {};
        MutableActor(handle)->wieldedItem = {};
        while (!GetActor(handle)->inventory.empty()) {
            const auto item = GetActor(handle)->inventory.back();
            MutableActor(handle)->inventory.pop_back();
            MutableItem(item)->location = ItemLocation::Inside(corpse);
            MutableItem(corpse)->contents.push_back(item);
            impl_->itemSystem.Notify({ItemChangeKind::Moved, item, {}});
        }
        impl_->itemSystem.Notify({ItemChangeKind::ContentsChanged, corpse, {}});
        ActorChanged(handle);
        impl_->QueueGravity(position);
        if (!player) { Require(DestroyActor(handle).has_value()); }
    }
}

std::uint32_t World::ActorMovementBaseCost(ActorHandle handle, std::uint32_t authoredBase) const {
    const auto* actor = GetActor(handle);
    Require(actor && authoredBase > 0);
    const auto scaled = (std::uint64_t{authoredBase} * LocomotionBaseCost(actor->combat.body) + 99) / 100;
    Require(scaled <= UINT32_MAX);
    return combat_runtime::MovementEncumbranceCost(*this, *actor, static_cast<std::uint32_t>(scaled));
}

WorldResult<void> World::SetActorExertion(ActorHandle handle, ExertionKind exertion) {
    auto* actor = MutableActor(handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!IsActorAlive(handle) || exertion > ExertionKind::Combat) { return eastl::unexpected(WorldError::invalid_request); }
    if (actor->combat.exertion != exertion) { actor->combat.exertion = exertion; ActorChanged(handle); }
    return {};
}

WorldResult<ItemHandle> World::CreateItem(ItemTypeId type, ItemLocation location) {
    Impl::GravityBatch batch(*this);
    const ItemDefinition* definition = impl_->content.TryGetItem(type);
    if (!definition) { return eastl::unexpected(WorldError::invalid_request); }
    if (!impl_->craftingMutationDepth && (definition->unfinishedCraft
        || (location.kind == LocationKind::item && GetItem(location.item)
            && (IsReservedItem(location.item) || impl_->content.GetItem(GetItem(location.item)->type).unfinishedCraft)))) {
        return eastl::unexpected(WorldError::invalid_request);
    }
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

bool World::CanUseWieldedItem(ActorHandle handle) const {
    const auto* actor = GetActor(handle);
    if (!actor || !IsActorAlive(handle)) { return false; }
    if (!actor->wieldedItem.value) { return true; }
    const auto* item = GetItem(actor->wieldedItem);
    if (!item || item->location != ItemLocation::Carried(handle)) { return false; }
    const auto& body = actor->combat.body;
    if (body.kind == BodyKind::Simple) { return false; }
    bool held = false;
    for (unsigned hand = 0; hand < 2; ++hand) {
        if (actor->equipment.hands[hand].item != actor->wieldedItem) { continue; }
        held = true;
        if (!Functional(body, hand == 0 ? BodyPart::LeftHand : BodyPart::RightHand)) { return false; }
    }
    return held;
}

void World::ClearWieldedItem(ItemHandle handle) {
    const auto* item = GetItem(handle);
    if (!item || item->location.kind != LocationKind::actor) { return; }
    const auto owner = item->location.actor;
    auto* actor = MutableActor(owner);
    bool changed = false;
    for (auto& held : actor->equipment.hands) {
        if (held.item == handle) { held = {}; changed = true; }
    }
    auto& worn = actor->equipment.worn;
    const auto found = eastl::find(worn.begin(), worn.end(), handle);
    if (found != worn.end()) { worn.erase(found); changed = true; }
    if (!changed && actor->wieldedItem != handle) { return; }
    actor->wieldedItem = actor->equipment.hands[0].item.value
        ? actor->equipment.hands[0].item : actor->equipment.hands[1].item;
    const auto id = item->id;
    const auto& action = actor->combat.action;
    const bool required = action.definitionDriven
        ? eastl::any_of(action.sources.hands.begin(), action.sources.hands.end(), [id](const CommittedHand& hand) {
            return hand.required && hand.item == id;
        }) : action.weapon == id;
    if (required && action.kind != ActorActionKind::None) {
        Combat().Disrupt(owner, {DisruptionCause::RequiredCapabilityLost});
    }
    ActorChanged(owner);
}

InventoryResult World::WieldItem(ActorHandle handle, ItemHandle selected) {
    auto* actor = MutableActor(handle);
    if (!actor || actor->parked || !IsActive(actor->position)) { return {InventoryOutcome::Inaccessible}; }
    if (GetActorRemainingTicks(handle) || actor->combat.action.kind != ActorActionKind::None
        || impl_->learningSystem.Busy(handle)) { return {InventoryOutcome::Busy}; }
    if (!IsActorAlive(handle)) { return {InventoryOutcome::Inaccessible}; }
    const auto old = actor->wieldedItem;
    if (old == selected) { return {InventoryOutcome::AlreadyThere}; }
    if (!selected.value) {
        if (!old.value) { return {InventoryOutcome::AlreadyThere}; }
        return ReleaseHand(handle, actor->equipment.hands[0].item == old ? Hand::Left : Hand::Right);
    }
    const auto* item = GetItem(selected);
    if (!item) { return {InventoryOutcome::InvalidItem}; }
    for (auto current = selected; current.value;) {
        const auto* value = GetItem(current);
        if (!value) { return {InventoryOutcome::InvalidItem}; }
        if (IsReservedItem(current) || value->location.installation.value
            || impl_->content.GetItem(value->type).unfinishedCraft) { return {InventoryOutcome::Reserved}; }
        if (value->location.kind != LocationKind::item) { break; }
        current = value->location.item;
    }
    const auto& grips = ItemMelee(impl_->content.GetItem(item->type)).grips;
    const auto root = impl_->Root(item->location);
    if (root.kind != LocationKind::actor || root.actor != handle) { return {InventoryOutcome::Inaccessible}; }
    // Inventory UI replacement is explicit: release only the old left-first
    // selection. Preserve all other equipment and roll back on any refusal.
    auto saved = eastl::move(actor->equipment);
    actor->equipment = saved;
    for (auto& held : actor->equipment.hands) { if (held.item == old) { held = {}; } }
    actor->wieldedItem = actor->equipment.hands[0].item.value
        ? actor->equipment.hands[0].item : actor->equipment.hands[1].item;
    bool needsHands = false;
    bool occupied = false;
    for (const auto& grip : grips) {
        for (const auto hand : {Hand::Left, Hand::Right}) {
            const auto query = QueryEquipment(handle, {EquipmentOperation::Hold, item->id, hand, grip.kind});
            if (query.reason != CombatRefusal::None) {
                needsHands |= query.reason == CombatRefusal::MissingCapability;
                occupied |= query.reason == CombatRefusal::NeedsFreeHand;
                continue;
            }
            const auto result = HoldItem(handle, selected, hand, grip.kind);
            if (result.outcome == InventoryOutcome::Succeeded) { return result; }
            MutableActor(handle)->equipment = eastl::move(saved);
            MutableActor(handle)->wieldedItem = old;
            return result;
        }
    }
    actor->equipment = eastl::move(saved);
    actor->wieldedItem = old;
    return {needsHands && !occupied ? InventoryOutcome::NeedsHands : InventoryOutcome::InvalidDestination};
}

WorldResult<void> World::TransferItem(ItemHandle handle, ItemLocation location) {
    Impl::GravityBatch batch(*this);
    if (!impl_->craftingMutationDepth && (IsReservedItem(handle)
        || (location.kind == LocationKind::item && GetItem(location.item)
            && (IsReservedItem(location.item) || impl_->content.GetItem(GetItem(location.item)->type).unfinishedCraft)))) {
        return eastl::unexpected(WorldError::invalid_request);
    }
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
    ClearWieldedItem(handle);
    EraseOne(impl_->Collection(value->location), handle);
    impl_->Collection(*destination).push_back(handle);
    value->location = *destination;
    impl_->craftingSystem.ReleaseUnavailableStations();
    impl_->itemSystem.BeginCallback();
    for (ItemHandle current : moved) { impl_->itemSystem.Notify({ItemChangeKind::Moved, current, {}}); }
    if (old.kind == LocationKind::item) { impl_->itemSystem.Notify({ItemChangeKind::ContentsChanged, old.item, old.installation}); }
    if (destination->kind == LocationKind::item) { impl_->itemSystem.Notify({ItemChangeKind::ContentsChanged, destination->item, destination->installation}); }
    impl_->itemSystem.EndCallback();
    return {};
}

WorldResult<void> World::DestroyItem(ItemHandle handle) {
    Impl::GravityBatch batch(*this);
    if (!impl_->craftingMutationDepth && IsReservedItem(handle)) { return eastl::unexpected(WorldError::invalid_request); }
    const Item* value = GetItem(handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    ClearWieldedItem(handle);
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

std::uint64_t World::GetItemVolume(ItemHandle handle) const {
    return GetItem(handle) ? impl_->ExternalVolume(handle) : 0;
}

bool World::IsReservedItem(ItemHandle handle) const {
    const Item* item = GetItem(handle);
    for (unsigned depth = 0; item && item->location.kind == LocationKind::item && depth < 64; ++depth) {
        item = GetItem(item->location.item);
        if (item && impl_->content.GetItem(item->type).unfinishedCraft) { return true; }
    }
    if (item && item->location.kind == LocationKind::object) {
        const auto* object = GetObject(item->location.object);
        return object && object->equipment.value && GetItem(object->equipment) == item;
    }
    return false;
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
    if (!actor || !IsActorAlive(actorHandle) || actor->parked || !IsActive(actor->position) || !item || IsReservedItem(itemHandle)) { return false; }
    const ItemLocation root = impl_->Root(item->location);
    return (root.kind == LocationKind::actor && root.actor == actorHandle)
        || (root.kind == LocationKind::ground && root.position == actor->position)
        || (root.kind == LocationKind::object && CanAccessObjectStorage(actorHandle, root.object));
}

ItemActionList World::QueryItemActions(ActorHandle actor, ItemHandle handle) const {
    return impl_->itemSystem.QueryActions(actor, handle);
}

ItemUseResult World::UseItem(ActorHandle actor, ItemHandle handle, const ItemUseRequest& request) {
    Impl::GravityBatch batch(*this);
    return impl_->itemSystem.Use(actor, handle, request);
}

WorldResult<void> World::SetItemEnergy(ItemHandle handle, ItemCapabilityId capability, std::uint32_t energy) {
    Impl::GravityBatch batch(*this);
    const auto result = impl_->itemSystem.SetEnergy({handle, capability}, energy);
    if (result) {
        const auto* item = GetItem(handle);
        if (item && item->location.kind == LocationKind::object) {
            const auto* object = GetObject(item->location.object);
            const auto* definition = object ? impl_->content.TryGetObject(object->type) : nullptr;
            if (definition && definition->behavior == ObjectBehaviorKind::GridBattery) { impl_->powerSystem.Invalidate(); }
        }
    }
    return result;
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
bool World::ItemEligible(ItemHandle item) const { return impl_->Eligible(item) && !IsReservedItem(item); }
TileCoord World::ItemRootPosition(ItemHandle handle) const {
    const auto* item = GetItem(handle);
    Require(item != nullptr);
    const auto root = impl_->Root(item->location);
    switch (root.kind) {
    case LocationKind::ground: return root.position;
    case LocationKind::actor: return GetActor(root.actor)->position;
    case LocationKind::object: return GetObject(root.object)->position;
    default: std::abort();
    }
}
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

void World::AttachCrafting(ItemHandle item) { impl_->craftingSystem.Attach(item); }
void World::DetachCrafting(ItemHandle item) { impl_->craftingSystem.Detach(item); }
void World::BeginCraftingMutation() {
    ++impl_->craftingMutationDepth;
    impl_->itemSystem.BeginCallback();
}
void World::EndCraftingMutation() {
    Require(impl_->craftingMutationDepth > 0);
    --impl_->craftingMutationDepth;
    impl_->itemSystem.EndCallback();
}
WorldResult<void> World::SetCraftingSettings(CraftingSettings settings) { return impl_->craftingSystem.SetSettings(settings); }
LearningSystem& World::Learning() { return impl_->learningSystem; }
const LearningSystem& World::Learning() const { return impl_->learningSystem; }
bool LearningSystem::CraftingBusy(ActorHandle actor) const { return world_.impl_->craftingSystem.Busy(actor); }
void World::LearnRecipe(PersistentId actor, CraftingRecipeId recipe, std::uint64_t points) {
    Require(impl_->content.TryGetCraftingRecipe(recipe) != nullptr);
    if (!actor || !points) { return; }
    const Impl::RecipeCredit credit{actor, recipe, points};
    if (!impl_->ApplyRecipeCredit(*this, credit, nullptr)) { impl_->pendingRecipeCredits.push_back(credit); }
}
CraftingSettings World::GetCraftingSettings() const { return impl_->craftingSystem.Settings(); }
WorldResult<CraftingResult> World::StartCrafting(ActorHandle actor, CraftingRecipeId recipe) {
    if (GetActor(actor) && !IsActorAlive(actor)) { return CraftingResult{CraftingOutcome::ActorUnavailable}; }
    if (impl_->learningSystem.Busy(actor)) { return CraftingResult{CraftingOutcome::ActorBusy}; }
    Impl::GravityBatch batch(*this); BeginCraftingMutation();
    auto result = impl_->craftingSystem.Start(actor, recipe); EndCraftingMutation(); return result;
}
WorldResult<CraftingResult> World::StartRecycling(ActorHandle actor, ItemHandle target, RecyclingRecipeId recipe) {
    if (GetActor(actor) && !IsActorAlive(actor)) { return CraftingResult{CraftingOutcome::ActorUnavailable}; }
    if (impl_->learningSystem.Busy(actor)) { return CraftingResult{CraftingOutcome::ActorBusy}; }
    Impl::GravityBatch batch(*this); BeginCraftingMutation();
    auto result = impl_->craftingSystem.Recycle(actor, target, recipe); EndCraftingMutation(); return result;
}
WorldResult<CraftingResult> World::ResumeCrafting(ActorHandle actor, ItemHandle item) {
    if (GetActor(actor) && !IsActorAlive(actor)) { return CraftingResult{CraftingOutcome::ActorUnavailable}; }
    if (impl_->learningSystem.Busy(actor)) { return CraftingResult{CraftingOutcome::ActorBusy}; }
    Impl::GravityBatch batch(*this); BeginCraftingMutation();
    auto result = impl_->craftingSystem.Resume(actor, item); EndCraftingMutation(); return result;
}
WorldResult<CraftingResult> World::PauseCrafting(ActorHandle actor, ItemHandle item) {
    if (GetActor(actor) && !IsActorAlive(actor)) { return CraftingResult{CraftingOutcome::ActorUnavailable}; }
    return impl_->craftingSystem.Pause(actor, item);
}
WorldResult<CraftingResult> World::CancelCrafting(ActorHandle actor, ItemHandle item) {
    if (GetActor(actor) && !IsActorAlive(actor)) { return CraftingResult{CraftingOutcome::ActorUnavailable}; }
    Impl::GravityBatch batch(*this); BeginCraftingMutation();
    auto result = impl_->craftingSystem.Cancel(actor, item); EndCraftingMutation(); return result;
}
eastl::optional<CraftingJobView> World::InspectCrafting(ItemHandle item) const { return impl_->craftingSystem.Inspect(item); }
void World::CollectCraftingJobs(ActorHandle actor, Vector<CraftingJobView>& output) const { impl_->craftingSystem.Collect(actor, output); }
bool World::ProcessCraftingEvents() {
    Impl::GravityBatch batch(*this); BeginCraftingMutation();
    const bool result = impl_->craftingSystem.ProcessEvents(); EndCraftingMutation(); return result;
}

WorldResult<ObjectHandle> World::CreateObject(TileCoord position, bool enabled) {
    if (auto result = impl_->Available(position); !result) {
        return eastl::unexpected(result.error());
    }
    if (ObjectAt(position).value != 0) {
        return eastl::unexpected(WorldError::occupied);
    }
    const auto key = impl_->objects.emplace(InteractiveObject(impl_->IssueId(), position, enabled));
    const ObjectHandle handle{key.raw};
    impl_->GetChunk(ChunkOf(position))->objects.push_back(handle);
    impl_->RefreshSchedule(*Live(impl_->objects, handle));
    impl_->MarkDirty(ChunkOf(position), true, true);
    return handle;
}

WorldResult<ObjectHandle> World::CreateObject(ObjectTypeId type, TileCoord position, bool enabled) {
    return CreateTypedObject(type, position, enabled, {});
}

WorldResult<ObjectHandle> World::CreateTypedObject(ObjectTypeId type, TileCoord position, bool enabled, ItemHandle equipment) {
    const auto* definition = impl_->content.TryGetObject(type);
    if (!definition) { return eastl::unexpected(WorldError::invalid_request); }
    if (auto available = impl_->Available(position); !available) { return eastl::unexpected(available.error()); }
    // Administrative creation retains legacy placements (including fixtures in
    // open space). Authored population and furniture movement require support.
    if (definition->blocksMovement && ActorAt(position).value) { return eastl::unexpected(WorldError::occupied); }
    if (ObjectAt(position).value) { return eastl::unexpected(WorldError::occupied); }
    auto behavior = MakeObjectBehavior(*definition);
    if (!behavior) { return eastl::unexpected(WorldError::invalid_request); }
    // Install the type before scheduling: typed objects never run the legacy
    // periodic deadline, including at the end of the representable clock.
    InteractiveObject object(impl_->IssueId(), position, enabled);
    object.type = type;
    object.kind = definition->kind;
    object.light = definition->light;
    object.behavior_ = eastl::move(*behavior);
    const auto key = impl_->objects.emplace(eastl::move(object));
    const ObjectHandle handle{key.raw};
    impl_->GetChunk(ChunkOf(position))->objects.push_back(handle);
    if (definition->packingItem.value) {
        BeginCraftingMutation();
        if (!equipment.value) {
            const auto created = CreateItem(definition->packingItem, ItemLocation::Ground(position));
            Require(created.has_value()); equipment = *created;
        }
        Live(impl_->objects, handle)->equipment = equipment;
        Require(TransferItem(equipment, ItemLocation::Stored(handle)).has_value());
        EndCraftingMutation();
    }
    impl_->RefreshSchedule(*Live(impl_->objects, handle));
    impl_->MarkDirty(ChunkOf(position), true, true);
    impl_->MarkGeometryDirty(ChunkOf(position));
    impl_->MarkLightingDirty(ChunkOf(position));
    impl_->powerSystem.SyncObject(handle);
    return handle;
}

bool World::BlocksMovement(TileCoord position) const {
    const auto* tile = GetTile(position);
    if (!tile || !Passable(impl_->content, tile->terrain)) { return true; }
    const auto* object = GetObject(ObjectAt(position));
    const auto* definition = object ? impl_->content.TryGetObject(object->type) : nullptr;
    return definition && definition->blocksMovement;
}

bool World::BlocksSight(TileCoord position) const {
    const auto* tile = GetTile(position);
    if (!tile || impl_->content.GetTerrain(tile->terrain).blocksSight) { return true; }
    const auto* object = GetObject(ObjectAt(position));
    const auto* definition = object ? impl_->content.TryGetObject(object->type) : nullptr;
    return definition && definition->blocksSight;
}

bool World::HasDirectSkyExposure(TileCoord position) const {
    if (!ValidPosition(position) || !GetTile(position)) { return false; }
    // The supporting tile is an incident top surface, not an obstruction.
    // Every face above it must be open and every overhead cell transparent,
    // matching direct-sky propagation without consulting the lighting cache.
    for (TileCoord lower = position; lower.y < world_max_y; ++lower.y) {
        const TileCoord upper{lower.x, lower.z, lower.y + 1};
        if (!OpenVerticalFace(*this, lower, upper) || BlocksSight(upper)) { return false; }
    }
    return true;
}

std::uint64_t World::GetObjectUsedVolume(ObjectHandle handle) const {
    return GetObject(handle) ? impl_->ObjectVolume(handle) : 0;
}

bool World::CanAccessObjectStorage(ActorHandle actorHandle, ObjectHandle handle) const {
    const auto* actor = GetActor(actorHandle);
    return actor && IsActorAlive(actorHandle) && !actor->parked && IsActive(actor->position)
        && CanReachStorage(*this, actor->position, handle, 1);
}

WorldResult<void> World::MoveObject(ObjectHandle handle, TileCoord destination) {
    const auto* object = GetObject(handle);
    if (!object) { return eastl::unexpected(WorldError::invalid_handle); }
    if (object->position == destination) { return {}; }
    if (auto available = impl_->Available(destination); !available) { return available; }
    const auto& terrain = impl_->content.GetTerrain(GetTile(destination)->terrain);
    if (!terrain.passable || terrain.openSpace) { return eastl::unexpected(WorldError::blocked); }
    const auto* definition = impl_->content.TryGetObject(object->type);
    if (ObjectAt(destination).value || (definition && definition->blocksMovement && ActorAt(destination).value)) {
        return eastl::unexpected(WorldError::occupied);
    }
    if (!object->type.value && object->enabled && IsActive(destination)
        && object->updateDeadline.Remaining(impl_->tick) > UINT64_MAX - impl_->tick) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    Impl::GravityBatch gravity(*this);
    impl_->itemSystem.BeginCallback();
    impl_->RelocateObject(*this, handle, destination);
    impl_->itemSystem.EndCallback();
    return {};
}

ObjectMoveResult World::QueryObjectMove(ActorHandle actorHandle, ObjectHandle handle, ObjectMoveKind kind) const {
    const auto* actor = GetActor(actorHandle);
    const auto* object = GetObject(handle);
    if (!actor || !IsActorAlive(actorHandle) || !object || actor->parked || !IsActive(actor->position) || !IsActive(object->position)
        || (kind != ObjectMoveKind::Push && kind != ObjectMoveKind::Pull)) { return {}; }
    if (actor->actionDeadline.Remaining(impl_->tick) || actor->combat.action.kind != ActorActionKind::None
        || impl_->craftingSystem.Busy(actorHandle) || impl_->learningSystem.Busy(actorHandle)
        || ActionTicks(ticks_per_turn, actor->speed) > UINT64_MAX - impl_->tick) {
        return {ObjectMoveOutcome::NotReady};
    }
    const TileCoord a = actor->position, o = object->position;
    const int dx = o.x - a.x, dz = o.z - a.z;
    if (a.y != o.y || std::abs(dx) + std::abs(dz) != 1) { return {ObjectMoveOutcome::OutOfReach}; }
    const auto* definition = impl_->content.TryGetObject(object->type);
    if (!definition || !definition->movable) { return {ObjectMoveOutcome::Immovable}; }
    if (StationWorker(handle)) { return {ObjectMoveOutcome::Occupied}; }
    const TileCoord actorDestination = kind == ObjectMoveKind::Push ? o : TileCoord{a.x - dx, a.z - dz, a.y};
    const TileCoord objectDestination = kind == ObjectMoveKind::Push ? TileCoord{o.x + dx, o.z + dz, o.y} : a;
    for (TileCoord destination : {actorDestination, objectDestination}) {
        const auto* tile = GetTile(destination);
        if (!tile || !IsActive(destination)) { return {ObjectMoveOutcome::Unavailable}; }
        const auto& terrain = impl_->content.GetTerrain(tile->terrain);
        if (!terrain.passable || terrain.openSpace) { return {ObjectMoveOutcome::Blocked}; }
        const auto occupant = ActorAt(destination);
        const auto furniture = ObjectAt(destination);
        if ((occupant.value && occupant != actorHandle) || (furniture.value && furniture != handle)) {
            return {ObjectMoveOutcome::Occupied};
        }
    }
    return {ObjectMoveOutcome::Moved, ticks_per_turn};
}

WorldResult<ObjectMoveResult> World::TryMoveObject(ActorHandle actorHandle, ObjectHandle handle, ObjectMoveKind kind) {
    if (!GetActor(actorHandle) || !GetObject(handle)) { return eastl::unexpected(WorldError::invalid_handle); }
    if (kind != ObjectMoveKind::Push && kind != ObjectMoveKind::Pull) { return eastl::unexpected(WorldError::invalid_request); }
    const auto result = QueryObjectMove(actorHandle, handle, kind);
    if (result.outcome != ObjectMoveOutcome::Moved) { return result; }
    const TileCoord a = GetActor(actorHandle)->position, o = GetObject(handle)->position;
    const int dx = o.x - a.x, dz = o.z - a.z;
    impl_->RelocatePair(*this, actorHandle, handle,
        kind == ObjectMoveKind::Push ? o : TileCoord{a.x - dx, a.z - dz, a.y},
        kind == ObjectMoveKind::Push ? TileCoord{o.x + dx, o.z + dz, o.y} : a);
    return result;
}

ObjectMoveResult World::QueryObjectStep(ActorHandle actorHandle, ObjectHandle handle, TileCoord direction) const {
    const auto* actor = GetActor(actorHandle);
    const auto* object = GetObject(handle);
    if (!actor || !IsActorAlive(actorHandle) || !object || actor->parked || !IsActive(actor->position) || !IsActive(object->position)
        || direction.y != 0 || direction.x < -1 || direction.x > 1 || direction.z < -1 || direction.z > 1
        || (direction.x == 0 && direction.z == 0)) { return {}; }
    if (actor->actionDeadline.Remaining(impl_->tick) || actor->combat.action.kind != ActorActionKind::None
        || impl_->craftingSystem.Busy(actorHandle) || impl_->learningSystem.Busy(actorHandle)) { return {ObjectMoveOutcome::NotReady}; }
    const TileCoord a = actor->position, o = object->position;
    if (a.y != o.y || a == o || eastl::max(std::abs(o.x - a.x), std::abs(o.z - a.z)) > 1) {
        return {ObjectMoveOutcome::OutOfReach};
    }
    const auto* definition = impl_->content.TryGetObject(object->type);
    if (!definition || !definition->movable) { return {ObjectMoveOutcome::Immovable}; }
    if (StationWorker(handle)) { return {ObjectMoveOutcome::Occupied}; }
    const TileCoord actorDestination{a.x + direction.x, a.z + direction.z, a.y};
    const TileCoord objectDestination{o.x + direction.x, o.z + direction.z, o.y};
    // The pair may enter each other's vacated tile, but cannot displace a third occupant.
    for (const auto destination : {objectDestination, actorDestination}) {
        const auto* tile = GetTile(destination);
        if (!tile) { return {ObjectMoveOutcome::Unavailable}; }
        const auto& terrain = impl_->content.GetTerrain(tile->terrain);
        if (!terrain.passable || terrain.openSpace) { return {ObjectMoveOutcome::Blocked}; }
        const auto occupant = ActorAt(destination);
        const auto furniture = ObjectAt(destination);
        if (occupant.value && occupant != actorHandle) { return {ObjectMoveOutcome::Occupied}; }
        if (furniture.value && furniture != handle
            && (destination == objectDestination || BlocksMovement(destination))) { return {ObjectMoveOutcome::Occupied}; }
    }
    const auto cost = ActorStepBaseCost(combat_runtime::MovementEncumbranceCost(*this, *actor,
        eastl::max(definition->moveCost, LocomotionBaseCost(actor->combat.body))), direction);
    if (ActionTicks(cost, actor->speed) > UINT64_MAX - impl_->tick) { return {ObjectMoveOutcome::NotReady}; }
    return {ObjectMoveOutcome::Moved, cost};
}

WorldResult<ObjectMoveResult> World::TryObjectStep(ActorHandle actor, ObjectHandle object, TileCoord direction) {
    if (!GetActor(actor) || !GetObject(object)) { return eastl::unexpected(WorldError::invalid_handle); }
    if (direction.y != 0 || direction.x < -1 || direction.x > 1 || direction.z < -1 || direction.z > 1
        || (direction.x == 0 && direction.z == 0)) { return eastl::unexpected(WorldError::invalid_request); }
    const auto result = QueryObjectStep(actor, object, direction);
    if (result.outcome != ObjectMoveOutcome::Moved) { return result; }
    const auto a = GetActor(actor)->position, o = GetObject(object)->position;
    impl_->RelocatePair(*this, actor, object, {a.x + direction.x, a.z + direction.z, a.y},
        {o.x + direction.x, o.z + direction.z, o.y});
    return result;
}

eastl::optional<ObjectStateView> World::DescribeObjectState(ObjectHandle handle) const {
    const auto* object = GetObject(handle);
    if (!object || !object->type.value) { return eastl::nullopt; }
    ObjectStateView view;
    if (object->behavior_) { view = object->behavior_->Describe(); }
    else { view.behavior = impl_->content.GetObject(object->type).behavior; }
    if (view.behavior == ObjectBehaviorKind::Solar) {
        view.solarDaylight = SolarDaylightRatio(GetTick());
        view.solarExposed = HasDirectSkyExposure(object->position);
        view.running = view.runRequested && view.solarExposed && IsSolarDaytime(GetTick());
    }
    return view;
}

DataObject World::CaptureObjectState(ObjectHandle handle) const {
    const auto* object = GetObject(handle);
    return object && object->behavior_ ? object->behavior_->SaveState() : DataObject{};
}

WorldResult<void> World::SetObjectEnergy(ObjectHandle handle, std::uint64_t energy) {
    auto* object = Live(impl_->objects, handle);
    if (!object) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!object->behavior_) { return eastl::unexpected(WorldError::invalid_request); }
    const auto result = object->behavior_->SetEnergy(energy);
    if (result) {
        impl_->MarkDirty(ChunkOf(object->position), false);
        impl_->powerSystem.SyncObject(handle);
    }
    return result;
}

WorldResult<void> World::PrepareGeneratorInterval(ObjectHandle handle, Tick now) {
    auto* object = Live(impl_->objects, handle);
    if (!object || !object->behavior_) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto result = object->behavior_->PrepareGeneratorInterval(now);
    if (result) { impl_->MarkDirty(ChunkOf(object->position), false); }
    return result;
}
WorldResult<void> World::SpendGeneratorEnergy(ObjectHandle handle, std::uint64_t energy) {
    auto* object = Live(impl_->objects, handle);
    if (!object || !object->behavior_) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto result = object->behavior_->SpendGeneratorEnergy(energy);
    if (result) { impl_->MarkDirty(ChunkOf(object->position), false); }
    return result;
}
WorldResult<void> World::PrepareSolarInterval(ObjectHandle handle, Tick now) {
    auto* object = Live(impl_->objects, handle);
    if (!object || !object->behavior_) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto& definition = impl_->content.GetObject(object->type);
    const auto allowance = HasDirectSkyExposure(object->position)
        ? SolarIntervalAllowance(now, definition.solar.peakOutputPerTick) : 0;
    const auto result = object->behavior_->PrepareSolarInterval(now, allowance);
    if (result) { impl_->MarkDirty(ChunkOf(object->position), false); }
    return result;
}
WorldResult<void> World::SpendSolarEnergy(ObjectHandle handle, std::uint64_t energy) {
    auto* object = Live(impl_->objects, handle);
    if (!object || !object->behavior_) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto result = object->behavior_->SpendSolarEnergy(energy);
    if (result) { impl_->MarkDirty(ChunkOf(object->position), false); }
    return result;
}
WorldResult<void> World::FundGridMachine(ObjectHandle handle, Tick ticks) {
    auto* object = Live(impl_->objects, handle);
    if (!object || !object->behavior_) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto result = object->behavior_->FundGridWork(ticks);
    if (result) { impl_->MarkDirty(ChunkOf(object->position), false); }
    return result;
}

bool World::CanUseObject(ActorHandle actorHandle, ObjectHandle objectHandle) const {
    const auto* actor = GetActor(actorHandle);
    const auto* object = GetObject(objectHandle);
    if (!actor || !IsActorAlive(actorHandle) || actor->parked || !IsActive(actor->position) || !object || !IsActive(object->position)) { return false; }
    const TileCoord a = actor->position;
    const TileCoord b = object->position;
    if (a.y != b.y || eastl::max(std::abs(a.x - b.x), std::abs(a.z - b.z)) > 1) { return false; }
    return true;
}

ObjectUseReason ObjectReadContext::CheckInputs(ActorHandle actor, ItemTypeId type, std::uint32_t count) const {
    if (!world_.CanUseObject(actor, self_)) { return ObjectUseReason::OutOfReach; }
    std::uint32_t found = 0;
    for (auto handle : world_.GetActor(actor)->inventory) {
        const auto* item = world_.GetItem(handle);
        if (item->type == type && item->contents.empty() && ++found >= count) { return ObjectUseReason::None; }
    }
    return ObjectUseReason::NoInputs;
}
ObjectUseReason ObjectReadContext::CheckCollect(ActorHandle actor) const {
    if (!world_.CanUseObject(actor, self_)) { return ObjectUseReason::OutOfReach; }
    const auto* definition = world_.GetContent().TryGetObject(world_.GetObject(self_)->type);
    if (definition && definition->capacityVolumeMl && !world_.CanAccessObjectStorage(actor, self_)) {
        return ObjectUseReason::OutOfReach;
    }
    const auto& contents = world_.GetObject(self_)->contents;
    bool found = false;
    const auto capacity = world_.GetActor(actor)->carryCapacityGrams;
    const auto mass = world_.GetCarriedMass(actor);
    std::uint64_t available = capacity > mass ? capacity - mass : 0;
    for (auto item : contents) {
        if (world_.IsReservedItem(item)) { continue; }
        found = true;
        const auto weight = world_.GetItemMass(item);
        if (weight > available || !world_.CheckItemTransfer(item, ItemLocation::Carried(actor))) {
            return ObjectUseReason::NoCapacity;
        }
        available -= weight;
    }
    return found ? ObjectUseReason::None : ObjectUseReason::NoContents;
}
void World::QueryObjectActions(ActorHandle actor, ObjectHandle handle, Vector<ObjectActionInfo>& output) const {
    const auto* object = GetObject(handle);
    if (!object) { return; }
    const auto start = output.size();
    if (object->behavior_) { object->behavior_->QueryActions(ObjectReadContext(*this, handle), actor, output); }
    else if (const auto* definition = impl_->content.TryGetObject(object->type);
        definition && definition->capacityVolumeMl) {
        output.push_back({ObjectAction::Collect, ObjectReadContext(*this, handle).CheckCollect(actor)});
    } else {
        output.push_back({ObjectAction::Enable,object->enabled ? ObjectUseReason::AlreadyEnabled : ObjectUseReason::None});
        output.push_back({ObjectAction::Disable,object->enabled ? ObjectUseReason::None : ObjectUseReason::AlreadyDisabled});
    }
    if (!CanUseObject(actor, handle)) {
        for (auto i = start; i < output.size(); ++i) { output[i].reason = ObjectUseReason::OutOfReach; }
    }
}

ObjectUseResult World::UseObject(ActorHandle actor, ObjectHandle handle, ObjectAction action) {
    Require(!impl_->objectCallback && !impl_->processingObjects);
    if (!CanUseObject(actor, handle)) {
        return {ObjectUseOutcome::Inaccessible, 0,
            GetActor(actor) ? ObjectUseReason::OutOfReach : ObjectUseReason::InvalidActor};
    }
    auto* object = Live(impl_->objects, handle);
    Vector<ObjectActionInfo> actions;
    QueryObjectActions(actor, handle, actions);
    const auto found = eastl::find_if(actions.begin(), actions.end(), [action](const ObjectActionInfo& info) { return info.action == action; });
    if (found == actions.end()) { return {}; }
    if (found->reason != ObjectUseReason::None) { return {ObjectUseOutcome::Refused, 0, found->reason}; }
    if (!object->behavior_) {
        if (action == ObjectAction::Collect) {
            Impl::ObjectCallbackScope scope(*this);
            Require(CollectObjectContents(actor, handle).has_value());
            return {ObjectUseOutcome::Succeeded, ticks_per_turn};
        }
        const auto changed = SetObjectEnabled(handle, action == ObjectAction::Enable);
        Require(changed.has_value());
        return {ObjectUseOutcome::Succeeded, 100};
    }
    Impl::ObjectCallbackScope scope(*this);
    ObjectMutationContext context(*this, handle);
    const auto result = object->behavior_->Use(context, actor, action);
    Require(result.outcome == ObjectUseOutcome::Succeeded ? result.baseCost > 0 : result.baseCost == 0);
    scope.End();
    if (context.destroyRequested_) { Require(DestroyObject(handle).has_value()); }
    else if (result.outcome == ObjectUseOutcome::Succeeded) {
        Require(result.baseCost > 0);
        object = Live(impl_->objects, handle);
        Require(object != nullptr);
        impl_->MarkDirty(ChunkOf(object->position), false);
        impl_->powerSystem.SyncObject(handle);
    }
    return result;
}

WorldResult<void> World::ConsumeObjectInputs(ActorHandle actorHandle, ObjectHandle handle, ItemTypeId type, std::uint32_t count) {
    Require(impl_->objectCallback);
    if (!CanUseObject(actorHandle, handle)) { return eastl::unexpected(WorldError::blocked); }
    Vector<ItemHandle> inputs;
    for (ItemHandle itemHandle : GetActor(actorHandle)->inventory) {
        const auto* item = GetItem(itemHandle);
        if (item->type == type && item->contents.empty()) { inputs.push_back(itemHandle); }
    }
    if (inputs.size() < count) { return eastl::unexpected(WorldError::invalid_request); }
    eastl::sort(inputs.begin(), inputs.end(), [this](ItemHandle a, ItemHandle b) { return GetItem(a)->id < GetItem(b)->id; });
    // All inputs are direct leaves. Destroying one cannot retire another; item
    // notifications remain deferred until the whole object callback returns.
    for (std::uint32_t i = 0; i < count; ++i) { Require(DestroyItem(inputs[i]).has_value()); }
    return {};
}

WorldResult<void> World::ProduceObjectOutputs(ObjectHandle handle, ItemTypeId type, std::uint32_t count) {
    Require(impl_->objectCallback);
    if (!GetObject(handle)) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!impl_->content.TryGetItem(type)) { return eastl::unexpected(WorldError::invalid_request); }
    if (!impl_->ObjectOutputsFit(handle, type, count)) { return eastl::unexpected(WorldError::capacity_exceeded); }
    for (std::uint32_t i = 0; i < count; ++i) { Require(CreateItem(type, ItemLocation::Stored(handle)).has_value()); }
    return {};
}

WorldResult<void> World::CollectObjectContents(ActorHandle actorHandle, ObjectHandle handle) {
    Require(impl_->objectCallback);
    if (!CanUseObject(actorHandle, handle)) { return eastl::unexpected(WorldError::blocked); }
    const auto* actor = GetActor(actorHandle);
    Vector<ItemHandle> contents = GetObject(handle)->contents;
    eastl::erase_if(contents, [this](ItemHandle item) { return IsReservedItem(item); });
    eastl::sort(contents.begin(), contents.end(), [this](ItemHandle a, ItemHandle b) { return GetItem(a)->id < GetItem(b)->id; });
    if (contents.empty()) { return eastl::unexpected(WorldError::invalid_request); }
    std::uint64_t available = actor->carryCapacityGrams - GetCarriedMass(actorHandle);
    for (ItemHandle item : contents) {
        const auto mass = GetItemMass(item);
        if (mass > available) { return eastl::unexpected(WorldError::capacity_exceeded); }
        available -= mass;
        if (const auto valid = CheckItemTransfer(item, ItemLocation::Carried(actorHandle)); !valid) { return valid; }
    }
    for (ItemHandle item : contents) { Require(TransferItem(item, ItemLocation::Carried(actorHandle)).has_value()); }
    return {};
}

ObjectMutationContext::ObjectMutationContext(World& world, ObjectHandle self) : world_(world), self_(self) {}
const World& ObjectMutationContext::GetWorld() const { return world_; }
ObjectHandle ObjectMutationContext::Self() const { return self_; }
WorldResult<void> ObjectMutationContext::ConsumeInputs(ActorHandle actor, ItemTypeId type, std::uint32_t count) {
    return world_.ConsumeObjectInputs(actor, self_, type, count);
}
WorldResult<void> ObjectMutationContext::Produce(ItemTypeId type, std::uint32_t count) {
    return world_.ProduceObjectOutputs(self_, type, count);
}
WorldResult<void> ObjectMutationContext::CollectContents(ActorHandle actor) { return world_.CollectObjectContents(actor, self_); }
void ObjectMutationContext::RequestDestroy() {
    Require(world_.impl_->objectCallback);
    destroyRequested_ = true;
}

WorldResult<void> World::DestroyObject(ObjectHandle handle) {
    Require(!impl_->objectCallback);
    Impl::GravityBatch batch(*this);
    InteractiveObject* value = Live(impl_->objects, handle);
    if (!value) {
        return eastl::unexpected(WorldError::invalid_handle);
    }
    const TileCoord position = value->position;
    impl_->craftingSystem.ReleaseStation(value->id);
    value->equipment = {}; // Destruction releases the original item to the ground.
    impl_->QueueGravity(position);
    while (!value->contents.empty()) {
        Require(TransferItem(value->contents.back(), ItemLocation::Ground(position)).has_value());
        value = Live(impl_->objects, handle);
        Require(value != nullptr);
    }
    EraseOne(impl_->GetChunk(ChunkOf(position))->objects, handle);
    const auto* definition = impl_->content.TryGetObject(value->type);
    const bool blocksSight = definition && definition->blocksSight;
    if (blocksSight) { impl_->MarkGeometryDirty(ChunkOf(position)); }
    if (blocksSight || (value->enabled && value->light.intensity)) { impl_->MarkLightingDirty(ChunkOf(position)); }
    impl_->powerSystem.UnregisterObject(value->id);
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
    if (!enabled) { impl_->craftingSystem.ReleaseStation(value->id); }
    impl_->RefreshSchedule(*value);
    impl_->powerSystem.SyncObject(handle);
    impl_->MarkDirty(ChunkOf(value->position), true, true);
    return {};
}

ObjectAppearance DescribeObject(const InteractiveObject& object) {
    return {object.kind, object.facing, object.enabled, object.broken, object.type};
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
    if (broken) { impl_->craftingSystem.ReleaseStation(object->id); }
    impl_->powerSystem.SyncObject(handle);
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
    for (const Actor& actor : impl_->actors) {
        if (!actor.player || actor.parked || !IsActive(actor.position)) { continue; }
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
    if (!IsActorAlive(handle) || !IsActive(value->position) || value->parked) { return eastl::unexpected(WorldError::chunk_unavailable); }
    if (baseCost == 0 || value->actionDeadline.Remaining(impl_->tick) != 0
        || value->combat.action.kind != ActorActionKind::None || impl_->craftingSystem.Busy(handle) || impl_->learningSystem.Busy(handle)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    Require(value->speed != 0);
    Require(value->turns != eastl::numeric_limits<std::uint64_t>::max());
    value->actionDeadline.Set(impl_->tick, ActionTicks(baseCost, value->speed));
    ++value->turns;
    impl_->MarkDirty(ChunkOf(value->position), false);
    return {};
}

WorldResult<void> World::SetObjectUpdateInterval(ObjectHandle handle, std::uint32_t interval) {
    InteractiveObject* value = Live(impl_->objects, handle);
    if (!value) { return eastl::unexpected(WorldError::invalid_handle); }
    if (interval == 0) { return eastl::unexpected(WorldError::invalid_request); }
    value->updateInterval = interval;
    value->updateDeadline.Set(impl_->tick, interval);
    impl_->MarkDirty(ChunkOf(value->position), false);
    return {};
}

Tick World::GetTick() const { return impl_->tick; }

Tick World::GetActorRemainingTicks(ActorHandle handle) const {
    const Actor* actor = GetActor(handle);
    Require(actor != nullptr);
    return actor->actionDeadline.Remaining(impl_->tick);
}

Tick World::GetObjectRemainingTicks(ObjectHandle handle) const {
    const InteractiveObject* object = GetObject(handle);
    Require(object != nullptr);
    return object->updateDeadline.Remaining(impl_->tick);
}

void World::ElapseTicks(Tick elapsed) {
    Impl::GravityBatch batch(*this);
    if (elapsed == 0) { return; }
    const Tick end = CheckedTickAdd(impl_->tick, elapsed);
    while (impl_->tick < end) {
        impl_->powerSystem.ProcessEvents();
        auto segment = end - impl_->tick;
        if (const auto event = impl_->powerSystem.NextEventIn()) {
            Require(*event != 0);
            segment = eastl::min(segment, *event);
        }
        ElapseSegment(segment);
    }
}

void World::ElapseSegment(Tick elapsed) {
    const Tick nextTick = CheckedTickAdd(impl_->tick, elapsed);
    // Relative timers on disk change even though runtime deadlines do not.
    // Dirty at chunk granularity, avoiding writes to every scheduled entity.
    for (const auto& column : impl_->columns_) {
        for (const Chunk& chunk : column.second.layers) {
            if (chunk.active && (!chunk.actors.empty() || !chunk.objects.empty())) {
                impl_->MarkDirty(chunk.position, false);
            }
        }
    }
    impl_->itemSystem.Elapse(elapsed);
    impl_->craftingSystem.Elapse(elapsed);
    impl_->learningSystem.Elapse(elapsed);
    impl_->combatSystem.Elapse(elapsed);
    for (Actor& actor : impl_->actors) {
        if (actor.combat.life == ActorLifeState::Alive && !actor.player && !actor.parked && actor.controller_ && IsActive(actor.position)) {
            actor.controller_->Elapse(elapsed);
        }
    }
    for (InteractiveObject& object : impl_->objects) {
        if (!object.enabled || !IsActive(object.position)) { continue; }
        if (object.behavior_) {
            const auto delay = object.behavior_->NextEventIn();
            if (!delay || *delay == 0) { continue; }
            object.behavior_->Elapse(elapsed);
            impl_->MarkDirty(ChunkOf(object.position), false);
        }
    }
    impl_->tick = nextTick;
}

void World::CollectActiveActors(Vector<ActorHandle>& output) const {
    output.clear();
    for (const auto& entry : impl_->actors.items()) {
        const Actor& actor = entry.second.get();
        if (actor.combat.life == ActorLifeState::Alive && !actor.parked && IsActive(actor.position)) { output.push_back({entry.first.raw}); }
    }
}

void World::CollectActiveObjects(Vector<ObjectHandle>& output) const {
    output.clear();
    for (const auto& entry : impl_->objects.items()) {
        if (IsActive(entry.second.get().position)) { output.push_back({entry.first.raw}); }
    }
}

bool World::HasActivePlayers() const {
    for (const auto& actor : impl_->actors) {
        if (actor.player && actor.combat.life == ActorLifeState::Alive
            && !actor.parked && IsActive(actor.position)) { return true; }
    }
    return false;
}

void World::CollectReadyPlayers(Vector<ActorHandle>& output) const {
    output.clear();
    for (const auto& entry : impl_->actors.items()) {
        const Actor& actor = entry.second.get();
        if (actor.combat.life == ActorLifeState::Alive && actor.player && !actor.actionDeadline.suspended && actor.actionDeadline.dueTick <= impl_->tick
            && actor.combat.action.kind == ActorActionKind::None && !impl_->craftingSystem.Busy({entry.first.raw})
            && !impl_->learningSystem.Busy({entry.first.raw})) {
            output.push_back({entry.first.raw});
        }
    }
    eastl::sort(output.begin(), output.end(), [this](ActorHandle a, ActorHandle b) {
        return GetActor(a)->id < GetActor(b)->id;
    });
}

void World::ProcessNpcActions(std::uint32_t& rng) {
    Require(!impl_->processingNpcs && !impl_->objectCallback);
    impl_->processingNpcs = true;
    auto& due = impl_->dueActors;
    due.clear();
    for (const auto& entry : impl_->actors.items()) {
        const Actor& actor = entry.second.get();
        if (actor.combat.life == ActorLifeState::Alive && !actor.player && !actor.parked && actor.controller_ && IsActive(actor.position)
            && actor.actionDeadline.dueTick <= impl_->tick
            && actor.combat.action.kind == ActorActionKind::None && !impl_->craftingSystem.Busy({entry.first.raw})
            && !impl_->learningSystem.Busy({entry.first.raw})) { due.push_back({actor.id, {entry.first.raw}}); }
    }
    eastl::sort(due.begin(), due.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (const auto record : due) {
        Actor* actor = Live(impl_->actors, record.handle);
        if (!actor || actor->combat.life == ActorLifeState::Dead || actor->id != record.id || actor->player || actor->parked || !actor->controller_
            || !IsActive(actor->position) || actor->actionDeadline.dueTick > impl_->tick
            || actor->combat.action.kind != ActorActionKind::None || impl_->craftingSystem.Busy(record.handle)
            || impl_->learningSystem.Busy(record.handle)) { continue; }
        const ActorIntent intent = actor->controller_->ChooseAction(ActorReadContext(*this, record.handle), rng);
        Require(intent.baseCost > 0);
        Require(intent.kind == ActorIntentKind::Wait || intent.kind == ActorIntentKind::Move
            || intent.kind == ActorIntentKind::Attack);
        if (intent.kind == ActorIntentKind::Attack) {
            const auto attack = impl_->combatSystem.Attack(record.handle, intent.target, rng);
            // A refused AI intent still makes progress; never leave a due NPC spinning.
            if (!attack.committed && IsActorAlive(record.handle)) { Require(ScheduleActorAction(record.handle, intent.baseCost).has_value()); }
            UpdatePlayerObservations();
            continue;
        }
        const std::uint64_t baseCost = intent.kind == ActorIntentKind::Move
            ? ActorMovementBaseCost(record.handle, intent.baseCost)
            : intent.baseCost;
        Require(baseCost <= eastl::numeric_limits<std::uint32_t>::max());
        const auto actionCost = intent.kind == ActorIntentKind::Move
            ? ActorStepBaseCost(static_cast<std::uint32_t>(baseCost), intent.direction)
            : static_cast<std::uint32_t>(baseCost);
        Require(SetActorExertion(record.handle, intent.kind == ActorIntentKind::Move ? ExertionKind::Moving : ExertionKind::Rest).has_value());
        Require(ScheduleActorAction(record.handle, actionCost).has_value());
        // Commit the deadline before execution: blocked movement still costs time.
        if (intent.kind == ActorIntentKind::Move) {
            const auto moved = TryActorStep(record.handle, intent.direction, true);
            Require(moved || moved.error() != WorldError::invalid_request);
        }
        UpdatePlayerObservations();
    }
    impl_->processingNpcs = false;
}

eastl::optional<Tick> World::NextActorEventIn() const {
    eastl::optional<Tick> next;
    for (const auto& entry : impl_->actors.items()) {
        const Actor& actor = entry.second.get();
        const ActorHandle handle{entry.first.raw};
        if (actor.combat.life == ActorLifeState::Dead) { continue; }
        if (impl_->learningSystem.Busy(handle)) {
            const auto delay = impl_->learningSystem.ActorEventIn(handle);
            if (delay && (!next || *delay < *next)) { next = *delay; }
            continue;
        }
        if (impl_->craftingSystem.Busy(handle)) {
            const auto delay = impl_->craftingSystem.ActorEventIn(handle);
            if (delay && (!next || *delay < *next)) { next = *delay; }
            continue;
        }
        if (actor.parked || !IsActive(actor.position) || (!actor.player && !actor.controller_)) { continue; }
        const Tick remaining = actor.actionDeadline.Remaining(impl_->tick);
        if (!next || remaining < *next) { next = remaining; }
    }
    return next;
}

eastl::optional<Tick> World::NextObjectEventIn() const {
    eastl::optional<Tick> next;
    for (const auto& entry : impl_->objects.items()) {
        const auto delay = impl_->ObjectEventDelay(ObjectHandle{entry.first.raw});
        if (delay && (!next || *delay < *next)) { next = *delay; }
    }
    return next;
}

bool World::ProcessObjectEvents() {
    Require(!impl_->processingObjects && !impl_->objectCallback);
    impl_->processingObjects = true;
    auto& due = impl_->dueObjects;
    due.clear();
    for (const auto& entry : impl_->objects.items()) {
        const auto& object = entry.second.get();
        if (!object.enabled || !IsActive(object.position)) { continue; }
        const auto delay = impl_->ObjectEventDelay(ObjectHandle{entry.first.raw});
        if (delay && *delay == 0) { due.push_back({object.id, {entry.first.raw}}); }
    }
    eastl::sort(due.begin(), due.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    bool processed = false;
    for (const auto record : due) {
        auto* object = Live(impl_->objects, record.handle);
        if (!object || object->id != record.id || !object->enabled || !IsActive(object->position)) { continue; }
        if (object->behavior_) {
            const auto delay = impl_->ObjectEventDelay(record.handle);
            if (!delay || *delay != 0) { continue; }
            Impl::ObjectCallbackScope scope(*this);
            ObjectMutationContext context(*this, record.handle);
            object->behavior_->ProcessEvent(context);
            scope.End();
            if (context.destroyRequested_) { Require(DestroyObject(record.handle).has_value()); }
            else {
                object = Live(impl_->objects, record.handle);
                Require(object != nullptr);
                const auto next = object->behavior_->NextEventIn();
                Require(!next || *next != 0);
                Require(object->updates != eastl::numeric_limits<std::uint64_t>::max());
                ++object->updates;
                impl_->MarkDirty(ChunkOf(object->position), false);
                impl_->powerSystem.SyncObject(record.handle);
            }
        } else {
            if (object->type.value || object->updateDeadline.dueTick > impl_->tick) { continue; }
            Require(RecordObjectUpdate(record.handle).has_value());
        }
        processed = true;
    }
    impl_->processingObjects = false;
    return processed;
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
        value->updateDeadline.Set(impl_->tick, value->updateInterval);
        impl_->MarkDirty(ChunkOf(value->position), false);
    }
    return {};
}

bool World::CheckInvariants() const {
    const Impl& state = *impl_;
    if (!state.powerSystem.CheckState(state.tick, state.next_id)) { return false; }
    for (const auto& device : state.powerSystem.Devices()) {
        if (device.object == 0 || device.object >= state.next_id) { return false; }
        const auto chunk = ChunkOf(device.position);
        if (state.GetChunk(chunk)) {
            const auto* object = GetObject(FindObject(device.object));
            if (!object || object->position != device.position) { return false; }
        } else if (!state.archives.contains(chunk) && !state.warmArchives.contains(chunk) && !state.database) {
            return false;
        }
        // Disk-only records were checked at Open and will be checked again on
        // incoming archive read; invariants deliberately do not scan the database.
    }
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
            eastl::array<ActorHandle, chunk_size * chunk_size> expectedOccupancy{};
            Vector<TileCoord> object_positions;
            PersistentId previousActorId = 0;
            for (ActorHandle handle : value.actors) {
                const Actor* actor = this->GetActor(handle);
                if (!actor || actor->id <= previousActorId || !ValidPosition(actor->position) || ChunkOf(actor->position) != value.position
                    || (actor->parked && !actor->player)) {
                    return false;
                }
                previousActorId = actor->id;
                if (!actor->parked && actor->combat.life == ActorLifeState::Alive) {
                    ActorHandle& occupant = expectedOccupancy[TileIndex(actor->position)];
                    if (occupant != ActorHandle{}) { return false; }
                    occupant = handle;
                }
                indexed_actors.push_back(handle);
            }
            if (value.actorOccupancy != expectedOccupancy) { return false; }
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
    if (state.actorIds.size() != state.actors.size()) { return false; }
    for (const auto& entry : state.actors.items()) {
        const Actor& value = entry.second.get();
        const ActorHandle handle{entry.first.raw};
        if (state.FindActor(value.id) != handle) { return false; }
        if (value.monsterType.value
            && (value.player || !state.content.TryGetMonster(value.monsterType))) { return false; }
        const auto controller = ReadAiController(CaptureAiController(value.controller_.get()));
        if (!controller || (value.player && value.controller_)) { return false; }
        if (!ValidateAiControllerContent(CaptureAiController(value.controller_.get()), state.content)) { return false; }
        if (!ValidateCombatState(value.combat, state.content)) { return false; }
        if (!ReadLearningState(CaptureLearningState(value.learning, state.content), state.content)) { return false; }
        if (value.learning.study.active && (state.craftingSystem.Busy(handle)
            || value.combat.action.kind != ActorActionKind::None
            || value.actionDeadline.Remaining(state.tick) != value.learning.study.remaining)) { return false; }
        const auto* wielded = GetItem(value.wieldedItem);
        if (value.wieldedItem.value && (!wielded || wielded->location != ItemLocation::Carried(handle))) { return false; }
        if (!value.combat.action.definitionDriven && value.combat.action.kind == ActorActionKind::MeleeWindup
            && value.combat.action.weapon != (wielded ? wielded->id : 0)) { return false; }
        const auto projected = value.equipment.hands[0].item.value ? value.equipment.hands[0].item : value.equipment.hands[1].item;
        if (projected != value.wieldedItem) { return false; }
        for (unsigned hand = 0; hand < 2; ++hand) {
            const auto held = value.equipment.hands[hand];
            if (!held.item.value) { continue; }
            const auto* item = GetItem(held.item);
            if (!item || item->location != ItemLocation::Carried(handle) || IsReservedItem(held.item)) { return false; }
            const auto& grips = ItemMelee(state.content.GetItem(item->type)).grips;
            if (eastl::none_of(grips.begin(), grips.end(), [&](const GripDefinition& grip) { return grip.kind == held.grip; })) { return false; }
            const auto other = value.equipment.hands[1 - hand];
            if ((held.grip == GripKind::TwoHanded) != (held.item == other.item)
                || (held.grip == GripKind::TwoHanded && other.grip != held.grip)) { return false; }
            if (eastl::find(value.equipment.worn.begin(), value.equipment.worn.end(), held.item) != value.equipment.worn.end()) { return false; }
        }
        for (std::size_t index = 0; index < value.equipment.worn.size(); ++index) {
            const auto item = value.equipment.worn[index];
            const auto* garment = GetItem(item);
            if (!garment || garment->location != ItemLocation::Carried(handle) || IsReservedItem(item)) { return false; }
            const auto& armor = state.content.GetItem(garment->type).armor;
            if (!armor) { return false; }
            for (std::size_t previous = 0; previous < index; ++previous) {
                const auto other = value.equipment.worn[previous];
                if (other == item) { return false; }
                const auto& otherArmor = *state.content.GetItem(GetItem(other)->type).armor;
                if (armor->layer == otherArmor.layer && (armor->rigidRegions & otherArmor.rigidRegions)) { return false; }
            }
        }
        if (value.combat.life == ActorLifeState::Dead && (value.combat.action.kind != ActorActionKind::None
            || value.learning.study.active || (value.player && !value.parked)
            || (value.combat.corpse && (!value.inventory.empty() || value.wieldedItem.value || !value.equipment.worn.empty())))) { return false; }
        const bool running = value.combat.life == ActorLifeState::Alive && !value.parked && IsActive(value.position) && (value.player || value.controller_);
        if (value.actionDeadline.suspended == running) { return false; }
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
        if (!MatchesPowerObject(state.powerSystem, state.content, value.id, value.position,
                value.type, value.enabled, value.broken, DescribeObjectState(handle).value_or(ObjectStateView{}), state.tick)) { return false; }
        const bool running = !value.type.value && value.enabled && IsActive(value.position);
        if (value.updateDeadline.suspended == running) { return false; }
        if (value.type.value) {
            const auto* definition = state.content.TryGetObject(value.type);
            if (!definition || (definition->behavior == ObjectBehaviorKind::None
                    || definition->behavior == ObjectBehaviorKind::GridBattery) != !value.behavior_
                || !ValidateObjectBehaviorState(*definition, CaptureObjectState(handle))) { return false; }
            const auto* equipment = GetItem(value.equipment);
            if (definition->packingItem.value) {
                if (!equipment || equipment->type != definition->packingItem || !equipment->contents.empty()
                    || equipment->location != ItemLocation::Stored(handle)) { return false; }
            } else if (value.equipment.value) { return false; }
            if ((definition->blocksMovement && ActorAt(value.position).value)
                || (definition->capacityVolumeMl && state.ObjectVolume(handle) > *definition->capacityVolumeMl)) { return false; }
        } else if (value.behavior_) { return false; }
        if (!ValidLightEmission(value.light) || value.kind == ObjectKind::None
            || !ValidObjectAppearance(DescribeObject(value))
            || value.updateInterval == 0 || value.updateDeadline.Remaining(state.tick) > value.updateInterval
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
                || (value.type != state.content.CorpseItem() && !state.Fits(item->type, item->location, child))) {
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
        if (!ValidateArchive(state.content, archive, state.next_id, checkedIds)
            || !ValidPowerArchive(state.powerSystem, state.content, archive, state.tick)) { return false; }
    }
    if (state.warmArchives.size() > Impl::maxWarmEntries
        || state.warmOrder.size() != state.warmArchives.size()) { return false; }
    std::size_t warmBytes = 0;
    for (const auto& entry : state.warmArchives) {
        const Archive& archive = entry.second;
        if (entry.first != archive.position || Occurrences(state.warmOrder, entry.first) != 1) { return false; }
        if (archive.items.capacity() != 0 || !archive.actors.empty()
            || eastl::any_of(archive.objects.begin(), archive.objects.end(),
                [](const SavedObject& object) { return object.type.value != 0; })
            || state.GetChunk(archive.position)
            || Occurrences(archived_positions, archive.position) != 0
            || !ValidateArchive(state.content, archive, state.next_id, ids)
            || !ValidPowerArchive(state.powerSystem, state.content, archive, state.tick)) { return false; }
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
