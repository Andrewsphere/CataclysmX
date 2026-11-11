#include "core/world.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

TEST_CASE("Parked players retain inventory and timing without occupying active tiles", "[world][parking]") {
    World world(TestContent());
    const auto origin = world.RequestChunks({{0, 0}, 0}, LoadMode::active);
    REQUIRE(origin);
    const auto neighbor = world.RequestChunks({{1, 0}, 0}, LoadMode::active);
    REQUIRE(neighbor);
    REQUIRE(world.ReconcileChunks());
    const auto guestResult = world.CreateActor({0, 0}, true);
    REQUIRE(guestResult);
    const ActorHandle guest = *guestResult;
    const PersistentId id = world.GetActor(guest)->id;
    CHECK(world.FindPlayerPosition(id) == TileCoord{0, 0});
    CHECK_FALSE(world.FindPlayerPosition(0));
    const auto item = world.CreateItem(TestItem("rock"), ItemLocation::Carried(guest));
    REQUIRE(item);
    const PersistentId itemId = world.GetItem(*item)->id;
    REQUIRE(world.ScheduleActorAction(guest));
    REQUIRE(world.SetActorParked(guest, true));
    REQUIRE(world.SetActorParked(guest, true));
    const auto live = world.CreateActor({0, 0}, true);
    REQUIRE(live);
    CHECK(world.ActorAt({0, 0}) == *live);
    Vector<ActorHandle> active;
    world.CollectActiveActors(active);
    CHECK(active == Vector<ActorHandle>{*live});
    CHECK(world.SetActorParked(guest, false) == eastl::unexpected(WorldError::occupied));
    CHECK(world.ScheduleActorAction(guest) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.RecordActorTurn(guest) == eastl::unexpected(WorldError::chunk_unavailable));
    world.ElapseTicks(25);
    CHECK(world.GetActorRemainingTicks(guest) == 100);
    CHECK(world.CheckInvariants());
    REQUIRE(world.MoveActor(guest, {16, 0}));
    CHECK(world.FindPlayerPosition(id) == TileCoord{16, 0});
    REQUIRE(world.SetActorParked(guest, false));
    CHECK(world.ActorAt({16, 0}) == guest);
    REQUIRE(world.SetActorParked(guest, true));
    REQUIRE(world.ReleaseRequest(*neighbor));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.FindActor(id) == ActorHandle{});
    CHECK(world.FindPlayerPosition(id) == TileCoord{16, 0});
    CHECK(world.CheckInvariants());
    const auto reload = world.RequestChunks({{1, 0}, 0}, LoadMode::active);
    REQUIRE(reload);
    REQUIRE(world.ReconcileChunks());
    const ActorHandle restored = world.FindActor(id);
    REQUIRE(world.GetActor(restored));
    CHECK(world.GetActor(restored)->parked);
    CHECK(world.FindPlayerPosition(id) == TileCoord{16, 0});
    CHECK(world.GetActorRemainingTicks(restored) == 100);
    CHECK(world.GetActor(restored)->inventory.size() == 1);
    CHECK(world.GetItem(world.FindItem(itemId))->location == ItemLocation::Carried(restored));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Parked players freeze in air and stop blocking falling actors", "[world][parking][gravity]") {
    World world(TestContent());
    REQUIRE(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile({2, 2, 2}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({2, 2, -1}, {TestTerrain("floor"), 0}));
    const auto guest = world.CreateActor({2, 2, 2}, true);
    REQUIRE(guest);
    REQUIRE(world.ScheduleActorAction(*guest, 71));
    REQUIRE(world.SetActorParked(*guest, true));
    for (int y = 0; y <= 3; ++y) {
        REQUIRE(world.SetTile({2, 2, y}, {TestTerrain("air"), 0}));
    }
    const auto falling = world.CreateActor({2, 2, 3});
    REQUIRE(falling);
    CHECK(world.GetActor(*falling)->position == TileCoord{2, 2, -1});
    world.SettleGravity();
    CHECK(world.GetActor(*guest)->position == TileCoord{2, 2, 2});
    CHECK(world.ActorAt({2, 2, 2}) == ActorHandle{});
    REQUIRE(world.SetActorParked(*guest, false));
    CHECK(world.GetActor(*guest)->position == TileCoord{2, 2, 0});
    CHECK(world.GetActorRemainingTicks(*guest) == 71);
    REQUIRE(world.SetActorParked(*falling, true) == eastl::unexpected(WorldError::invalid_request));
    REQUIRE(world.DestroyActor(*falling));
    CHECK(world.GetActor(*guest)->position == TileCoord{2, 2, -1});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Only player actors can be parked and unparking validates terrain", "[world][parking]") {
    World world(TestContent());
    REQUIRE(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    const auto npc = world.CreateActor({1, 0});
    REQUIRE(npc);
    CHECK(world.SetActorParked(*npc, true) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.SetActorParked({}, true) == eastl::unexpected(WorldError::invalid_handle));
    const auto guest = world.CreateActor({2, 0}, true);
    REQUIRE(guest);
    REQUIRE(world.SetActorParked(*guest, true));
    REQUIRE(world.SetTile({2, 0}, {TestTerrain("wall"), 0}));
    CHECK(world.SetActorParked(*guest, false) == eastl::unexpected(WorldError::blocked));
    CHECK(world.GetActor(*guest)->parked);
}
}
