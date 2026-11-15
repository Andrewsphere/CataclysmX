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
    "library", "auto_repair", "warehouse", "workshop"
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
        const auto first = ChunkOf(building.bounds.min);
        const auto last = ChunkOf({building.bounds.max.x - 1, building.bounds.max.z - 1});
        for (int y = -1; y <= 0; ++y) {
            for (int z = first.z; z <= last.z; ++z) {
                for (int x = first.x; x <= last.x; ++x) {
                    const ChunkCoord chunk{x, z, y};
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

TEST_CASE("Expanded urban manifest loads all 26 explicitly populated building types",
    "[content][urban-content][manifest]") {
    const auto& content = UrbanContent();
    CHECK(content.BuildingPatternCount() == 29); // 26 populated patterns plus three legacy demos.
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

TEST_CASE("Every urban surface and every basement variant has cardinal floor and furniture access",
    "[content][urban-content][layout][reachability]") {
    const auto& content = UrbanContent();
    for (const auto id : populatedIds) {
        const auto& pattern = content.GetBuildingPattern(content.FindBuildingPattern(id));
        INFO("building=" << pattern.id.c_str() << " variant=surface");
        CheckAccess(content, pattern, pattern.tiles, 0, pattern.entrance, nullptr);
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

TEST_CASE("Urban patterns fit current parcel frontages and bounded generation includes all 26 types",
    "[content][urban-content][worldgen][eligibility]") {
    const auto& content = UrbanContent();
    const auto example = GenerateRegionPlan(content, 0, {1, -1});
    REQUIRE_FALSE(example.parcels.Empty());
    for (const auto id : populatedIds) {
        const auto& pattern = content.GetBuildingPattern(content.FindBuildingPattern(id));
        INFO("building=" << id.data() << " dimensions=" << pattern.width << 'x' << pattern.height);
        const bool sideEntrance = pattern.entrance.z != 0 && pattern.entrance.z != pattern.height - 1;
        const int width = sideEntrance ? pattern.height : pattern.width;
        const int height = sideEntrance ? pattern.width : pattern.height;
        // North/south frontages differ by a half-turn, hence have identical dimensions.
        bool north = false, south = false;
        for (const auto& parcel : example.parcels) {
            INFO("parcel=" << parcel.owner.parcel << " frontage=" << static_cast<int>(parcel.frontage));
            CHECK(width <= parcel.bounds.max.x - parcel.bounds.min.x - 4);
            CHECK(height <= parcel.bounds.max.z - parcel.bounds.min.z - 4);
            north |= parcel.frontage == Frontage::North;
            south |= parcel.frontage == Frontage::South;
        }
        CHECK(north);
        CHECK(south);
    }
    Vector<std::uint8_t> seen(content.BuildingPatternCount() + 1, 0);
    unsigned diverseRegions = 0, regionCount = 0;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        for (const auto region : {RegionCoord{0, 0}, RegionCoord{-1, 1}, RegionCoord{1, -1}}) {
            INFO("seed=" << seed << " region=(" << region.x << ',' << region.z << ')');
            const auto plan = GenerateRegionPlan(content, seed, region);
            CHECK(plan == GenerateRegionPlan(content, seed, region));
            Vector<std::uint8_t> local(seen.size(), 0);
            unsigned distinct = 0;
            for (const auto& building : plan.buildings) {
                seen[building.pattern.value] = 1;
                if (!local[building.pattern.value]) { ++distinct; local[building.pattern.value] = 1; }
                const auto* parcel = ParcelAt(plan, building.entrance);
                REQUIRE(parcel);
                CHECK(building.owner == parcel->owner);
                CHECK(building.bounds.min.x >= parcel->bounds.min.x + 2);
                CHECK(building.bounds.min.z >= parcel->bounds.min.z + 2);
                CHECK(building.bounds.max.x <= parcel->bounds.max.x - 2);
                CHECK(building.bounds.max.z <= parcel->bounds.max.z - 2);
            }
            diverseRegions += distinct > 1;
            ++regionCount;
        }
    }
    for (const auto id : populatedIds) {
        INFO("building=" << id.data());
        CHECK(seen[content.FindBuildingPattern(id).value] != 0);
    }
    // Diversity is typical across the sample, not a promise for each random region.
    CHECK(diverseRegions > regionCount / 2);
}

TEST_CASE("Urban chunk placements are deterministic rotated and owned across chunk boundaries",
    "[content][urban-content][worldgen][population][determinism]") {
    const auto& content = UrbanContent();
    bool rotations[4] = {};
    bool crossChunk = false;
    bool sawFurniture = false, sawItems = false, sawMonsters = false;
    ChunkPlacements scratch;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const auto plan = GenerateRegionPlan(content, seed, {-1, 1});
        INFO("seed=" << seed << " region=(-1,1)");
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
            GenerateChunkPlacements(content, seed, plan, chunk, scratch);
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
            GenerateChunkPlacements(content, seed, plan, chunks[i - 1], scratch);
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
            const auto expected = pattern.population.furniture.size() + (building.basementVariant == 0 ? 0
                : pattern.basements[building.basementVariant - 1].population.furniture.size());
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
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const auto plan = GenerateRegionPlan(content, seed, {1, -1});
        INFO("seed=" << seed << " region=(1,-1)");
        for (const auto chunk : PopulationChunks(plan)) {
            GenerateChunkPlacements(content, seed, plan, chunk, scratch);
            for (const auto& monster : scratch.monsters) {
                const auto* owner = BuildingAt(plan, monster.position);
                REQUIRE(owner);
                const auto& pattern = content.GetBuildingPattern(owner->pattern);
                sawFirefighter |= pattern.id == "fire_station" && monster.type == firefighterId;
                sawNurse |= pattern.id == "clinic" && monster.type == nurseId;
            }
        }
    }
    CHECK(sawFirefighter);
    CHECK(sawNurse);
}

template<class T> T Take(WorldResult<T> result) { REQUIRE(result); return eastl::move(*result); }

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
