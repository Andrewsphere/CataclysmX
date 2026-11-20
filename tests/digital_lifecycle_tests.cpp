#include "core/world.h"
#include "combat/combat_system.h"
#include "core/ratio.h"
#include "digital/information_system.h"
#include "digital/machine_system.h"
#include "objects/machine_job.h"
#include "power/power_system.h"
#include "skills/skill_state.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> result) {
    REQUIRE(result);
    return eastl::move(*result);
}
ContentCatalog MachineContent() {
    const ContentSource source{"lifecycle", R"({format_version:1,objects:[
      {id:'printer',name:'Printer',behavior:'selectable_machine',packing_item:'printer_item',
       capacity_volume_ml:100,selectable_recipes:['print']}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
       appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
       appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,
       blocks_sight:false,looks_like:'floor'},
      {type:'item',id:'filament',name:'Filament',mass_g:10,volume_ml:1},
      {type:'item',id:'product',name:'Product',mass_g:15,volume_ml:20},
      {type:'item',id:'printer_item',name:'Printer',mass_g:100,volume_ml:100,
       placed_object:'printer',digital_device:'controller',supplied_information:['designs']},
      {type:'information',id:'designs',name:'Designs',designs:['print']},
      {type:'skill',id:'computers',name:'Computers'},
      {type:'machine_recipe',id:'print',inputs:[{item:'filament',count:2}],
       result:{item:'product',count:1},duration:300,energy_per_tick:1,
       setup_work:200,setup_work_per_unit:20,
       learning:{skill:'computers',level:0,xp_per_execution:1}}
    ]})"};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    return Take(eastl::move(result));
}
struct MachineRig {
    ContentCatalog content = MachineContent();
    World world{content};
    LoadRequest left = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    LoadRequest right = Take(world.RequestChunks({{1, 0}, 0}, LoadMode::active));
    ActorHandle actor{};
    ObjectHandle machine{};
    MachineRig() {
        REQUIRE(world.ReconcileChunks());
        for (int x = 14; x <= 17; ++x) {
            for (int z = 7; z <= 9; ++z) {
                TileCoord p{x, z};
                if (auto object = world.ObjectAt(p); object.value) { REQUIRE(world.DestroyObject(object)); }
                if (auto creature = world.ActorAt(p); creature.value) { REQUIRE(world.DestroyActor(creature)); }
                REQUIRE(world.SetTile(p, {content.FindTerrain("floor")}));
            }
        }
        actor = Take(world.CreateActor({15, 8}, true));
        machine = Take(world.CreateObject(content.FindObject("printer"), {16, 8}));
        for (int i = 0; i < 2; ++i) {
            Take(world.CreateItem(content.FindItem("filament"), ItemLocation::Carried(actor)));
        }
        REQUIRE(Take(world.Machines().Start(actor, machine, content.FindMachineRecipe("print"), 1)).outcome
            == MachineOutcome::Started);
        world.ElapseTicks(30);
    }
    MachineView View(ActorHandle observer) {
        Vector<MachineView> views;
        world.Machines().Collect(observer, views);
        REQUIRE(views.size() == 1);
        return views.front();
    }
};
struct SaveFile {
    std::filesystem::path directory = std::filesystem::temp_directory_path()
        / ("cataclysmx-lifecycle-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string path;
    SaveFile() { REQUIRE(std::filesystem::create_directory(directory)); path = (directory / "world.sqlite").string(); }
    ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
};

TEST_CASE("Losing a setup worker immediately releases the paid slice", "[digital][lifecycle]") {
    MachineRig r;
    const auto before = r.View(r.actor);
    const auto initialState = r.world.CaptureObjectState(r.machine);
    const auto initialJob = ReadMachineJobState(*initialState.Find("job"));
    REQUIRE(initialJob);
    REQUIRE(before.remainingTicks == 70);
    bool parked = false;
    SECTION("parked") { REQUIRE(r.world.SetActorParked(r.actor, true)); parked = true; }
    SECTION("dead") { r.world.Combat().Damage(r.actor, {}, BodyPart::Torso, {DamageType::Blunt, 10000}); }
    SECTION("destroyed") { REQUIRE(r.world.DestroyActor(r.actor)); }
    CHECK_FALSE(r.world.Machines().Busy(r.actor));
    CHECK(r.world.CheckInvariants());
    if (parked) {
        CHECK(r.world.GetActorRemainingTicks(r.actor) == 70);
    }
    const auto helper = Take(r.world.CreateActor({15, 9}, true));
    const auto paused = r.View(helper);
    CHECK(paused.worker == 0);
    CHECK_FALSE(paused.running);
    CHECK(paused.remainingTicks == 70);
    CHECK(paused.setupCompleted == before.setupCompleted);
    const auto pausedState = r.world.CaptureObjectState(r.machine);
    const auto pausedJob = ReadMachineJobState(*pausedState.Find("job"));
    REQUIRE(pausedJob);
    CHECK(pausedJob->pendingWork == initialJob->pendingWork);
    const auto turns = r.world.GetActor(helper)->turns;
    REQUIRE(Take(r.world.Machines().Use(helper, r.machine, MachineAction::Resume)).outcome == MachineOutcome::Resumed);
    CHECK(r.world.GetActorRemainingTicks(helper) == 70);
    CHECK(r.world.GetActor(helper)->turns == turns);
    CHECK(r.world.CheckInvariants());
}

TEST_CASE("Unloading either setup participant preserves the paid slice", "[digital][lifecycle]") {
    MachineRig r;
    const auto machineId = r.world.GetObject(r.machine)->id;
    const auto actorId = r.world.GetActor(r.actor)->id;
    SECTION("worker chunk") { REQUIRE(r.world.ReleaseRequest(r.left)); }
    SECTION("machine chunk") { REQUIRE(r.world.ReleaseRequest(r.right)); }
    REQUIRE(r.world.ReconcileChunks());
    CHECK(r.world.CheckInvariants());
    if (r.world.GetActor(r.actor)) {
        CHECK_FALSE(r.world.Machines().Busy(r.actor));
        CHECK(r.world.GetActorRemainingTicks(r.actor) == 70);
    }
    Take(r.world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    Take(r.world.RequestChunks({{1, 0}, 0}, LoadMode::active));
    REQUIRE(r.world.ReconcileChunks());
    r.actor = r.world.FindActor(actorId);
    r.machine = r.world.FindObject(machineId);
    REQUIRE(r.actor != ActorHandle{});
    REQUIRE(r.machine != ObjectHandle{});
    const auto view = r.View(r.actor);
    CHECK(view.hasJob);
    CHECK_FALSE(view.running);
    CHECK(view.worker == 0);
    CHECK(view.remainingTicks == 70);
    CHECK(view.setupCompleted == 0);
    CHECK(r.world.CheckInvariants());
}

TEST_CASE("Parking a copying player aborts immediately without publishing", "[digital][lifecycle]") {
    Diagnostics diagnostics;
    auto content = Take(LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics));
    World world{content};
    Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile({2, 2}, {content.FindTerrain("floor"), 0}));
    const auto actor = Take(world.CreateActor({2, 2}, true));
    const auto phone = Take(world.CreateItem(content.FindItem("phone"), ItemLocation::Carried(actor)));
    const auto stick = Take(world.CreateItem(content.FindItem("usb_data_stick"), ItemLocation::Carried(actor)));
    const InformationEndpoint source{world.GetItem(phone)->id, 0}, destination{world.GetItem(stick)->id, 0};
    const auto package = content.FindInformation("digital_workshop_primer");
    const auto before = world.CaptureItemState(stick);
    const auto learning = CaptureLearningState(world.GetActor(actor)->learning, content);
    REQUIRE(Take(world.Information().Copy(actor, source, destination, package)).outcome == InformationOutcome::Started);
    world.ElapseTicks(20);
    REQUIRE(world.SetActorParked(actor, true));
    CHECK_FALSE(world.Information().Busy(actor));
    CHECK(world.GetActorRemainingTicks(actor) == 80);
    CHECK_FALSE(world.Information().HasPackage(destination, package));
    CHECK(world.CaptureItemState(stick) == before);
    CHECK(CaptureLearningState(world.GetActor(actor)->learning, content) == learning);
    CHECK(world.CheckInvariants());
    world.ElapseTicks(100);
    world.Information().ProcessEvents();
    CHECK_FALSE(world.Information().HasPackage(destination, package));
}

TEST_CASE("Unloading a destination controller aborts its remote worker", "[digital][lifecycle]") {
    Diagnostics diagnostics;
    auto content = Take(LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics));
    World world{content};
    Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    const auto right = Take(world.RequestChunks({{1, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    for (int x = 15; x <= 16; ++x) {
        TileCoord p{x, 8};
        if (auto object = world.ObjectAt(p); object.value) { REQUIRE(world.DestroyObject(object)); }
        if (auto creature = world.ActorAt(p); creature.value) { REQUIRE(world.DestroyActor(creature)); }
        REQUIRE(world.SetTile(p, {content.FindTerrain("floor"), 0}));
    }
    const auto actor = Take(world.CreateActor({15, 8}, true));
    const auto phone = Take(world.CreateItem(content.FindItem("phone"), ItemLocation::Carried(actor)));
    if (!world.Power().GridAt({16, 8})) {
        Take(world.Power().RegisterBuilding({16, 8}, PowerBounds{{16, 8}, {16, 8}}));
    }
    const auto machine = Take(world.CreateObject(content.FindObject("home_polymer_printer_station"), {16, 8}));
    const InformationEndpoint source{world.GetItem(phone)->id, 0}, destination{0, world.GetObject(machine)->id};
    const auto package = content.FindInformation("digital_workshop_primer");
    REQUIRE(Take(world.Information().Copy(actor, source, destination, package)).outcome == InformationOutcome::Started);
    world.ElapseTicks(20);
    REQUIRE(world.ReleaseRequest(right));
    REQUIRE(world.ReconcileChunks());
    CHECK_FALSE(world.Information().Busy(actor));
    CHECK(world.GetActorRemainingTicks(actor) == 80);
    CHECK(world.CheckInvariants());
    Take(world.RequestChunks({{1, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    CHECK_FALSE(world.Information().HasPackage(destination, package));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Saving after a released setup worker starts new work has no stale claim", "[digital][lifecycle][save]") {
    MachineRig r;
    REQUIRE(r.world.SetActorParked(r.actor, true));
    REQUIRE(r.world.SetActorParked(r.actor, false));
    r.world.ElapseTicks(70);
    const auto second = Take(r.world.CreateObject(r.content.FindObject("printer"), {14, 8}));
    for (int i = 0; i < 2; ++i) {
        Take(r.world.CreateItem(r.content.FindItem("filament"), ItemLocation::Carried(r.actor)));
    }
    REQUIRE(Take(r.world.Machines().Start(r.actor, second, r.content.FindMachineRecipe("print"), 1)).outcome
        == MachineOutcome::Started);
    SaveFile file;
    Diagnostics diagnostics;
    const auto saved = r.world.CreateSave(file.path.c_str(), {}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(saved);
    CHECK(r.world.CheckInvariants());
}
}
