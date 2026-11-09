#pragma once

#include "core/world.h"

namespace cataclysmx {

inline constexpr int region_size = 256;
inline constexpr std::uint32_t world_generation_version = 3;

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
    RegionCoord region{};
    std::uint32_t parcel = 0;
    bool operator==(const FeatureOwner&) const = default;
};
enum class Frontage { North, South };
struct ParcelPlan {
    FeatureOwner owner{};
    TileRect bounds{};
    Frontage frontage = Frontage::North;
    int streetZ = 0;
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
    bool operator==(const BuildingPlan&) const = default;
};
struct RegionPlan {
    RegionCoord region{};
    std::uint64_t seed = 0;
    TileRect bounds{};
    TileRect townBounds{}; // Nominal 112x112, even in partial edge regions.
    TileCoord townCenter{};
    bool hasTown = false; // False when nominal town exceeds world bounds.
    eastl::array<BoundaryCrossing, 4> boundaries{}; // North, east, south, west.
    Vector<RoadSegment> roads;
    Vector<ParcelPlan> parcels; // Block row, block column, north/south half.
    Vector<BuildingPlan> buildings; // Same order, omitting ineligible parcels.
    bool operator==(const RegionPlan&) const = default;
};

// Invalid public coordinates abort, including INT_MIN/INT_MAX, before arithmetic.
// Tiles: [-1,000,000,1,000,000]; regions: [-3907,3906]; chunks:
// [-62500,62500], on horizontal axes; tile/chunk Y: [world_min_y,world_max_y].
// RegionOf uses mathematical floor division and ignores Y.
// Plans must be unmodified outputs of GenerateRegionPlan, and materialization
// must use the same immutable catalog and seed. No World or shared RNG is used.
RegionCoord RegionOf(TileCoord position);
// Recoverable application/content boundary; diagnostic strings are optional.
// Validates a 3D World catalog: floor/wall/air are required. Air must be
// openSpace; floor and any grass/road/rubble must be passable floor support.
// Stairs are optional; if present the 'stairs' role must have stairs=true.
ContentResult<void> ValidateWorldgenContent(const ContentCatalog& content,
                                          Diagnostics* diagnostics = nullptr);
// Environment and regional geometry are horizontal: Y is explicitly ignored.
EnvironmentSample SampleEnvironment(std::uint64_t seed, TileCoord position);
RegionPlan GenerateRegionPlan(const ContentCatalog& content, std::uint64_t seed,
                              RegionCoord region);
// Row-major z*16+x. Invalid slots of partial edge chunks are undamaged wall.
// Surface-only calls permit legacy catalogs without air. Upper-layer generation
// requires the 'air' openSpace role; missing/invalid roles are invariant failures.
// Y<0 is solid wall; Y>0 is air, except Y=1 has floor roofs covering each
// building's rectangular plan footprint (including abandoned buildings).
// Required floor/wall terrain absence is an invariant failure. Optional grass,
// tree, road, rubble fall back to floor, wall, floor, floor respectively.
// At Y=0 the inclusive legacy demo square [-32,32] x [-32,32] is always floor;
// IsRoad masks it too. No towns or buildings intersect that square.
eastl::array<Tile, chunk_size * chunk_size> GenerateChunk(
    const ContentCatalog& content, std::uint64_t seed, const RegionPlan& plan,
    ChunkCoord chunk);

// Queries are horizontal footprint queries, ignoring Y. Require valid world
// positions; return false/null outside plan bounds.
// Borrowed records remain valid only while the owning plan is unchanged/alive.
bool Contains(TileRect bounds, TileCoord position);
bool IsRoad(const RegionPlan& plan, TileCoord position);
const ParcelPlan* ParcelAt(const RegionPlan& plan, TileCoord position);
const BuildingPlan* BuildingAt(const RegionPlan& plan, TileCoord position);

// Design: canonical border keys make neighbors independent; hashes are local
// unsigned integer mixing, never globally unique feature IDs. Fixed rectilinear
// routes reserve perpendicular border approaches and avoid town parcels. Eight
// 53x26/27 parcels have north/south frontage and two-tile minimum setbacks.
// Weighted fitting templates rotate from any entrance side (corner preference:
// north, east, south, west). No cache, entities, persistence, or neighbor loads.
// Generation is deterministic for the same catalog and world_generation_version.
// World persists that version separately from the save schema. Edge records exist on borders
// outside the world; their nominal geometry is simply clipped when rasterized.
}
