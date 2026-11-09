#include "core/server.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>

namespace {
using namespace cataclysmx;

Command Use(const World& world, ItemHandle item, const char* capability, const char* action,
    PersistentId other = 0, int dx = 0, int dz = 0) {
    const auto* value = world.GetItem(item);
    REQUIRE(value);
    Command command;
    command.kind = CommandKind::use_item;
    command.item = value->id;
    command.capability = FindItemCapability(world.GetContent().GetItem(value->type), capability);
    command.itemAction = action;
    command.otherItem = other;
    command.dx = dx; command.dz = dz;
    return command;
}

TEST_CASE("Server item failures are zero cost and snapshots expose typed authoritative state", "[server][items]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const auto charger = world.CreateItem(TestItem("field_charger"), ItemLocation::Carried(server.GetPlayer()));
    const auto battery = world.CreateItem(TestItem("rechargeable_battery"), ItemLocation::Carried(server.GetPlayer()));
    REQUIRE(charger); REQUIRE(battery);
    const auto start = Use(world, *charger, "charger", "start");
    const auto refused = server.Send(start);
    CHECK(refused.outcome == Outcome::item_use_failed);
    CHECK(refused.itemOutcome == ItemUseOutcome::Refused);
    CHECK(refused.itemReason.code == "missing_battery");
    CHECK_FALSE(refused.advanced);
    CHECK(refused.cost == 0);
    CHECK(server.GetTick() == 0);

    const auto installed = server.Send(Use(world, *charger, "charger", "install",
        world.GetItem(*battery)->id));
    CHECK(installed.outcome == Outcome::used_item);
    CHECK(installed.cost == 100);
    const auto started = server.Send(start);
    CHECK(started.outcome == Outcome::used_item);
    CHECK(started.cost == 100);
    const auto before = world.CaptureItemState(*charger);
    const auto view = server.View();
    REQUIRE(view.inventory.size() == 2);
    CHECK(view.inventory[0].id == world.GetItem(*charger)->id);
    CHECK(view.inventory[1].parent == world.GetItem(*charger)->id);
    CHECK(view.inventory[1].installation == start.capability);
    REQUIRE(view.inventory[1].energy.size() == 1);
    CHECK(view.inventory[1].energy[0].energy == 3);
    CHECK(view.carriedMassGrams == world.GetItemMass(*charger));
    CHECK(world.CaptureItemState(*charger) == before);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server revalidates item access and capacity before spending an action", "[server][items]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.SetActorCarryCapacity(server.GetPlayer(), 1));
    const auto capacity = server.Send({CommandKind::pick_up});
    CHECK(capacity.outcome == Outcome::capacity_exceeded);
    CHECK_FALSE(capacity.advanced);
    CHECK(world.ItemsAt({0, 0}).size() == 1);
    CHECK(server.GetTick() == 0);

    const auto other = world.CreateActor({0, 2}); REQUIRE(other);
    const auto device = world.CreateItem(TestItem("field_charger"), ItemLocation::Carried(*other)); REQUIRE(device);
    const auto refused = server.Send(Use(world, *device, "charger", "start"));
    CHECK(refused.itemOutcome == ItemUseOutcome::Inaccessible);
    CHECK_FALSE(refused.advanced);
    Command store;
    store.kind = CommandKind::store_item;
    store.item = world.GetItem(*device)->id;
    CHECK(server.Send(store).outcome == Outcome::not_carried);
    CHECK(world.GetItem(*device)->location == ItemLocation::Carried(*other));
    CHECK(server.GetTick() == 0);
}

TEST_CASE("Server storage commands preserve identities and report volume refusals", "[server][items]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const auto bag = world.ItemsAt({0, 0})[0];
    const auto bandage = world.GetItem(bag)->contents[0];
    const auto id = world.GetItem(bandage)->id;
    Command move;
    move.kind = CommandKind::store_item; move.item = id;
    CHECK(server.Send(move).outcome == Outcome::stored_item);
    CHECK(world.GetItem(bandage)->location == ItemLocation::Carried(server.GetPlayer()));
    move.otherItem = world.GetItem(bag)->id;
    CHECK(server.Send(move).outcome == Outcome::stored_item);
    CHECK(world.GetItem(bandage)->location == ItemLocation::Inside(bag));
    move.item = world.GetItem(bag)->id; move.otherItem = id;
    const auto before = server.GetTick();
    CHECK(server.Send(move).outcome == Outcome::invalid_container);
    CHECK(server.GetTick() == before);
    CHECK(world.GetItem(bandage)->id == id);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server timed door use survives action boundaries and opens once", "[server][items]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const auto device = world.CreateItem(TestItem("door_controller"), ItemLocation::Ground({0, 0})); REQUIRE(device);
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("closed_door"), 5}));
    const auto armed = server.Send(Use(world, *device, "timer", "arm", 0, 1, 0));
    REQUIRE(armed.outcome == Outcome::used_item);
    CHECK(server.GetTick() == 100);
    CHECK(world.GetTile({1, 0})->terrain == TestTerrain("closed_door"));
    Command wait; wait.waitCost = 250;
    CHECK(server.Send(wait).outcome == Outcome::waited);
    CHECK(server.GetTick() == 350);
    CHECK(world.GetTile({1, 0})->terrain == TestTerrain("open_door"));
    CHECK(world.GetTile({1, 0})->damage == 5);
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("closed_door"), 0}));
    server.Send({});
    CHECK(world.GetTile({1, 0})->terrain == TestTerrain("closed_door"));
    CHECK(world.ActiveItemCount() == 0);
}

TEST_CASE("Queued item commands validate current access on execution", "[server][items][scheduling]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    const auto device = world.CreateItem(TestItem("door_controller"), ItemLocation::Ground({0, 0})); REQUIRE(device);
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("closed_door"), 0}));
    REQUIRE(server.SetPacing({100, true, true}));
    REQUIRE(server.Send({}).advanced);
    CHECK(server.Send(Use(world, *device, "timer", "arm", 0, 1, 0)).outcome == Outcome::queued);
    REQUIRE(world.TransferItem(*device, ItemLocation::Ground({5, 0})));
    const auto update = server.Update(1'000'000);
    REQUIRE(update.commandResult);
    CHECK(update.commandResult->itemOutcome == ItemUseOutcome::Inaccessible);
    CHECK_FALSE(update.commandResult->advanced);
    CHECK(world.ActiveItemCount() == 0);
    CHECK(world.GetActor(server.GetPlayer())->turns == 1);
}

TEST_CASE("Server checkpoints resume simultaneous charging and door activity mid action", "[server][items][save]") {
    struct TemporarySave {
        std::filesystem::path directory = std::filesystem::temp_directory_path()
            / ("cataclysmx-item-session-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporarySave() { REQUIRE(std::filesystem::create_directory(directory)); }
        ~TemporarySave() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } temporary;
    const auto path = (temporary.directory / "session.sqlite").string();
    Server original(TestContent());
    World& world = original.GetWorld();
    const auto device = world.CreateItem(TestItem("utility_controller"), ItemLocation::Ground({0, 0})); REQUIRE(device);
    const auto battery = world.CreateItem(TestItem("rechargeable_battery"), ItemLocation::Ground({0, 0})); REQUIRE(battery);
    const auto deviceId = world.GetItem(*device)->id;
    const auto batteryId = world.GetItem(*battery)->id;
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("closed_door"), 7}));
    const auto charge = Use(world, *device, "charger", "start");
    const auto arm = Use(world, *device, "timer", "arm", 0, 1, 0);
    REQUIRE(world.UseItem(original.GetPlayer(), *device,
        {charge.capability, "install", *battery}).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(world.UseItem(original.GetPlayer(), *device,
        {charge.capability, "start"}).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(world.UseItem(original.GetPlayer(), *device,
        {arm.capability, "arm", {}, 1, 0}).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(original.SetPacing({100, true, true}));
    REQUIRE(original.Send({}).advanced);
    CHECK(original.Update(170000).tick == 17);
    const auto deviceState = world.CaptureItemState(*device);
    const auto batteryState = world.CaptureItemState(*battery);
    REQUIRE(original.CreateSave(path.c_str()));
    auto loaded = Server::LoadSave(TestContent(), path.c_str()); REQUIRE(loaded);
    Server& restored = **loaded;
    World& fresh = restored.GetWorld();
    CHECK(restored.GetTick() == 17);
    CHECK(fresh.CaptureItemState(fresh.FindItem(deviceId)) == deviceState);
    CHECK(fresh.CaptureItemState(fresh.FindItem(batteryId)) == batteryState);
    CHECK(fresh.ActiveItemCount() == 1);
    CHECK(restored.View().groundItems == original.View().groundItems);
    original.Update(830000); restored.Update(830000);
    REQUIRE(original.SetPacing({100, false, true}));
    REQUIRE(restored.SetPacing({100, false, true}));
    Command wait; wait.waitCost = 250;
    REQUIRE(original.Send(wait).advanced); REQUIRE(restored.Send(wait).advanced);
    CHECK(restored.GetTick() == original.GetTick());
    CHECK(restored.View().groundItems == original.View().groundItems);
    CHECK(restored.View().cells == original.View().cells);
    CHECK(fresh.GetTile({1, 0})->terrain == TestTerrain("open_door"));
    CHECK(fresh.GetTile({1, 0})->damage == 7);
    CHECK(fresh.CheckInvariants());
}
}
