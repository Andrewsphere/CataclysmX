#include "core/first_house_scenario.h"

#include "worldgen/worldgen.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/utility.h>
#include <cstdlib>
#include <initializer_list>

namespace cataclysmx {
namespace {

std::int64_t Distance(TileCoord position) {
    return std::int64_t{position.x} * position.x + std::int64_t{position.z} * position.z;
}

std::int64_t SettlementDistance(const SettlementDescriptor& settlement) {
    return Distance({eastl::clamp(0, settlement.bounds.min.x, settlement.bounds.max.x - 1),
        eastl::clamp(0, settlement.bounds.min.z, settlement.bounds.max.z - 1)});
}

bool Before(TileCoord entrance, FeatureOwner owner, const FirstHouseStart& best, FeatureOwner bestOwner) {
    if (Distance(entrance) != Distance(best.entrance)) { return Distance(entrance) < Distance(best.entrance); }
    if (entrance.x != best.entrance.x) { return entrance.x < best.entrance.x; }
    if (entrance.z != best.entrance.z) { return entrance.z < best.entrance.z; }
    if (owner.region.x != bestOwner.region.x) { return owner.region.x < bestOwner.region.x; }
    if (owner.region.z != bestOwner.region.z) { return owner.region.z < bestOwner.region.z; }
    if (owner.settlement != bestOwner.settlement) { return owner.settlement < bestOwner.settlement; }
    return owner.parcel < bestOwner.parcel;
}

bool HasPopulation(const PatternPopulation& population) {
    return !population.furniture.empty() || !population.loot.empty() || !population.monsters.empty();
}

// An edge house faces the outskirts, with no second building across its
// frontage before the countryside.
// This uses geometry rather than the summary town rectangle or region boundary.
bool EdgeBuilding(const SettlementPlan& plan, const BuildingPlan& building, int stepX, int stepZ) {
    for (const auto& other : plan.buildings) {
        if (other.owner == building.owner) { continue; }
        if (stepX != 0 && other.bounds.min.z < building.bounds.max.z
            && other.bounds.max.z > building.bounds.min.z
            && (stepX > 0 ? other.bounds.min.x >= building.bounds.max.x
                          : other.bounds.max.x <= building.bounds.min.x)) { return false; }
        if (stepZ != 0 && other.bounds.min.x < building.bounds.max.x
            && other.bounds.max.x > building.bounds.min.x
            && (stepZ > 0 ? other.bounds.min.z >= building.bounds.max.z
                          : other.bounds.max.z <= building.bounds.min.z)) { return false; }
    }
    return true;
}

struct StartChunk {
    ChunkCoord position;
    eastl::array<Tile, chunk_size * chunk_size> tiles;
    ChunkPlacements placements;
};

// Only candidate chunks are rasterized, once each. Plans have their actual
// 256-tile bounds: never pass a settlement-sized facade to region queries.
class StartQuery {
public:
    StartQuery(const ContentCatalog& content, std::uint32_t seed, PopulationMode population)
        : content_(content), seed_(seed), population_(population) {}

    const StartChunk& Chunk(TileCoord position) {
        const auto chunk = ChunkOf(position);
        for (const auto& cached : chunks_) {
            if (cached.position == chunk) { return cached; }
        }
        const auto& plan = Plan(position);
        StartChunk result;
        result.position = chunk;
        result.tiles = GenerateChunk(content_, seed_, plan, chunk);
        GenerateChunkPlacements(content_, seed_, plan, chunk, result.placements, population_);
        chunks_.push_back(eastl::move(result));
        return chunks_.back();
    }

    bool Supported(TileCoord position, bool door = false) {
        const auto& chunk = Chunk(position);
        const auto& terrain = content_.GetTerrain(chunk.tiles[TileIndex(position)].terrain);
        const auto* opened = door ? content_.TryGetTerrain(terrain.opensTo) : nullptr;
        for (const auto& object : chunk.placements.furniture) {
            if (object.position == position && content_.GetObject(object.type).blocksMovement) { return false; }
        }
        return (terrain.passable || (opened && opened->passable && !opened->openSpace))
            && !terrain.openSpace && !terrain.stairs;
    }

    bool Road(TileCoord position) { return IsRoad(Plan(position), position); }

    bool Free(TileCoord position) {
        const auto& placements = Chunk(position).placements;
        for (const auto& object : placements.furniture) {
            if (object.position == position) { return false; }
        }
        for (const auto& item : placements.items) {
            if (item.position == position) { return false; }
        }
        for (const auto& monster : placements.monsters) {
            if (monster.position == position) { return false; }
        }
        return true;
    }

private:
    const RegionPlan& Plan(TileCoord position) {
        const auto region = RegionOf(position);
        for (const auto& plan : plans_) {
            if (plan.region == region) { return plan; }
        }
        plans_.push_back(GenerateRegionPlan(content_, seed_, region));
        return plans_.back();
    }

    const ContentCatalog& content_;
    std::uint32_t seed_;
    PopulationMode population_;
    Vector<RegionPlan> plans_;
    Vector<StartChunk> chunks_;
};

ContentResult<BuildingStart> FindStart(const ContentCatalog& content,
    std::uint32_t seed, bool houseOnly, Diagnostics* diagnostics) {
    if (auto valid = ValidateWorldgenContent(content, diagnostics); !valid) {
        return eastl::unexpected(valid.error());
    }
    const auto house = content.FindBuildingPattern("furnished_house");
    bool hasResidential = false;
    for (std::uint32_t i = 1; i <= content.BuildingPatternCount(); ++i) {
        const auto& pattern = content.GetBuildingPattern({i});
        hasResidential |= pattern.site == BuildingSite::Building && pattern.use == BuildingUse::Residential;
    }
    Vector<SettlementDescriptor> candidates;
    // Patch coordinates are aligned even west/north of zero; passing -1 would
    // query the same patch as -6, but explicit origins make the bound clear.
    for (int z = -1; z <= 1; ++z) {
        for (int x = -1; x <= 1; ++x) {
            const auto patch = GeneratePlanningPatch(seed, {x * planning_patch_regions, z * planning_patch_regions});
            for (const auto& settlement : patch.settlements) {
                if (settlement.scale == SettlementScale::Small) { candidates.push_back(settlement); }
            }
        }
    }
    eastl::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        const auto left = SettlementDistance(a), right = SettlementDistance(b);
        if (left != right) { return left < right; }
        if (a.id.region.x != b.id.region.x) { return a.id.region.x < b.id.region.x; }
        if (a.id.region.z != b.id.region.z) { return a.id.region.z < b.id.region.z; }
        return a.id.ordinal < b.id.ordinal;
    });
    if (!houseOnly || house.value) {
        for (const auto& candidate : candidates) {
            const auto settlement = GenerateSettlementPlan(content, seed, candidate);
            StartQuery query(content, seed, houseOnly ? PopulationMode::Authored : PopulationMode::Urban);
            FirstHouseStart best;
            FeatureOwner bestOwner;
            bool found = false;
            for (const auto& building : settlement.buildings) {
                const auto& pattern = content.GetBuildingPattern(building.pattern);
                if (houseOnly) {
                    if (building.pattern != house) { continue; }
                } else {
                    if (pattern.site != BuildingSite::Building) { continue; }
                    if (hasResidential) {
                        if (pattern.use != BuildingUse::Residential) { continue; }
                    } else {
                        if (pattern.use != BuildingUse::Mixed) { continue; }
                        const bool basementPopulated = building.basementVariant
                            && HasPopulation(pattern.basements[building.basementVariant - 1].population);
                        if (!HasPopulation(pattern.population) && !basementPopulated) { continue; }
                    }
                }
                const auto entrance = building.entrance;
                const auto street = building.driveway.last;
                const int stepX = (street.x > entrance.x) - (street.x < entrance.x);
                const int stepZ = (street.z > entrance.z) - (street.z < entrance.z);
                if (std::abs(stepX) + std::abs(stepZ) != 1) { continue; }
                if (!houseOnly && !EdgeBuilding(settlement, building, stepX, stepZ)) { continue; }
                const TileCoord outside{entrance.x + stepX, entrance.z + stepZ, entrance.y};
                if (Contains(building.bounds, outside) || !query.Road(outside) || !query.Supported(outside)
                    || !query.Supported(entrance, true) || !query.Free(outside)) { continue; }
                // The driveway must reach the street, with a supported road
                // retreat on at least one side. No visibility/wall safety rule.
                bool accessible = true;
                for (auto position = outside; position != street;) {
                    if (!query.Road(position) || !query.Supported(position)) { accessible = false; break; }
                    position.x += stepX;
                    position.z += stepZ;
                }
                if (!accessible || !query.Road(street) || !query.Supported(street)) { continue; }
                bool retreat = false;
                for (int sign : {-1, 1}) {
                    bool clear = true;
                    for (int distance = 1; distance <= 4; ++distance) {
                        const TileCoord position{street.x + sign * stepZ * distance,
                            street.z + sign * stepX * distance, street.y};
                        if (!query.Road(position) || !query.Supported(position)) { clear = false; break; }
                    }
                    retreat |= clear;
                }
                if (!retreat) { continue; }
                if (!found || Before(entrance, building.owner, best, bestOwner)) {
                    best = {outside, entrance};
                    bestOwner = building.owner;
                    found = true;
                }
            }
            if (found) { return best; }
        }
    }
    if (diagnostics) {
        diagnostics->Add(houseOnly
            ? "First-house scenario: no usable furnished_house in the origin 3x3 planning patches"
            : "Town scenario: no usable residential edge entrance in a small settlement in the origin 3x3 planning patches");
    }
    return eastl::unexpected(ContentError::InvalidData);
}

}

ContentResult<FirstHouseStart> FindFirstHouseStart(const ContentCatalog& content,
    std::uint32_t seed, Diagnostics* diagnostics) {
    return FindStart(content, seed, true, diagnostics);
}

ContentResult<BuildingStart> FindTownStart(const ContentCatalog& content,
    std::uint32_t seed, Diagnostics* diagnostics) {
    return FindStart(content, seed, false, diagnostics);
}

}
