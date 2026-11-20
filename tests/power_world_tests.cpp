#include "core/world.h"
#include "power/power_system.h"
#include "core/save_database.h"
#include "worldgen/worldgen.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <EASTL/utility.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;

template<class T, class Error>
T Take(eastl::expected<T, Error> result) {
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

ContentCatalog PowerWorldContent(bool buildings = false) {
    // No regional recipes or full content pack: only three small requested columns
    // are needed to exercise a workshop beyond the player's streaming bubble.
    const ContentSource source{"power-world", R"({format_version:1,objects:[
      {id:'generator',name:'Generator',behavior:'generator',generator:{fuel_item:'fuel',fuel_capacity:10000,
        energy_per_fuel:1000,output_per_tick:1}},
      {id:'solar',name:'Solar installation',behavior:'solar',blocks_movement:true,
        solar:{peak_output_per_tick:1}},
      {id:'woodgas',name:'Wood-gas installation',behavior:'generator',blocks_movement:true,
        generator:{fuel_item:'fuel',fuel_capacity:24000,energy_per_fuel:4000,output_per_tick:4}},
      {id:'shade',name:'Shade',behavior:'none',blocks_sight:true},
      {id:'battery_box',name:'Battery box',behavior:'grid_battery'},
      {id:'bench',name:'Bench',behavior:'none',tool_traits:[{id:'cut',tier:1}],
        crafting_power:{store:'grid',energy_per_work:10000}},
      {id:'machine',name:'Machine',behavior:'crafting_machine',machine:{input:'material',input_count:1,
        output:'product',output_count:1,duration_ticks:300,energy_capacity:0,energy_per_tick:1,grid_powered:true}}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'fuel',name:'Fuel',mass_g:1,volume_ml:1},
      {type:'item',id:'solar_kit',name:'Solar kit',mass_g:1,volume_ml:1,placed_object:'solar'},
      {type:'item',id:'woodgas_kit',name:'Wood-gas kit',mass_g:1,volume_ml:1,placed_object:'woodgas'},
      {type:'item',id:'material',name:'Material',mass_g:1,volume_ml:1},
      {type:'item',id:'product',name:'Product',mass_g:1,volume_ml:1},
      {type:'tool_trait',id:'cut',name:'Cut'},
      {type:'recipe',id:'cut_product',inputs:[],tool_traits:[{id:'cut',tier:1}],
        result:{item:'product',count:1},work:300},
      {type:'item',id:'copper_wire',name:'Copper wire',mass_g:1,volume_ml:1},
      {type:'item',id:'powered_gadget',name:'Gadget',mass_g:1,volume_ml:1,capabilities:[
        {type:'energy_store',id:'cell',capacity_energy:1000,initial_energy:500,rechargeable:true}]},
      {type:'item',id:'wiring_kit',name:'Wiring kit',mass_g:1,volume_ml:1,
        wiring_material:'copper_wire',wiring_tiles_per_material:16},
      {type:'item',id:'meter',name:'Meter',mass_g:1,volume_ml:1,grid_meter:true},
      {type:'item',id:'battery',name:'Battery',mass_g:1,volume_ml:1,placed_object:'battery_box',capabilities:[
        {type:'energy_store',id:'cell',capacity_energy:1000,initial_energy:0,rechargeable:true,battery_class:'cell'}]}
    ]})"};
    Diagnostics diagnostics;
    const ContentSource buildingSource{"power-buildings", R"({format_version:1,definitions:[
      {type:'building_pattern',id:'power_house',rows:['....','....','....','....'],palette:{'.':'floor'},entrance:{x:1,z:0}}
    ]})"};
    const ContentSource sources[] = {source, buildingSource};
    auto content = LoadContent({sources, buildings ? 2u : 1u}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    return Take(eastl::move(content));
}

struct SaveDirectory {
    std::filesystem::path directory;
    std::string database;

    SaveDirectory() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; ; ++attempt) {
            directory = parent / ("cataclysmx-power-world-" + std::to_string(stamp)
                + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(directory, error);
            REQUIRE_FALSE(error);
            if (created) { break; }
        }
        database = (directory / "world.sqlite").string();
    }
    ~SaveDirectory() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

LoadRequest LoadColumn(World& world, int x, LoadMode mode = LoadMode::active,
    LoadReason reason = LoadReason::Other) {
    const auto request = Take(world.RequestChunks({{x, 0}, 0}, mode, reason));
    REQUIRE(world.ReconcileChunks());
    return request;
}

void ClearFloor(World& world, PowerBounds bounds) {
    for (int x = bounds.min.x; x <= bounds.max.x; ++x) {
        for (int z = bounds.min.z; z <= bounds.max.z; ++z) {
            const TileCoord position{x, z, bounds.min.y};
            if (const auto object = world.ObjectAt(position); object.value) { REQUIRE(world.DestroyObject(object)); }
            if (const auto actor = world.ActorAt(position); actor.value) { REQUIRE(world.DestroyActor(actor)); }
            REQUIRE(world.SetTile(position, {world.GetContent().FindTerrain("floor")}));
        }
    }
}

void ClearSky(World& world, TileCoord position) {
    for (int y = position.y + 1; y <= world_max_y; ++y) {
        const TileCoord above{position.x, position.z, y};
        if (const auto object = world.ObjectAt(above); object.value) { REQUIRE(world.DestroyObject(object)); }
        REQUIRE(world.SetTile(above, {world.GetContent().FindTerrain("air")}));
    }
}

ChunkResidencyDebug ColumnDebug(const World& world, ColumnCoord column) {
    Vector<ChunkResidencyDebug> debug;
    ChunkResidencyCounters counters;
    world.CollectChunkResidencyDebug(debug, counters);
    for (const auto& entry : debug) { if (entry.column == column) { return entry; } }
    FAIL("Expected resident power column");
    return {};
}

void CheckRemoteOnly(const World& world) {
    Vector<ChunkResidencyDebug> debug;
    ChunkResidencyCounters counters;
    world.CollectChunkResidencyDebug(debug, counters);
    CHECK(counters.loadedColumns == 3);
    CHECK(counters.activeColumns == 3);
    CHECK(counters.playerColumns == 1);
    CHECK(counters.machineColumns == 2);
    CHECK(counters.machineOnlyColumns == 2);
    CHECK(counters.sharedColumns == 0);
    CHECK(counters.loadedChunks == 3 * world_layer_count);
    for (const ColumnCoord column : {ColumnCoord{2, 0}, ColumnCoord{4, 0}}) {
        const auto entry = ColumnDebug(world, column);
        CHECK(entry.active);
        CHECK(entry.playerActiveRequests == 0);
        CHECK(entry.playerResidentRequests == 0);
        CHECK(entry.otherActiveRequests == 0);
        CHECK(entry.otherResidentRequests == 0);
        CHECK(entry.machineOwners == 1);
    }
    CHECK(ColumnDebug(world, {2, 0}).machineDependencies == 1);
    CHECK(ColumnDebug(world, {4, 0}).machineDependencies == 0);
}

struct PowerWorldRig {
    ContentCatalog content = PowerWorldContent();
    World world{content};
    LoadRequest home{};
    ActorHandle actor{};

    PowerWorldRig() {
        home = LoadColumn(world, 0, LoadMode::active, LoadReason::Player);
        ClearFloor(world, {{6, 6}, {15, 14}});
        actor = Take(world.CreateActor({8, 8}, true));
    }
    ItemHandle Carry(const char* id) {
        return Take(world.CreateItem(content.FindItem(id), ItemLocation::Carried(actor)));
    }
    ItemCapabilityId Store() const {
        return content.GetItem(content.FindItem("battery")).energyStores.front().capability;
    }
    void RemoteGrid() {
        for (int x = 40; x <= 72; x += 16) {
            Take(world.Power().ExtendBounds({{x, 8}, {x + 15, 8}}));
        }
    }
    ObjectHandle StartMachine(TileCoord position) {
        const auto machine = Take(world.CreateObject(content.FindObject("machine"), position));
        Carry("material");
        REQUIRE(world.UseObject(actor, machine, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
        return machine;
    }
};

TEST_CASE("World checkpoint restores remote power holds before the player approaches", "[power][world][save][residency]") {
    SaveDirectory files;
    auto content = PowerWorldContent();
    PersistentId playerId = 0;
    PersistentId generatorId = 0;
    PersistentId machineId = 0;
    SavedPowerState power;
    DataObject generatorState;
    DataObject machineState;
    {
        World world(content);
        LoadColumn(world, 0, LoadMode::active, LoadReason::Player);
        const auto sourceRequest = LoadColumn(world, 2);
        const auto machineRequest = LoadColumn(world, 4);
        for (int x : {8, 40, 72}) { ClearFloor(world, {{x, 7}, {x + 2, 9}}); }
        const auto player = Take(world.CreateActor({41, 8}, true));
        playerId = world.GetActor(player)->id;
        for (int x = 40; x <= 72; x += 16) { Take(world.Power().ExtendBounds({{x, 8}, {x + 15, 8}})); }
        const auto generator = Take(world.CreateObject(content.FindObject("generator"), {40, 8}));
        generatorId = world.GetObject(generator)->id;
        REQUIRE(world.SetObjectEnergy(generator, 1000));
        REQUIRE(world.UseObject(player, generator, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
        REQUIRE(world.MoveActor(player, {73, 8}));
        const auto machine = Take(world.CreateObject(content.FindObject("machine"), {72, 8}));
        machineId = world.GetObject(machine)->id;
        Take(world.CreateItem(content.FindItem("material"), ItemLocation::Carried(player)));
        REQUIRE(world.UseObject(player, machine, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
        REQUIRE(world.MoveActor(player, {8, 8}));
        REQUIRE(world.ReleaseRequest(sourceRequest));
        REQUIRE(world.ReleaseRequest(machineRequest));
        REQUIRE(world.ReconcileChunks());
        CheckRemoteOnly(world);

        SessionState session;
        session.player = playerId;
        session.playerPosition = {8, 8};
        REQUIRE(world.CreateSave(files.database.c_str(), session));
        world.ElapseTicks(25);
        REQUIRE(world.Save(session)); // Exercise the integrated global/chunk snapshot on an update too.
        REQUIRE(world.DescribeObjectState(machine));
        CHECK(world.DescribeObjectState(machine)->remainingWork == 275);
        CHECK(world.DescribeObjectState(machine)->fundedTicks == 75);
        CHECK(world.DescribeObjectState(generator)->energy == 900);
        power = world.Power().Capture();
        generatorState = world.CaptureObjectState(generator);
        machineState = world.CaptureObjectState(machine);
        CHECK(world.CheckInvariants());
    }

    World restored(content);
    const auto session = Take(restored.LoadSave(files.database.c_str()));
    CHECK(session.player == playerId);
    CHECK(session.tick == 25);
    CHECK(restored.LoadedChunks().empty());
    CHECK(restored.Power().Capture() == power);
    // Only the far-away player's column is requested. The global power index must
    // recover both remote owners without first discovering their chunk objects.
    LoadColumn(restored, 0, LoadMode::active, LoadReason::Player);
    CheckRemoteOnly(restored);
    const auto player = restored.FindActor(playerId);
    const auto generator = restored.FindObject(generatorId);
    const auto machine = restored.FindObject(machineId);
    REQUIRE(restored.GetActor(player));
    CHECK(restored.GetActor(player)->position == TileCoord{8, 8});
    REQUIRE(restored.GetObject(generator));
    REQUIRE(restored.GetObject(machine));
    CHECK(restored.CaptureObjectState(generator) == generatorState);
    CHECK(restored.CaptureObjectState(machine) == machineState);
    restored.Power().ProcessEvents();
    restored.ProcessObjectEvents();
    CHECK(restored.CaptureObjectState(generator) == generatorState);
    CHECK(restored.CaptureObjectState(machine) == machineState);
    restored.ElapseTicks(75);
    restored.Power().ProcessEvents();
    restored.ProcessObjectEvents();
    CHECK(restored.DescribeObjectState(machine)->remainingWork == 200);
    CHECK(restored.DescribeObjectState(machine)->fundedTicks == 100);
    CHECK(restored.DescribeObjectState(generator)->energy == 800);

    REQUIRE(restored.MoveActor(player, {41, 8}));
    REQUIRE(restored.UseObject(player, generator, ObjectAction::Stop).outcome == ObjectUseOutcome::Succeeded);
    REQUIRE(restored.MoveActor(player, {73, 8}));
    REQUIRE(restored.UseObject(player, machine, ObjectAction::Stop).outcome == ObjectUseOutcome::Succeeded);
    REQUIRE(restored.MoveActor(player, {8, 8}));
    REQUIRE(restored.ReconcileChunks());
    CHECK_FALSE(restored.GetChunk({2, 0}));
    CHECK_FALSE(restored.GetChunk({4, 0}));
    Vector<ChunkResidencyDebug> debug;
    ChunkResidencyCounters counters;
    restored.CollectChunkResidencyDebug(debug, counters);
    CHECK(counters.loadedColumns == 1);
    CHECK(counters.machineColumns == 0);
    CHECK(counters.machineOnlyColumns == 0);
    CHECK(restored.CheckInvariants());
}

TEST_CASE("Completed remote machine releases its owner and battery dependency", "[power][world][residency][accounting]") {
    PowerWorldRig rig;
    const auto sourceRequest = LoadColumn(rig.world, 2);
    const auto machineRequest = LoadColumn(rig.world, 4);
    ClearFloor(rig.world, {{40, 7}, {42, 9}});
    ClearFloor(rig.world, {{72, 7}, {74, 9}});
    rig.RemoteGrid();
    const auto box = Take(rig.world.CreateObject(rig.content.FindObject("battery_box"), {40, 8}));
    const auto battery = Take(rig.world.CreateItem(rig.content.FindItem("battery"), ItemLocation::Stored(box)));
    REQUIRE(rig.world.SetItemEnergy(battery, rig.Store(), 1000));
    REQUIRE(rig.world.MoveActor(rig.actor, {73, 8}));
    const auto machine = rig.StartMachine({72, 8});
    REQUIRE(rig.world.MoveActor(rig.actor, {8, 8}));
    REQUIRE(rig.world.ReleaseRequest(sourceRequest));
    REQUIRE(rig.world.ReleaseRequest(machineRequest));
    REQUIRE(rig.world.ReconcileChunks());
    CHECK(ColumnDebug(rig.world, {2, 0}).machineOwners == 0);
    CHECK(ColumnDebug(rig.world, {2, 0}).machineDependencies == 1);
    CHECK(ColumnDebug(rig.world, {4, 0}).machineOwners == 1);
    rig.world.ElapseTicks(300);
    rig.world.ProcessObjectEvents();
    const auto state = rig.world.DescribeObjectState(machine);
    REQUIRE(state);
    CHECK_FALSE(state->hasJob);
    CHECK_FALSE(state->runRequested);
    CHECK(state->remainingWork == 0);
    CHECK(state->fundedTicks == 0);
    CHECK(rig.world.GetItemEnergy(battery, rig.Store())->energy == 700);
    const auto* object = rig.world.GetObject(machine);
    REQUIRE(object);
    REQUIRE(object->contents.size() == 1);
    CHECK(rig.world.GetItem(object->contents.front())->type == rig.content.FindItem("product"));
    REQUIRE(rig.world.ReconcileChunks());
    CHECK_FALSE(rig.world.GetChunk({2, 0}));
    CHECK_FALSE(rig.world.GetChunk({4, 0}));
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("An idle machine spawned in an inactive chunk does not acquire a workshop hold", "[power][world][residency]") {
    PowerWorldRig rig;
    const auto request = LoadColumn(rig.world, 2, LoadMode::resident);
    ClearFloor(rig.world, {{40, 8}, {40, 8}});
    const auto machine = Take(rig.world.CreateObject(rig.content.FindObject("machine"), {40, 8}));
    REQUIRE(rig.world.GetObject(machine));
    CHECK_FALSE(rig.world.IsActive({40, 8}));
    CHECK_FALSE(rig.world.DescribeObjectState(machine)->runRequested);
    const auto entry = ColumnDebug(rig.world, {2, 0});
    CHECK(entry.otherResidentRequests == 1);
    CHECK(entry.machineOwners == 0);
    CHECK(entry.machineDependencies == 0);
    REQUIRE(rig.world.ReleaseRequest(request));
    REQUIRE(rig.world.ReconcileChunks());
    CHECK_FALSE(rig.world.GetChunk({2, 0}));
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Reconciliation activates an archived source across the player's grid boundary", "[power][world][residency]") {
    PowerWorldRig rig;
    const auto sourceRequest = LoadColumn(rig.world, 1, LoadMode::resident);
    ClearFloor(rig.world, {{16, 8}, {16, 8}});
    Take(rig.world.Power().ExtendBounds({{14, 8}, {17, 8}}));
    const auto box = Take(rig.world.CreateObject(rig.content.FindObject("battery_box"), {16, 8}));
    const auto battery = Take(rig.world.CreateItem(rig.content.FindItem("battery"), ItemLocation::Stored(box)));
    const auto batteryId = rig.world.GetItem(battery)->id;
    REQUIRE(rig.world.SetItemEnergy(battery, rig.Store(), 373));
    REQUIRE(rig.world.ReleaseRequest(sourceRequest));
    REQUIRE(rig.world.ReconcileChunks());
    CHECK_FALSE(rig.world.GetChunk({1, 0}));
    REQUIRE(rig.world.MoveActor(rig.actor, {15, 8}));
    const PowerDemand demand{{15, 8}, 73};
    CHECK(rig.world.Power().CanSupply({&demand, 1}) == PowerOutcome::InsufficientPower);
    REQUIRE(rig.world.ReconcileChunks());
    CHECK(rig.world.IsActive({16, 8}));
    const auto entry = ColumnDebug(rig.world, {1, 0});
    CHECK(entry.playerActiveRequests == 1);
    CHECK(entry.otherActiveRequests == 0);
    CHECK(entry.otherResidentRequests == 0);
    CHECK(entry.machineOwners == 0);
    CHECK(entry.machineDependencies == 0);
    Vector<ChunkResidencyDebug> debug;
    ChunkResidencyCounters counters;
    rig.world.CollectChunkResidencyDebug(debug, counters);
    CHECK(counters.playerColumns == 2);
    CHECK(counters.machineColumns == 0);
    CHECK(counters.machineOnlyColumns == 0);
    REQUIRE(rig.world.Power().TrySupply({&demand, 1}) == PowerOutcome::Supplied);
    CHECK(rig.world.GetItemEnergy(rig.world.FindItem(batteryId), rig.Store())->energy == 300);
    REQUIRE(rig.world.MoveActor(rig.actor, {8, 8}));
    REQUIRE(rig.world.ReconcileChunks());
    CHECK_FALSE(rig.world.GetChunk({1, 0}));
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("A paid machine slice survives source shutdown and then pauses without free work", "[power][world][accounting]") {
    PowerWorldRig rig;
    Take(rig.world.Power().ExtendBounds({{7, 8}, {9, 8}}));
    const auto generator = Take(rig.world.CreateObject(rig.content.FindObject("generator"), {7, 8}));
    REQUIRE(rig.world.SetObjectEnergy(generator, 1000));
    const auto machine = rig.StartMachine({9, 8});
    REQUIRE(rig.world.UseObject(rig.actor, generator, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    rig.world.Power().ProcessEvents();
    rig.world.ProcessObjectEvents();
    CHECK(rig.world.DescribeObjectState(machine)->fundedTicks == 100);
    CHECK(rig.world.DescribeObjectState(generator)->energy == 900);
    REQUIRE(rig.world.UseObject(rig.actor, generator, ObjectAction::Stop).outcome == ObjectUseOutcome::Succeeded);
    rig.world.ElapseTicks(150);
    rig.world.ProcessObjectEvents();
    auto state = rig.world.DescribeObjectState(machine);
    REQUIRE(state);
    CHECK(state->hasJob);
    CHECK(state->runRequested);
    CHECK_FALSE(state->running);
    CHECK(state->remainingWork == 200);
    CHECK(state->fundedTicks == 0);
    CHECK(rig.world.DescribeObjectState(generator)->energy == 900);
    REQUIRE(rig.world.UseObject(rig.actor, generator, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    rig.world.ElapseTicks(200);
    rig.world.ProcessObjectEvents();
    state = rig.world.DescribeObjectState(machine);
    REQUIRE(state);
    CHECK_FALSE(state->hasJob);
    CHECK(state->remainingWork == 0);
    CHECK(rig.world.DescribeObjectState(generator)->energy == 700);
    CHECK(rig.world.GetObject(machine)->contents.size() == 1);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Battery installation consumes ordinary wire and preserves the original charged item", "[power][world][installation]") {
    PowerWorldRig rig;
    const auto kit = rig.Carry("wiring_kit");
    const auto battery = rig.Carry("battery");
    const auto id = rig.world.GetItem(battery)->id;
    REQUIRE(rig.world.SetItemEnergy(battery, rig.Store(), 373));
    const auto before = rig.world.CaptureItemState(battery);
    const auto powerBefore = rig.world.Power().Capture();
    const auto revision = rig.world.GetRevision();
    CHECK(rig.world.PlacePowerObject(rig.actor, battery, kit, {9, 8}).outcome == PowerActionOutcome::MissingMaterials);
    CHECK_FALSE(rig.world.ObjectAt({9, 8}).value);
    CHECK(rig.world.GetItem(battery)->location == ItemLocation::Carried(rig.actor));
    CHECK(rig.world.CaptureItemState(battery) == before);
    CHECK(rig.world.Power().Capture() == powerBefore);
    CHECK(rig.world.GetRevision() == revision);
    const auto wire = rig.Carry("copper_wire");
    const auto installed = rig.world.PlacePowerObject(rig.actor, battery, kit, {9, 8});
    REQUIRE(installed.outcome == PowerActionOutcome::Succeeded);
    CHECK(installed.baseCost == 4 * ticks_per_turn);
    const auto box = rig.world.ObjectAt({9, 8});
    REQUIRE(rig.world.GetObject(box));
    CHECK(rig.world.GetObject(box)->type == rig.content.FindObject("battery_box"));
    CHECK(rig.world.FindItem(id) == battery);
    CHECK(rig.world.GetItem(battery)->location == ItemLocation::Stored(box));
    CHECK(rig.world.CaptureItemState(battery) == before);
    CHECK(rig.world.GetItemEnergy(battery, rig.Store())->energy == 373);
    REQUIRE(rig.world.GetObject(box)->contents.size() == 1);
    CHECK(rig.world.GetObject(box)->contents.front() == battery);
    CHECK_FALSE(rig.world.GetItem(wire));
    CHECK(rig.world.GetItem(kit));
    CHECK_FALSE(rig.world.CanInspectPower(rig.actor));
    const auto meter = rig.Carry("meter");
    CHECK(rig.world.CanInspectPower(rig.actor));
    REQUIRE(rig.world.TransferItem(meter, ItemLocation::Ground({8, 8})));
    CHECK_FALSE(rig.world.CanInspectPower(rig.actor));
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("World wiring rounds only newly covered tiles and failure is transactional", "[power][world][wiring]") {
    PowerWorldRig rig;
    const auto kit = rig.Carry("wiring_kit");
    const auto firstWire = rig.Carry("copper_wire");
    const auto secondWire = rig.Carry("copper_wire");
    const auto first = rig.world.WireGrid(rig.actor, kit, {{6, 6}, {9, 9}});
    REQUIRE(first.outcome == PowerActionOutcome::Succeeded);
    CHECK(first.baseCost == 16 * ticks_per_turn);
    CHECK_FALSE(rig.world.GetItem(firstWire));
    REQUIRE(rig.world.GetItem(secondWire));
    const PowerBounds extension{{9, 9}, {11, 14}}; // 18 tiles, one already covered: ceil(17 / 16) = 2.
    CHECK(Take(rig.world.Power().Quote(extension)).newTiles == 17);
    const auto powerBefore = rig.world.Power().Capture();
    const auto revision = rig.world.GetRevision();
    const auto mapRevision = rig.world.GetMapRevision();
    const auto remaining = rig.world.GetActorRemainingTicks(rig.actor);
    CHECK(rig.world.WireGrid(rig.actor, kit, extension).outcome == PowerActionOutcome::MissingMaterials);
    CHECK(rig.world.Power().Capture() == powerBefore);
    CHECK(rig.world.GetRevision() == revision);
    CHECK(rig.world.GetMapRevision() == mapRevision);
    CHECK(rig.world.GetActorRemainingTicks(rig.actor) == remaining);
    CHECK(rig.world.GetItem(secondWire)->location == ItemLocation::Carried(rig.actor));
    const auto thirdWire = rig.Carry("copper_wire");
    const auto extended = rig.world.WireGrid(rig.actor, kit, extension);
    REQUIRE(extended.outcome == PowerActionOutcome::Succeeded);
    CHECK(extended.baseCost == 17 * ticks_per_turn);
    CHECK_FALSE(rig.world.GetItem(secondWire));
    CHECK_FALSE(rig.world.GetItem(thirdWire));
    CHECK(rig.world.GetItem(kit));
    CHECK(rig.world.Power().GridAt({6, 6}) == rig.world.Power().GridAt({11, 14}));
    const auto after = rig.world.Power().Capture();
    CHECK(rig.world.WireGrid(rig.actor, kit, extension).outcome == PowerActionOutcome::NoNewCoverage);
    CHECK(rig.world.WireGrid(rig.actor, kit, {{6, 6}, {6, 6, 1}}).outcome == PowerActionOutcome::InvalidArea);
    CHECK(rig.world.Power().Capture() == after);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("World wiring joins grids through vertical faces without a merge material surcharge", "[power][world][wiring]") {
    PowerWorldRig rig;
    const auto below = Take(rig.world.Power().ExtendBounds({{6, 6, -1}, {9, 9, -1}}));
    const auto above = Take(rig.world.Power().ExtendBounds({{6, 6, 1}, {9, 9, 1}}));
    REQUIRE(below != above);
    const PowerBounds bridge{{6, 6}, {9, 9}};
    const auto quote = Take(rig.world.Power().Quote(bridge));
    CHECK(quote.newTiles == 16);
    CHECK(quote.mergedGrids == 2);
    CHECK(quote.wiringUnits == 16);
    const auto kit = rig.Carry("wiring_kit");
    const auto wire = rig.Carry("copper_wire");
    REQUIRE(rig.world.WireGrid(rig.actor, kit, bridge).outcome == PowerActionOutcome::Succeeded);
    CHECK_FALSE(rig.world.GetItem(wire));
    CHECK(rig.world.Power().GridAt({6, 6, -1}) == rig.world.Power().GridAt({9, 9, 1}));
    CHECK(rig.world.Power().GridAt({6, 6}) == rig.world.Power().GridAt({9, 9, 1}));
    CHECK(rig.world.Power().Capture().grids.size() == 1);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Generated electrical registration failure leaves existing residency intact", "[power][world][residency]") {
    auto content = PowerWorldContent(true);
    World world(content);
    const auto request = LoadColumn(world, 0);
    const auto plan = GenerateRegionPlan(content, world.GetSeed(), {1, 1}, world.GetGenerationVersion());
    REQUIRE_FALSE(plan.buildings.empty());
    const auto target = plan.buildings.front().bounds.min;
    const auto targetChunk = ChunkOf(target);
    // Deliberately bypass resident-only gameplay wiring through the admin primitive.
    Take(world.Power().ExtendBounds({target, target}));
    REQUIRE(world.UpdateRequest(request, {targetChunk, 0}, LoadMode::active));
    const auto power = world.Power().Capture();
    const auto revision = world.GetRevision();
    const auto mapRevision = world.GetMapRevision();
    const auto oldTile = *world.GetTile({8, 8});
    const auto count = world.LoadedChunks().size();
    const auto result = world.ReconcileChunks();
    REQUIRE_FALSE(result);
    CHECK(result.error() == SaveError::InvalidData);
    REQUIRE(world.GetTile({8, 8}));
    CHECK(*world.GetTile({8, 8}) == oldTile);
    CHECK_FALSE(world.GetChunk(targetChunk));
    CHECK(world.LoadedChunks().size() == count);
    CHECK(world.Power().Capture() == power);
    CHECK(world.GetRevision() == revision);
    CHECK(world.GetMapRevision() == mapRevision);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Loading refuses a world with electrical geometry but no entities", "[power][world][save]") {
    SaveDirectory files;
    auto content = PowerWorldContent();
    { World initial(content); REQUIRE(initial.CreateSave(files.database.c_str(), {})); }
    World world(content);
    Take(world.Power().ExtendBounds({{0, 0}, {1, 1}}));
    const auto power = world.Power().Capture();
    const auto result = world.LoadSave(files.database.c_str());
    REQUIRE_FALSE(result);
    CHECK(result.error() == SaveError::InvalidState);
    CHECK(world.Power().Capture() == power);
    CHECK_FALSE(world.HasSave());
}

TEST_CASE("Incoming disk equipment is rechecked against the global index before restore", "[power][world][save][residency]") {
    SaveDirectory files;
    auto content = PowerWorldContent();
    {
        World initial(content);
        LoadColumn(initial, 0); LoadColumn(initial, 2);
        ClearFloor(initial, {{40, 8}, {42, 8}});
        const auto generator = Take(initial.CreateObject(content.FindObject("generator"), {40, 8}));
        REQUIRE(initial.SetObjectEnergy(generator, 1000));
        REQUIRE(initial.CreateSave(files.database.c_str(), {}));
    }
    World world(content);
    Take(world.LoadSave(files.database.c_str()));
    const auto home = LoadColumn(world, 0);
    SaveDatabase external(content);
    const auto snapshot = Take(external.Open(files.database.c_str()));
    auto archive = Take(external.ReadChunk({2, 0}, snapshot.nextId));
    REQUIRE(archive); REQUIRE(archive->objects.size() == 1);
    const char* mutation = nullptr;
    SECTION("position changes after database open") {
        mutation = "UPDATE chunks SET payload=json_set(payload,'$.objects[0].position.x',41) WHERE x=2 AND z=0 AND y=0";
    }
    SECTION("job intent changes after database open") {
        mutation = "UPDATE chunks SET payload=json_set(payload,'$.objects[0].state.run_requested',json('true')) WHERE x=2 AND z=0 AND y=0";
    }
    SECTION("equipment disappears after database open") {
        mutation = "UPDATE chunks SET payload=json_set(payload,'$.objects',json('[]')) WHERE x=2 AND z=0 AND y=0";
    }
    SECTION("equipment role changes after database open") {
        mutation = "UPDATE chunks SET payload=json_set(payload,'$.objects[0].type','','$.objects[0].state',json('{}')) WHERE x=2 AND z=0 AND y=0";
    }
    // Deliberately bypass SaveDatabase::Commit, which correctly rejects an
    // inconsistent index. This exercises corruption occurring after Open.
    struct RawConnection { sqlite3* value = nullptr; ~RawConnection() { sqlite3_close(value); } } raw;
    REQUIRE(sqlite3_open(files.database.c_str(), &raw.value) == SQLITE_OK);
    REQUIRE(mutation);
    REQUIRE(sqlite3_exec(raw.value, mutation, nullptr, nullptr, nullptr) == SQLITE_OK);
    const auto before = world.Power().Capture();
    const auto revision = world.GetRevision();
    REQUIRE(world.UpdateRequest(home, {{2, 0}, 0}, LoadMode::active));
    const auto result = world.ReconcileChunks();
    REQUIRE_FALSE(result); CHECK(result.error() == SaveError::InvalidData);
    CHECK(world.GetChunk({0, 0})); CHECK_FALSE(world.GetChunk({2, 0}));
    CHECK(world.Power().Capture() == before);
    CHECK(world.GetRevision() == revision);
    // Invariants deliberately validate RAM only, not externally changed disk.
    CHECK(world.CheckInvariants());
}

TEST_CASE("Power invariants detect missing and inconsistent live and archived records", "[power][world][residency]") {
    PowerWorldRig rig;
    const auto sourceRequest = LoadColumn(rig.world, 2);
    ClearFloor(rig.world, {{40, 8}, {42, 8}});
    const auto generator = Take(rig.world.CreateObject(rig.content.FindObject("generator"), {40, 8}));
    const auto id = rig.world.GetObject(generator)->id;
    REQUIRE(rig.world.CheckInvariants());
    SECTION("missing live record") {
        rig.world.Power().UnregisterObject(id);
        CHECK_FALSE(rig.world.CheckInvariants());
    }
    SECTION("wrong live role") {
        auto power = rig.world.Power().Capture();
        power.devices.front().kind = PowerDeviceKind::Battery;
        REQUIRE(rig.world.Power().Restore(power, rig.world.GetTick()));
        CHECK_FALSE(rig.world.CheckInvariants());
    }
    SECTION("archived record removed") {
        REQUIRE(rig.world.ReleaseRequest(sourceRequest)); REQUIRE(rig.world.ReconcileChunks());
        REQUIRE_FALSE(rig.world.GetObject(generator));
        rig.world.Power().UnregisterObject(id);
        CHECK_FALSE(rig.world.CheckInvariants());
        Take(rig.world.RequestChunks({{2, 0}, 0}, LoadMode::active));
        const auto result = rig.world.ReconcileChunks();
        REQUIRE_FALSE(result); CHECK(result.error() == SaveError::InvalidData);
        CHECK_FALSE(rig.world.GetChunk({2, 0}));
    }
}

TEST_CASE("Restored player source dependencies are ready in one reconcile and release when parked", "[power][world][save][residency]") {
    SaveDirectory files;
    auto content = PowerWorldContent();
    PersistentId playerId = 0;
    PersistentId batteryId = 0;
    {
        World initial(content);
        LoadColumn(initial, 0, LoadMode::active, LoadReason::Player); LoadColumn(initial, 2);
        ClearFloor(initial, {{8, 8}, {9, 8}}); ClearFloor(initial, {{40, 8}, {41, 8}});
        const auto player = Take(initial.CreateActor({8, 8}, true)); playerId = initial.GetActor(player)->id;
        for (int x : {9, 25, 41}) { Take(initial.Power().ExtendBounds({{x, 8}, {x + 15, 8}})); }
        Take(initial.Power().ExtendBounds({{40, 8}, {41, 8}}));
        const auto box = Take(initial.CreateObject(content.FindObject("battery_box"), {40, 8}));
        const auto item = Take(initial.CreateItem(content.FindItem("battery"), ItemLocation::Stored(box)));
        batteryId = initial.GetItem(item)->id;
        const auto store = content.GetItem(content.FindItem("battery")).energyStores.front().capability;
        REQUIRE(initial.SetItemEnergy(item, store, 500));
        SessionState session; session.player = playerId; session.playerPosition = {8, 8};
        REQUIRE(initial.CreateSave(files.database.c_str(), session));
    }
    World restored(content); Take(restored.LoadSave(files.database.c_str()));
    LoadColumn(restored, 0, LoadMode::active, LoadReason::Player);
    const auto player = restored.FindActor(playerId);
    REQUIRE(restored.GetActor(player));
    REQUIRE(restored.GetItem(restored.FindItem(batteryId)));
    CHECK(restored.IsActive({40, 8}));
    CHECK_FALSE(restored.GetChunk({1, 0})); // Electrical coverage is not blanket residency.
    CHECK_FALSE(restored.Power().GridAt({8, 8})); // Reach, not worker coverage, loads this source.
    const PowerDemand draw{{9, 8}, 1};
    CHECK(restored.Power().CanSupply({&draw, 1}) == PowerOutcome::Supplied);
    CHECK(ColumnDebug(restored, {2, 0}).playerActiveRequests == 1);
    REQUIRE(restored.SetActorParked(player, true)); REQUIRE(restored.ReconcileChunks());
    CHECK_FALSE(restored.GetChunk({2, 0}));
    REQUIRE(restored.SetActorParked(player, false)); REQUIRE(restored.ReconcileChunks());
    CHECK(restored.IsActive({40, 8}));
    CHECK(restored.Power().CanSupply({&draw, 1}) == PowerOutcome::Supplied);
    CHECK(restored.CheckInvariants());
}

TEST_CASE("A first manual bench start across wiring boundaries finds archived supply", "[power][world][residency][crafting]") {
    PowerWorldRig rig;
    const auto source = LoadColumn(rig.world, 2);
    ClearFloor(rig.world, {{40, 8}, {40, 8}});
    for (int x : {14, 30}) { Take(rig.world.Power().ExtendBounds({{x, 8}, {x + 15, 8}})); }
    const auto bench = Take(rig.world.CreateObject(rig.content.FindObject("bench"), {14, 8}));
    const auto box = Take(rig.world.CreateObject(rig.content.FindObject("battery_box"), {40, 8}));
    const auto battery = Take(rig.world.CreateItem(rig.content.FindItem("battery"), ItemLocation::Stored(box)));
    const auto batteryId = rig.world.GetItem(battery)->id;
    REQUIRE(rig.world.SetItemEnergy(battery, rig.Store(), 500));
    REQUIRE(rig.world.ReleaseRequest(source)); REQUIRE(rig.world.ReconcileChunks());
    CHECK_FALSE(rig.world.GetChunk({2, 0}));
    REQUIRE(rig.world.MoveActor(rig.actor, {13, 8}));
    REQUIRE(rig.world.ReconcileChunks());
    CHECK_FALSE(rig.world.Power().GridAt({13, 8}));
    REQUIRE(rig.world.IsActive({40, 8}));
    CHECK(ColumnDebug(rig.world, {2, 0}).machineOwners == 0);
    const auto started = Take(rig.world.StartCrafting(rig.actor, rig.content.FindCraftingRecipe("cut_product")));
    REQUIRE(started.outcome == CraftingOutcome::Started);
    CHECK(rig.world.StationWorker(bench) == rig.world.GetActor(rig.actor)->id);
    CHECK(rig.world.GetItemEnergy(rig.world.FindItem(batteryId), rig.Store())->energy == 400);
    REQUIRE(rig.world.SetActorParked(rig.actor, true)); REQUIRE(rig.world.ReconcileChunks());
    CHECK_FALSE(rig.world.GetChunk({2, 0})); // No autonomous hold from the bench or paid job.
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Continuing manual jobs retain their snapshotted source radius live and on activation", "[power][world][residency][crafting][save]") {
    SaveDirectory files;
    PowerWorldRig rig;
    const auto source = LoadColumn(rig.world, 2);
    ClearFloor(rig.world, {{40, 8}, {40, 8}});
    for (int x : {11, 27}) { Take(rig.world.Power().ExtendBounds({{x, 8}, {x + 15, 8}})); }
    Take(rig.world.CreateObject(rig.content.FindObject("bench"), {11, 8}));
    const auto box = Take(rig.world.CreateObject(rig.content.FindObject("battery_box"), {40, 8}));
    const auto battery = Take(rig.world.CreateItem(rig.content.FindItem("battery"), ItemLocation::Stored(box)));
    REQUIRE(rig.world.SetItemEnergy(battery, rig.Store(), 500));
    REQUIRE(rig.world.ReleaseRequest(source)); REQUIRE(rig.world.ReconcileChunks());
    REQUIRE_FALSE(rig.world.GetChunk({2, 0}));
    REQUIRE(rig.world.SetCraftingSettings({3, 100})); REQUIRE(rig.world.ReconcileChunks());
    const auto job = Take(rig.world.StartCrafting(rig.actor, rig.content.FindCraftingRecipe("cut_product")));
    REQUIRE(job.outcome == CraftingOutcome::Started);
    const auto playerId = rig.world.GetActor(rig.actor)->id;
    REQUIRE(rig.world.SetCraftingSettings({0, 100})); REQUIRE(rig.world.ReconcileChunks());
    CHECK(rig.world.IsActive({40, 8}));
    SECTION("live assignment release drops the wider area") {
        REQUIRE(Take(rig.world.CancelCrafting(rig.actor, job.unfinished)).outcome == CraftingOutcome::Cancelled);
        REQUIRE(rig.world.ReconcileChunks());
        CHECK_FALSE(rig.world.GetChunk({2, 0}));
    }
    SECTION("incoming assignment expands preflight before first activation") {
        SessionState session; session.player = playerId; session.playerPosition = {8, 8};
        REQUIRE(rig.world.CreateSave(files.database.c_str(), session));
        World restored(rig.content); Take(restored.LoadSave(files.database.c_str()));
        REQUIRE(restored.SetCraftingSettings({0, 100}));
        SECTION("valid source archive") {
            LoadColumn(restored, 0, LoadMode::active, LoadReason::Player);
            CHECK(restored.IsActive({40, 8}));
            CHECK(ColumnDebug(restored, {2, 0}).playerActiveRequests == 1);
        }
        SECTION("corrupt source archive is discovered before restoring the worker") {
            struct RawConnection { sqlite3* value = nullptr; ~RawConnection() { sqlite3_close(value); } } raw;
            REQUIRE(sqlite3_open(files.database.c_str(), &raw.value) == SQLITE_OK);
            REQUIRE(sqlite3_exec(raw.value,
                "UPDATE chunks SET payload=json_set(payload,'$.objects',json('[]')) WHERE x=2 AND z=0 AND y=0",
                nullptr, nullptr, nullptr) == SQLITE_OK);
            const auto revision = restored.GetRevision();
            Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active, LoadReason::Player));
            const auto result = restored.ReconcileChunks();
            REQUIRE_FALSE(result); CHECK(result.error() == SaveError::InvalidData);
            CHECK(restored.LoadedChunks().empty());
            CHECK_FALSE(restored.FindActor(playerId).value);
            CHECK(restored.GetRevision() == revision);
        }
        CHECK(restored.CheckInvariants());
    }
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Corrupt crafting-area source archives refuse activation before publishing the player", "[power][world][residency][save]") {
    SaveDirectory files;
    auto content = PowerWorldContent();
    PersistentId playerId = 0;
    {
        World initial(content);
        LoadColumn(initial, 0); LoadColumn(initial, 2);
        ClearFloor(initial, {{8, 8}, {8, 8}}); ClearFloor(initial, {{40, 8}, {40, 8}});
        const auto player = Take(initial.CreateActor({8, 8}, true)); playerId = initial.GetActor(player)->id;
        for (int x : {9, 25}) { Take(initial.Power().ExtendBounds({{x, 8}, {x + 15, 8}})); }
        Take(initial.CreateObject(content.FindObject("battery_box"), {40, 8}));
        SessionState session; session.player = playerId; session.playerPosition = {8, 8};
        REQUIRE(initial.CreateSave(files.database.c_str(), session));
    }
    World restored(content); Take(restored.LoadSave(files.database.c_str()));
    LoadColumn(restored, 4);
    struct RawConnection { sqlite3* value = nullptr; ~RawConnection() { sqlite3_close(value); } } raw;
    REQUIRE(sqlite3_open(files.database.c_str(), &raw.value) == SQLITE_OK);
    REQUIRE(sqlite3_exec(raw.value,
        "UPDATE chunks SET payload=json_set(payload,'$.objects',json('[]')) WHERE x=2 AND z=0 AND y=0",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    const auto power = restored.Power().Capture();
    const auto revision = restored.GetRevision();
    const auto count = restored.LoadedChunks().size();
    Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active, LoadReason::Player));
    const auto result = restored.ReconcileChunks();
    REQUIRE_FALSE(result); CHECK(result.error() == SaveError::InvalidData);
    CHECK_FALSE(restored.FindActor(playerId).value);
    CHECK_FALSE(restored.GetChunk({0, 0})); CHECK_FALSE(restored.GetChunk({2, 0}));
    CHECK(restored.GetChunk({4, 0}));
    CHECK(restored.LoadedChunks().size() == count);
    CHECK(restored.GetRevision() == revision); CHECK(restored.Power().Capture() == power);
    CHECK(restored.CheckInvariants());
}

TEST_CASE("Planned generated coverage preflights adjacent-area source dependencies", "[power][world][residency][worldgen][save]") {
    SaveDirectory files;
    auto content = PowerWorldContent(true);
    World initial(content);
    const auto plan = GenerateRegionPlan(content, initial.GetSeed(), {1, 1}, initial.GetGenerationVersion());
    TileCoord playerPosition{}, sourcePosition{};
    bool found = false;
    for (const auto& building : plan.buildings) {
        const auto coverage = DescribeBuildingPower(content.GetBuildingPattern(building.pattern),
            building.bounds, building.basementVariant);
        if (!coverage) { continue; }
        const TileCoord candidate{coverage->bounds.min.x - 1, coverage->bounds.min.z};
        const auto near = ChunkOf(candidate);
        const auto first = ChunkOf(coverage->bounds.min);
        const auto far = ChunkOf(coverage->bounds.max);
        if (near.x != first.x || near.z != first.z || (near.x == far.x && near.z == far.z)) { continue; }
        playerPosition = candidate;
        sourcePosition = {coverage->bounds.max.x, coverage->bounds.max.z};
        found = true; break;
    }
    REQUIRE(found);
    const auto home = ChunkOf(playerPosition);
    const auto remote = ChunkOf(sourcePosition);
    Take(initial.RequestChunks({home, 0}, LoadMode::active, LoadReason::Player));
    Take(initial.RequestChunks({remote, 0}, LoadMode::active));
    REQUIRE(initial.ReconcileChunks());
    ClearFloor(initial, {playerPosition, playerPosition}); ClearFloor(initial, {sourcePosition, sourcePosition});
    const auto player = Take(initial.CreateActor(playerPosition, true));
    const auto playerId = initial.GetActor(player)->id;
    Take(initial.CreateObject(content.FindObject("battery_box"), sourcePosition));
    // Simulate an indexed source predating this building's first registration.
    // Sources outside electrical coverage are valid; no station index is needed.
    auto power = initial.Power().Capture(); power.grids.clear(); power.generatedBindings.clear();
    REQUIRE(initial.Power().Restore(power, initial.GetTick()));
    SessionState session; session.player = playerId; session.playerPosition = playerPosition;
    REQUIRE(initial.CreateSave(files.database.c_str(), session));
    World restored(content); Take(restored.LoadSave(files.database.c_str()));
    struct RawConnection { sqlite3* value = nullptr; ~RawConnection() { sqlite3_close(value); } } raw;
    REQUIRE(sqlite3_open(files.database.c_str(), &raw.value) == SQLITE_OK);
    const auto mutation = "UPDATE chunks SET payload=json_set(payload,'$.objects',json('[]')) WHERE x="
        + std::to_string(remote.x) + " AND z=" + std::to_string(remote.z) + " AND y=0";
    REQUIRE(sqlite3_exec(raw.value, mutation.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    const auto before = restored.Power().Capture();
    const auto revision = restored.GetRevision();
    Take(restored.RequestChunks({home, 0}, LoadMode::active, LoadReason::Player));
    const auto result = restored.ReconcileChunks();
    REQUIRE_FALSE(result); CHECK(result.error() == SaveError::InvalidData);
    CHECK(restored.LoadedChunks().empty()); CHECK_FALSE(restored.FindActor(playerId).value);
    CHECK(restored.Power().Capture() == before); CHECK(restored.GetRevision() == revision);
}

TEST_CASE("An arbitrary powered item inside furniture is not an installed grid battery", "[power][world][accounting]") {
    PowerWorldRig rig;
    Take(rig.world.Power().ExtendBounds({{8, 8}, {10, 8}}));
    const auto box = Take(rig.world.CreateObject(rig.content.FindObject("battery_box"), {9, 8}));
    const auto gadget = Take(rig.world.CreateItem(rig.content.FindItem("powered_gadget"), ItemLocation::Stored(box)));
    const PowerDemand draw{{8, 8}, 1};
    CHECK(rig.world.Power().CanSupply({&draw, 1}) == PowerOutcome::InsufficientPower);
    REQUIRE(rig.world.TransferItem(gadget, ItemLocation::Carried(rig.actor)));
    const auto battery = Take(rig.world.CreateItem(rig.content.FindItem("battery"), ItemLocation::Stored(box)));
    REQUIRE(rig.world.SetItemEnergy(battery, rig.Store(), 100));
    CHECK(rig.world.Power().CanSupply({&draw, 1}) == PowerOutcome::Supplied);
    const auto extra = Take(rig.world.CreateItem(rig.content.FindItem("material"), ItemLocation::Stored(box)));
    CHECK(rig.world.Power().CanSupply({&draw, 1}) == PowerOutcome::InsufficientPower);
    REQUIRE(rig.world.DestroyItem(extra));
    REQUIRE(rig.world.Power().TrySupply({&draw, 1}) == PowerOutcome::Supplied);
    CHECK(rig.world.GetItemEnergy(battery, rig.Store())->energy == 99);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Solar direct exposure follows resident vertical geometry rather than light", "[power][world][solar][geometry]") {
    PowerWorldRig rig;
    const TileCoord position{9, 8};
    ClearSky(rig.world, position);
    CHECK(rig.world.HasDirectSkyExposure(position));
    CHECK_FALSE(rig.world.HasDirectSkyExposure({1000, 1000}));
    CHECK_FALSE(rig.world.HasDirectSkyExposure({9, 8, world_max_y + 1}));
    const auto source = Take(rig.world.CreateObject(rig.content.FindObject("solar"), position));
    REQUIRE(rig.world.UseObject(rig.actor, source, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    rig.world.SetSkyLight(0);
    REQUIRE(rig.world.SetObjectLight(source, {255, 8}));
    CHECK(rig.world.DescribeObjectState(source)->solarExposed);
    CHECK(rig.world.DescribeObjectState(source)->solarDaylight == SolarDaylightRatio(0));
    CHECK(rig.world.DescribeObjectState(source)->running);
    const TileCoord roof{9, 8, 1};
    REQUIRE(rig.world.SetTile(roof, {rig.content.FindTerrain("floor")}));
    CHECK_FALSE(rig.world.HasDirectSkyExposure(position));
    CHECK_FALSE(rig.world.DescribeObjectState(source)->running);
    rig.world.SetSkyLight(255);
    CHECK_FALSE(rig.world.DescribeObjectState(source)->solarExposed);
    REQUIRE(rig.world.SetTile(roof, {rig.content.FindTerrain("air")}));
    CHECK(rig.world.HasDirectSkyExposure(position));
    const auto shade = Take(rig.world.CreateObject(rig.content.FindObject("shade"), roof));
    CHECK_FALSE(rig.world.HasDirectSkyExposure(position));
    REQUIRE(rig.world.MoveObject(shade, {10, 8}));
    CHECK(rig.world.HasDirectSkyExposure(position));
    REQUIRE(rig.world.MoveObject(shade, roof) == eastl::unexpected(WorldError::blocked));
    REQUIRE(rig.world.DestroyObject(shade));
    REQUIRE(rig.world.SetTile({9, 8, world_max_y}, {rig.content.FindTerrain("wall")}));
    CHECK_FALSE(rig.world.HasDirectSkyExposure(position));
}

TEST_CASE("Fixed solar and wood-gas kits place stopped and refuse player relocation", "[power][world][solar][placement]") {
    PowerWorldRig rig;
    const auto kit = rig.Carry("solar_kit");
    CHECK(rig.world.PlacePowerObject(rig.actor, kit, {}, {15, 14}).outcome == PowerActionOutcome::OutOfReach);
    CHECK(rig.world.GetItem(kit));
    const auto blocker = Take(rig.world.CreateObject({9, 8}));
    CHECK(rig.world.PlacePowerObject(rig.actor, kit, {}, {9, 8}).outcome == PowerActionOutcome::Occupied);
    REQUIRE(rig.world.DestroyObject(blocker));
    REQUIRE(rig.world.PlacePowerObject(rig.actor, kit, {}, {9, 8}).outcome == PowerActionOutcome::Succeeded);
    CHECK_FALSE(rig.world.GetItem(kit));
    const auto source = rig.world.ObjectAt({9, 8});
    REQUIRE(source.value);
    CHECK_FALSE(rig.world.DescribeObjectState(source)->runRequested);
    CHECK(rig.world.DescribeObjectState(source)->generatorOutputRemaining == 0);
    CHECK(rig.world.GetObject(source)->contents.empty());
    CHECK_FALSE(rig.world.GetObject(source)->equipment.value);
    CHECK(rig.world.QueryObjectMove(rig.actor, source, ObjectMoveKind::Push).outcome == ObjectMoveOutcome::Immovable);
    CHECK(rig.world.QueryObjectStep(rig.actor, source, {0, 1}).outcome == ObjectMoveOutcome::Immovable);
    CHECK(rig.world.QueryPackStation(rig.actor, source) != StationActionOutcome::Packed);
    const auto woodKit = rig.Carry("woodgas_kit");
    REQUIRE(rig.world.PlacePowerObject(rig.actor, woodKit, {}, {8, 9}).outcome == PowerActionOutcome::Succeeded);
    CHECK_FALSE(rig.world.GetItem(woodKit));
    const auto woodgas = rig.world.ObjectAt({8, 9});
    REQUIRE(woodgas.value);
    CHECK_FALSE(rig.world.DescribeObjectState(woodgas)->runRequested);
    CHECK(rig.world.DescribeObjectState(woodgas)->energy == 0);
    CHECK(rig.world.QueryObjectMove(rig.actor, woodgas, ObjectMoveKind::Pull).outcome == ObjectMoveOutcome::Immovable);
    CHECK(rig.world.QueryPackStation(rig.actor, woodgas) != StationActionOutcome::Packed);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Solar exposure changes wake power without replacing a claimed allowance", "[power][world][solar][geometry]") {
    PowerWorldRig rig;
    const TileCoord position{9, 8};
    ClearSky(rig.world, position);
    Take(rig.world.Power().ExtendBounds({position, position}));
    const auto source = Take(rig.world.CreateObject(rig.content.FindObject("solar"), position));
    REQUIRE(rig.world.UseObject(rig.actor, source, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
    const PowerDemand draw{position, 1};
    REQUIRE(rig.world.Power().TrySupply({&draw, 1}) == PowerOutcome::Supplied);
    const auto remaining = rig.world.DescribeObjectState(source)->generatorOutputRemaining;
    const TileCoord roof{9, 8, 1};
    REQUIRE(rig.world.SetTile(roof, {rig.content.FindTerrain("floor")}));
    CHECK(rig.world.Power().NextEventIn() == Tick{0});
    CHECK(rig.world.Power().TrySupply({&draw, 1}) == PowerOutcome::InsufficientPower);
    CHECK(rig.world.DescribeObjectState(source)->generatorOutputRemaining == remaining);
    REQUIRE(rig.world.SetTile(roof, {rig.content.FindTerrain("air")}));
    REQUIRE(rig.world.Power().TrySupply({&draw, 1}) == PowerOutcome::Supplied);
    CHECK(rig.world.DescribeObjectState(source)->generatorOutputRemaining == remaining - 1);
    rig.world.Power().ProcessEvents();
    const auto shade = Take(rig.world.CreateObject(rig.content.FindObject("shade"), roof));
    CHECK(rig.world.Power().NextEventIn() == Tick{0});
    CHECK(rig.world.Power().CanSupply({&draw, 1}) == PowerOutcome::InsufficientPower);
    rig.world.Power().ProcessEvents();
    REQUIRE(rig.world.DestroyObject(shade));
    CHECK(rig.world.Power().NextEventIn() == Tick{0});
    CHECK(rig.world.Power().CanSupply({&draw, 1}) == PowerOutcome::Supplied);
}

TEST_CASE("Started solar retains offscreen columns overnight and restores its paid ledger", "[power][world][solar][save][residency]") {
    SaveDirectory files;
    auto content = PowerWorldContent();
    PersistentId sourceId = 0;
    DataObject state;
    SavedPowerState power;
    {
        World world(content);
        LoadColumn(world, 0, LoadMode::active, LoadReason::Player);
        const auto remote = LoadColumn(world, 2);
        ClearFloor(world, {{8, 8}, {9, 8}});
        ClearFloor(world, {{40, 8}, {41, 8}});
        ClearSky(world, {40, 8});
        const auto player = Take(world.CreateActor({41, 8}, true));
        Take(world.Power().ExtendBounds({{40, 8}, {41, 8}}));
        const auto source = Take(world.CreateObject(content.FindObject("solar"), {40, 8}));
        sourceId = world.GetObject(source)->id;
        REQUIRE(world.UseObject(player, source, ObjectAction::Start).outcome == ObjectUseOutcome::Succeeded);
        const PowerDemand draw{{40, 8}, 7};
        REQUIRE(world.Power().TrySupply({&draw, 1}) == PowerOutcome::Supplied);
        REQUIRE(world.MoveActor(player, {8, 8}));
        REQUIRE(world.ReleaseRequest(remote));
        REQUIRE(world.ReconcileChunks());
        CHECK(ColumnDebug(world, {2, 0}).machineOwners == 1);
        SessionState session;
        session.player = world.GetActor(player)->id;
        session.playerPosition = {8, 8};
        REQUIRE(world.CreateSave(files.database.c_str(), session));
        state = world.CaptureObjectState(source);
        power = world.Power().Capture();
    }
    World restored(content);
    const auto session = Take(restored.LoadSave(files.database.c_str()));
    LoadColumn(restored, 0, LoadMode::active, LoadReason::Player);
    const auto source = restored.FindObject(sourceId);
    REQUIRE(restored.GetObject(source));
    CHECK(restored.CaptureObjectState(source) == state);
    CHECK(restored.Power().Capture() == power);
    restored.ElapseTicks(Tick{ticks_per_turn} * 60 * 60 * 12);
    CHECK(restored.DescribeObjectState(source)->solarDaylight == 0);
    CHECK(restored.DescribeObjectState(source)->runRequested);
    CHECK_FALSE(restored.DescribeObjectState(source)->running);
    REQUIRE(restored.ReconcileChunks());
    CHECK(ColumnDebug(restored, {2, 0}).machineOwners == 1);
    REQUIRE(restored.Save(session));
    CHECK(restored.CheckInvariants());
}
}
