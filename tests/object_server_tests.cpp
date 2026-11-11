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
    CHECK(server.GetWorld().GetActor(server.GetPlayer())->remainingTicks == 0);
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

TEST_CASE("Server machine interaction respects vertical access and terrain priority", "[server][objects]") {
    const auto content = MachineContent();
    Server server(content);
    World& world = server.GetWorld();
    const auto above = CreateMachine(server, {0, 0, 1});
    GiveInputs(server);
    Blocked(server, Interact(0, 0, 1));
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

}
