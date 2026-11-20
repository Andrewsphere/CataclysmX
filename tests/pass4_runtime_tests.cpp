#include "core/first_house_scenario.h"
#include "core/save_database.h"
#include "core/server.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <EASTL/utility.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <string>

namespace {
using namespace cataclysmx;

ContentCatalog RuntimeContent() {
    const ContentSource source{"pass4-runtime", R"({format_version:1,
      ai_profiles:[{id:'sighted'}],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'terrain',id:'closed_door',name:'Door',movement:{passable:false},blocks_sight:true,opens_to:'open_door',looks_like:'wall'},
      {type:'terrain',id:'open_door',name:'Open door',movement:{passable:true},blocks_sight:false,closes_to:'closed_door',looks_like:'floor'},
      {type:'item',id:'rock',name:'Rock',mass_g:1,volume_ml:1},
      {type:'item',id:'bandage',name:'Bandage',mass_g:1,volume_ml:1},
      {type:'item',id:'backpack',name:'Backpack',mass_g:1,volume_ml:1,capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
      {type:'monster',id:'walker',name:'Walker',ai_profile:'sighted'},
      {type:'monster_group',id:'undead',entries:[{monster:'walker'}]},
      {type:'building_pattern',id:'furnished_house',use:'residential',
       rows:['.......','..#....','..#....','..#....','..#....','..#....','.......'],
       palette:{'.':'floor','#':'wall'},entrance:{x:3,z:0},
       layers:[{y:1,rows:['.......','.......','.......','.......','.......','.......','.......']}],
       monsters:[{x:3,z:3,y:0,group:'undead'},{x:4,z:3,y:0,group:'undead'},{x:3,z:3,y:1,group:'undead'}]}
    ]})"};
    Diagnostics diagnostics;
    auto loaded = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded);
    return eastl::move(*loaded);
}

BuildingPlan House(const ContentCatalog& content) {
    const auto patch = GeneratePlanningPatch(42, {0, 0});
    for (const auto& descriptor : patch.settlements) {
        if (descriptor.scale != SettlementScale::Small) { continue; }
        const auto plan = GenerateSettlementPlan(content, 42, descriptor);
        if (!plan.buildings.empty()) { return plan.buildings.front(); }
    }
    FAIL("Expected a small settlement with a fitting house");
    return {};
}

TileCoord Position(const BuildingPlan& building, TileCoord local) {
    for (int turn = 0; turn < building.quarterTurns; ++turn) {
        const int x = local.x;
        local.x = 6 - local.z;
        local.z = x;
    }
    return {building.bounds.min.x + local.x, building.bounds.min.z + local.z, local.y};
}

LoadRequest Load(World& world, TileCoord position) {
    const auto chunk = ChunkOf(position);
    const auto request = world.RequestChunks({{chunk.x, chunk.z, 0}, 0}, LoadMode::resident);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    return *request;
}

struct RuntimeSave {
    std::filesystem::path directory;
    std::string path;
    RuntimeSave() {
        static std::atomic<unsigned> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() /
            ("cataclysmx-pass4-" + std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1)));
        REQUIRE(std::filesystem::create_directory(directory));
        path = (directory / "world.sqlite").string();
    }
    ~RuntimeSave() { std::error_code error; std::filesystem::remove_all(directory, error); }
};

TEST_CASE("Initial safety uses inclusive circles at radii zero sixteen and sixty four", "[pass4][runtime][safety]") {
    const auto content = RuntimeContent();
    const auto building = House(content);
    const auto monster = Position(building, {3, 3, 0});
    const auto upper = Position(building, {3, 3, 1});
    for (const int radius : {0, 16, 64}) {
        INFO("radius=" << radius);
        World world(content, 42);
        const GenerationSettings settings{PopulationMode::Authored, {monster.x - radius, monster.z, 0}, radius};
        world.ConfigureGeneration(settings);
        CHECK(world.GetGenerationSettings() == settings);
        Load(world, monster);
        CHECK(static_cast<bool>(world.ActorAt(monster).value) == (radius == 0));
        CHECK(world.ActorAt(upper).value != 0);
        CHECK(world.CheckInvariants());
    }
    SECTION("A tile just outside the circle is not excluded") {
        World world(content, 42);
        world.ConfigureGeneration({PopulationMode::Authored, {monster.x - 17, monster.z, 0}, 16});
        Load(world, monster);
        CHECK(world.ActorAt(monster).value != 0);
    }
    SECTION("Diagonal distance distinguishes the circle from a square") {
        World outside(content, 42);
        outside.ConfigureGeneration({PopulationMode::Authored,
            {monster.x - 12, monster.z - 12, 0}, 16});
        Load(outside, monster);
        CHECK(outside.ActorAt(monster).value != 0);
        World inside(content, 42);
        inside.ConfigureGeneration({PopulationMode::Authored,
            {monster.x - 9, monster.z - 12, 0}, 16});
        Load(inside, monster);
        CHECK(inside.ActorAt(monster).value == 0);
    }
    SECTION("An intervening wall does not protect the initial hostile placement") {
        World world(content, 42);
        world.ConfigureGeneration({PopulationMode::Authored, Position(building, {1, 3, 0}), 16});
        Load(world, monster);
        const auto wall = Position(building, {2, 3, 0});
        // The wall may be in the adjacent chunk; request it without changing policy.
        Load(world, wall);
        REQUIRE(world.GetTile(wall));
        CHECK(world.GetTile(wall)->terrain == content.FindTerrain("wall"));
        CHECK(world.ActorAt(monster).value == 0);
        CHECK(world.ActorAt(upper).value != 0);
    }
}

TEST_CASE("Saved generation policy does not clear current actors or respawn cleared chunks", "[pass4][runtime][save]") {
    const auto content = RuntimeContent();
    const auto building = House(content);
    const auto position = Position(building, {3, 3, 0});
    const auto upper = Position(building, {3, 3, 1});
    const GenerationSettings settings{PopulationMode::Authored, position, 64};
    RuntimeSave file;
    PersistentId retained = 0;
    {
        World world(content, 42);
        world.ConfigureGeneration(settings);
        Load(world, position);
        REQUIRE(world.ActorAt(position).value == 0);
        REQUIRE(world.ActorAt(upper).value != 0);
        REQUIRE(world.DestroyActor(world.ActorAt(upper)));
        const auto created = world.CreateMonster(content.FindMonster("walker"), position);
        REQUIRE(created);
        retained = world.GetActor(*created)->id;
        REQUIRE(world.CreateSave(file.path.c_str(), {}));
    }
    {
        World restored(content);
        REQUIRE(restored.LoadSave(file.path.c_str()));
        CHECK(restored.GetGenerationSettings() == settings);
        auto request = Load(restored, position);
        REQUIRE(restored.GetActor(restored.ActorAt(position)));
        CHECK(restored.GetActor(restored.ActorAt(position))->id == retained);
        CHECK(restored.ActorAt(upper).value == 0);
        REQUIRE(restored.ReleaseRequest(request));
        REQUIRE(restored.ReconcileChunks());
        Load(restored, position);
        REQUIRE(restored.GetActor(restored.ActorAt(position)));
        CHECK(restored.GetActor(restored.ActorAt(position))->id == retained);
        CHECK(restored.ActorAt(upper).value == 0);
    }
    sqlite3* database = nullptr;
    REQUIRE(sqlite3_open(file.path.c_str(), &database) == SQLITE_OK);
    REQUIRE(sqlite3_exec(database,
        "UPDATE world_state SET payload=json_set(payload,'$.generation_version',7);",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_close(database) == SQLITE_OK);
    World rejected(content);
    CHECK(rejected.LoadSave(file.path.c_str()) == eastl::unexpected(SaveError::UnsupportedVersion));
}

TEST_CASE("Saved generation settings validate radius position and population", "[pass4][runtime][save]") {
    const auto content = RuntimeContent();
    RuntimeSave file;
    SaveDatabase database(content);
    REQUIRE(database.Create(file.path.c_str()));
    const GenerationSettings invalid[] = {
        {PopulationMode::Authored, {}, -1},
        {PopulationMode::Urban, {}, 65},
        {static_cast<PopulationMode>(255), {}, 16},
        {PopulationMode::Urban, {0, 0, world_max_y + 1}, 16}
    };
    for (const auto settings : invalid) {
        SaveSnapshot snapshot;
        snapshot.generation = settings;
        REQUIRE(database.Begin());
        CHECK(database.WriteSnapshot(snapshot) == eastl::unexpected(SaveError::InvalidData));
    }
    SaveSnapshot old;
    old.generationVersion = world_generation_version - 1;
    REQUIRE(database.Begin());
    CHECK(database.WriteSnapshot(old) == eastl::unexpected(SaveError::UnsupportedVersion));
}

TEST_CASE("Saved initial exclusion applies to chunks first materialized after load", "[pass4][runtime][save][safety]") {
    const auto content = RuntimeContent();
    const auto building = House(content);
    const auto target = Position(building, {3, 3, 0});
    const TileCoord start{target.x - 64, target.z, target.y};
    RuntimeSave file;
    {
        World world(content, 42);
        world.ConfigureGeneration({PopulationMode::Authored, start, 64});
        Load(world, start);
        REQUIRE_FALSE(world.GetChunk(ChunkOf(target)));
        REQUIRE(world.CreateSave(file.path.c_str(), {}));
    }
    World restored(content);
    REQUIRE(restored.LoadSave(file.path.c_str()));
    Load(restored, target);
    CHECK(restored.ActorAt(target).value == 0);
    CHECK(restored.ActorAt(Position(building, {3, 3, 1})).value != 0);
    CHECK(restored.CheckInvariants());
}

TEST_CASE("Joining and reconnecting do not repeat scenario hostile exclusion", "[pass4][runtime][multiplayer][safety]") {
    const auto content = RuntimeContent();
    Server server(content, 42, NewGameScenario::Town);
    auto& world = server.GetWorld();
    const auto start = world.GetActor(server.GetPlayer())->position;
    ActorHandle arrival{};
    for (int z = -4; z <= 4 && !arrival.value; ++z) {
        for (int x = -4; x <= 4 && !arrival.value; ++x) {
            const auto result = world.CreateMonster(content.FindMonster("walker"),
                {start.x + x, start.z + z, start.y});
            if (result) { arrival = *result; }
        }
    }
    REQUIRE(arrival.value != 0);
    const auto arrivalId = world.GetActor(arrival)->id;
    const auto joined = server.JoinPlayer("Guest", 0, 12345);
    REQUIRE(joined);
    REQUIRE(server.ActivatePlayer(joined->player));
    CHECK(world.FindActor(arrivalId).value != 0);
    REQUIRE(server.DisconnectPlayer(joined->player));
    const auto rejoined = server.JoinPlayer("Guest", joined->reconnectToken, 0);
    REQUIRE(rejoined);
    REQUIRE(server.ActivatePlayer(rejoined->player));
    CHECK(world.FindActor(arrivalId).value != 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Scenario defaults retain authored first house and explicit town safety", "[pass4][runtime][scenario]") {
    const auto content = RuntimeContent();
    CHECK(DefaultStartSafetyRadius(NewGameScenario::Town) == 16);
    for (const auto scenario : {NewGameScenario::FirstHouse, NewGameScenario::Demo, NewGameScenario::ZombieShowcase}) {
        CHECK(DefaultStartSafetyRadius(scenario) == 0);
    }
    const auto start = FindFirstHouseStart(content, 42);
    REQUIRE(start);
    Server first(content, 42, NewGameScenario::FirstHouse);
    const auto& settings = first.GetWorld().GetGenerationSettings();
    CHECK(settings == GenerationSettings{PopulationMode::Authored, start->playerPosition, 0});
    const auto plan = GenerateRegionPlan(content, 42, RegionOf(start->entrance));
    const auto* building = BuildingAt(plan, start->entrance);
    REQUIRE(building);
    CHECK(building->pattern == content.FindBuildingPattern("furnished_house"));
    CHECK(first.GetWorld().ActorAt(Position(*building, {3, 3, 0})).value != 0);
    Server town(content, 42, NewGameScenario::Town, 64);
    CHECK(town.GetWorld().GetGenerationSettings().population == PopulationMode::Urban);
    CHECK(town.GetWorld().GetGenerationSettings().safetyRadius == 64);
}

}
