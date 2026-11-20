#include "content/content.h"
#include "core/simulation.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

#include <initializer_list>

namespace {
using namespace cataclysmx;

constexpr eastl::string_view populatedIds[] = {
    "furnished_house", "bungalow", "duplex", "row_house", "farmhouse", "apartment",
    "diner", "restaurant", "laundromat", "office", "grocery_store", "convenience_store",
    "pharmacy", "hardware_store", "bookstore", "clothing_store", "electronics_store",
    "sporting_goods_store", "clinic", "fire_station", "police_station", "school",
    "library", "auto_repair", "warehouse", "workshop", "hospital",
    "courtyard_house", "tall_row_house", "office_annex", "corner_store",
    "neighborhood_park", "civic_square", "apartment_walkup"
};
constexpr PatternPoint cardinal[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

const ContentCatalog& UrbanContent() {
    static const ContentCatalog content = [] {
        Diagnostics diagnostics;
        auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(loaded.has_value());
        REQUIRE(diagnostics.Empty());
        const auto valid = ValidateWorldgenContent(*loaded, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(valid.has_value());
        REQUIRE(diagnostics.Empty());
        return eastl::move(*loaded);
    }();
    return content;
}

struct UrbanSample {
    std::uint64_t seed = 0;
    SettlementPlan city;
};

// Canonical cities are shared by discovery tests; do not search hundreds of
// regions by repeatedly preparing the same settlement and population.
const Vector<UrbanSample>& UrbanSamples() {
    static const Vector<UrbanSample> samples = [] {
        Vector<UrbanSample> result;
        for (std::uint64_t seed = 0; seed < 8; ++seed) {
            const auto patch = GeneratePlanningPatch(seed, {0, 0});
            for (const auto& descriptor : patch.settlements) {
                if (descriptor.scale == SettlementScale::Large) {
                    result.push_back({seed, GenerateSettlementPlan(UrbanContent(), seed, descriptor)});
                    break;
                }
            }
        }
        REQUIRE(result.size() == 8);
        return result;
    }();
    return samples;
}

const Vector<RegionPlan>& UrbanRegions() {
    static const Vector<RegionPlan> plans = [] {
        Vector<RegionPlan> result;
        for (const auto& sample : UrbanSamples()) {
            result.push_back(GenerateRegionPlan(UrbanContent(), sample.seed,
                RegionOf(sample.city.settlement.center)));
        }
        return result;
    }();
    return plans;
}

bool Populated(const PatternPopulation& population) {
    return !population.furniture.empty() || !population.loot.empty() || !population.monsters.empty();
}

bool Inside(const BuildingPattern& pattern, PatternPoint p) {
    return p.x >= 0 && p.z >= 0 && p.x < pattern.width && p.z < pattern.height;
}

std::size_t Index(const BuildingPattern& pattern, PatternPoint p) {
    return static_cast<std::size_t>(p.z * pattern.width + p.x);
}

// Only supported floor and doors that can actually open to supported floor are
// traversable. Locked doors are not implicitly unlocked, and corners are cardinal.
bool Walkable(const ContentCatalog& content, TerrainId id, bool openDoors) {
    const auto& terrain = content.GetTerrain(id);
    if (terrain.passable && !terrain.openSpace) { return true; }
    if (!openDoors || terrain.opensTo.value == 0) { return false; }
    const auto& opened = content.GetTerrain(terrain.opensTo);
    return opened.passable && !opened.openSpace;
}

Vector<std::uint8_t> WalkMask(const ContentCatalog& content, const BuildingPattern& pattern,
    const Vector<TerrainId>& tiles, int y, const PatternPopulation* variant,
    bool openDoors = true, bool furniture = true) {
    REQUIRE(tiles.size() == static_cast<std::size_t>(pattern.width * pattern.height));
    Vector<std::uint8_t> mask(tiles.size(), 0);
    for (std::size_t i = 0; i < tiles.size(); ++i) { mask[i] = Walkable(content, tiles[i], openDoors); }
    if (furniture) {
        const auto block = [&](const PatternPopulation& population) {
            for (const auto& marker : population.furniture) {
                if (marker.position.y == y && content.GetObject(marker.type).blocksMovement) {
                    mask[Index(pattern, {marker.position.x, marker.position.z})] = 0;
                }
            }
        };
        block(pattern.population);
        if (variant) { block(*variant); }
    }
    return mask;
}

Vector<std::uint8_t> Flood(const BuildingPattern& pattern, const Vector<std::uint8_t>& mask,
    PatternPoint source) {
    REQUIRE(Inside(pattern, source));
    REQUIRE(mask[Index(pattern, source)] != 0);
    Vector<std::uint8_t> reached(mask.size(), 0);
    Vector<PatternPoint> queue;
    queue.reserve(mask.size());
    queue.push_back(source);
    reached[Index(pattern, source)] = 1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto current = queue[head];
        for (const auto delta : cardinal) {
            const PatternPoint next{current.x + delta.x, current.z + delta.z};
            if (!Inside(pattern, next)) { continue; }
            const auto index = Index(pattern, next);
            if (!mask[index] || reached[index]) { continue; }
            reached[index] = 1;
            queue.push_back(next);
        }
    }
    return reached;
}

bool HasApproach(const BuildingPattern& pattern, const Vector<std::uint8_t>& reached, TileCoord p) {
    for (const auto delta : cardinal) {
        const PatternPoint next{p.x + delta.x, p.z + delta.z};
        if (Inside(pattern, next) && reached[Index(pattern, next)]) { return true; }
    }
    return false;
}

void CheckAccess(const ContentCatalog& content, const BuildingPattern& pattern,
    const Vector<TerrainId>& tiles, int y, PatternPoint source, const PatternPopulation* variant) {
    INFO("building=" << pattern.id.c_str() << " y=" << y
        << " source=(" << source.x << ',' << source.z << ')');
    const auto mask = WalkMask(content, pattern, tiles, y, variant);
    const auto reached = Flood(pattern, mask, source);
    for (int z = 0; z < pattern.height; ++z) {
        for (int x = 0; x < pattern.width; ++x) {
            INFO("walkable coordinate=(" << x << ',' << z << ',' << y << ')');
            const auto index = Index(pattern, {x, z});
            if (mask[index]) { CHECK(reached[index] != 0); }
        }
    }
    const auto checkPopulation = [&](const PatternPopulation& population) {
        for (const auto& marker : population.furniture) {
            if (marker.position.y != y) { continue; }
            INFO("furniture=" << content.GetObject(marker.type).id.c_str() << " coordinate=("
                << marker.position.x << ',' << marker.position.z << ',' << y << ')');
            CHECK(HasApproach(pattern, reached, marker.position));
        }
        for (const auto& marker : population.loot) {
            if (marker.position.y != y) { continue; }
            INFO("loot=" << content.GetLootPool(marker.pool).id.c_str() << " coordinate=("
                << marker.position.x << ',' << marker.position.z << ',' << y << ')');
            if (marker.destination == LootDestination::Furniture) {
                CHECK(HasApproach(pattern, reached, marker.position));
            } else {
                CHECK(reached[Index(pattern, {marker.position.x, marker.position.z})] != 0);
            }
        }
        for (const auto& marker : population.monsters) {
            if (marker.position.y != y) { continue; }
            INFO("monster group=" << content.GetMonsterGroup(marker.group).id.c_str() << " coordinate=("
                << marker.position.x << ',' << marker.position.z << ',' << y << ')');
            CHECK(reached[Index(pattern, {marker.position.x, marker.position.z})] != 0);
        }
    };
    checkPopulation(pattern.population);
    if (variant) { checkPopulation(*variant); }
}

TileCoord RotatedPosition(const BuildingPattern& pattern, const BuildingPlan& building, TileCoord p) {
    int width = pattern.width;
    int height = pattern.height;
    for (unsigned turn = 0; turn < building.quarterTurns; ++turn) {
        const int oldX = p.x;
        p.x = height - 1 - p.z;
        p.z = oldX;
        eastl::swap(width, height);
    }
    return {building.bounds.min.x + p.x, building.bounds.min.z + p.z, p.y};
}

Vector<ChunkCoord> PopulationChunks(const RegionPlan& plan) {
    Vector<ChunkCoord> chunks;
    for (const auto& building : plan.buildings) {
        const auto& pattern = UrbanContent().GetBuildingPattern(building.pattern);
        const auto first = ChunkOf(building.bounds.min);
        const auto last = ChunkOf({building.bounds.max.x - 1, building.bounds.max.z - 1});
        int bottom = 0, top = pattern.occupiedFloors;
        for (const auto& layer : pattern.layers) {
            bottom = eastl::min(bottom, layer.y);
            top = eastl::max(top, layer.y);
        }
        if (building.basementVariant) {
            bottom = eastl::min(bottom, pattern.basements[building.basementVariant - 1].layer.y);
        }
        for (int y = bottom; y <= top; ++y) {
            for (int z = first.z; z <= last.z; ++z) {
                for (int x = first.x; x <= last.x; ++x) {
                    const ChunkCoord chunk{x, z, y};
                    if (RegionOf({x * chunk_size, z * chunk_size}) != plan.region) { continue; }
                    bool present = false;
                    for (const auto existing : chunks) { present |= existing == chunk; }
                    if (!present) { chunks.push_back(chunk); }
                }
            }
        }
    }
    return chunks;
}

const PatternPopulation& Population(const BuildingPattern& pattern, const BuildingPlan& building,
    const GenerationKey& key) {
    CHECK(key.owner == building.owner);
    if (key.basementVariant == 0) { return pattern.population; }
    REQUIRE(key.basementVariant == building.basementVariant);
    REQUIRE(key.basementVariant <= pattern.basements.size());
    return pattern.basements[key.basementVariant - 1].population;
}

TEST_CASE("Expanded city manifest preserves the existing urban sites and adds eighteen patterns",
    "[content][urban-content][manifest]") {
    const auto& content = UrbanContent();
    CHECK(content.BuildingPatternCount() == 55); // 34 existing populated sites, 18 city variants, three demos.
    for (const auto id : populatedIds) {
        INFO("building=" << id.data());
        const auto patternId = content.FindBuildingPattern(id);
        REQUIRE(patternId.value != 0);
        const auto& pattern = content.GetBuildingPattern(patternId);
        CHECK(Populated(pattern.population));
        CHECK_FALSE(pattern.population.furniture.empty());
        CHECK_FALSE(pattern.population.loot.empty());
        for (const auto& basement : pattern.basements) {
            INFO("variant=" << basement.id.c_str());
            CHECK(Populated(basement.population));
            CHECK_FALSE(basement.population.furniture.empty());
            CHECK_FALSE(basement.population.loot.empty());
        }
    }
}

TEST_CASE("Furnished house rooms connect only through the intended internal openable door",
    "[content][urban-content][layout][house]") {
    const auto& content = UrbanContent();
    const auto& house = content.GetBuildingPattern(content.FindBuildingPattern("furnished_house"));
    // Authored room probes, away from furniture. Ignore furniture for this wall regression.
    constexpr PatternPoint left{5, 2}, right{7, 2}, door{6, 3};
    INFO("building=furnished_house variant=surface divider door=(6,3,0)");
    auto closed = WalkMask(content, house, house.tiles, 0, nullptr, false, false);
    const auto& terrain = content.GetTerrain(house.tiles[Index(house, door)]);
    REQUIRE_FALSE(terrain.passable);
    REQUIRE(terrain.opensTo.value != 0);
    REQUIRE(content.GetTerrain(terrain.opensTo).passable);
    CHECK_FALSE(Flood(house, closed, left)[Index(house, right)]);
    // Open precisely this door, rather than all doors or an arbitrary wall tile.
    closed[Index(house, door)] = Walkable(content, terrain.opensTo, false);
    const auto opened = Flood(house, closed, left);
    CHECK(opened[Index(house, door)] != 0);
    CHECK(opened[Index(house, right)] != 0);
    closed[Index(house, door)] = 0;
    CHECK_FALSE(Flood(house, closed, left)[Index(house, right)]);
}

TEST_CASE("Every urban surface upper floor and basement variant has cardinal floor and furniture access",
    "[content][urban-content][layout][reachability]") {
    const auto& content = UrbanContent();
    for (const auto id : populatedIds) {
        const auto& pattern = content.GetBuildingPattern(content.FindBuildingPattern(id));
        INFO("building=" << pattern.id.c_str() << " variant=surface");
        CheckAccess(content, pattern, pattern.tiles, 0, pattern.entrance, nullptr);
        for (const auto& layer : pattern.layers) {
            if (layer.y <= 0) { continue; }
            bool found = false;
            PatternPoint source{};
            for (int z = 0; z < pattern.height; ++z) {
                for (int x = 0; x < pattern.width; ++x) {
                    if (!content.GetTerrain(layer.tiles[Index(pattern, {x, z})]).stairs) { continue; }
                    const Vector<TerrainId>* below = layer.y == 1 ? &pattern.tiles : nullptr;
                    for (const auto& other : pattern.layers) {
                        if (other.y == layer.y - 1) { below = &other.tiles; }
                    }
                    REQUIRE(below);
                    CHECK(content.GetTerrain((*below)[Index(pattern, {x, z})]).stairs);
                    if (!found) { source = {x, z}; found = true; }
                }
            }
            // Explicit roofs have no stair and no population. Occupied floors
            // must be connected to the floor below, not just locally walkable.
            if (found) { CheckAccess(content, pattern, layer.tiles, layer.y, source, nullptr); }
            else {
                for (const auto& marker : pattern.population.furniture) { CHECK(marker.position.y != layer.y); }
                for (const auto& marker : pattern.population.loot) { CHECK(marker.position.y != layer.y); }
                for (const auto& marker : pattern.population.monsters) { CHECK(marker.position.y != layer.y); }
            }
        }
        for (const auto& basement : pattern.basements) {
            INFO("building=" << pattern.id.c_str() << " variant=" << basement.id.c_str());
            PatternPoint source{};
            bool found = false;
            for (int z = 0; z < pattern.height; ++z) {
                for (int x = 0; x < pattern.width; ++x) {
                    if (!content.GetTerrain(basement.layer.tiles[Index(pattern, {x, z})]).stairs) { continue; }
                    INFO("paired stairs coordinate=(" << x << ',' << z << ",-1)");
                    CHECK(content.GetTerrain(pattern.tiles[Index(pattern, {x, z})]).stairs);
                    if (!found) { source = {x, z}; found = true; }
                }
            }
            REQUIRE(found);
            // One stair source, not a multi-source flood that could hide disconnected stairs.
            CheckAccess(content, pattern, basement.layer.tiles, -1, source, &basement.population);
        }
    }
}

TEST_CASE("Urban patterns fit their lot classes and bounded generation includes every authored site",
    "[content][urban-content][worldgen][eligibility]") {
    const auto& content = UrbanContent();
    // Lots belong to complete settlements, not to the spatial region slices.
    Vector<std::uint8_t> seen(content.BuildingPatternCount() + 1, 0);
    bool uses[5] = {}, eras[2] = {};
    for (const auto& sample : UrbanSamples()) {
            INFO("seed=" << sample.seed);
            const auto& plan = sample.city;
            REQUIRE_FALSE(plan.buildings.empty());
            CHECK(plan.buildings.size() == plan.parcels.size());
            Vector<std::uint8_t> local(seen.size(), 0);
            unsigned distinct = 0;
            for (const auto& building : plan.buildings) {
                seen[building.pattern.value] = 1;
                if (!local[building.pattern.value]) { ++distinct; local[building.pattern.value] = 1; }
                const ParcelPlan* parcel = nullptr;
                for (const auto& candidate : plan.parcels) {
                    if (candidate.owner == building.owner) { parcel = &candidate; break; }
                }
                REQUIRE(parcel);
                CHECK(building.owner == parcel->owner);
                const auto& pattern = content.GetBuildingPattern(building.pattern);
                CHECK(pattern.site == parcel->site);
                CHECK(pattern.era == parcel->era);
                CHECK(pattern.minSettlementSize <= plan.settlement.scale);
                CHECK(pattern.occupiedFloors >= 1);
                REQUIRE(static_cast<unsigned>(pattern.use) < 5);
                REQUIRE(static_cast<unsigned>(pattern.era) < 2);
                uses[static_cast<unsigned>(pattern.use)] = true;
                eras[static_cast<unsigned>(pattern.era)] = true;
                CHECK(building.bounds.min.x >= parcel->bounds.min.x + 1);
                CHECK(building.bounds.min.z >= parcel->bounds.min.z + 1);
                CHECK(building.bounds.max.x <= parcel->bounds.max.x - 1);
                CHECK(building.bounds.max.z <= parcel->bounds.max.z - 1);
                const bool horizontal = parcel->frontage == Frontage::North || parcel->frontage == Frontage::South;
                if (horizontal) {
                    CHECK(parcel->bounds.max.x - parcel->bounds.min.x
                        == building.bounds.max.x - building.bounds.min.x + 2);
                } else {
                    CHECK(parcel->bounds.max.z - parcel->bounds.min.z
                        == building.bounds.max.z - building.bounds.min.z + 2);
                }
            }
            CHECK(distinct > 1);
    }
    for (const auto id : populatedIds) {
        INFO("building=" << id.data());
        CHECK(seen[content.FindBuildingPattern(id).value] != 0);
    }
    for (unsigned i = 1; i < 5; ++i) { CHECK(uses[i]); }
    CHECK(eras[0]);
    CHECK(eras[1]);
}

TEST_CASE("Urban chunk placements are deterministic rotated and owned across chunk boundaries",
    "[content][urban-content][worldgen][population][determinism]") {
    const auto& content = UrbanContent();
    bool rotations[4] = {};
    bool crossChunk = false;
    bool sawFurniture = false, sawItems = false, sawMonsters = false;
    ChunkPlacements scratch;
    for (const auto& plan : UrbanRegions()) {
        const auto seed = plan.seed;
        INFO("seed=" << seed << " region=(" << plan.region.x << ',' << plan.region.z << ')');
        for (const auto& building : plan.buildings) {
            REQUIRE(building.quarterTurns < 4);
            rotations[building.quarterTurns] = true;
            crossChunk |= ChunkOf(building.bounds.min)
                != ChunkOf({building.bounds.max.x - 1, building.bounds.max.z - 1});
        }
        const auto chunks = PopulationChunks(plan);
        Vector<ChunkPlacements> forward;
        for (const auto chunk : chunks) {
            INFO("chunk=(" << chunk.x << ',' << chunk.z << ',' << chunk.y << ')');
            GenerateChunkPlacements(content, seed, plan, chunk, scratch, PopulationMode::Authored);
            forward.push_back(scratch);
            const auto ownerOf = [&](const auto& placement) -> const BuildingPlan& {
                INFO("coordinate=(" << placement.position.x << ',' << placement.position.z << ','
                    << placement.position.y << ") variant=" << placement.key.basementVariant
                    << " marker=" << placement.key.placement);
                CHECK(ChunkOf(placement.position) == chunk);
                const auto* owner = BuildingAt(plan, placement.position);
                REQUIRE(owner);
                CHECK(placement.key.owner == owner->owner);
                return *owner;
            };
            for (const auto& placement : scratch.furniture) {
                sawFurniture = true;
                const auto& owner = ownerOf(placement);
                const auto& pattern = content.GetBuildingPattern(owner.pattern);
                INFO("building=" << pattern.id.c_str() << " variant=" << placement.key.basementVariant
                    << " furniture marker=" << placement.key.placement << " coordinate=("
                    << placement.position.x << ',' << placement.position.z << ',' << placement.position.y << ')');
                const auto& population = Population(pattern, owner, placement.key);
                REQUIRE(placement.key.placement < population.furniture.size());
                const auto& marker = population.furniture[placement.key.placement];
                CHECK(placement.key.kind == GenerationPlacementKind::Furniture);
                CHECK(placement.key.ordinal == 0);
                CHECK(placement.position == RotatedPosition(pattern, owner, marker.position));
                CHECK(placement.type == marker.type);
                CHECK(static_cast<unsigned>(placement.facing)
                    == (static_cast<unsigned>(marker.facing) + owner.quarterTurns) % 4);
            }
            for (const auto& placement : scratch.items) {
                sawItems = true;
                const auto& owner = ownerOf(placement);
                const auto& pattern = content.GetBuildingPattern(owner.pattern);
                INFO("building=" << pattern.id.c_str() << " variant=" << placement.key.basementVariant
                    << " loot marker=" << placement.key.placement << " coordinate=("
                    << placement.position.x << ',' << placement.position.z << ',' << placement.position.y << ')');
                const auto& population = Population(pattern, owner, placement.key);
                REQUIRE(placement.key.placement < population.loot.size());
                const auto& marker = population.loot[placement.key.placement];
                CHECK(placement.key.kind == GenerationPlacementKind::Loot);
                CHECK(placement.position == RotatedPosition(pattern, owner, marker.position));
                CHECK(placement.destination == marker.destination);
                CHECK(content.TryGetItem(placement.type) != nullptr);
            }
            for (const auto& placement : scratch.monsters) {
                sawMonsters = true;
                const auto& owner = ownerOf(placement);
                const auto& pattern = content.GetBuildingPattern(owner.pattern);
                INFO("building=" << pattern.id.c_str() << " variant=" << placement.key.basementVariant
                    << " monster marker=" << placement.key.placement << " coordinate=("
                    << placement.position.x << ',' << placement.position.z << ',' << placement.position.y << ')');
                const auto& population = Population(pattern, owner, placement.key);
                REQUIRE(placement.key.placement < population.monsters.size());
                const auto& marker = population.monsters[placement.key.placement];
                CHECK(placement.key.kind == GenerationPlacementKind::Monster);
                CHECK(placement.key.ordinal == 0);
                CHECK(placement.position == RotatedPosition(pattern, owner, marker.position));
                bool member = false;
                for (const auto& entry : content.GetMonsterGroup(marker.group).entries) {
                    member |= entry.monster == placement.type;
                }
                CHECK(member);
            }
        }
        for (std::size_t i = chunks.size(); i > 0; --i) {
            INFO("reverse chunk index=" << i - 1);
            GenerateChunkPlacements(content, seed, plan, chunks[i - 1], scratch, PopulationMode::Authored);
            CHECK(scratch == forward[i - 1]);
        }
        // Count deterministic furniture markers once across the complete owned chunk union.
        for (const auto& building : plan.buildings) {
            const auto& pattern = content.GetBuildingPattern(building.pattern);
            INFO("building=" << pattern.id.c_str() << " parcel=" << building.owner.parcel);
            std::size_t actual = 0;
            for (const auto& batch : forward) {
                for (const auto& placement : batch.furniture) { actual += placement.key.owner == building.owner; }
            }
            std::size_t expected = 0;
            const auto count = [&](const PatternPopulation& population) {
                for (const auto& marker : population.furniture) {
                    expected += Contains(plan.bounds, RotatedPosition(pattern, building, marker.position));
                }
            };
            count(pattern.population);
            if (building.basementVariant != 0) { count(pattern.basements[building.basementVariant - 1].population); }
            CHECK(actual == expected);
        }
    }
    for (unsigned turn = 0; turn < 4; ++turn) { INFO("rotation=" << turn); CHECK(rotations[turn]); }
    CHECK(crossChunk);
    CHECK(sawFurniture);
    CHECK(sawItems);
    CHECK(sawMonsters);
}

TEST_CASE("Urban professional groups favor distinct specialists and generate them at matching sites",
    "[content][urban-content][monsters][worldgen]") {
    const auto& content = UrbanContent();
    const auto firefighterId = content.FindMonster("zombie_firefighter");
    const auto nurseId = content.FindMonster("zombie_nurse");
    REQUIRE(firefighterId.value != 0);
    REQUIRE(nurseId.value != 0);
    const auto& firefighter = content.GetMonster(firefighterId);
    const auto& nurse = content.GetMonster(nurseId);
    CHECK_FALSE(firefighter.name.empty());
    CHECK_FALSE(nurse.name.empty());
    CHECK(firefighter.name != nurse.name);
    CHECK(firefighter.speed != nurse.speed);
    CHECK(firefighter.attackDamage != nurse.attackDamage);
    const auto favored = [&](eastl::string_view groupName, MonsterTypeId specialist) {
        INFO("group=" << groupName.data());
        const auto id = content.FindMonsterGroup(groupName);
        REQUIRE(id.value != 0);
        const auto& group = content.GetMonsterGroup(id);
        REQUIRE_FALSE(group.entries.empty());
        std::uint64_t professionalWeight = 0, otherWeight = 0;
        for (const auto& entry : group.entries) {
            REQUIRE(content.TryGetMonster(entry.monster));
            if (entry.monster == specialist) { professionalWeight += entry.weight; }
            else { otherWeight += entry.weight; }
        }
        CHECK(professionalWeight > otherWeight);
        CHECK(otherWeight > 0); // Ordinary undead remain possible.
    };
    favored("monsters_fire_station", firefighterId);
    favored("monsters_medical", nurseId);
    bool sawFirefighter = false, sawNurse = false;
    ChunkPlacements scratch;
    for (const auto& sample : UrbanSamples()) {
        for (const auto& building : sample.city.buildings) {
            const auto& pattern = content.GetBuildingPattern(building.pattern);
            if (!((pattern.id == "fire_station" && !sawFirefighter)
                || (pattern.id == "clinic" && !sawNurse))) { continue; }
            RegionPlan plan;
            bool cached = false;
            for (const auto& marker : pattern.population.monsters) {
                const auto position = RotatedPosition(pattern, building, marker.position);
                const auto region = RegionOf(position);
                if (!cached || plan.region != region) {
                    plan = GenerateRegionPlan(content, sample.seed, region);
                    cached = true;
                }
                GenerateChunkPlacements(content, sample.seed, plan, ChunkOf(position), scratch,
                    PopulationMode::Authored);
                for (const auto& monster : scratch.monsters) {
                    if (monster.key.owner != building.owner) { continue; }
                    sawFirefighter |= pattern.id == "fire_station" && monster.type == firefighterId;
                    sawNurse |= pattern.id == "clinic" && monster.type == nurseId;
                }
            }
        }
        if (sawFirefighter && sawNurse) { break; }
    }
    CHECK(sawFirefighter);
    CHECK(sawNurse);
}

TEST_CASE("Imported zombie roles belong to suitable groups and spawn in generated towns",
    "[content][urban-content][monsters][worldgen][zombie-spawns]") {
    const auto& content = UrbanContent();
    struct Role { eastl::string_view monster, group; };
    constexpr Role roles[] = {
        {"zombie_cashier", "monsters_shoppers"},
        {"zombie_librarian", "monsters_school"},
        {"zombie_postal_carrier", "monsters_residential"},
        {"zombie_driver", "monsters_residential"},
        {"zombie_courier", "monsters_office"},
        {"zombie_janitor", "monsters_school"},
        {"zombie_cleaner", "monsters_residential"},
        {"zombie_paramedic", "monsters_medical"},
        {"zombie_maintenance_worker", "monsters_workshop"},
        {"zombie_security_guard", "monsters_police_station"},
        {"zombie_laboratory_technician", "monsters_medical"},
        {"zombie_sailor", "monsters_warehouse"},
        {"zombie_miner", "monsters_workshop"},
        {"zombie_homeless", "monsters_outdoors"},
        {"zombie_traveler", "monsters_outdoors"},
        {"zombie_athlete", "monsters_outdoors"},
        {"zombie_musician", "monsters_residential"},
        {"zombie_butcher", "monsters_restaurant"},
        {"zombie_carnival_attendant", "monsters_outdoors"},
        {"zombie_professor", "monsters_school"},
        {"zombie_refuse_collector", "monsters_outdoors"},
        {"zombie_commuter", "monsters_residential"},
        {"zombie_antique_dealer", "monsters_shoppers"},
    };
    Vector<bool> seen(content.MonsterCount() + 1, false);
    ChunkPlacements scratch;
    for (const auto& plan : UrbanRegions()) {
        const auto seed = plan.seed;
        for (const auto chunk : PopulationChunks(plan)) {
            GenerateChunkPlacements(content, seed, plan, chunk, scratch, PopulationMode::Authored);
            for (const auto& monster : scratch.monsters) { seen[monster.type.value] = true; }
        }
    }
    unsigned generatedRoles = 0;
    for (const auto& role : roles) {
        INFO("monster=" << role.monster.data() << ", group=" << role.group.data());
        const auto type = content.FindMonster(role.monster);
        REQUIRE(type.value != 0);
        const auto group = content.FindMonsterGroup(role.group);
        REQUIRE(group.value != 0);
        bool member = false;
        for (const auto& entry : content.GetMonsterGroup(group).entries) {
            if (entry.monster == type && entry.weight > 0) { member = true; }
        }
        CHECK(member);
        generatedRoles += seen[type.value];
        const auto& definition = content.GetMonster(type);
        CHECK(definition.speed == 100);
        CHECK(definition.attackDamage == 10);
        CHECK(definition.accuracy == 9000);
    }
    // Weighted draws need not produce every rare role in a small bounded sample.
    // Membership above covers every role; generated placements cover actual draws.
    CHECK(generatedRoles >= 8);
}

template<class T> T Take(WorldResult<T> result) { REQUIRE(result); return eastl::move(*result); }

TEST_CASE("Canonical cities retain civic plots and authored footprint roofs",
    "[worldgen][urban-content][density]") {
    const auto& content = UrbanContent();
    bool checkedSteppedRoof = false, checkedCourtyard = false, checkedCivic = false;
    unsigned landmarks = 0, parks = 0, squares = 0;
    for (const auto& sample : UrbanSamples()) {
        const auto seed = sample.seed;
        const auto& city = sample.city;
        REQUIRE_FALSE(city.buildings.empty());
        CHECK(city.buildings.size() == city.parcels.size());
        for (const auto& building : city.buildings) {
            const auto& pattern = content.GetBuildingPattern(building.pattern);
            landmarks += pattern.site == BuildingSite::Landmark;
            parks += pattern.site == BuildingSite::Park;
            squares += pattern.site == BuildingSite::Square;
            if (pattern.site == BuildingSite::Landmark) {
                CHECK(pattern.width >= 32);
                CHECK(pattern.width * pattern.height >= 600);
            }
            RegionPlan plan;
            bool cached = false;
            const auto checkTile = [&](TileCoord local, const char* terrain) {
                const auto position = RotatedPosition(pattern, building, local);
                const auto region = RegionOf(position);
                if (!cached || plan.region != region) {
                    plan = GenerateRegionPlan(content, seed, region);
                    cached = true;
                }
                const auto tiles = GenerateChunk(content, seed, plan, ChunkOf(position));
                CHECK(tiles[TileIndex(position)].terrain == content.FindTerrain(terrain));
            };
            if (pattern.id == "corner_store" && !checkedSteppedRoof) {
                checkTile({12, 8, 0}, "pavement");
                checkTile({12, 8, 1}, "air"); // No phantom roof over the side approach.
                checkTile({3, 8, 1}, "floor");
                checkedSteppedRoof = true;
            }
            if (pattern.id == "courtyard_house" && !checkedCourtyard) {
                checkTile({8, 10, 1}, "air");
                checkTile({8, 10, 2}, "air"); // Garden remains open above both floors.
                checkTile({3, 8, 2}, "floor");
                checkedCourtyard = true;
            }
            if (!checkedCivic && (pattern.site == BuildingSite::Park || pattern.site == BuildingSite::Square)) {
                checkTile({pattern.entrance.x, pattern.entrance.z, 1}, "air");
                checkedCivic = true;
            }
        }
    }
    CHECK(landmarks > 0);
    CHECK(parks > 0);
    CHECK(squares > 0);
    CHECK(checkedCivic);
    CHECK(checkedSteppedRoof);
    CHECK(checkedCourtyard);
}

TEST_CASE("Authored multi-storey buildings allow real stair travel and retain upstairs population on reload",
    "[worldgen][urban-content][vertical][world]") {
    const auto& content = UrbanContent();
    for (const auto name : {"hospital", "police_station", "fire_station", "courtyard_house",
                            "tall_row_house", "office_annex", "apartment_walkup"}) {
        INFO(name);
        const auto id = content.FindBuildingPattern(name);
        const auto& pattern = content.GetBuildingPattern(id);
        BuildingPlan building;
        std::uint64_t seed = 0;
        bool found = false;
        for (const auto& sample : UrbanSamples()) {
            for (const auto& candidate : sample.city.buildings) {
                if (candidate.pattern == id) {
                    building = candidate; seed = sample.seed; found = true; break;
                }
            }
            if (found) { break; }
        }
        REQUIRE(found);
        PatternPoint stairs{};
        found = false;
        for (int z = 0; z < pattern.height; ++z) {
            for (int x = 0; x < pattern.width; ++x) {
                if (content.GetTerrain(pattern.tiles[Index(pattern, {x, z})]).stairs) {
                    stairs = {x, z}; found = true;
                }
            }
        }
        REQUIRE(found);
        const auto start = RotatedPosition(pattern, building, {stairs.x, stairs.z, 0});
        World world(content, seed);
        const auto request = Take(world.RequestChunks({ChunkOf(start), 2}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        CHECK(world.GetGenerationVersion() == world_generation_version);
        const auto actor = Take(world.CreateActor(start, true));
        int floors = 1;
        for (int y = 1; y <= 2; ++y) {
            const TileCoord landing{start.x, start.z, y};
            if (!content.GetTerrain(world.GetTile(landing)->terrain).stairs) { break; }
            REQUIRE(world.TryActorStep(actor, {0, 0, 1}));
            CHECK(world.GetActor(actor)->position == landing);
            ++floors;
        }
        CHECK(floors >= 2);
        if (pattern.site == BuildingSite::Landmark && pattern.id != "fire_station") { CHECK(floors == 3); }
        TileCoord furniturePosition{};
        found = false;
        for (const auto& marker : pattern.population.furniture) {
            if (marker.position.y != floors - 1) { continue; }
            furniturePosition = RotatedPosition(pattern, building, marker.position);
            found = true;
            break;
        }
        REQUIRE(found);
        const auto object = world.ObjectAt(furniturePosition);
        REQUIRE(object.value);
        const auto objectId = world.GetObject(object)->id;
        const auto actorId = world.GetActor(actor)->id;
        const auto destination = world.GetActor(actor)->position;
        REQUIRE(world.ReleaseRequest(request));
        REQUIRE(world.ReconcileChunks());
        CHECK_FALSE(world.GetActor(actor));
        CHECK_FALSE(world.GetObject(object));
        Take(world.RequestChunks({ChunkOf(start), 2}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        const auto restored = world.FindActor(actorId);
        REQUIRE(world.GetActor(restored));
        CHECK(world.GetActor(restored)->position == destination);
        CHECK(world.ObjectAt(furniturePosition) == world.FindObject(objectId));
        for (int y = floors - 1; y > 0; --y) { REQUIRE(world.TryActorStep(restored, {0, 0, -1})); }
        CHECK(world.GetActor(restored)->position == start);
        REQUIRE(world.CheckInvariants());
    }
}

TEST_CASE("Urban resource recipe crafts a usable cutting tool from its authored ingredients",
    "[content][urban-content][items][crafting]") {
    const auto& content = UrbanContent();
    const auto recipeId = content.FindCraftingRecipe("make_scrap_knife");
    REQUIRE(recipeId.value != 0);
    const auto& recipe = content.GetCraftingRecipe(recipeId);
    REQUIRE(recipe.enabled);
    REQUIRE(recipe.work > 0);
    REQUIRE_FALSE(recipe.inputs.Empty());
    CHECK(recipe.result.item == content.FindItem("scrap_knife"));
    World world{content};
    Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    const auto actor = Take(world.CreateActor({2, 2}, true));
    Vector<ItemHandle> ingredients;
    for (const auto& input : recipe.inputs) {
        for (unsigned count = 0; count < input.Count(); ++count) {
            ingredients.push_back(Take(world.CreateItem(input.Item(), ItemLocation::Carried(actor))));
        }
    }
    const auto started = Take(world.StartCrafting(actor, recipeId));
    REQUIRE(started.outcome == CraftingOutcome::Started);
    std::uint32_t rng = 1;
    const auto advanced = AdvanceUntilInput(world, rng, 1000);
    CHECK(advanced.stop == SimulationStop::InputRequired);
    CHECK_FALSE(world.GetItem(started.unfinished));
    for (const auto ingredient : ingredients) { CHECK_FALSE(world.GetItem(ingredient)); }
    unsigned products = 0;
    for (const auto item : world.GetActor(actor)->inventory) {
        products += world.GetItem(item)->type == recipe.result.item;
    }
    CHECK(products == recipe.result.count);
    // The output's actual trait must satisfy another urban recipe, not just have a tool-like name.
    const auto cord = Take(world.CreateItem(content.FindItem("electrical_cord"), ItemLocation::Carried(actor)));
    const auto salvage = Take(world.StartCrafting(actor, content.FindCraftingRecipe("salvage_wire_bundle")));
    REQUIRE(salvage.outcome == CraftingOutcome::Started);
    CHECK(world.IsReservedItem(cord));
    REQUIRE(world.CheckInvariants());
}
}
