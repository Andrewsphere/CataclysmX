#include "core/server.h"
#include "objects/object_behavior.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

ContentCatalog MachineContent() {
    REQUIRE(TestContent().FindObject("scrap_press").value != 0);
    return TestContent();
}

ObjectHandle CreateMachine(Server& server, TileCoord position = {1, 0}) {
    World& world = server.GetWorld();
    REQUIRE(world.SetTile(position, {world.GetContent().FindTerrain("floor"), 0}));
    const auto machine = world.CreateObject(world.GetContent().FindObject("scrap_press"), position);
    REQUIRE(machine);
    return *machine;
}

ObjectStateView State(const World& world, ObjectHandle machine) {
    const auto state = world.DescribeObjectState(machine);
    REQUIRE(state);
    return *state;
}

Vector<ItemHandle> GiveInputs(Server& server, std::uint32_t count = 3) {
    Vector<ItemHandle> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto input = server.GetWorld().CreateItem(server.GetWorld().GetContent().FindItem("scrap"),
            ItemLocation::Carried(server.GetPlayer()));
        REQUIRE(input);
        result.push_back(*input);
    }
    return result;
}

Command Interact(int dx = 1, int dz = 0, int dy = 0) {
    Command command;
    command.kind = CommandKind::interact;
    command.dx = dx;
    command.dz = dz;
    command.dy = dy;
    return command;
}

void Used(Server& server, Command command = Interact()) {
    const Tick before = server.GetTick();
    const auto result = server.Send(command);
    CHECK(result.outcome == Outcome::toggled);
    CHECK(result.advanced);
    CHECK(result.cost == ActionTicks(100, server.GetWorld().GetActor(server.GetPlayer())->speed));
    CHECK(server.GetTick() == before + result.cost);
}

void Blocked(Server& server, Command command = Interact()) {
    const Tick before = server.GetTick();
    const auto turns = server.GetWorld().GetActor(server.GetPlayer())->turns;
    const auto result = server.Send(command);
    CHECK(result.outcome == Outcome::blocked);
    CHECK_FALSE(result.advanced);
    CHECK(result.cost == 0);
    CHECK(server.GetTick() == before);
    CHECK(server.GetWorld().GetActorRemainingTicks(server.GetPlayer()) == 0);
    CHECK(server.GetWorld().GetActor(server.GetPlayer())->turns == turns);
}

TEST_CASE("Server machine interaction consumes direct inputs and collects completed output", "[server][objects]") {
    const auto content = MachineContent();
    Server server(content);
    World& world = server.GetWorld();
    const auto machine = CreateMachine(server);
    const auto inputs = GiveInputs(server);
    Used(server);
    for (const auto input : inputs) { CHECK(world.GetItem(input) == nullptr); }
    CHECK(world.GetActor(server.GetPlayer())->inventory.empty());
    CHECK(world.GetObject(machine)->contents.empty());
    CHECK(State(world, machine).running);
    CHECK(State(world, machine).remainingWork == 200);
    CHECK(State(world, machine).energy == 2800);

    Command wait;
    wait.waitCost = 200;
    REQUIRE(server.Send(wait).advanced);
    CHECK_FALSE(State(world, machine).hasJob);
    CHECK_FALSE(State(world, machine).running);
    CHECK(State(world, machine).remainingWork == 0);
    CHECK(State(world, machine).energy == 2400);
    REQUIRE(world.GetObject(machine)->contents.size() == 1);
    const auto output = world.GetObject(machine)->contents.front();
    const auto outputId = world.GetItem(output)->id;
    CHECK(world.GetItem(output)->type == content.FindItem("metal_pipe"));
    CHECK(world.GetActor(server.GetPlayer())->inventory.empty());

    // Inspection is a direct World query and must not advance the machine.
    const auto before = server.GetTick();
    const auto view = server.View();
    CHECK(view.inventory.empty());
    CHECK(State(world, machine).energy == 2400);
    CHECK(server.GetTick() == before);
    Used(server);
    CHECK(world.GetObject(machine)->contents.empty());
    CHECK(world.GetItem(output)->location == ItemLocation::Carried(server.GetPlayer()));
    REQUIRE(server.View().inventory.size() == 1);
    CHECK(server.View().inventory.front().id == outputId);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server machine stop and resume preserve work and charge action costs", "[server][objects]") {
    const auto content = MachineContent();
    Server server(content);
    World& world = server.GetWorld();
    const auto machine = CreateMachine(server);
    GiveInputs(server);
    Used(server);
    Used(server); // Running context chooses Stop.
    CHECK(State(world, machine).hasJob);
    CHECK_FALSE(State(world, machine).running);
    CHECK(State(world, machine).remainingWork == 200);
    CHECK(State(world, machine).energy == 2800);
    REQUIRE(server.Send({}).advanced);
    CHECK(State(world, machine).remainingWork == 200);
    CHECK(State(world, machine).energy == 2800);
    Used(server); // Paused context resumes without any remaining inputs.
    CHECK(State(world, machine).running);
    CHECK(State(world, machine).remainingWork == 100);
    CHECK(world.GetActor(server.GetPlayer())->inventory.empty());
    REQUIRE(server.Send({}).advanced);
    CHECK_FALSE(State(world, machine).hasJob);
    REQUIRE(world.GetObject(machine)->contents.size() == 1);
    Used(server);
    CHECK(server.GetTick() == 600);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server machine missing inputs and power failures spend no time", "[server][objects]") {
    const auto content = MachineContent();
    Server server(content);
    World& world = server.GetWorld();
    const auto machine = CreateMachine(server);
    Blocked(server);
    const auto inputs = GiveInputs(server);
    REQUIRE(world.SetObjectEnergy(machine, 0));
    Blocked(server);
    CHECK_FALSE(State(world, machine).hasJob);
    CHECK(world.GetActor(server.GetPlayer())->inventory.size() == inputs.size());
    for (const auto input : inputs) { REQUIRE(world.GetItem(input)); }

    REQUIRE(world.SetObjectEnergy(machine, 2));
    Used(server);
    CHECK(State(world, machine).hasJob);
    CHECK_FALSE(State(world, machine).running);
    CHECK(State(world, machine).remainingWork == 299);
    Blocked(server); // A power-paused job cannot resume without energy.
    CHECK(State(world, machine).remainingWork == 299);
    REQUIRE(world.SetObjectEnergy(machine, 3000));
    Used(server);
    CHECK(State(world, machine).running);
    CHECK(State(world, machine).remainingWork == 199);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server machines ignore nested and ground recipe inputs", "[server][objects]") {
    const auto content = MachineContent();
    Server server(content);
    World& world = server.GetWorld();
    const auto machine = CreateMachine(server);
    const auto bag = world.CreateItem(content.FindItem("backpack"), ItemLocation::Carried(server.GetPlayer()));
    REQUIRE(bag);
    const auto nested = world.CreateItem(content.FindItem("scrap"), ItemLocation::Inside(*bag));
    const auto ground = world.CreateItem(content.FindItem("scrap"), ItemLocation::Ground({0, 0}));
    REQUIRE(nested);
    REQUIRE(ground);
    const auto direct = GiveInputs(server, 2);
    Blocked(server);
    CHECK_FALSE(State(world, machine).hasJob);
    for (const auto input : direct) { REQUIRE(world.GetItem(input)); }
    GiveInputs(server, 1);
    Used(server);
    CHECK(world.GetItem(*nested)->location == ItemLocation::Inside(*bag));
    CHECK(world.GetItem(*ground)->location == ItemLocation::Ground({0, 0}));
    CHECK(world.GetItem(*bag)->contents.size() == 1);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server machine collection checks capacity before transferring output", "[server][objects]") {
    const auto content = MachineContent();
    Server server(content);
    World& world = server.GetWorld();
    const auto machine = CreateMachine(server);
    GiveInputs(server);
    Used(server);
    Command wait;
    wait.waitCost = 200;
    REQUIRE(server.Send(wait).advanced);
    REQUIRE(world.GetObject(machine)->contents.size() == 1);
    const auto output = world.GetObject(machine)->contents.front();
    REQUIRE(world.SetActorCarryCapacity(server.GetPlayer(), 1));
    Blocked(server);
    CHECK(world.GetObject(machine)->contents.size() == 1);
    CHECK(world.GetActor(server.GetPlayer())->inventory.empty());
    REQUIRE(world.SetActorCarryCapacity(server.GetPlayer(), 2000));
    Used(server);
    CHECK(world.GetItem(output)->location == ItemLocation::Carried(server.GetPlayer()));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server machine interaction rejects vertical access and respects terrain priority", "[server][objects]") {
    const auto content = MachineContent();
    Server server(content);
    World& world = server.GetWorld();
    const auto above = CreateMachine(server, {0, 0, 1});
    GiveInputs(server);
    CHECK(server.Send(Interact(0,0,1)).outcome == Outcome::invalid_command);
    CHECK_FALSE(State(world, above).hasJob);
    CHECK(world.GetActor(server.GetPlayer())->inventory.size() == 3);

    const auto machine = CreateMachine(server);
    REQUIRE(world.SetTile({1, 0}, {content.FindTerrain("closed_door"), 0}));
    const auto opened = server.Send(Interact());
    CHECK(opened.outcome == Outcome::opened);
    CHECK(opened.advanced);
    CHECK_FALSE(State(world, machine).hasJob);
    CHECK(world.GetActor(server.GetPlayer())->inventory.size() == 3);
    const auto closed = server.Send(Interact());
    CHECK(closed.outcome == Outcome::closed);
    CHECK(closed.advanced);
    CHECK_FALSE(State(world, machine).hasJob);
    REQUIRE(world.SetTile({1, 0}, {content.FindTerrain("floor"), 0}));
    Used(server);
    CHECK(State(world, machine).hasJob);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Machine action discovery is read-only and agrees with execution refusals", "[server][objects][interaction]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto machine = CreateMachine(server);
    Vector<ObjectActionInfo> actions;
    const auto before = world.CaptureObjectState(machine);
    world.QueryObjectActions(server.GetPlayer(),machine,actions);
    REQUIRE(actions.size() == 3);
    CHECK(actions[0].reason == ObjectUseReason::NoInputs);
    CHECK(actions[1].reason == ObjectUseReason::NoJob);
    CHECK(actions[2].reason == ObjectUseReason::NoContents);
    for (const auto& action : actions) {
        CHECK(world.UseObject(server.GetPlayer(),machine,action.action).reason == action.reason);
    }
    CHECK(world.CaptureObjectState(machine) == before);
    CHECK(server.GetTick() == 0);
    GiveInputs(server);
    actions.clear(); world.QueryObjectActions(server.GetPlayer(),machine,actions);
    CHECK(actions[0].reason == ObjectUseReason::None);
    REQUIRE(world.SetObjectEnergy(machine,0));
    actions.clear(); world.QueryObjectActions(server.GetPlayer(),machine,actions);
    CHECK(actions[0].reason == ObjectUseReason::NoPower);
}

TEST_CASE("Explicit interaction preserves selected object action and rejects stale targets", "[server][objects][interaction]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto machine = CreateMachine(server);
    GiveInputs(server);
    Vector<InteractionChoice> choices;
    QueryInteractions(world,server.GetPlayer(),{1,0},choices);
    REQUIRE(choices.size() == 3);
    Command command; command.kind = CommandKind::interact_action; command.interaction = choices[0].request;
    REQUIRE(ValidCommandShape(command));
    REQUIRE(server.Send(command).outcome == Outcome::toggled);
    CHECK(State(world,machine).running);
    // Repeating Start must not turn into Stop when machine state changes.
    CHECK(server.Send(command).outcome == Outcome::blocked);
    CHECK(State(world,machine).running);
    REQUIRE(world.DestroyObject(machine));
    CreateMachine(server);
    CHECK(server.Send(command).outcome == Outcome::unavailable);
    choices.clear(); QueryInteractions(world,server.GetPlayer(),{2,0},choices);
    CHECK(choices.empty());
}

}

TEST_CASE("Door actions include unavailable alternatives and revalidate terrain and occupancy", "[server][interaction][doors]") {
    using namespace cataclysmx;
    Server server(TestContent()); auto& world = server.GetWorld();
    REQUIRE(world.SetTile({1,0},{TestTerrain("closed_door"),0}));
    Vector<InteractionChoice> choices; QueryInteractions(world,server.GetPlayer(),{1,0},choices);
    REQUIRE(choices.size() == 2);
    CHECK(choices[0].reason == ObjectUseReason::None);
    CHECK(choices[1].reason == ObjectUseReason::AlreadyClosed);
    Command open; open.kind = CommandKind::interact_action; open.interaction = choices[0].request;
    REQUIRE(server.Send(open).outcome == Outcome::opened);
    CHECK(world.GetActor(server.GetPlayer())->position == TileCoord{});
    CHECK(server.Send(open).outcome == Outcome::unavailable);
    REQUIRE(world.CreateActor({1,0}));
    choices.clear(); QueryInteractions(world,server.GetPlayer(),{1,0},choices);
    REQUIRE(choices.size() == 2);
    CHECK(choices[0].reason == ObjectUseReason::AlreadyOpen);
    CHECK(choices[1].reason == ObjectUseReason::Occupied);
    Command close; close.kind = CommandKind::interact_action; close.interaction = choices[1].request;
    const auto tick = server.GetTick();
    CHECK(server.Send(close).outcome == Outcome::blocked); CHECK(server.GetTick() == tick);
}

TEST_CASE("Lockable door discovers four actions and preserves exact transitions and damage", "[server][interaction][doors]") {
    using namespace cataclysmx;
    Server server(TestContent()); auto& world = server.GetWorld();
    REQUIRE(world.SetTile({1,1},{TestTerrain("lockable_closed_door"),17}));
    const auto query = [&] { Vector<InteractionChoice> choices; QueryInteractions(world,server.GetPlayer(),{1,1},choices); return choices; };
    auto choices = query(); REQUIRE(choices.size() == 4);
    CHECK(choices[0].reason == ObjectUseReason::None);
    CHECK(choices[1].reason == ObjectUseReason::AlreadyClosed);
    CHECK(choices[2].reason == ObjectUseReason::None);
    CHECK(choices[3].reason == ObjectUseReason::AlreadyUnlocked);
    const auto send = [&](const InteractionChoice& choice) { Command command; command.kind = CommandKind::interact_action; command.interaction = choice.request; return server.Send(command); };
    const auto lock = send(choices[2]); CHECK(lock.outcome == Outcome::locked); CHECK(lock.cost == 100);
    CHECK(world.GetTile({1,1})->damage == 17);
    CHECK(send(choices[0]).outcome == Outcome::unavailable);
    CHECK_FALSE(server.Send({CommandKind::move,1,1}).advanced);
    CHECK_FALSE(server.Send({CommandKind::interact,1,1}).advanced);
    choices = query(); REQUIRE(choices.size() == 4);
    CHECK(choices[0].reason == ObjectUseReason::Locked);
    CHECK(choices[2].reason == ObjectUseReason::AlreadyLocked);
    CHECK(choices[3].reason == ObjectUseReason::None);
    CHECK_FALSE(send(choices[2]).advanced); CHECK_FALSE(send(choices[1]).advanced);
    const auto tick = server.GetTick(); CHECK_FALSE(send(choices[0]).advanced); CHECK(server.GetTick() == tick);
    CHECK(send(choices[3]).outcome == Outcome::unlocked);
    CHECK(world.GetTile({1,1})->terrain == TestTerrain("lockable_closed_door"));
    choices = query(); REQUIRE(choices.size() == 4); CHECK(send(choices[0]).outcome == Outcome::opened);
    choices = query(); REQUIRE(choices.size() == 4);
    CHECK(choices[0].reason == ObjectUseReason::AlreadyOpen);
    CHECK(choices[2].reason == ObjectUseReason::MustClose);
    CHECK(choices[3].reason == ObjectUseReason::AlreadyUnlocked);
    CHECK_FALSE(send(choices[2]).advanced);
    CHECK(send(choices[1]).outcome == Outcome::closed);
    choices = query(); REQUIRE(choices.size() == 4);
    CHECK(send(choices[0]).outcome == Outcome::opened);
    REQUIRE(world.CreateActor({1,1})); choices = query(); REQUIRE(choices.size() == 4);
    CHECK(choices[1].reason == ObjectUseReason::Occupied);
    CHECK_FALSE(send(choices[1]).advanced);
    CHECK(world.GetTile({1,1})->damage == 17); CHECK(world.CheckInvariants());
}

TEST_CASE("Object action discovery includes diagonal and same-tile targets but never other layers", "[server][objects][interaction]") {
    Server server(TestContent()); auto& world = server.GetWorld();
    const auto diagonal = CreateMachine(server,{1,1});
    REQUIRE(world.SetTile({1,0},{TestTerrain("wall"),0}));
    REQUIRE(world.SetTile({0,1},{TestTerrain("wall"),0}));
    const auto current = CreateMachine(server,{0,0});
    const auto upper = CreateMachine(server,{0,0,1});
    CHECK(world.CanUseObject(server.GetPlayer(),diagonal));
    CHECK(world.CanUseObject(server.GetPlayer(),current));
    CHECK_FALSE(world.CanUseObject(server.GetPlayer(),upper));
    Vector<InteractionChoice> choices;
    QueryInteractions(world,server.GetPlayer(),{1,1},choices); CHECK(choices.size() == 3);
    choices.clear(); QueryInteractions(world,server.GetPlayer(),{0,0},choices); CHECK(choices.size() == 3);
    choices.clear(); QueryInteractions(world,server.GetPlayer(),{0,0,1},choices); CHECK(choices.empty());
    Command vertical; vertical.kind = CommandKind::interact_action;
    vertical.interaction = {{0,0,1},world.GetObject(upper)->id,{},InteractionKind::Object,ObjectAction::Start};
    CHECK_FALSE(server.Send(vertical).advanced);
}

TEST_CASE("A door locked after arming rejects timed opening and a locked door cannot be armed", "[server][doors][items]") {
    Server server(TestContent()); auto& world = server.GetWorld();
    REQUIRE(world.SetTile({1,0},{TestTerrain("lockable_closed_door"),9}));
    const auto device = world.CreateItem(TestItem("door_controller"),ItemLocation::Ground({0,0})); REQUIRE(device);
    const auto capability = FindItemCapability(world.GetContent().GetItem(TestItem("door_controller")),"timer");
    REQUIRE(capability.value != 0);
    const ItemUseRequest arm{capability,"arm",{},1,0};
    REQUIRE(world.UseItem(server.GetPlayer(),*device,arm).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(world.SetTile({1,0},{TestTerrain("locked_door"),9}));
    world.ElapseTicks(300); world.ProcessItemEvents();
    CHECK(world.GetTile({1,0})->terrain == TestTerrain("locked_door"));
    CHECK(world.GetTile({1,0})->damage == 9);
    CHECK(world.UseItem(server.GetPlayer(),*device,arm).outcome == ItemUseOutcome::Refused);
    CHECK(world.ActiveItemCount() == 0); CHECK(world.CheckInvariants());
}
