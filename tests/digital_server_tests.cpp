#include "core/server.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ContentCatalog DigitalServerContent() {
    const ContentSource source{"digital-server", R"({format_version:1,definitions:[
      {type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white'}},
      {type:'terrain',id:'wall',name:'Wall',movement:{passable:false},blocks_sight:true,appearance:{symbol:'#',foreground:'white'}},
      {type:'terrain',id:'closed_door',name:'Door',movement:{passable:false},blocks_sight:true,appearance:{symbol:'+',foreground:'white'}},
      {type:'terrain',id:'open_door',name:'Open door',movement:{passable:true},blocks_sight:false,appearance:{symbol:'/',foreground:'white'}},
      {type:'terrain',id:'air',name:'Air',movement:{passable:true},open_space:true,blocks_sight:false,appearance:{symbol:' ',foreground:'white'}},
      {type:'item',id:'rock',name:'Rock',mass_g:10,volume_ml:20},
      {type:'item',id:'bandage',name:'Bandage',mass_g:2,volume_ml:5},
      {type:'item',id:'backpack',name:'Backpack',mass_g:10,volume_ml:10,capabilities:[{type:'container',id:'storage',capacity_volume_ml:100}]},
      {type:'skill',id:'computers',name:'Computers'},
      {type:'item',id:'printer',name:'Printer',mass_g:100,volume_ml:100,
       digital_device:'controller',supplied_information:['supplied'],placed_object:'station'},
      {type:'information',id:'supplied',name:'Supplied',designs:['print'],software:true},
      {type:'machine_recipe',id:'print',inputs:[{item:'rock',count:1}],
       result:{item:'bandage',count:1},duration:1200,energy_per_tick:1,
       learning:{skill:'computers',level:0,xp_per_execution:10}}
    ],objects:[{id:'station',name:'Printer',behavior:'selectable_machine',
       packing_item:'printer',capacity_volume_ml:100,selectable_recipes:['print']}]
    })"};
    Diagnostics diagnostics;
    auto content = LoadContent({&source, 1}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(content);
    return eastl::move(*content);
}

TEST_CASE("Digital commands require canonical explicit identities", "[digital][server]") {
    Command command; command.kind = CommandKind::begin_machine;
    CHECK_FALSE(ValidCommandShape(command));
    command.target = 9; command.machineRecipe = {2}; command.quantity = 10;
    REQUIRE(ValidCommandShape(command));
    command.item = 4; CHECK_FALSE(ValidCommandShape(command)); command.item = 0;
    command.quantity = 0; CHECK_FALSE(ValidCommandShape(command));
    command = {}; command.kind = CommandKind::machine_action; command.target = 9;
    command.machineAction = MachineAction::Collect;
    CHECK(ValidCommandShape(command));
    command.machineRecipe = {2}; CHECK_FALSE(ValidCommandShape(command));
}

TEST_CASE("Information commands reject ambiguous endpoints and unused host fields", "[digital][server]") {
    Command command; command.kind = CommandKind::copy_information;
    command.informationSource.item = 1; command.informationDestination.object = 2;
    command.information = {3}; command.informationHost = 4;
    REQUIRE(ValidCommandShape(command));
    command.informationSource.object = 5; CHECK_FALSE(ValidCommandShape(command));
    command.informationSource.object = 0; command.informationDestination = command.informationSource;
    CHECK_FALSE(ValidCommandShape(command));
    command = {}; command.kind = CommandKind::cancel_information;
    CHECK(ValidCommandShape(command));
    command.informationHost = 1; CHECK_FALSE(ValidCommandShape(command));
    command = {}; command.information = {1}; CHECK_FALSE(ValidCommandShape(command));
}

TEST_CASE("Server setup stop preserves its paid slice without scheduling a second action", "[digital][server]") {
    auto content = DigitalServerContent();
    Diagnostics diagnostics;
    const auto valid = ValidateServerContent(content, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(valid);
    Server server(content);
    auto pacing = server.GetPacing(); pacing.rateLimited = true; pacing.ticksPerSecond = 100;
    REQUIRE(server.SetPacing(pacing));
    auto& world = server.GetWorld();
    const auto material = world.CreateItem(content.FindItem("rock"), ItemLocation::Carried(server.GetPlayer()));
    REQUIRE(material);
    REQUIRE(world.SetTile({0, 1}, {content.FindTerrain("floor"), 0}));
    const auto machine = world.CreateObject(content.FindObject("station"), {0, 1}); REQUIRE(machine);
    const auto machineId = world.GetObject(*machine)->id;
    Command command; command.kind = CommandKind::begin_machine; command.target = machineId;
    command.machineRecipe = content.FindMachineRecipe("print"); command.quantity = 1;
    const auto start = server.Send(command);
    REQUIRE(start.outcome == Outcome::machine_changed); REQUIRE(start.machineOutcome == MachineOutcome::Started);
    REQUIRE(start.advanced); REQUIRE(world.GetActorRemainingTicks(server.GetPlayer()) != 0);
    const auto funded = world.GetActorRemainingTicks(server.GetPlayer());
    const auto tick = world.GetTick();
    const auto turns = world.GetActor(server.GetPlayer())->turns;
    command = {}; command.kind = CommandKind::machine_action; command.target = machineId;
    command.machineAction = MachineAction::Stop;
    const auto stop = server.Send(command);
    CHECK(stop.outcome == Outcome::machine_changed); CHECK(stop.machineOutcome == MachineOutcome::PauseRequested);
    CHECK_FALSE(stop.advanced); CHECK(stop.cost == 0);
    CHECK(world.GetTick() == tick); CHECK(world.GetActorRemainingTicks(server.GetPlayer()) == funded);
    CHECK(world.GetActor(server.GetPlayer())->turns == turns);
    auto view = server.View(0); REQUIRE(view.machines.size() == 1);
    CHECK(view.machines[0].pauseRequested); CHECK(view.machines[0].setup);
    server.Update(funded * 10000);
    view = server.View(0); REQUIRE(view.machines.size() == 1);
    CHECK(view.machines[0].setup); CHECK_FALSE(view.machines[0].running);
    CHECK_FALSE(view.machines[0].pauseRequested); CHECK(view.machines[0].worker == 0);
    CHECK(world.GetActorRemainingTicks(server.GetPlayer()) == 0);
    CHECK(view.machines[0].setupCompleted > 0);
    command.machineAction = MachineAction::Resume;
    const auto resume = server.Send(command);
    CHECK(resume.outcome == Outcome::machine_changed); CHECK(resume.machineOutcome == MachineOutcome::Resumed);
    CHECK(resume.advanced); CHECK(resume.cost != 0);
}
}
