#include "core/world.h"
#include "core/ratio.h"
#include "digital/machine_system.h"
#include "skills/learning_system.h"
#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

namespace {
using namespace cataclysmx;
template<class T, class Error>
T Take(eastl::expected<T, Error> result) {
    REQUIRE(result.has_value());
    return eastl::move(*result);
}
ContentCatalog MachineContent() {
    const ContentSource source{"machine-world", R"({format_version:1,objects:[
      {id:'printer',name:'Printer',behavior:'selectable_machine',packing_item:'printer_item',
        capacity_volume_ml:100,selectable_recipes:['print','skilled_print']}
    ],definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,
        appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,
        appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,
        blocks_sight:false,looks_like:'floor'},
      {type:'skill',id:'computers',name:'Computers'},
      {type:'item',id:'filament',name:'Filament',mass_g:10,volume_ml:1},
      {type:'item',id:'product',name:'Product',mass_g:15,volume_ml:20},
      {type:'item',id:'box',name:'Box',mass_g:1,volume_ml:10,
        capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
      {type:'item',id:'printer_item',name:'Printer',mass_g:100,volume_ml:100,
        placed_object:'printer',digital_device:'controller',supplied_information:['designs']},
      {type:'information',id:'designs',name:'Designs',designs:['print','skilled_print']},
      {type:'machine_recipe',id:'print',inputs:[{item:'filament',count:2}],
        result:{item:'product',count:1},duration:300,energy_per_tick:1,
        setup_work:200,setup_work_per_unit:20,
        learning:{skill:'computers',level:0,xp_per_execution:10}},
      {type:'machine_recipe',id:'skilled_print',inputs:[{item:'filament',count:2}],
        result:{item:'product',count:1},duration:300,energy_per_tick:1,
        setup_work:200,setup_work_per_unit:20,skills:[{skill:'computers',level:1}],
        learning:{skill:'computers',level:1,xp_per_execution:20}}
    ]})"};
    Diagnostics diagnostics;
    auto result = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    return Take(eastl::move(result));
}
struct MachineRig {
    ContentCatalog content = MachineContent();
    World world{content};
    ActorHandle actor{};
    ObjectHandle machine{};
    MachineRecipeId recipe = content.FindMachineRecipe("print");
    MachineRig() {
        Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        for (int z = 5; z <= 12; ++z) { for (int x = 5; x <= 12; ++x) {
            const TileCoord p{x, z};
            if (const auto object = world.ObjectAt(p); object.value) { REQUIRE(world.DestroyObject(object)); }
            if (const auto creature = world.ActorAt(p); creature.value) { REQUIRE(world.DestroyActor(creature)); }
            REQUIRE(world.SetTile(p, {content.FindTerrain("floor")}));
        } }
        actor = Take(world.CreateActor({8, 8}, true));
        machine = Take(world.CreateObject(content.FindObject("printer"), {9, 8}));
    }
    ItemHandle Input(ItemLocation location) {
        return Take(world.CreateItem(content.FindItem("filament"), location));
    }
    void Inputs(unsigned count) {
        for (unsigned i = 0; i < count; ++i) { Input(ItemLocation::Carried(actor)); }
    }
    MachineView View() {
        Vector<MachineView> views;
        world.Machines().Collect(actor, views);
        REQUIRE(views.size() == 1);
        return eastl::move(views.front());
    }
    void Advance(Tick ticks) {
        world.ElapseTicks(ticks);
        world.Machines().ProcessEvents();
    }
    MachineResult Start(std::uint16_t quantity = 1) {
        return Take(world.Machines().Start(actor, machine, recipe, quantity));
    }
    MachineResult Use(MachineAction action) {
        return Take(world.Machines().Use(actor, machine, action));
    }
};
}

TEST_CASE("machine start refusals are atomic and expanded counts bounded", "[machine][world]") {
    MachineRig rig;
    const auto input = rig.Input(ItemLocation::Carried(rig.actor));
    CHECK(rig.Start().outcome == MachineOutcome::MissingInputs);
    CHECK_FALSE(rig.View().hasJob);
    CHECK(rig.world.GetItem(input)->location.kind == LocationKind::actor);
    CHECK(rig.Start(0).outcome == MachineOutcome::InvalidQuantity);
    CHECK(rig.Start(128).outcome == MachineOutcome::InvalidQuantity);
    CHECK_FALSE(rig.world.Machines().Busy(rig.actor));
    CHECK(rig.world.GetActorRemainingTicks(rig.actor) == 0);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("machine selection reserves persistent originals and output room", "[machine][world]") {
    MachineRig rig;
    const auto first = rig.Input(ItemLocation::Ground({8, 9}));
    const auto second = rig.Input(ItemLocation::Stored(rig.machine));
    const auto spare = rig.Input(ItemLocation::Carried(rig.actor));
    CHECK(rig.Start().outcome == MachineOutcome::Started);
    CHECK(rig.world.IsReservedItem(first));
    CHECK(rig.world.IsReservedItem(second));
    CHECK_FALSE(rig.world.IsReservedItem(spare));
    CHECK(rig.world.GetItem(first)->location.object == rig.machine);
    CHECK(rig.View().reservedVolumeMl == 20);
    CHECK(rig.world.GetObjectUsedVolume(rig.machine) == 20);
    CHECK_FALSE(rig.world.TransferItem(first, ItemLocation::Carried(rig.actor)));
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("machine preview finds batch capacity without changing world", "[machine][world]") {
    MachineRig rig;
    rig.Inputs(20);
    const auto before = rig.world.GetRevision();
    const auto view = rig.View();
    REQUIRE(view.recipes.size() == 2);
    bool found = false;
    for (const auto& recipe : view.recipes) {
        if (recipe.recipe != rig.recipe) { continue; }
        found = true;
        CHECK(recipe.availability == MachineOutcome::Started);
        CHECK(recipe.maximumQuantity == 5);
    }
    CHECK(found);
    CHECK(rig.world.GetRevision() == before);
    CHECK(rig.Start(6).outcome == MachineOutcome::NoCapacity);
    CHECK_FALSE(rig.View().hasJob);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("setup stop honors paid slice and cancel releases originals", "[machine][world]") {
    MachineRig rig;
    rig.Inputs(2);
    CHECK(rig.Start().baseCost == 0);
    CHECK(rig.world.GetActorRemainingTicks(rig.actor) == 100);
    CHECK(rig.Use(MachineAction::Stop).outcome == MachineOutcome::PauseRequested);
    rig.Advance(100);
    const auto view = rig.View();
    CHECK(view.setup);
    CHECK_FALSE(view.running);
    CHECK(view.setupCompleted == 100 * ratioScale);
    CHECK_FALSE(rig.world.Machines().Busy(rig.actor));
    CHECK(rig.Use(MachineAction::Cancel).outcome == MachineOutcome::Cancelled);
    CHECK_FALSE(rig.View().hasJob);
    CHECK(rig.world.GetObjectUsedVolume(rig.machine) == 2);
    for (auto item : rig.world.GetObject(rig.machine)->contents) {
        if (item == rig.world.GetObject(rig.machine)->equipment) { continue; }
        CHECK_FALSE(rig.world.IsReservedItem(item));
    }
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("setup worker loss preserves paid remainder without charging again", "[machine][world]") {
    MachineRig rig;
    rig.Inputs(2);
    REQUIRE(rig.Start().outcome == MachineOutcome::Started);
    rig.Advance(40);
    const auto turns = rig.world.GetActor(rig.actor)->turns;
    REQUIRE(rig.world.MoveActor(rig.actor, {6, 8}));
    rig.world.Machines().ProcessEvents();
    REQUIRE(rig.world.MoveActor(rig.actor, {8, 8}));
    CHECK_FALSE(rig.View().running);
    CHECK(rig.View().remainingTicks == 60);
    CHECK(rig.Use(MachineAction::Resume).outcome == MachineOutcome::Resumed);
    CHECK(rig.world.GetActor(rig.actor)->turns == turns);
    CHECK(rig.world.GetActorRemainingTicks(rig.actor) == 60);
    rig.Advance(60);
    CHECK(rig.View().setupCompleted == 100 * ratioScale);
    CHECK(rig.world.GetActor(rig.actor)->turns == turns + 1);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("another worker resumes setup without replacing initiator", "[machine][world]") {
    MachineRig rig;
    rig.Inputs(2);
    const auto initiator = rig.world.GetActor(rig.actor)->id;
    REQUIRE(rig.Start().outcome == MachineOutcome::Started);
    REQUIRE(rig.Use(MachineAction::Stop).outcome == MachineOutcome::PauseRequested);
    rig.Advance(100);
    const auto helper = Take(rig.world.CreateActor({8, 9}, true));
    CHECK(Take(rig.world.Machines().Use(helper, rig.machine, MachineAction::Resume)).outcome == MachineOutcome::Resumed);
    CHECK(rig.View().initiator == initiator);
    CHECK(rig.View().worker == rig.world.GetActor(helper)->id);
    rig.Advance(100);
    rig.Advance(20);
    const auto processing = rig.View();
    CHECK_FALSE(processing.setup);
    CHECK(processing.running);
    CHECK(processing.worker == 0);
    CHECK(processing.remainingTicks == 300);
    CHECK_FALSE(rig.world.Machines().Busy(helper));
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("processing stop and resume do not repeat setup or reserve again", "[machine][world]") {
    MachineRig rig;
    rig.Inputs(2);
    REQUIRE(rig.Start().outcome == MachineOutcome::Started);
    rig.Advance(100); rig.Advance(100); rig.Advance(20);
    const auto turns = rig.world.GetActor(rig.actor)->turns;
    CHECK(rig.Use(MachineAction::Resume).outcome == MachineOutcome::Busy);
    CHECK(rig.Use(MachineAction::Stop).baseCost == 100);
    CHECK_FALSE(rig.View().running);
    rig.Advance(100);
    CHECK(rig.View().remainingTicks == 300);
    CHECK(rig.Use(MachineAction::Resume).outcome == MachineOutcome::Resumed);
    CHECK(rig.View().remainingTicks == 300);
    CHECK(rig.View().setupCompleted == 220 * ratioScale);
    CHECK(rig.world.GetActor(rig.actor)->turns == turns);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("machine collection takes public contents but not active reserved inputs", "[machine][world]") {
    MachineRig rig;
    rig.Inputs(2);
    const auto publicItem = Take(rig.world.CreateItem(rig.content.FindItem("product"), ItemLocation::Stored(rig.machine)));
    REQUIRE(rig.Start().outcome == MachineOutcome::Started);
    const auto helper = Take(rig.world.CreateActor({8, 9}, true));
    const auto collected = Take(rig.world.Machines().Use(helper, rig.machine, MachineAction::Collect));
    CHECK(collected.outcome == MachineOutcome::Collected);
    CHECK(collected.baseCost == 100);
    CHECK(rig.world.GetItem(publicItem)->location.actor == helper);
    CHECK(rig.View().hasJob);
    CHECK(rig.world.GetObjectUsedVolume(rig.machine) == 20);
    CHECK(Take(rig.world.Machines().Use(helper, rig.machine, MachineAction::Collect)).outcome == MachineOutcome::NoContents);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("machine inputs include nested carried leaves", "[machine][world]") {
    MachineRig rig;
    const auto box = Take(rig.world.CreateItem(rig.content.FindItem("box"), ItemLocation::Carried(rig.actor)));
    const auto first = rig.Input(ItemLocation::Inside(box));
    const auto second = rig.Input(ItemLocation::Inside(box));
    REQUIRE(rig.Start().outcome == MachineOutcome::Started);
    CHECK(rig.world.GetItem(box)->contents.empty());
    CHECK(rig.world.IsReservedItem(first));
    CHECK(rig.world.IsReservedItem(second));
    CHECK_FALSE(rig.world.IsReservedItem(box));
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("setup parking keeps paid time and another qualified worker preserves credit", "[machine][world]") {
    MachineRig rig;
    const auto skill = rig.content.FindSkill("computers");
    REQUIRE(rig.world.Learning().SetLevel(rig.actor, skill, 1));
    rig.Inputs(2);
    rig.recipe = rig.content.FindMachineRecipe("skilled_print");
    REQUIRE(rig.Start().outcome == MachineOutcome::Started);
    rig.Advance(30);
    const auto initiator = rig.world.GetActor(rig.actor)->id;
    REQUIRE(rig.world.SetActorParked(rig.actor, true));
    rig.world.Machines().ProcessEvents();
    const auto helper = Take(rig.world.CreateActor({8, 9}, true));
    CHECK(Take(rig.world.Machines().Use(helper, rig.machine, MachineAction::Resume)).outcome == MachineOutcome::MissingSkills);
    REQUIRE(rig.world.Learning().SetLevel(helper, skill, 1));
    const auto turns = rig.world.GetActor(helper)->turns;
    CHECK(Take(rig.world.Machines().Use(helper, rig.machine, MachineAction::Resume)).outcome == MachineOutcome::Resumed);
    CHECK(rig.world.GetActor(helper)->turns == turns);
    CHECK(rig.world.GetActorRemainingTicks(helper) == 70);
    Vector<MachineView> views;
    rig.world.Machines().Collect(helper, views);
    REQUIRE(views.size() == 1);
    CHECK(views[0].initiator == initiator);
    CHECK(views[0].worker == rig.world.GetActor(helper)->id);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("setup initiator death retains the paid slice for a helper", "[machine][world]") {
    MachineRig rig;
    rig.Inputs(2);
    REQUIRE(rig.Start().outcome == MachineOutcome::Started);
    rig.Advance(25);
    const auto initiator = rig.world.GetActor(rig.actor)->id;
    REQUIRE(rig.world.DestroyActor(rig.actor));
    rig.world.Machines().ProcessEvents();
    const auto helper = Take(rig.world.CreateActor({8, 9}, true));
    REQUIRE(Take(rig.world.Machines().Use(helper, rig.machine, MachineAction::Resume)).outcome == MachineOutcome::Resumed);
    Vector<MachineView> views;
    rig.world.Machines().Collect(helper, views);
    REQUIRE(views.size() == 1);
    CHECK(views[0].initiator == initiator);
    CHECK(views[0].remainingTicks == 75);
    CHECK(views[0].setupCompleted == 0);
    CHECK(rig.world.GetActorRemainingTicks(helper) == 75);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("machine actions safely reject untyped objects", "[machine][world]") {
    MachineRig rig;
    const auto generic = Take(rig.world.CreateObject(TileCoord{10, 8}, true));
    CHECK(rig.world.Machines().QueryStart(rig.actor, generic, rig.recipe, 1) == MachineOutcome::Unavailable);
    for (const auto action : {MachineAction::Stop, MachineAction::Resume, MachineAction::Cancel, MachineAction::Collect}) {
        CHECK(Take(rig.world.Machines().Use(rig.actor, generic, action)).outcome == MachineOutcome::Unavailable);
    }
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("unavailable machine finishes paid setup slice then pauses", "[machine][world]") {
    MachineRig rig;
    rig.Inputs(2);
    REQUIRE(rig.Start().outcome == MachineOutcome::Started);
    rig.Advance(40);
    SECTION("disabled") { REQUIRE(rig.world.SetObjectEnabled(rig.machine, false)); }
    SECTION("broken") {
        REQUIRE(rig.world.SetObjectAppearance(rig.machine, ObjectKind::Generic, ObjectFacing::North, true));
    }
    rig.Advance(60);
    const auto view = rig.View();
    CHECK(view.setup);
    CHECK_FALSE(view.running);
    CHECK(view.setupCompleted == 100 * ratioScale);
    CHECK(view.remainingTicks == 0);
    CHECK(view.reservedVolumeMl == 20);
    CHECK_FALSE(rig.world.Machines().Busy(rig.actor));
    CHECK(rig.Use(MachineAction::Resume).outcome == MachineOutcome::Unavailable);
    CHECK(rig.world.CheckInvariants());
}
