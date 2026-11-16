#include "core/debug_items.h"
#include "core/server.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

TEST_CASE("Debug grants create initialized batteries and containers without advancing time", "[debug-items]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto player = server.GetPlayer();
    const auto before = server.View(0);
    const auto revision = world.GetRevision();
    const auto tick = server.GetTick();
    const auto result = DebugGiveItems(server, server.GetHostPlayerId(), {TestItem("disposable_battery"), 2});
    REQUIRE(result.outcome == DebugGiveOutcome::Given);
    CHECK(result.count == 2);
    REQUIRE(DebugGiveItems(server, server.GetHostPlayerId(), {TestItem("backpack"), 1}).outcome == DebugGiveOutcome::Given);
    CHECK(server.GetTick() == tick);
    CHECK(world.GetTick() == tick);
    CHECK(world.GetRevision() > revision);
    const auto after = server.View(0);
    CHECK(after.revision > before.revision);
    CHECK(after.remainingTicks == before.remainingTicks);
    REQUIRE(after.inventory.size() == before.inventory.size() + 3);

    Vector<ItemHandle> batteries;
    ItemHandle pack{};
    for (const auto item : world.GetActor(player)->inventory) {
        const auto* instance = world.GetItem(item);
        REQUIRE(instance);
        CHECK(instance->location == ItemLocation::Carried(player));
        if (instance->type == TestItem("disposable_battery")) { batteries.push_back(item); }
        if (instance->type == TestItem("backpack")) { pack = item; }
    }
    REQUIRE(batteries.size() == 2);
    CHECK(batteries[0] != batteries[1]);
    CHECK(world.GetItem(batteries[0])->id != world.GetItem(batteries[1])->id);
    const auto power = FindItemCapability(TestContent().GetItem(TestItem("disposable_battery")), "power");
    for (const auto battery : batteries) {
        const auto energy = world.GetItemEnergy(battery, power);
        REQUIRE(energy);
        CHECK(energy->energy == 1000);
    }
    REQUIRE(pack.value != 0);
    CHECK(world.GetItem(pack)->contents.empty());
    const auto packId = world.GetItem(pack)->id;
    bool packVisible = false;
    unsigned batteriesVisible = 0;
    for (const auto& entry : after.inventory) {
        if (entry.type == TestItem("disposable_battery")) {
            ++batteriesVisible;
            REQUIRE(entry.energy.size() == 1);
            CHECK(entry.energy.front().energy == 1000);
            CHECK(entry.energy.front().capacity == 1000);
        }
        if (entry.id == packId) {
            packVisible = true;
            CHECK(entry.capacityVolumeMl == 20000);
            CHECK(entry.usedVolumeMl == 0);
        }
    }
    CHECK(packVisible);
    CHECK(batteriesVisible == 2);
    // A granted container supports ordinary storage, and battery state is per instance.
    REQUIRE(world.TransferItem(batteries[0], ItemLocation::Inside(pack)));
    REQUIRE(world.SetItemEnergy(batteries[0], power, 123));
    REQUIRE(world.GetItemEnergy(batteries[1], power));
    CHECK(world.GetItemEnergy(batteries[1], power)->energy == 1000);
    CHECK(world.GetItem(batteries[1])->location == ItemLocation::Carried(player));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Debug grant capacity preflight refuses whole batches without consuming identities", "[debug-items]") {
    Server server(TestContent());
    Server control(TestContent());
    auto& world = server.GetWorld();
    const auto player = server.GetPlayer();
    const auto type = TestItem("rock");
    const auto mass = TestContent().GetItem(type).massGrams;
    const auto capacity = world.GetCarriedMass(player) + mass * 2;
    REQUIRE(world.SetActorCarryCapacity(player, capacity));
    REQUIRE(control.GetWorld().SetActorCarryCapacity(control.GetPlayer(), capacity));
    const auto inventory = server.View(0).inventory;
    const auto revision = world.GetRevision();
    const auto tick = server.GetTick();
    const auto refused = DebugGiveItems(server, server.GetHostPlayerId(), {type, 3});
    CHECK(refused.outcome == DebugGiveOutcome::CapacityExceeded);
    CHECK(refused.count == 0);
    CHECK(server.View(0).inventory == inventory);
    CHECK(world.GetRevision() == revision);
    CHECK(server.GetTick() == tick);

    REQUIRE(DebugGiveItems(server, server.GetHostPlayerId(), {type, 2}).outcome == DebugGiveOutcome::Given);
    REQUIRE(DebugGiveItems(control, control.GetHostPlayerId(), {type, 2}).outcome == DebugGiveOutcome::Given);
    // Equal following identities detect even a create-and-rollback refusal.
    CHECK(server.View(0).inventory == control.View(0).inventory);
    CHECK(world.GetCarriedMass(player) == capacity);
    const auto fullInventory = server.View(0).inventory;
    const auto fullRevision = world.GetRevision();
    CHECK(DebugGiveItems(server, server.GetHostPlayerId(), {type, 1}).outcome == DebugGiveOutcome::CapacityExceeded);
    CHECK(server.View(0).inventory == fullInventory);
    CHECK(world.GetRevision() == fullRevision);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Debug grants reject invalid types wrappers and counts without changing the world", "[debug-items]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto inventory = server.View(0).inventory;
    const auto revision = world.GetRevision();
    const auto tick = server.GetTick();
    const auto wrapper = TestItem("unfinished_craft");
    REQUIRE(TestContent().GetItem(wrapper).unfinishedCraft);
    for (const auto type : {ItemTypeId{}, ItemTypeId{UINT32_MAX}, wrapper}) {
        const auto result = DebugGiveItems(server, server.GetHostPlayerId(), {type, 1});
        CHECK(result.outcome == DebugGiveOutcome::InvalidItem);
        CHECK(result.count == 0);
    }
    for (const auto count : {0u, 1001u, UINT32_MAX}) {
        const auto result = DebugGiveItems(server, server.GetHostPlayerId(), {TestItem("bandage"), count});
        CHECK(result.outcome == DebugGiveOutcome::InvalidCount);
        CHECK(result.count == 0);
    }
    CHECK(server.View(0).inventory == inventory);
    CHECK(world.GetRevision() == revision);
    CHECK(server.GetTick() == tick);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Debug grants require the available unparked host player", "[debug-items]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto host = server.GetHostPlayerId();
    const auto guest = world.CreateActor({1, 0}, true);
    REQUIRE(guest);
    const auto guestId = world.GetActor(*guest)->id;
    const DebugItemGrant request{TestItem("bandage"), 1};
    const auto inventory = server.View(0).inventory;
    const auto revision = world.GetRevision();
    CHECK(DebugGiveItems(server, 0, request).outcome == DebugGiveOutcome::Unavailable);
    CHECK(DebugGiveItems(server, guestId, request).outcome == DebugGiveOutcome::Unavailable);
    CHECK(world.GetActor(*guest)->inventory.empty());
    CHECK(world.GetRevision() == revision);
    REQUIRE(world.SetActorParked(server.GetPlayer(), true));
    const auto parkedRevision = world.GetRevision();
    const auto result = DebugGiveItems(server, host, request);
    CHECK(result.outcome == DebugGiveOutcome::Unavailable);
    CHECK(result.count == 0);
    CHECK(world.GetRevision() == parkedRevision);
    REQUIRE(world.SetActorParked(server.GetPlayer(), false));
    CHECK(server.View(0).inventory == inventory);
    CHECK(server.GetTick() == 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Debug grants accept the inclusive thousand item limit at normal carry capacity", "[debug-items]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    const auto player = server.GetPlayer();
    const auto before = server.View(0).inventory.size();
    const auto massBefore = world.GetCarriedMass(player);
    const auto type = TestItem("bandage");
    const auto addedMass = TestContent().GetItem(type).massGrams * 1000u;
    REQUIRE(world.SetActorCarryCapacity(player, massBefore + addedMass));
    const auto result = DebugGiveItems(server, server.GetHostPlayerId(), {type, 1000});
    REQUIRE(result.outcome == DebugGiveOutcome::Given);
    CHECK(result.count == 1000);
    const auto view = server.View(0);
    CHECK(view.inventory.size() == before + 1000);
    PersistentId previous = 0;
    unsigned bandages = 0;
    for (const auto& entry : view.inventory) {
        if (entry.type != type) { continue; }
        CHECK(entry.id > previous);
        previous = entry.id;
        ++bandages;
    }
    CHECK(bandages == 1000);
    CHECK(world.GetCarriedMass(player) == massBefore + addedMass);
    CHECK(server.GetTick() == 0);
    CHECK(world.CheckInvariants());
}
}
