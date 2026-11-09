#include "core/world.h"
#include "core/simulation.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>
#include <limits>

namespace {
using namespace cataclysmx;

template<class T>
T Take(WorldResult<T> result) {
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

ItemTypeId Type(eastl::string_view name) {
    const auto type = TestContent().FindItem(name);
    REQUIRE(type.value != 0);
    return type;
}

ItemCapabilityId Capability(const World& world, ItemHandle item, eastl::string_view name) {
    const auto id = FindItemCapability(world.GetContent().GetItem(world.GetItem(item)->type), name);
    REQUIRE(id.value != 0);
    return id;
}

std::uint32_t Energy(const World& world, ItemHandle item) {
    const auto state = world.GetItemEnergy(item, Capability(world, item, "power"));
    REQUIRE(state);
    return state->energy;
}

struct ChargingSnapshot {
    bool enabled = false;
    std::uint32_t remainder = 0;
    bool operator==(const ChargingSnapshot&) const = default;
};
ChargingSnapshot ChargingState(const World& world, ItemHandle item) {
    const auto snapshot = world.CaptureItemState(item);
    const auto* state = snapshot.Find("charger");
    REQUIRE(state != nullptr);
    return {*state->GetBoolean("enabled"), static_cast<std::uint32_t>(*state->GetUnsigned("remainder"))};
}

ItemUseResult Use(World& world, ActorHandle actor, ItemHandle item, const char* action,
    ItemHandle other = {}, int dx = 0, int dz = 0, int dy = 0) {
    const auto capability = Capability(world, item,
        eastl::string_view(action) == "arm" || eastl::string_view(action) == "disarm" ? "timer" : "charger");
    return world.UseItem(actor, item, {capability, action, other, dx, dz, dy});
}

LoadRequest Load(World& world) {
    const auto request = Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    return request;
}

struct ChargingRig {
    World world{TestContent()};
    LoadRequest request = Load(world);
    ActorHandle actor = Take(world.CreateActor({1, 1}, true));
    ItemHandle pack = Take(world.CreateItem(Type("backpack"), ItemLocation::Carried(actor)));
    ItemHandle charger = Take(world.CreateItem(Type("field_charger"), ItemLocation::Inside(pack)));
    ItemHandle battery = Take(world.CreateItem(Type("rechargeable_battery"), ItemLocation::Carried(actor)));

    ChargingRig() {
        REQUIRE(Use(world, actor, charger, "install", battery).outcome == ItemUseOutcome::Succeeded);
        REQUIRE(Use(world, actor, charger, "start").outcome == ItemUseOutcome::Succeeded);
    }
};

TEST_CASE("Falling roots preserve nested handles and notify deployed devices", "[world][items][gravity]") {
    World world(TestContent());
    const auto request = Load(world);
    const Tile air{TestTerrain("air"), 0};
    REQUIRE(world.SetTile({1, 1, -3}, {TestTerrain("floor"), 0}));
    for (int y = -2; y <= 2; ++y) { REQUIRE(world.SetTile({1, 1, y}, air)); }
    REQUIRE(world.SetTile({1, 1, 2}, {TestTerrain("floor"), 0}));
    const auto actor = Take(world.CreateActor({1, 1, -3}));
    const auto pack = Take(world.CreateItem(Type("backpack"), ItemLocation::Ground({1, 1, 2})));
    const auto nested = Take(world.CreateItem(Type("backpack"), ItemLocation::Inside(pack)));
    const auto rock = Take(world.CreateItem(Type("rock"), ItemLocation::Inside(nested)));
    const auto id = world.GetItem(rock)->id;
    const auto packId = world.GetItem(pack)->id;
    REQUIRE(world.SetTile({1, 1, 2}, air));
    CHECK(world.GetItem(pack)->location == ItemLocation::Ground({1, 1, -2}));
    CHECK(world.GetItem(nested)->location == ItemLocation::Inside(pack));
    CHECK(world.GetItem(rock)->location == ItemLocation::Inside(nested));
    CHECK(world.FindItem(id) == rock);
    REQUIRE(world.DestroyActor(actor));
    CHECK(world.GetItem(pack)->location == ItemLocation::Ground({1, 1, -3}));
    CHECK(world.ItemsAt({1, 1, -3}) == Vector<ItemHandle>{pack});
    REQUIRE(world.TransferItem(pack, ItemLocation::Ground({1, 1, 2})));
    CHECK(world.GetItem(pack)->location == ItemLocation::Ground({1, 1, -3}));

    // A deployed timer must receive the same movement notification when gravity
    // relocates it as when an explicit transfer does.
    REQUIRE(world.SetTile({2, 2, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({2, 2, -1}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({3, 2, 0}, {TestTerrain("closed_door"), 0}));
    const auto user = Take(world.CreateActor({2, 2}, true));
    const auto timer = Take(world.CreateItem(Type("door_controller"), ItemLocation::Ground({2, 2})));
    REQUIRE(Use(world, user, timer, "arm", {}, 1, 0).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(world.SetTile({2, 2, 0}, air));
    CHECK(world.GetActor(user)->position == TileCoord{2, 2, -1});
    // The actor blocks the next tile, so the item waits until it moves away.
    CHECK(world.GetItem(timer)->location == ItemLocation::Ground({2, 2, 0}));
    REQUIRE(world.SetTile({3, 2, -1}, {TestTerrain("floor"), 0}));
    REQUIRE(world.MoveActor(user, {3, 2, -1}));
    CHECK(world.GetItem(timer)->location == ItemLocation::Ground({2, 2, -1}));
    CHECK(world.ActiveItemCount() == 0);
    world.ElapseTicks(300);
    world.ProcessItemEvents();
    CHECK(world.GetTile({3, 2, 0})->terrain == TestTerrain("closed_door"));
    CHECK(world.CheckInvariants());
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    Load(world);
    const auto restoredPack = world.FindItem(packId);
    const auto restoredRock = world.FindItem(id);
    REQUIRE(world.GetItem(restoredPack));
    REQUIRE(world.GetItem(restoredRock));
    CHECK(restoredPack != pack);
    CHECK(restoredRock != rock);
    CHECK(world.GetItem(restoredPack)->location == ItemLocation::Ground({1, 1, -3}));
    CHECK(world.GetItem(restoredRock)->location.item == world.GetItem(restoredPack)->contents.front());
    CHECK(world.CheckInvariants());
}

TEST_CASE("Door controllers preserve elevation and cannot target through closed floor faces", "[world][items][layers]") {
    World world(TestContent());
    Load(world);
    const TileCoord position{4, 4, 3};
    const TileCoord horizontalDoor{5, 4, 3};
    const TileCoord lowerDoor{4, 4, 2};
    REQUIRE(world.SetTile(position, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile(horizontalDoor, {TestTerrain("closed_door"), 9}));
    REQUIRE(world.SetTile(lowerDoor, {TestTerrain("closed_door"), 7}));
    const auto actor = Take(world.CreateActor(position, true));
    const auto timer = Take(world.CreateItem(Type("door_controller"), ItemLocation::Ground(position)));
    REQUIRE(Use(world, actor, timer, "arm", {}, 1, 0).outcome == ItemUseOutcome::Succeeded);
    const auto snapshot = world.CaptureItemState(timer);
    CHECK(snapshot.Find("timer")->Find("target")->GetSigned("y") == 3);
    world.ElapseTicks(300);
    world.ProcessItemEvents();
    CHECK(*world.GetTile(horizontalDoor) == Tile{TestTerrain("open_door"), 9});

    const auto before = world.CaptureItemState(timer);
    CHECK(Use(world, actor, timer, "arm", {}, 0, 0, -1).outcome == ItemUseOutcome::Refused);
    CHECK(world.CaptureItemState(timer) == before);
    REQUIRE(world.SetTile(position, {TestTerrain("air"), 0})); // Closed door below supports the user/device.
    CHECK(world.GetActor(actor)->position == position);
    REQUIRE(Use(world, actor, timer, "arm", {}, 0, 0, -1).outcome == ItemUseOutcome::Succeeded);
    SECTION("open face permits the timed effect and resulting fall") {
        world.ElapseTicks(300);
        world.ProcessItemEvents();
        CHECK(*world.GetTile(lowerDoor) == Tile{TestTerrain("open_door"), 7});
        CHECK(world.GetActor(actor)->position == lowerDoor);
    }
    SECTION("closing the face before the deadline invalidates the target") {
        REQUIRE(world.SetTile(position, {TestTerrain("floor"), 0}));
        world.ElapseTicks(300);
        world.ProcessItemEvents();
        CHECK(world.GetTile(lowerDoor)->terrain == TestTerrain("closed_door"));
        CHECK(world.GetActor(actor)->position == position);
    }
    CHECK(world.ActiveItemCount() == 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Charging partitions preserve fractional energy and source conservation", "[world][items]") {
    ChargingRig whole;
    ChargingRig partitioned;
    whole.world.ElapseTicks(137);
    for (int i = 0; i < 137; ++i) {
        partitioned.world.ElapseTicks(1);
        partitioned.world.ProcessItemEvents();
    }
    CHECK(Energy(whole.world, whole.battery) == 4);
    CHECK(Energy(whole.world, whole.charger) == 1996);
    CHECK(Energy(whole.world, whole.charger) + Energy(whole.world, whole.battery) == 2000);
    CHECK(whole.world.CaptureItemState(whole.charger) == partitioned.world.CaptureItemState(partitioned.charger));
    CHECK(whole.world.CaptureItemState(whole.battery) == partitioned.world.CaptureItemState(partitioned.battery));
    CHECK(ChargingState(whole.world, whole.charger).remainder == 11);
    CHECK(whole.world.NextItemEventIn() == eastl::optional<Tick>{33197});
    REQUIRE(whole.world.SetItemEnergy(whole.charger, Capability(whole.world, whole.charger, "power"), 2));
    CHECK(whole.world.NextItemEventIn() == eastl::optional<Tick>{63});
    whole.world.ElapseTicks(62);
    CHECK(ChargingState(whole.world, whole.charger).enabled);
    whole.world.ElapseTicks(1);
    CHECK_FALSE(ChargingState(whole.world, whole.charger).enabled);
    CHECK(ChargingState(whole.world, whole.charger).remainder == 0);
    CHECK(Energy(whole.world, whole.charger) == 0);
    CHECK(Energy(whole.world, whole.battery) == 6);
    CHECK_FALSE(whole.world.NextItemEventIn().has_value());
    CHECK(whole.world.CheckInvariants());
}

TEST_CASE("Nested charging freezes for parked actors and inactive roots", "[world][items]") {
    ChargingRig rig;
    rig.world.ElapseTicks(17);
    const auto frozen = rig.world.CaptureItemState(rig.charger);
    REQUIRE(rig.world.SetActorParked(rig.actor, true));
    CHECK_FALSE(rig.world.CanAccessItem(rig.actor, rig.charger));
    CHECK_FALSE(rig.world.NextItemEventIn().has_value());
    CHECK(rig.world.ActiveItemCount() == 1);
    rig.world.ElapseTicks(std::numeric_limits<Tick>::max());
    rig.world.ProcessItemEvents();
    CHECK(rig.world.CaptureItemState(rig.charger) == frozen);
    REQUIRE(rig.world.SetActorParked(rig.actor, false));
    REQUIRE(rig.world.UpdateRequest(rig.request, {{0, 0}, 0}, LoadMode::resident));
    REQUIRE(rig.world.ReconcileChunks());
    rig.world.ElapseTicks(10000);
    CHECK(rig.world.CaptureItemState(rig.charger) == frozen);
    REQUIRE(rig.world.UpdateRequest(rig.request, {{0, 0}, 0}, LoadMode::active));
    REQUIRE(rig.world.ReconcileChunks());
    rig.world.ElapseTicks(17);
    CHECK(Energy(rig.world, rig.battery) == 1);
    CHECK(ChargingState(rig.world, rig.charger).remainder == 2);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Unload cycles restore operating state with fresh handles", "[world][items]") {
    ChargingRig rig;
    rig.world.ElapseTicks(17);
    REQUIRE(rig.world.SetActorCarryCapacity(rig.actor, 4321));
    const PersistentId actorId = rig.world.GetActor(rig.actor)->id;
    const PersistentId chargerId = rig.world.GetItem(rig.charger)->id;
    const PersistentId batteryId = rig.world.GetItem(rig.battery)->id;
    const auto frozen = rig.world.CaptureItemState(rig.charger);
    for (int cycle = 0; cycle < 3; ++cycle) {
        const ItemHandle oldCharger = rig.charger;
        REQUIRE(rig.world.ReleaseRequest(rig.request));
        REQUIRE(rig.world.ReconcileChunks());
        CHECK(rig.world.GetItem(oldCharger) == nullptr);
        CHECK(rig.world.ActiveItemCount() == 0);
        rig.world.ElapseTicks(99999);
        rig.request = Load(rig.world);
        rig.actor = rig.world.FindActor(actorId);
        rig.charger = rig.world.FindItem(chargerId);
        rig.battery = rig.world.FindItem(batteryId);
        CHECK(rig.charger != oldCharger);
        CHECK(rig.world.GetActor(rig.actor)->carryCapacityGrams == 4321);
        CHECK(rig.world.CaptureItemState(rig.charger) == frozen);
        CHECK(rig.world.GetItem(rig.battery)->location.installation == Capability(rig.world, rig.charger, "charger"));
        CHECK(rig.world.ActiveItemCount() == 1);
        CHECK(rig.world.CheckInvariants());
    }
    rig.world.ElapseTicks(17);
    CHECK(Energy(rig.world, rig.battery) == 1);
}

TEST_CASE("Battery disconnect and destruction stop the old charger immediately", "[world][items]") {
    ChargingRig rig;
    rig.world.ElapseTicks(17);
    const auto spare = Take(rig.world.CreateItem(Type("rechargeable_battery"), ItemLocation::Carried(rig.actor)));
    const auto before = rig.world.CaptureItemState(rig.charger);
    const auto occupied = Use(rig.world, rig.actor, rig.charger, "install", spare);
    CHECK(occupied.outcome == ItemUseOutcome::Refused);
    CHECK(occupied.reason.code == "occupied");
    CHECK(rig.world.CaptureItemState(rig.charger) == before);
    REQUIRE(Use(rig.world, rig.actor, rig.charger, "remove").outcome == ItemUseOutcome::Succeeded);
    CHECK(rig.world.GetItem(rig.battery)->location == ItemLocation::Carried(rig.actor));
    CHECK(ChargingState(rig.world, rig.charger) == ChargingSnapshot{});
    CHECK(rig.world.ActiveItemCount() == 0);
    REQUIRE(Use(rig.world, rig.actor, rig.charger, "install", spare).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(Use(rig.world, rig.actor, rig.charger, "start").outcome == ItemUseOutcome::Succeeded);
    REQUIRE(rig.world.DestroyItem(spare));
    CHECK(ChargingState(rig.world, rig.charger) == ChargingSnapshot{});
    CHECK_FALSE(rig.world.NextItemEventIn().has_value());
    rig.world.ElapseTicks(std::numeric_limits<Tick>::max());
    CHECK(Energy(rig.world, rig.charger) == 2000);
    REQUIRE(Use(rig.world, rig.actor, rig.charger, "install", rig.battery).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(Use(rig.world, rig.actor, rig.charger, "start").outcome == ItemUseOutcome::Succeeded);
    REQUIRE(rig.world.DestroyItem(rig.charger));
    CHECK(rig.world.GetItem(rig.battery) == nullptr);
    CHECK(rig.world.ActiveItemCount() == 0);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Terminal charging saturates safely and mounted batteries are passive", "[world][items]") {
    ChargingRig rig;
    rig.world.ElapseTicks(std::numeric_limits<Tick>::max());
    CHECK(Energy(rig.world, rig.charger) == 1000);
    CHECK(Energy(rig.world, rig.battery) == 1000);
    CHECK(rig.world.ActiveItemCount() == 0);
    CHECK_FALSE(rig.world.NextItemEventIn().has_value());
    const auto full = Use(rig.world, rig.actor, rig.charger, "start");
    CHECK(full.outcome == ItemUseOutcome::Refused);
    CHECK(full.reason.code == "full");
    REQUIRE(Use(rig.world, rig.actor, rig.charger, "remove").outcome == ItemUseOutcome::Succeeded);
    const auto disposable = Take(rig.world.CreateItem(Type("disposable_battery"), ItemLocation::Carried(rig.actor)));
    const auto incompatible = Use(rig.world, rig.actor, rig.charger, "install", disposable);
    CHECK(incompatible.outcome == ItemUseOutcome::Refused);
    CHECK(incompatible.reason.code == "incompatible_battery");
    const auto otherCharger = Take(rig.world.CreateItem(Type("field_charger"), ItemLocation::Carried(rig.actor)));
    const auto other = Use(rig.world, rig.actor, rig.charger, "install", otherCharger);
    CHECK(other.outcome == ItemUseOutcome::Refused);
    CHECK(other.reason.code == "incompatible_battery");
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Ordinary volume and full descendant carry mass fail atomically", "[world][items]") {
    World world(TestContent());
    Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    const auto pack = Take(world.CreateItem(Type("backpack"), ItemLocation::Ground({1, 1})));
    for (int i = 0; i < 80; ++i) { Take(world.CreateItem(Type("rock"), ItemLocation::Inside(pack))); }
    CHECK(world.GetContainerUsedVolume(pack) == 20000);
    CHECK(world.GetItemMass(pack) == 41000);
    const auto rock = Take(world.CreateItem(Type("rock"), ItemLocation::Ground({1, 1})));
    CHECK(world.TransferItem(rock, ItemLocation::Inside(pack)) == eastl::unexpected(WorldError::capacity_exceeded));
    CHECK(world.GetItem(rock)->location == ItemLocation::Ground({1, 1}));
    CHECK(world.CreateItem(Type("rock"), ItemLocation::Inside(pack)) == eastl::unexpected(WorldError::capacity_exceeded));
    REQUIRE(world.SetActorCarryCapacity(actor, 40999));
    CHECK(world.TransferItem(pack, ItemLocation::Carried(actor)) == eastl::unexpected(WorldError::capacity_exceeded));
    CHECK(world.GetCarriedMass(actor) == 0);
    REQUIRE(world.SetActorCarryCapacity(actor, 41000));
    REQUIRE(world.TransferItem(pack, ItemLocation::Carried(actor)));
    CHECK(world.GetCarriedMass(actor) == 41000);
    CHECK(world.SetActorCarryCapacity(actor, 40999) == eastl::unexpected(WorldError::capacity_exceeded));
    CHECK(world.CreateItem(Type("bandage"), ItemLocation::Inside(pack)) == eastl::unexpected(WorldError::capacity_exceeded));
    const auto first = world.GetItem(pack)->contents.front();
    REQUIRE(world.TransferItem(first, ItemLocation::Inside(pack))); // Full-container same-location no-op.
    REQUIRE(world.TransferItem(first, ItemLocation::Carried(actor))); // Same-root mass is not counted twice.
    CHECK(world.GetCarriedMass(actor) == 41000);
    CHECK(world.TransferItem(rock, ItemLocation::Inside(pack)) == eastl::unexpected(WorldError::capacity_exceeded));
    CHECK(world.CreateItem(Type("bandage"), ItemLocation::Inside(pack)) == eastl::unexpected(WorldError::capacity_exceeded));
    CHECK(world.GetItem(pack)->contents.size() == 79);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Mounts do not use storage volume and battery removal checks destination mass", "[world][items]") {
    ChargingRig rig;
    CHECK(rig.world.GetContainerUsedVolume(rig.charger) == 0);
    CHECK(rig.world.GetItemMass(rig.charger) == 530);
    REQUIRE(rig.world.TransferItem(rig.pack, ItemLocation::Ground({1, 1})));
    REQUIRE(rig.world.SetActorCarryCapacity(rig.actor, 29));
    const auto before = rig.world.CaptureItemState(rig.charger);
    const auto result = Use(rig.world, rig.actor, rig.charger, "remove");
    CHECK(result.outcome == ItemUseOutcome::Refused);
    CHECK(result.reason.code == "no_capacity");
    CHECK(rig.world.CaptureItemState(rig.charger) == before);
    CHECK(rig.world.GetItem(rig.battery)->location.kind == LocationKind::item);
    CHECK(rig.world.GetCarriedMass(rig.actor) == 0);
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Tree depth is bounded before create and subtree transfer commits", "[world][items]") {
    World world(TestContent());
    Load(world);
    const auto root = Take(world.CreateItem(Type("backpack"), ItemLocation::Ground({1, 1})));
    ItemHandle deepest = root;
    for (int i = 1; i < 64; ++i) { deepest = Take(world.CreateItem(Type("backpack"), ItemLocation::Inside(deepest))); }
    CHECK(world.CreateItem(Type("rock"), ItemLocation::Inside(deepest)) == eastl::unexpected(WorldError::capacity_exceeded));
    const auto extra = Take(world.CreateItem(Type("backpack"), ItemLocation::Ground({1, 1})));
    CHECK(world.TransferItem(extra, ItemLocation::Inside(deepest)) == eastl::unexpected(WorldError::capacity_exceeded));
    CHECK(world.TransferItem(root, ItemLocation::Inside(deepest)) == eastl::unexpected(WorldError::containment_cycle));
    CHECK(world.GetItem(extra)->location == ItemLocation::Ground({1, 1}));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Door attempts honor exact deadlines input barriers and limits", "[world][items][simulation]") {
    World world(TestContent());
    Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    const auto timer = Take(world.CreateItem(Type("door_controller"), ItemLocation::Ground({1, 1})));
    REQUIRE(world.SetTile({2, 1}, {TestTerrain("closed_door"), 7}));
    const auto invalidTarget = Use(world, actor, timer, "arm", {}, 0, 1);
    CHECK(invalidTarget.outcome == ItemUseOutcome::Refused);
    CHECK(invalidTarget.reason.code == "invalid_target");
    REQUIRE(Use(world, actor, timer, "arm", {}, 1, 0).outcome == ItemUseOutcome::Succeeded);
    CHECK(world.NextItemEventIn() == eastl::optional<Tick>{300});
    CHECK(world.GetActor(actor)->remainingTicks == 0); // Item actions return costs, but never schedule actors.
    Tick tick = 0;
    std::uint32_t rng = 123;
    CHECK(AdvanceUntilInput(world, tick, rng, 1000).stop == SimulationStop::InputRequired);
    CHECK(tick == 0);
    REQUIRE(world.ScheduleActorAction(actor, 1000));
    CHECK(AdvanceUntilInput(world, tick, rng, 299).stop == SimulationStop::LimitReached);
    CHECK(tick == 299);
    CHECK(world.GetTile({2, 1})->terrain == TestTerrain("closed_door"));
    CHECK(AdvanceUntilInput(world, tick, rng, 300).stop == SimulationStop::LimitReached);
    CHECK(world.GetTile({2, 1})->terrain == TestTerrain("open_door"));
    CHECK(world.GetTile({2, 1})->damage == 7);
    CHECK(world.ActiveItemCount() == 0);
    CHECK_FALSE(world.NextItemEventIn().has_value());
    CHECK(AdvanceUntilInput(world, tick, rng, 1000).stop == SimulationStop::InputRequired);
    CHECK(tick == 1000);
    CHECK(rng == 123);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Moving armed devices cancels attempts and failed arm is atomic", "[world][items]") {
    World world(TestContent());
    Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    const auto timer = Take(world.CreateItem(Type("door_controller"), ItemLocation::Ground({1, 1})));
    REQUIRE(world.SetTile({2, 1}, {TestTerrain("closed_door"), 0}));
    REQUIRE(Use(world, actor, timer, "arm", {}, 1, 0).outcome == ItemUseOutcome::Succeeded);
    world.ElapseTicks(299);
    REQUIRE(world.TransferItem(timer, ItemLocation::Carried(actor)));
    CHECK(world.ActiveItemCount() == 0);
    const auto state = world.CaptureItemState(timer);
    const auto notDeployed = Use(world, actor, timer, "arm", {}, 1, 0);
    CHECK(notDeployed.outcome == ItemUseOutcome::Refused);
    CHECK(notDeployed.reason.code == "not_deployed");
    CHECK(world.CaptureItemState(timer) == state);
    world.ElapseTicks(std::numeric_limits<Tick>::max());
    world.ProcessItemEvents();
    CHECK(world.GetTile({2, 1})->terrain == TestTerrain("closed_door"));
    REQUIRE(world.TransferItem(timer, ItemLocation::Ground({1, 1})));
    REQUIRE(Use(world, actor, timer, "arm", {}, 1, 0).outcome == ItemUseOutcome::Succeeded);
    REQUIRE(world.SetTile({2, 1}, {TestTerrain("wall"), 0}));
    world.ElapseTicks(300);
    world.ProcessItemEvents();
    CHECK(world.GetTile({2, 1})->terrain == TestTerrain("wall"));
    CHECK(world.ActiveItemCount() == 0); // Failed due attempt still disarms.
    CHECK(world.CheckInvariants());
}

TEST_CASE("Door clocks freeze across unload and complete without actor events", "[world][items][simulation]") {
    World world(TestContent());
    auto request = Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    auto timer = Take(world.CreateItem(Type("door_controller"), ItemLocation::Ground({1, 1})));
    const PersistentId timerId = world.GetItem(timer)->id;
    REQUIRE(world.SetTile({2, 1}, {TestTerrain("closed_door"), 0}));
    REQUIRE(Use(world, actor, timer, "arm", {}, 1, 0).outcome == ItemUseOutcome::Succeeded);
    world.ElapseTicks(17);
    REQUIRE(world.DestroyActor(actor));
    REQUIRE(world.UpdateRequest(request, {{0, 0}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    CHECK_FALSE(world.NextItemEventIn().has_value());
    world.ElapseTicks(1000);
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    world.ElapseTicks(1000);
    request = Load(world);
    timer = world.FindItem(timerId);
    REQUIRE(world.GetItem(timer) != nullptr);
    CHECK(world.NextItemEventIn() == eastl::optional<Tick>{283});
    Tick tick = 0;
    std::uint32_t rng = 42;
    CHECK(AdvanceUntilInput(world, tick, rng, 282).stop == SimulationStop::LimitReached);
    CHECK(world.GetTile({2, 1})->terrain == TestTerrain("closed_door"));
    CHECK(AdvanceUntilInput(world, tick, rng, 283).stop == SimulationStop::NoEvents);
    CHECK(tick == 283);
    CHECK(world.GetTile({2, 1})->terrain == TestTerrain("open_door"));
    CHECK(AdvanceUntilInput(world, tick, rng, 283).stop == SimulationStop::NoEvents);
    CHECK(rng == 42);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Activity eligibility follows transferred ground and object roots", "[world][items]") {
    ChargingRig rig;
    const auto object = Take(rig.world.CreateObject({4, 4}));
    REQUIRE(rig.world.TransferItem(rig.pack, ItemLocation::Stored(object)));
    CHECK_FALSE(rig.world.CanAccessItem(rig.actor, rig.charger));
    rig.world.ElapseTicks(100);
    CHECK(Energy(rig.world, rig.battery) == 3);
    REQUIRE(rig.world.DestroyObject(object));
    CHECK(rig.world.GetItem(rig.pack)->location == ItemLocation::Ground({4, 4}));
    rig.world.ElapseTicks(100);
    CHECK(Energy(rig.world, rig.battery) == 6);
    CHECK_FALSE(rig.world.CanAccessItem(rig.actor, rig.charger));
    REQUIRE(rig.world.MoveActor(rig.actor, {4, 4}));
    CHECK(rig.world.CanAccessItem(rig.actor, rig.charger));
    CHECK(rig.world.CheckInvariants());
}

TEST_CASE("Context actions expose typed arguments and revalidate access and energy", "[world][items]") {
    World world(TestContent());
    Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    const auto device = Take(world.CreateItem(Type("utility_controller"), ItemLocation::Carried(actor)));
    const auto charger = Capability(world, device, "charger");
    const auto timer = Capability(world, device, "timer");
    const auto power = Capability(world, device, "power");
    const auto actions = world.QueryItemActions(actor, device);
    REQUIRE(actions.size() == 3);
    CHECK(actions[1] == ItemActionInfo{charger, "start",
        ItemActionArgument::None, ItemUseOutcome::Refused, 100, ItemActionCategory::Operate,
        {"item.action.0", "Start charging"}, {"missing_battery", {"item.refusal.3", "Install a battery first."}}});
    CHECK(actions[0] == ItemActionInfo{charger, "install",
        ItemActionArgument::Item, ItemUseOutcome::Succeeded, 100, ItemActionCategory::ManageContents,
        {"item.action.2", "Install battery"}, {}});
    CHECK(actions[2] == ItemActionInfo{timer, "arm",
        ItemActionArgument::Direction, ItemUseOutcome::Refused, 100, ItemActionCategory::Operate,
        {"item.action.4", "Arm door controller"}, {"not_deployed", {"item.refusal.8", "Deploy the controller on the ground before arming it."}}});
    const auto battery = Take(world.CreateItem(Type("rechargeable_battery"), ItemLocation::Ground({1, 1})));
    REQUIRE(Use(world, actor, device, "install", battery).outcome == ItemUseOutcome::Succeeded);
    const auto before = world.CaptureItemState(device);
    CHECK(world.SetItemEnergy(device, power, 2001) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.SetItemEnergy(device, charger, 0) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.CaptureItemState(device) == before);
    REQUIRE(world.SetItemEnergy(device, power, 0));
    const auto noPower = Use(world, actor, device, "start");
    CHECK(noPower.outcome == ItemUseOutcome::Refused);
    CHECK(noPower.reason.code == "no_power");
    REQUIRE(world.SetItemEnergy(device, power, 2000));
    const auto started = Use(world, actor, device, "start");
    CHECK(started.outcome == ItemUseOutcome::Succeeded);
    CHECK(started.baseCost == 100);
    REQUIRE(world.TransferItem(device, ItemLocation::Ground({2, 1})));
    CHECK(Use(world, actor, device, "stop").outcome == ItemUseOutcome::Inaccessible);
    CHECK(ChargingState(world, device).enabled);
    REQUIRE(world.MoveActor(actor, {2, 1}));
    CHECK(Use(world, actor, device, "stop").outcome == ItemUseOutcome::Succeeded);
    CHECK(world.GetActor(actor)->turns == 0);
    CHECK(world.GetActor(actor)->remainingTicks == 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Item action argument shapes are validated before behavior mutation", "[world][items]") {
    World world(TestContent());
    Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    const auto device = Take(world.CreateItem(Type("utility_controller"), ItemLocation::Ground({1, 1})));
    const auto battery = Take(world.CreateItem(Type("rechargeable_battery"), ItemLocation::Carried(actor)));
    const auto charger = Capability(world, device, "charger");
    const auto timer = Capability(world, device, "timer");
    REQUIRE(world.SetTile({2, 1}, {TestTerrain("closed_door"), 0}));
    const auto before = world.CaptureItemState(device);
    const ItemUseRequest malformed[] = {
        {charger, "start", battery},
        {charger, "start", {}, 1, 0},
        {charger, "start", {}, 0, 0, 1},
        {charger, "install"},
        {charger, "install", battery, 1, 0},
        {charger, "install", battery, 0, 0, -1},
        {timer, "arm", battery, 1, 0},
        {timer, "arm"},
        {timer, "arm", {}, 1, 1},
        {timer, "arm", {}, 1, 0, 1},
        {timer, "arm", {}, 0, 0, std::numeric_limits<int>::min()},
        {timer, "arm", {}, 2, 0},
        {timer, "arm", {}, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}
    };
    for (const auto& request : malformed) {
        const auto result = world.UseItem(actor, device, request);
        CHECK(result.outcome == ItemUseOutcome::InvalidAction);
        CHECK(result.baseCost == 0);
        CHECK(world.CaptureItemState(device) == before);
        CHECK(world.GetItem(battery)->location == ItemLocation::Carried(actor));
        CHECK(world.ActiveItemCount() == 0);
    }
    REQUIRE(Use(world, actor, device, "install", battery).outcome == ItemUseOutcome::Succeeded);
    const auto actions = world.QueryItemActions(actor, device);
    REQUIRE(actions.size() == 3);
    CHECK(actions[0].action == "remove");
    CHECK(actions[1].action == "start");
    CHECK(actions[2].action == "arm");
    CHECK(world.CheckInvariants());
}

TEST_CASE("A child's movement notification does not cancel its armed ancestor", "[world][items]") {
    World world(TestContent());
    Load(world);
    const auto actor = Take(world.CreateActor({1, 1}, true));
    const auto device = Take(world.CreateItem(Type("utility_controller"), ItemLocation::Ground({1, 1})));
    const auto battery = Take(world.CreateItem(Type("rechargeable_battery"), ItemLocation::Carried(actor)));
    REQUIRE(world.SetTile({2, 1}, {TestTerrain("closed_door"), 0}));
    REQUIRE(Use(world, actor, device, "arm", {}, 1, 0).outcome == ItemUseOutcome::Succeeded);
    world.ElapseTicks(17);
    const auto before = world.CaptureItemState(device);
    REQUIRE(Use(world, actor, device, "install", battery).outcome == ItemUseOutcome::Succeeded);
    CHECK(world.CaptureItemState(device) == before);
    CHECK(world.NextItemEventIn() == eastl::optional<Tick>{283});
    world.ElapseTicks(283);
    world.ProcessItemEvents();
    CHECK(world.GetTile({2, 1})->terrain == TestTerrain("open_door"));
    CHECK(world.ActiveItemCount() == 0);
    CHECK(world.CheckInvariants());
}
}
