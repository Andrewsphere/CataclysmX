#include "core/world.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

TEST_CASE("Player memory changes only at observations and clears stale furniture on revisit", "[memory]") {
    World world(TestContent());
    auto request = world.RequestChunks({{0,0},0}, LoadMode::active);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    for (int x = 1; x <= 3; ++x) {
        REQUIRE(world.SetTile({x,1,0}, {TestTerrain("floor"),0}));
    }
    auto player = world.CreateActor({1,1,0}, true);
    REQUIRE(player);
    const auto id = world.GetActor(*player)->id;
    auto furniture = world.CreateObject({2,1,0}, true);
    REQUIRE(furniture);
    REQUIRE(world.SetObjectAppearance(*furniture, ObjectKind::Chair, ObjectFacing::East, true));
    CHECK(world.GetRememberedTile(id, {2,1,0}).terrain.value == 0);
    world.UpdatePlayerObservations();
    const RememberedTile observed{TestTerrain("floor"), {ObjectKind::Chair, ObjectFacing::East, true, true}};
    CHECK(world.GetRememberedTile(id, {2,1,0}) == observed);
    REQUIRE(world.SetActorParked(*player, true));
    REQUIRE(world.DestroyObject(*furniture));
    REQUIRE(world.SetTile({2,1,0}, {TestTerrain("open_door"),0}));
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(id, {2,1,0}) == observed);
    REQUIRE(world.SetActorParked(*player, false));
    // Queries and low-level changes did not learn; the explicit boundary does.
    CHECK(world.GetRememberedTile(id, {2,1,0}) == observed);
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(id, {2,1,0}) == RememberedTile{TestTerrain("open_door"), {}});
    REQUIRE(world.DestroyActor(*player));
    CHECK(world.GetPlayerMemories().empty());
}

TEST_CASE("Memories are per player, survive unloading and exclude inactive players", "[memory]") {
    World world(TestContent());
    auto request = world.RequestChunks({{0,0},0}, LoadMode::active);
    REQUIRE(request);
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    REQUIRE(world.SetTile({1,1,0}, {TestTerrain("floor"),0}));
    REQUIRE(world.SetTile({2,1,0}, {TestTerrain("floor"),0}));
    REQUIRE(world.SetTile({14,14,0}, {TestTerrain("floor"),0}));
    auto first = world.CreateActor({1,1,0}, true);
    auto second = world.CreateActor({14,14,0}, true);
    REQUIRE(first);
    REQUIRE(second);
    const auto firstId = world.GetActor(*first)->id;
    const auto secondId = world.GetActor(*second)->id;
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(firstId, {2,1,0}).terrain == TestTerrain("floor"));
    CHECK(world.GetRememberedTile(secondId, {2,1,0}).terrain.value == 0);
    const auto before = world.GetPlayerMemories();
    REQUIRE(world.UpdateRequest(*request, {{0,0},0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.SetTile({2,1,0}, {TestTerrain("open_door"),0}));
    world.UpdatePlayerObservations();
    CHECK(world.GetPlayerMemories() == before);
    REQUIRE(world.ReleaseRequest(*request));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetPlayerMemories() == before);
    CHECK(world.GetLightLevel({1,1,0}) == 0);
    REQUIRE(world.RequestChunks({{0,0},0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(firstId, {2,1,0}).terrain == TestTerrain("open_door"));
    CHECK(world.GetRememberedTile(secondId, {2,1,0}).terrain.value == 0);
}

TEST_CASE("Observations learn lower open columns but not the floor above", "[memory]") {
    World world(TestContent());
    REQUIRE(world.RequestChunks({{0,0},0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    REQUIRE(world.SetTile({5,5,0}, {TestTerrain("stairs"),0}));
    REQUIRE(world.SetTile({5,5,1}, {TestTerrain("stairs"),0}));
    auto player = world.CreateActor({5,5,0}, true);
    REQUIRE(player);
    auto object = world.CreateObject({5,5,1});
    REQUIRE(object);
    REQUIRE(world.SetObjectAppearance(*object, ObjectKind::Table));
    world.UpdatePlayerObservations();
    const auto id = world.GetActor(*player)->id;
    CHECK(world.GetRememberedTile(id, {5,5,1}) == RememberedTile{});
    REQUIRE(world.SetTile({6,5,1}, {TestTerrain("air"),0}));
    REQUIRE(world.SetTile({6,5,0}, {TestTerrain("grass"),0}));
    REQUIRE(world.MoveActor(*player, {5,5,1}));
    world.UpdatePlayerObservations();
    const auto remembered = world.GetRememberedTile(id, {5,5,1});
    CHECK(remembered.terrain == TestTerrain("stairs"));
    CHECK(remembered.object.kind == ObjectKind::Table);
    CHECK(world.GetRememberedTile(id, {6,5,0}).terrain == TestTerrain("grass"));
}

TEST_CASE("Memory observes lit targets at the full sight radius independent of observer light", "[memory]") {
    World world(TestContent());
    VisionProfile vision;
    vision.range = 12;
    REQUIRE(world.SetVisionProfile(vision));
    REQUIRE(world.RequestChunks({{0,0},0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    world.SetSkyLight(0);
    for (int x = 2; x <= 15; ++x) {
        REQUIRE(world.SetTile({x,5,0}, {TestTerrain("floor"),0}));
        REQUIRE(world.SetTile({x,5,1}, {TestTerrain("floor"),0}));
    }
    auto player = world.CreateActor({2,5,0}, true);
    REQUIRE(player);
    const auto id = world.GetActor(*player)->id;
    auto lamp = world.CreateObject({14,5,0}, true);
    REQUIRE(lamp);
    REQUIRE(world.SetObjectAppearance(*lamp, ObjectKind::Lamp));
    REQUIRE(world.SetObjectLight(*lamp, {192,1}));
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(id, {14,5,0}).object.kind == ObjectKind::Lamp);
    CHECK(world.GetRememberedTile(id, {15,5,0}).terrain.value == 0);
}

TEST_CASE("Memory snapshots own canonical values across player and chunk insertion histories", "[memory][streaming]") {
    World world(TestContent());
    REQUIRE(world.SetVisionProfile({1, 1, 1, 240, 1}));
    world.SetSkyLight(0);
    for (int x : {1, -1, 0}) {
        REQUIRE(world.RequestChunks({{x, 0}, 0}, LoadMode::active));
    }
    REQUIRE(world.ReconcileChunks());
    auto first = world.CreateActor({24, 8}, true);
    auto second = world.CreateActor({24, 10}, true);
    REQUIRE(first);
    REQUIRE(second);
    const auto firstId = world.GetActor(*first)->id;
    const auto secondId = world.GetActor(*second)->id;
    // Learn the higher-ID player first, then explore chunks out of spatial order.
    REQUIRE(world.SetActorParked(*first, true));
    world.UpdatePlayerObservations();
    REQUIRE(world.SetActorParked(*first, false));
    world.UpdatePlayerObservations();
    for (int x : {-8, 8}) {
        REQUIRE(world.MoveActor(*first, {x, 8}));
        world.UpdatePlayerObservations();
    }
    const auto snapshot = world.GetPlayerMemories();
    REQUIRE(snapshot.size() == 2);
    CHECK(snapshot[0].player == firstId);
    CHECK(snapshot[1].player == secondId);
    REQUIRE(snapshot[0].chunks.size() == 3);
    for (int x = -1; x <= 1; ++x) {
        CHECK(snapshot[0].chunks[x + 1].position == ChunkCoord{x, 0, 0});
    }
    REQUIRE(snapshot[1].chunks.size() == 1);
    CHECK(snapshot[1].chunks[0].position == ChunkCoord{1, 0, 0});
    REQUIRE(world.SetTile({8, 8}, {TestTerrain("grass"), 0}));
    world.UpdatePlayerObservations();
    CHECK(world.GetRememberedTile(firstId, {8, 8}).terrain == TestTerrain("grass"));
    CHECK(snapshot[0].chunks[1].terrain[TileIndex({8, 8})] == TestTerrain("floor"));
    REQUIRE(world.DestroyActor(*first));
    CHECK(world.GetRememberedTile(firstId, {8, 8}) == RememberedTile{});
    const auto remaining = world.GetPlayerMemories();
    REQUIRE(remaining.size() == 1);
    CHECK(remaining[0] == snapshot[1]);
    CHECK(world.CheckInvariants());
}
}
