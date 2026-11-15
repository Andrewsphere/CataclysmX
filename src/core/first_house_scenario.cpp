#include "core/first_house_scenario.h"

#include "worldgen/worldgen.h"

namespace cataclysmx {
namespace {

std::int64_t Distance(TileCoord position) {
    return static_cast<std::int64_t>(position.x) * position.x
        + static_cast<std::int64_t>(position.z) * position.z;
}

bool Before(TileCoord entrance, FeatureOwner owner, const FirstHouseStart& best, FeatureOwner bestOwner) {
    if (Distance(entrance) != Distance(best.entrance)) { return Distance(entrance) < Distance(best.entrance); }
    if (entrance.x != best.entrance.x) { return entrance.x < best.entrance.x; }
    if (entrance.z != best.entrance.z) { return entrance.z < best.entrance.z; }
    if (owner.region.x != bestOwner.region.x) { return owner.region.x < bestOwner.region.x; }
    if (owner.region.z != bestOwner.region.z) { return owner.region.z < bestOwner.region.z; }
    return owner.parcel < bestOwner.parcel;
}

Tile TerrainAt(const ContentCatalog& content, const RegionPlan& plan, TileCoord position) {
    const auto tiles = GenerateChunk(content, plan.seed, plan, ChunkOf(position));
    return tiles[static_cast<std::size_t>(TileIndex(position))];
}

}

ContentResult<FirstHouseStart> FindFirstHouseStart(const ContentCatalog& content,
    std::uint32_t seed, Diagnostics* diagnostics) {
    if (auto valid = ValidateWorldgenContent(content, diagnostics); !valid) {
        return eastl::unexpected(valid.error());
    }
    const auto house = content.FindBuildingPattern("furnished_house");
    FirstHouseStart best;
    FeatureOwner bestOwner;
    bool found = false;
    ChunkPlacements population;
    // Squared region distance, then x,z. Evaluate all nine regions before
    // choosing the nearest entrance, rather than stopping at the first region.
    constexpr RegionCoord regions[] = {{0, 0}, {-1, 0}, {0, -1}, {0, 1}, {1, 0},
        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    if (house.value != 0) {
        for (const auto region : regions) {
            const auto plan = GenerateRegionPlan(content, seed, region);
            for (const auto& building : plan.buildings) {
                if (building.pattern != house) { continue; }
                const auto entrance = building.entrance;
                const int step = building.driveway.last.z < entrance.z ? -1 : 1;
                const TileCoord outside{entrance.x, entrance.z + step, entrance.y};
                if (Contains(building.bounds, outside) || !IsRoad(plan, outside)) { continue; }
                const auto& floor = content.GetTerrain(TerrainAt(content, plan, outside).terrain);
                if (!floor.passable || floor.openSpace || floor.stairs) { continue; }
                const auto& door = content.GetTerrain(TerrainAt(content, plan, entrance).terrain);
                const auto* opened = content.TryGetTerrain(door.opensTo);
                if ((!door.passable && (!opened || !opened->passable)) || door.openSpace) { continue; }
                GenerateChunkPlacements(content, seed, plan, ChunkOf(outside), population);
                bool occupied = false;
                for (const auto& object : population.furniture) { occupied |= object.position == outside; }
                for (const auto& item : population.items) { occupied |= item.position == outside; }
                for (const auto& monster : population.monsters) { occupied |= monster.position == outside; }
                if (occupied) { continue; }
                if (!found || Before(entrance, building.owner, best, bestOwner)) {
                    best = {outside, entrance};
                    bestOwner = building.owner;
                    found = true;
                }
            }
        }
    }
    if (!found) {
        if (diagnostics) {
            diagnostics->Add("First-house scenario: no suitable furnished_house entrance in the origin 3x3 regions");
        }
        return eastl::unexpected(ContentError::InvalidData);
    }
    return best;
}

}
