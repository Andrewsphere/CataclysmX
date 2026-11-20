#pragma once

#include "core/world.h"
#include "power/power_types.h"
#include "worldgen/generation_settings.h"

namespace cataclysmx {

inline constexpr int region_size = 256;
inline constexpr int planning_patch_regions = 6;
inline constexpr int planning_patch_size = region_size * planning_patch_regions;

struct RegionCoord {
    int x = 0;
    int z = 0;
    bool operator==(const RegionCoord&) const = default;
};

// Half-open tile geometry. Bounds in plans are clipped to the inclusive world
// coordinate limit; road endpoints and nominal town geometry need not be.
struct TileRect {
    TileCoord min{};
    TileCoord max{};
    bool operator==(const TileRect&) const = default;
};

enum class Biome { Meadow, Woodland };
struct EnvironmentSample {
    int moisture = 0; // All three fields are in [0,1000].
    int temperature = 0;
    int woodland = 0;
    Biome biome = Biome::Meadow;
    bool operator==(const EnvironmentSample&) const = default;
};

enum class BorderAxis { X, Z }; // X: constant x border, east/west heading.
struct BoundaryKey {
    BorderAxis axis = BorderAxis::X;
    int border = 0; // Geometric world coordinate, a multiple of 256.
    int alongRegion = 0; // Region z for X borders; region x for Z borders.
    bool operator==(const BoundaryKey&) const = default;
};
struct BoundaryCrossing {
    BoundaryKey key{};
    TileCoord position{}; // On the geometric border, NOT the last owned tile.
    int width = 3;
    int approachLength = 16;
    bool operator==(const BoundaryCrossing&) const = default;
};
struct RoadSegment {
    TileCoord first{}; // Inclusive centerline endpoints; axis-aligned.
    TileCoord last{};
    int width = 3;
    bool operator==(const RoadSegment&) const = default;
};
struct FeatureOwner {
    RegionCoord region{}; // Canonical planning-patch origin, in region coordinates.
    std::uint32_t parcel = 0;
    std::uint32_t settlement = 0;
    bool operator==(const FeatureOwner&) const = default;
};
// Preserve legacy values; use an explicit switch, not ordinal rotation arithmetic.
enum class Frontage { North, South, East, West };
enum class TownProfile : std::uint8_t { LegacyGrid, MainStreet, Crossroads, OffsetNeighborhood };
enum class SettlementEra : std::uint8_t { Older, Mixed, Newer };
struct SettlementId {
    RegionCoord region{}; // Planning-patch origin, each component a multiple of six.
    std::uint32_t ordinal = 0;
    bool operator==(const SettlementId&) const = default;
};
struct SettlementDescriptor {
    SettlementId id{};
    TileRect bounds{};
    TileCoord center{};
    SettlementScale scale = SettlementScale::Small;
    SettlementEra era = SettlementEra::Older;
    bool operator==(const SettlementDescriptor&) const = default;
};
struct PlanningPatch {
    RegionCoord origin{};
    Vector<SettlementDescriptor> settlements;
    Vector<RoadSegment> roads; // Countryside connections; not population coverage.
    bool operator==(const PlanningPatch&) const = default;
};
struct ParcelPlan {
    FeatureOwner owner{};
    TileRect bounds{};
    Frontage frontage = Frontage::North;
    int streetZ = 0; // Street centerline for North/South; unused for East/West.
    BuildingSite site = BuildingSite::Building;
    int streetX = 0; // Street centerline for East/West; unused for North/South.
    BuildingUse district = BuildingUse::Mixed;
    BuildingEra era = BuildingEra::Older;
    bool operator==(const ParcelPlan&) const = default;
};
struct BuildingPlan {
    FeatureOwner owner{};
    BuildingPatternId pattern{};
    std::uint8_t quarterTurns = 0; // Clockwise, [0,3], from content rows.
    TileRect bounds{};
    TileCoord entrance{};
    RoadSegment driveway{}; // Width one, entrance through street centerline.
    std::uint64_t abandonmentSeed = 0;
    bool abandoned = false;
    std::uint32_t basementVariant = 0; // 0 none; otherwise 1-based local index.
    bool operator==(const BuildingPlan&) const = default;
};
enum class GenerationPlacementKind : std::uint8_t { Furniture, Loot, Monster, IndoorPopulation, OutdoorPopulation };
// Transient identity scoped to a seed/catalog/version. Never a PersistentId,
// runtime handle or saved marker; hashes of these tuples need not be unique.
struct GenerationKey {
    FeatureOwner owner{};
    std::uint32_t basementVariant = 0; // 0 common population.
    GenerationPlacementKind kind = GenerationPlacementKind::Furniture;
    std::uint32_t placement = 0; // Index within the authored population array.
    std::uint32_t ordinal = 0; // Loot leaf order; furniture/monster always zero.
    bool operator==(const GenerationKey&) const = default;
};
struct GeneratedFurniture {
    GenerationKey key{};
    TileCoord position{};
    ObjectTypeId type{};
    ObjectFacing facing = ObjectFacing::North;
    bool operator==(const GeneratedFurniture&) const = default;
};
struct GeneratedItem {
    GenerationKey key{};
    TileCoord position{};
    ItemTypeId type{};
    LootDestination destination = LootDestination::Ground;
    bool operator==(const GeneratedItem&) const = default;
};
struct GeneratedMonster {
    GenerationKey key{};
    TileCoord position{};
    MonsterTypeId type{};
    bool operator==(const GeneratedMonster&) const = default;
};
struct SettlementPlan {
    SettlementDescriptor settlement;
    Vector<RoadSegment> roads;
    Vector<ParcelPlan> parcels;
    Vector<BuildingPlan> buildings;
    Vector<TileRect> serviceAlleys;
    Vector<GeneratedMonster> monsters; // Prepared once; deterministic before chunk cropping.
    std::uint32_t eligibleOutdoorTiles = 0;
    std::uint64_t indoorExpected = 0; // ratioScale units, before scenario exclusion.
    bool operator==(const SettlementPlan&) const = default;
};
struct RegionPlan {
    RegionCoord region{};
    std::uint64_t seed = 0;
    TileRect bounds{};
    // Summary of the nearest intersecting settlement for existing debug callers.
    // Actual membership always uses settlements and explicit developed geometry.
    TileRect townBounds{};
    TileCoord townCenter{};
    bool hasTown = false;
    eastl::array<BoundaryCrossing, 4> boundaries{};
    Vector<RoadSegment> roads;
    Vector<ParcelPlan> parcels;
    Vector<BuildingPlan> buildings;
    std::uint32_t generationVersion = world_generation_version;
    TownProfile townProfile = TownProfile::LegacyGrid;
    Vector<TileRect> serviceAlleys;
    Vector<SettlementDescriptor> settlements;
    Vector<GeneratedMonster> urbanMonsters;
    bool operator==(const RegionPlan&) const = default;
};
struct ChunkPlacements {
    Vector<GeneratedFurniture> furniture;
    Vector<GeneratedItem> items;
    Vector<GeneratedMonster> monsters;
    bool operator==(const ChunkPlacements&) const = default;
};

struct GeneratedBuildingPower {
    TileCoord origin{}; // Surface footprint minimum; stable across chunks/floors.
    PowerBounds bounds{};
    bool operator==(const GeneratedBuildingPower&) const = default;
};

// Complete inclusive extents, not clipped to the requesting chunk. Authored upper
// layers and the selected basement share one grid. Outdoor civic sites emit none.
// Clears output retaining capacity; registration may repeat from multiple chunks.
void GenerateBuildingPower(const ContentCatalog& content, const RegionPlan& plan,
    ChunkCoord chunk, Vector<GeneratedBuildingPower>& output);
// First-house/scenario hook: footprint is the rotated, half-open surface extent;
// basementVariant is zero or the same 1-based selection used for terrain/population.
// The caller registers the result before spawning devices. No machines are started.
eastl::optional<GeneratedBuildingPower> DescribeBuildingPower(const BuildingPattern& pattern,
    TileRect footprint, std::uint32_t basementVariant);

// Invalid public coordinates abort, including INT_MIN/INT_MAX, before arithmetic.
// Tiles: [-1,000,000,1,000,000]; regions: [-3907,3906]; chunks:
// [-62500,62500], on horizontal axes; tile/chunk Y: [world_min_y,world_max_y].
// RegionOf uses mathematical floor division and ignores Y.
// Plans must be unmodified outputs of GenerateRegionPlan, and materialization
// must use the same immutable catalog and seed. No World or shared RNG is used.
RegionCoord RegionOf(TileCoord position);
// Recoverable application/content boundary; diagnostic strings are optional.
// Validates a 3D World catalog: floor/wall/air are required. Air must be
// openSpace; floor and any grass/road/rubble/pavement must be passable floor support.
// Stairs are optional; if present the 'stairs' role must have stairs=true.
ContentResult<void> ValidateWorldgenContent(const ContentCatalog& content,
                                          Diagnostics* diagnostics = nullptr);
// Environment and regional geometry are horizontal: Y is explicitly ignored.
EnvironmentSample SampleEnvironment(std::uint64_t seed, TileCoord position);
// Cheap descriptors: the patch containing region has ten settlements before
// finite-world clipping (six small, three medium, one large). No content RNG.
PlanningPatch GeneratePlanningPatch(std::uint64_t seed, RegionCoord region);
// Full canonical settlement geometry/population, independent of querying region.
SettlementPlan GenerateSettlementPlan(const ContentCatalog& content,
    std::uint64_t seed, const SettlementDescriptor& settlement);
RegionPlan GenerateRegionPlan(const ContentCatalog& content, std::uint64_t seed,
                              RegionCoord region);
// Only world_generation_version is supported. The explicit version is an
// invariant guard, not a legacy dispatch mechanism.
RegionPlan GenerateRegionPlan(const ContentCatalog& content, std::uint64_t seed,
                              RegionCoord region, std::uint32_t generationVersion);
// Row-major z*16+x. Invalid slots of partial edge chunks are undamaged wall.
// Surface-only calls permit small catalogs without air. Upper-layer generation
// requires the 'air' openSpace role; missing/invalid roles are invariant failures.
// Base Y<0 is solid wall; Y>0 is air. Authored layers and the once-selected
// basement overlay it. Explicit space classification keeps civic sites and
// courtyards open; taller buildings author their occupied floors and roof.
// Required floor/wall terrain absence is an invariant failure. Optional grass,
// tree, road, rubble, pavement fall back to floor, wall, floor, floor, floor.
// At Y=0 the inclusive legacy demo square [-32,32] x [-32,32] is always floor;
// IsRoad masks it too. No towns or buildings intersect that square.
eastl::array<Tile, chunk_size * chunk_size> GenerateChunk(
    const ContentCatalog& content, std::uint64_t seed, const RegionPlan& plan,
    ChunkCoord chunk);

// Pure, chunk-owned placements in canonical feature/authored/leaf order. Clears
// all outputs while retaining capacity. Choices use
// local generation streams, never World RNG, handles or persistent entity IDs.
void GenerateChunkPlacements(const ContentCatalog& content, std::uint64_t seed,
    const RegionPlan& plan, ChunkCoord chunk, ChunkPlacements& output,
    PopulationMode population = PopulationMode::Urban);

// Queries are horizontal footprint queries, ignoring Y. Require valid world
// positions; return false/null outside plan bounds.
// Borrowed records remain valid only while the owning plan is unchanged/alive.
bool Contains(TileRect bounds, TileCoord position);
bool IsRoad(const RegionPlan& plan, TileCoord position);
const ParcelPlan* ParcelAt(const RegionPlan& plan, TileCoord position);
const BuildingPlan* BuildingAt(const RegionPlan& plan, TileCoord position);

// Planning patches own stable settlement identities; settlements own geometry and
// initial population. Region plans are disposable spatial slices, not city borders.
// Hashes seed local streams, never substitute for full feature identity tuples.
// No cache, runtime entities, persistence or neighboring World loads are owned here.
// World persists the generation version separately from the save schema.
}
