#include "core/first_house_scenario.h"
#include "core/server.h"
#include "worldgen/worldgen.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>

namespace {
using namespace cataclysmx;

ContentCatalog LoadPack(const char* path) {
    Diagnostics diagnostics;
    auto result = LoadContentPack(path, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    return eastl::move(*result);
}

ContentCatalog SmallTown(bool sparsePopulation = false) {
    const ContentSource source{"small-town.json5", R"({format_version:1,
      objects:[{id:'cabinet',name:'Cabinet',behavior:'none',capacity_volume_ml:1000}],
      definitions:[
        {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
        {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
        {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
        {type:'terrain',id:'closed_door',name:'Door',movement:{passable:false},blocks_sight:true,opens_to:'open_door',looks_like:'wall'},
        {type:'terrain',id:'open_door',name:'Open door',movement:{passable:true},blocks_sight:false,closes_to:'closed_door',looks_like:'floor'},
        {type:'terrain',id:'stairs',name:'Stairs',movement:{passable:true},stairs:true,blocks_sight:false,looks_like:'floor'},
        {type:'item',id:'rock',name:'Rock',mass_g:10,volume_ml:20},
        {type:'item',id:'bandage',name:'Bandage',mass_g:2,volume_ml:5},
        {type:'item',id:'backpack',name:'Backpack',mass_g:10,volume_ml:10,capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
        {type:'building_pattern',id:'small_shop',rows:['...','...','...'],
         palette:{'.':'floor'},entrance:{x:1,z:0},
         furniture:[{x:1,z:1,y:0,object:'cabinet'}]}
      ]})"};
    const ContentSource emptyShops{"empty-shops.json5", R"({format_version:1,definitions:[
        {type:'building_pattern',id:'empty_shop',weight:30,rows:['...','...','...'],
         palette:{'.':'floor'},entrance:{x:1,z:0}}
    ]})"};
    const ContentSource sources[] = {source, emptyShops};
    Diagnostics diagnostics;
    auto result = LoadContent({sources, sparsePopulation ? 2u : 1u}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(result);
    REQUIRE(ValidateServerContent(*result));
    return eastl::move(*result);
}

bool Populated(const PatternPopulation& population) {
    return !population.furniture.empty() || !population.loot.empty() || !population.monsters.empty();
}

bool Populated(const BuildingPattern& pattern, const BuildingPlan& building) {
    return Populated(pattern.population) || (building.basementVariant != 0
        && Populated(pattern.basements[building.basementVariant - 1].population));
}

const TerrainDefinition& TerrainAt(const ContentCatalog& content, const RegionPlan& plan, TileCoord position) {
    const auto tiles = GenerateChunk(content, plan.seed, plan, ChunkOf(position));
    return content.GetTerrain(tiles[static_cast<std::size_t>(TileIndex(position))].terrain);
}

TEST_CASE("Town query selects a usable residential edge in a small settlement", "[town][scenario][worldgen]") {
    const auto content = LoadPack(CATACLYSMX_CONTENT_PACK);
    for (const std::uint32_t seed : {0u, 13u, 42u}) {
        INFO("seed=" << seed);
        Diagnostics diagnostics;
        const auto start = FindTownStart(content, seed, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str());
        REQUIRE(start);
        CHECK(start == FindTownStart(content, seed));
        const auto region = RegionOf(start->entrance);
        const auto plan = GenerateRegionPlan(content, seed, region);
        const auto* building = BuildingAt(plan, start->entrance);
        REQUIRE(building);
        const auto& pattern = content.GetBuildingPattern(building->pattern);
        INFO("starting pattern=" << pattern.id.c_str());
        CHECK(building->entrance == start->entrance);
        CHECK(pattern.site == BuildingSite::Building);
        CHECK(pattern.use == BuildingUse::Residential);
        const auto patch = GeneratePlanningPatch(seed, building->owner.region);
        CHECK(patch.origin.x >= -planning_patch_regions);
        CHECK(patch.origin.x <= planning_patch_regions);
        CHECK(patch.origin.z >= -planning_patch_regions);
        CHECK(patch.origin.z <= planning_patch_regions);
        const SettlementDescriptor* descriptor = nullptr;
        for (const auto& settlement : patch.settlements) {
            if (settlement.id.region == building->owner.region
                && settlement.id.ordinal == building->owner.settlement) { descriptor = &settlement; }
        }
        REQUIRE(descriptor);
        CHECK(descriptor->scale == SettlementScale::Small);
        const auto settlement = GenerateSettlementPlan(content, seed, *descriptor);
        const int stepX = start->playerPosition.x - start->entrance.x;
        const int stepZ = start->playerPosition.z - start->entrance.z;
        for (const auto& other : settlement.buildings) {
            if (other.owner == building->owner) { continue; }
            if (stepX != 0 && other.bounds.min.z < building->bounds.max.z
                && other.bounds.max.z > building->bounds.min.z) {
                CHECK_FALSE((stepX > 0 ? other.bounds.min.x >= building->bounds.max.x
                                      : other.bounds.max.x <= building->bounds.min.x));
            }
            if (stepZ != 0 && other.bounds.min.x < building->bounds.max.x
                && other.bounds.max.x > building->bounds.min.x) {
                CHECK_FALSE((stepZ > 0 ? other.bounds.min.z >= building->bounds.max.z
                                      : other.bounds.max.z <= building->bounds.min.z));
            }
        }
        const auto outsidePlan = GenerateRegionPlan(content, seed, RegionOf(start->playerPosition));
        CHECK_FALSE(Contains(building->bounds, start->playerPosition));
        CHECK(IsRoad(outsidePlan, start->playerPosition));
        CHECK(start->playerPosition.y == start->entrance.y);
        CHECK(std::abs(start->playerPosition.x - start->entrance.x)
            + std::abs(start->playerPosition.z - start->entrance.z) == 1);
        const auto& floor = TerrainAt(content, outsidePlan, start->playerPosition);
        CHECK(floor.passable); CHECK_FALSE(floor.openSpace); CHECK_FALSE(floor.stairs);
        const auto& door = TerrainAt(content, plan, start->entrance);
        const auto* opened = content.TryGetTerrain(door.opensTo);
        CHECK((door.passable || (opened && opened->passable)));
        CHECK_FALSE(door.openSpace);
        ChunkPlacements placements;
        GenerateChunkPlacements(content, seed, outsidePlan, ChunkOf(start->playerPosition), placements);
        for (const auto& object : placements.furniture) { CHECK(object.position != start->playerPosition); }
        for (const auto& item : placements.items) { CHECK(item.position != start->playerPosition); }
        for (const auto& monster : placements.monsters) { CHECK(monster.position != start->playerPosition); }
    }
}

TEST_CASE("Town server starts at the queried position with the usual player supplies", "[town][scenario][server]") {
    const auto content = LoadPack(CATACLYSMX_CONTENT_PACK);
    REQUIRE(ValidateServerContent(content));
    for (const std::uint32_t seed : {0u, 13u, 42u}) {
        INFO("seed=" << seed);
        const auto start = FindTownStart(content, seed);
        REQUIRE(start);
        const auto plan = GenerateRegionPlan(content, seed, RegionOf(start->entrance));
        const auto* building = BuildingAt(plan, start->entrance);
        REQUIRE(building);
        INFO("starting pattern=" << content.GetBuildingPattern(building->pattern).id.c_str());
        Server server(content, seed, NewGameScenario::Town);
        auto& world = server.GetWorld();
        const auto* player = world.GetActor(server.GetPlayer());
        REQUIRE(player);
        CHECK(player->player);
        CHECK(player->monsterType.value == 0);
        CHECK(player->position == start->playerPosition);
        CHECK(world.ActorAt(start->playerPosition) == server.GetPlayer());
        CHECK(server.View(0).player_position == start->playerPosition);
        CHECK(server.GetTick() == 0);
        CHECK(world.IsActive(start->playerPosition));
        REQUIRE(world.GetChunk(ChunkOf(start->playerPosition)));
        REQUIRE(world.GetTile(start->playerPosition));
        REQUIRE(player->inventory.size() == 1);
        const auto bag = player->inventory[0];
        REQUIRE(world.GetItem(bag));
        CHECK(world.GetItem(bag)->type == content.FindItem("backpack"));
        CHECK(world.GetItem(bag)->location == ItemLocation::Carried(server.GetPlayer()));
        REQUIRE(world.GetItem(bag)->contents.size() == 1);
        const auto bandage = world.GetItem(bag)->contents[0];
        REQUIRE(world.GetItem(bandage));
        CHECK(world.GetItem(bandage)->type == content.FindItem("bandage"));
        CHECK(world.GetItem(bandage)->location == ItemLocation::Inside(bag));
        CHECK(world.CheckInvariants());
    }
}

TEST_CASE("Town accepts populated patterns without the first house", "[town][scenario][content]") {
    const auto content = SmallTown();
    CHECK(content.FindBuildingPattern("furnished_house").value == 0);
    Diagnostics diagnostics;
    const auto start = FindTownStart(content, 42, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(start);
    const auto plan = GenerateRegionPlan(content, 42, RegionOf(start->entrance));
    const auto* building = BuildingAt(plan, start->entrance);
    REQUIRE(building);
    INFO("seed=42 starting pattern=" << content.GetBuildingPattern(building->pattern).id.c_str());
    CHECK(content.GetBuildingPattern(building->pattern).id == "small_shop");
    CHECK(Populated(content.GetBuildingPattern(building->pattern), *building));
    CHECK(FindFirstHouseStart(content, 42) == eastl::unexpected(ContentError::InvalidData));
}

TEST_CASE("Town rejects demo content without a populated pattern", "[town][scenario][content]") {
    Diagnostics diagnostics;
    const auto result = FindTownStart(TestContent(), 42, &diagnostics);
    REQUIRE_FALSE(result);
    CHECK(result.error() == ContentError::InvalidData);
    CHECK_FALSE(diagnostics.Empty());
}

TEST_CASE("Curated first house has reproducible starts", "[town][scenario][house]") {
    const auto content = LoadPack(CATACLYSMX_FIRST_HOUSE_PACK);
    for (const auto seed : {0u, 13u, 42u}) {
        INFO("seed=" << seed << " starting pattern=furnished_house");
        const auto start = FindFirstHouseStart(content, seed);
        REQUIRE(start);
        CHECK(start == FindFirstHouseStart(content, seed));
        const auto plan = GenerateRegionPlan(content, seed, RegionOf(start->entrance));
        const auto* building = BuildingAt(plan, start->entrance);
        REQUIRE(building);
        CHECK(content.GetBuildingPattern(building->pattern).id == "furnished_house");
    }
}

TEST_CASE("Town mixed fallback skips unpopulated patterns", "[town][scenario][worldgen]") {
    const auto content = SmallTown(true);
    for (const std::uint32_t seed : {0u, 13u, 42u}) {
        const auto start = FindTownStart(content, seed);
        REQUIRE(start);
        CHECK(start == FindTownStart(content, seed));
        const auto plan = GenerateRegionPlan(content, seed, RegionOf(start->entrance));
        const auto* building = BuildingAt(plan, start->entrance);
        REQUIRE(building);
        const auto& pattern = content.GetBuildingPattern(building->pattern);
        CHECK(pattern.use == BuildingUse::Mixed);
        CHECK(Populated(pattern, *building));
    }
}
}
