#include "content/content.h"
#include "core/ratio.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/utility.h>
#include <tuple>

namespace {
using namespace cataclysmx;
constexpr std::uint64_t citySeed = 42;

const ContentCatalog& PopulationContent() {
    static const ContentCatalog content = [] {
        Diagnostics diagnostics;
        auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(loaded);
        REQUIRE(ValidateWorldgenContent(*loaded, &diagnostics));
        return eastl::move(*loaded);
    }();
    return content;
}

// Share the expensive canonical plans, not a World or a second population model.
const Vector<SettlementPlan>& RepresentativeCities() {
    static const Vector<SettlementPlan> plans = [] {
        Vector<SettlementPlan> result;
        const auto patch = GeneratePlanningPatch(citySeed, {6, 6});
        for (const auto scale : {SettlementScale::Small, SettlementScale::Medium, SettlementScale::Large}) {
            const auto descriptor = eastl::find_if(patch.settlements.begin(), patch.settlements.end(),
                [scale](const auto& value) { return value.scale == scale; });
            REQUIRE(descriptor != patch.settlements.end());
            result.push_back(GenerateSettlementPlan(PopulationContent(), citySeed, *descriptor));
        }
        return result;
    }();
    return plans;
}

const Vector<PopulationSpace>* Spaces(const BuildingPattern& pattern,
    const BuildingPlan& building, int y) {
    if (y == 0) { return &pattern.spaces; }
    for (const auto& layer : pattern.layers) {
        if (layer.y == y) { return &layer.spaces; }
    }
    if (building.basementVariant) {
        const auto& layer = pattern.basements[building.basementVariant - 1].layer;
        if (layer.y == y) { return &layer.spaces; }
    }
    return nullptr;
}

PopulationSpace MarkerSpace(const BuildingPattern& pattern, const BuildingPlan& building,
    TileCoord local) {
    const auto* spaces = Spaces(pattern, building, local.y);
    REQUIRE(spaces);
    if (spaces->empty()) { return PopulationSpace::Interior; }
    return (*spaces)[static_cast<std::size_t>(local.z * pattern.width + local.x)];
}

TileCoord LocalPosition(const BuildingPattern& pattern, const BuildingPlan& building, TileCoord p) {
    const int x = p.x - building.bounds.min.x;
    const int z = p.z - building.bounds.min.z;
    switch (building.quarterTurns) {
    case 0: return {x, z, p.y};
    case 1: return {z, pattern.height - 1 - x, p.y};
    case 2: return {pattern.width - 1 - x, pattern.height - 1 - z, p.y};
    case 3: return {pattern.width - 1 - z, x, p.y};
    }
    FAIL("Invalid building rotation");
    return {};
}

bool KeyLess(const GenerationKey& a, const GenerationKey& b) {
    return std::tie(a.owner.region.x, a.owner.region.z, a.owner.settlement, a.owner.parcel,
               a.basementVariant, a.kind, a.placement, a.ordinal)
        < std::tie(b.owner.region.x, b.owner.region.z, b.owner.settlement, b.owner.parcel,
               b.basementVariant, b.kind, b.placement, b.ordinal);
}

template<class T>
void SortAndCheckKeys(Vector<T>& values) {
    eastl::sort(values.begin(), values.end(),
        [](const T& a, const T& b) { return KeyLess(a.key, b.key); });
    for (std::size_t i = 1; i < values.size(); ++i) {
        CHECK_FALSE(values[i - 1].key == values[i].key);
    }
}

// Each region and requested terrain/placement chunk is generated at most once.
// In particular, validating thousands of actors does not re-plan a city per actor.
struct RasterChunk {
    ChunkCoord coord;
    eastl::array<Tile, chunk_size * chunk_size> tiles;
    ChunkPlacements placements;
};

class CityRaster {
public:
    const RegionPlan& Region(RegionCoord coord) {
        for (const auto& plan : regions_) {
            if (plan.region == coord) { return plan; }
        }
        regions_.push_back(GenerateRegionPlan(PopulationContent(), citySeed, coord));
        return regions_.back();
    }

    const RasterChunk& Chunk(ChunkCoord coord) {
        for (const auto& chunk : chunks_) {
            if (chunk.coord == coord) { return chunk; }
        }
        const auto& plan = Region(RegionOf({coord.x * chunk_size, coord.z * chunk_size}));
        RasterChunk chunk{coord, GenerateChunk(PopulationContent(), citySeed, plan, coord), {}};
        GenerateChunkPlacements(PopulationContent(), citySeed, plan, coord, chunk.placements);
        chunks_.push_back(eastl::move(chunk));
        return chunks_.back();
    }

    const Tile& At(TileCoord position) {
        const auto coord = ChunkOf(position);
        return Chunk(coord).tiles[static_cast<std::size_t>(
            (position.z - coord.z * chunk_size) * chunk_size + position.x - coord.x * chunk_size)];
    }

private:
    Vector<RegionPlan> regions_;
    Vector<RasterChunk> chunks_;
};

bool SupportedWalkable(TerrainId id) {
    const auto& content = PopulationContent();
    const auto& terrain = content.GetTerrain(id);
    if (terrain.passable && !terrain.openSpace) { return true; }
    const auto* open = content.TryGetTerrain(terrain.opensTo);
    return open && open->passable && !open->openSpace;
}

TEST_CASE("Bundled city populations conserve indoor baselines and outdoor density at every scale",
          "[city][worldgen][population]") {
    const auto& content = PopulationContent();
    CHECK(world_generation_version == 8);
    std::size_t selectedBasements = 0;
    std::size_t exteriorAnchors = 0;
    std::size_t upperActors = 0;
    std::size_t basementActors = 0;
    for (const auto& city : RepresentativeCities()) {
        CAPTURE(static_cast<int>(city.settlement.scale));
        REQUIRE_FALSE(city.buildings.empty());
        REQUIRE_FALSE(city.monsters.empty());
        std::uint64_t baseline = 0;
        std::size_t indoorCount = 0;
        std::size_t outdoorCount = 0;
        for (const auto& building : city.buildings) {
            const auto& pattern = content.GetBuildingPattern(building.pattern);
            std::uint64_t buildingBaseline = 0;
            const auto count = [&](const PatternPopulation& population) {
                for (const auto& marker : population.monsters) {
                    if (marker.position.y >= eastl::max(1, static_cast<int>(pattern.occupiedFloors))) { continue; }
                    const auto space = MarkerSpace(pattern, building, marker.position);
                    if (space == PopulationSpace::Interior) { buildingBaseline += marker.chance; }
                    if (space == PopulationSpace::Exterior) { ++exteriorAnchors; }
                }
            };
            count(pattern.population);
            if (building.basementVariant) {
                ++selectedBasements;
                REQUIRE(building.basementVariant <= pattern.basements.size());
                count(pattern.basements[building.basementVariant - 1].population);
            }
            baseline += buildingBaseline;
            const std::uint64_t multiplier = city.settlement.scale == SettlementScale::Small ? 5
                : city.settlement.scale == SettlementScale::Medium ? 6 : 7;
            const auto actual = static_cast<std::uint64_t>(eastl::count_if(
                city.monsters.begin(), city.monsters.end(), [&](const auto& actor) {
                    return actor.key.kind == GenerationPlacementKind::IndoorPopulation
                        && actor.key.owner == building.owner;
                })) * ratioScale;
            const auto expected = buildingBaseline * multiplier;
            CAPTURE(pattern.id.c_str(), building.owner.parcel, building.basementVariant);
            CHECK((actual > expected ? actual - expected : expected - actual) < ratioScale);
        }
        const std::uint64_t multiplier = city.settlement.scale == SettlementScale::Small ? 5
            : city.settlement.scale == SettlementScale::Medium ? 6 : 7;
        REQUIRE(baseline > 0);
        CHECK(city.indoorExpected == baseline * multiplier);

        auto sorted = city.monsters;
        SortAndCheckKeys(sorted);
        eastl::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return std::tie(a.position.x, a.position.z, a.position.y)
                < std::tie(b.position.x, b.position.z, b.position.y);
        });
        CityRaster raster;
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            const auto& actor = sorted[i];
            CAPTURE(actor.position.x, actor.position.z, actor.position.y);
            if (i) { CHECK_FALSE(sorted[i - 1].position == actor.position); }
            CHECK(Contains(city.settlement.bounds, actor.position));
            CHECK(content.TryGetMonster(actor.type) != nullptr);
            const auto& chunk = raster.Chunk(ChunkOf(actor.position));
            const auto& terrain = content.GetTerrain(raster.At(actor.position).terrain);
            CHECK(terrain.passable);
            CHECK_FALSE(terrain.openSpace);
            CHECK_FALSE(terrain.stairs);
            for (const auto& furniture : chunk.placements.furniture) {
                if (furniture.position == actor.position) {
                    CHECK_FALSE(content.GetObject(furniture.type).blocksMovement);
                }
            }
            if (actor.key.kind == GenerationPlacementKind::IndoorPopulation) {
                ++indoorCount;
                const auto building = eastl::find_if(city.buildings.begin(), city.buildings.end(),
                    [&](const auto& value) { return value.owner == actor.key.owner; });
                REQUIRE(building != city.buildings.end());
                const auto& pattern = content.GetBuildingPattern(building->pattern);
                REQUIRE(Contains(building->bounds, actor.position));
                CHECK(actor.position.y < eastl::max(1, static_cast<int>(pattern.occupiedFloors)));
                CHECK(MarkerSpace(pattern, *building, LocalPosition(pattern, *building, actor.position))
                    == PopulationSpace::Interior);
                upperActors += actor.position.y > 0;
                basementActors += actor.position.y < 0;
            } else {
                CHECK(actor.key.kind == GenerationPlacementKind::OutdoorPopulation);
                CHECK(actor.position.y == 0);
                const auto& region = raster.Region(RegionOf(actor.position));
                if (const auto* building = BuildingAt(region, actor.position)) {
                    const auto& pattern = content.GetBuildingPattern(building->pattern);
                    CHECK(MarkerSpace(pattern, *building, LocalPosition(pattern, *building, actor.position))
                        == PopulationSpace::Exterior);
                }
                ++outdoorCount;
            }
        }
        // Indoor rounding happens once per building; outdoor rounding once per city.
        const auto indoorUnits = static_cast<std::uint64_t>(indoorCount) * ratioScale;
        const auto indoorDifference = indoorUnits > city.indoorExpected
            ? indoorUnits - city.indoorExpected : city.indoorExpected - indoorUnits;
        CHECK(indoorDifference <= city.buildings.size() * ratioScale);
        const std::uint64_t density = city.settlement.scale == SettlementScale::Small ? 50
            : city.settlement.scale == SettlementScale::Medium ? 150 : 300;
        REQUIRE(city.eligibleOutdoorTiles > 0);
        const auto expected = static_cast<std::uint64_t>(city.eligibleOutdoorTiles) * density;
        const auto actual = static_cast<std::uint64_t>(outdoorCount) * ratioScale;
        CHECK((actual > expected ? actual - expected : expected - actual) < ratioScale);
    }
    CHECK(selectedBasements > 0);
    CHECK(exteriorAnchors > 0);
    CHECK(upperActors > 0);
    CHECK(basementActors > 0);
}

void AddBuildingChunks(const BuildingPlan& building, Vector<ChunkCoord>& chunks) {
    const auto& pattern = PopulationContent().GetBuildingPattern(building.pattern);
    const int bottom = building.basementVariant ? pattern.basements[building.basementVariant - 1].layer.y : 0;
    const auto first = ChunkOf(building.bounds.min);
    const auto last = ChunkOf({building.bounds.max.x - 1, building.bounds.max.z - 1});
    // Include the roof: an accidentally generated roof actor must not escape the oracle.
    for (int y = bottom; y <= eastl::max(1, static_cast<int>(pattern.occupiedFloors)); ++y) {
        for (int z = first.z; z <= last.z; ++z) {
            for (int x = first.x; x <= last.x; ++x) {
                const ChunkCoord chunk{x, z, y};
                if (eastl::find(chunks.begin(), chunks.end(), chunk) == chunks.end()) { chunks.push_back(chunk); }
            }
        }
    }
}

TEST_CASE("Clipped cross-region city chunks preserve canonical population in either visitation order",
          "[city][worldgen][population][seams]") {
    const auto& content = PopulationContent();
    const auto& city = RepresentativeCities().back();
    const BuildingPlan* seam = nullptr;
    for (const auto& building : city.buildings) {
        if (RegionOf(building.bounds.min)
            == RegionOf({building.bounds.max.x - 1, building.bounds.max.z - 1})) { continue; }
        if (!seam || content.GetBuildingPattern(building.pattern).occupiedFloors
            > content.GetBuildingPattern(seam->pattern).occupiedFloors) { seam = &building; }
    }
    REQUIRE(seam);
    Vector<ChunkCoord> chunks;
    AddBuildingChunks(*seam, chunks);
    // Also exercise outdoor authored anchors; these bias Urban but are not extra
    // authored spawns in that mode.
    std::size_t authoredExteriorMarkers = 0;
    for (const auto& sample : RepresentativeCities()) {
        for (const auto& building : sample.buildings) {
            const auto& pattern = content.GetBuildingPattern(building.pattern);
            for (const auto& marker : pattern.population.monsters) {
                if (MarkerSpace(pattern, building, marker.position) == PopulationSpace::Exterior) {
                    ++authoredExteriorMarkers;
                    AddBuildingChunks(building, chunks);
                    break;
                }
            }
        }
    }
    REQUIRE_FALSE(chunks.empty());
    CityRaster raster;
    ChunkPlacements forward;
    ChunkPlacements reverse;
    const auto collect = [&](bool backwards, ChunkPlacements& combined) {
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            const auto chunk = chunks[backwards ? chunks.size() - 1 - i : i];
            const auto& region = raster.Region(RegionOf({chunk.x * chunk_size, chunk.z * chunk_size}));
            ChunkPlacements urban;
            ChunkPlacements authored;
            GenerateChunkPlacements(content, citySeed, region, chunk, urban, PopulationMode::Urban);
            GenerateChunkPlacements(content, citySeed, region, chunk, authored, PopulationMode::Authored);
            CHECK(urban.furniture == authored.furniture);
            CHECK(urban.items == authored.items);
            for (const auto& actor : authored.monsters) {
                CHECK(actor.key.kind == GenerationPlacementKind::Monster);
                CHECK(ChunkOf(actor.position) == chunk);
            }
            for (const auto& actor : urban.monsters) {
                CHECK(actor.key.kind != GenerationPlacementKind::Monster);
                CHECK(ChunkOf(actor.position) == chunk);
            }
            combined.furniture.insert(combined.furniture.end(), urban.furniture.begin(), urban.furniture.end());
            combined.items.insert(combined.items.end(), urban.items.begin(), urban.items.end());
            combined.monsters.insert(combined.monsters.end(), urban.monsters.begin(), urban.monsters.end());
        }
        SortAndCheckKeys(combined.furniture);
        SortAndCheckKeys(combined.items);
        SortAndCheckKeys(combined.monsters);
    };
    collect(false, forward);
    collect(true, reverse);
    CHECK(forward == reverse);
    REQUIRE_FALSE(forward.furniture.empty());
    REQUIRE_FALSE(forward.items.empty());
    REQUIRE_FALSE(forward.monsters.empty());
    CHECK(authoredExteriorMarkers > 0);
    Vector<GeneratedMonster> expected;
    for (const auto& sample : RepresentativeCities()) {
        for (const auto& actor : sample.monsters) {
            if (eastl::find(chunks.begin(), chunks.end(), ChunkOf(actor.position)) != chunks.end()) {
                expected.push_back(actor);
            }
        }
    }
    SortAndCheckKeys(expected);
    CHECK(forward.monsters == expected);
}

TEST_CASE("Small city generated supported surface connects every entrance and driveway to its center",
          "[city][worldgen][roads]") {
    const auto& city = RepresentativeCities().front();
    const auto bounds = city.settlement.bounds;
    const int width = bounds.max.x - bounds.min.x;
    const int height = bounds.max.z - bounds.min.z;
    Vector<std::uint8_t> walkable(static_cast<std::size_t>(width * height), 0);
    Vector<std::uint8_t> reached(walkable.size(), 0);
    const auto index = [&](TileCoord p) {
        return static_cast<std::size_t>((p.z - bounds.min.z) * width + p.x - bounds.min.x);
    };
    CityRaster raster;
    for (int z = bounds.min.z; z < bounds.max.z; ++z) {
        for (int x = bounds.min.x; x < bounds.max.x; ++x) {
            walkable[index({x, z})] = SupportedWalkable(raster.At({x, z}).terrain);
        }
    }
    REQUIRE(walkable[index(city.settlement.center)]);
    Vector<TileCoord> queue{city.settlement.center};
    reached[index(city.settlement.center)] = 1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto p = queue[head];
        for (const TileCoord next : {TileCoord{p.x - 1, p.z}, {p.x + 1, p.z},
                 {p.x, p.z - 1}, {p.x, p.z + 1}}) {
            if (!Contains(bounds, next)) { continue; }
            const auto i = index(next);
            if (walkable[i] && !reached[i]) {
                reached[i] = 1;
                queue.push_back(next);
            }
        }
    }
    REQUIRE_FALSE(city.buildings.empty());
    for (const auto& building : city.buildings) {
        CAPTURE(PopulationContent().GetBuildingPattern(building.pattern).id.c_str());
        CHECK(reached[index(building.entrance)] != 0);
        const auto& driveway = building.driveway;
        const int dx = (driveway.last.x > driveway.first.x) - (driveway.last.x < driveway.first.x);
        const int dz = (driveway.last.z > driveway.first.z) - (driveway.last.z < driveway.first.z);
        for (auto p = driveway.first;; p.x += dx, p.z += dz) {
            REQUIRE(Contains(bounds, p));
            CHECK(reached[index(p)] != 0);
            if (p == driveway.last) { break; }
        }
    }
}
} // namespace
