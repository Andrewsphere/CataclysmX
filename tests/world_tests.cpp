#include "core/world.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <EASTL/utility.h>
#include <EASTL/sort.h>

namespace {

using namespace cataclysmx;

Vector<ActorHandle> ActiveActors(const World& world) {
    Vector<ActorHandle> result;
    world.CollectActiveActors(result);
    eastl::sort(result.begin(), result.end(), [&world](auto a, auto b) { return world.GetActor(a)->id < world.GetActor(b)->id; });
    return result;
}
Vector<ObjectHandle> ActiveObjects(const World& world) {
    Vector<ObjectHandle> result;
    world.CollectActiveObjects(result);
    eastl::sort(result.begin(), result.end(), [&world](auto a, auto b) { return world.GetObject(a)->id < world.GetObject(b)->id; });
    return result;
}

template<class T>
T take(WorldResult<T> result) {
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

LoadRequest load(World& world, ChunkRegion region = {{0, 0}, 0}, LoadMode mode = LoadMode::active) {
    const LoadRequest request = take(world.RequestChunks(region, mode));
    world.ReconcileChunks();
    REQUIRE(world.CheckInvariants());
    return request;
}

Vector<PersistentId> actor_ids(const World& world) {
    Vector<PersistentId> result;
    for (ActorHandle handle : ActiveActors(world)) {
        result.push_back(world.GetActor(handle)->id);
    }
    return result;
}

Vector<PersistentId> object_ids(const World& world) {
    Vector<PersistentId> result;
    for (ObjectHandle handle : ActiveObjects(world)) {
        result.push_back(world.GetObject(handle)->id);
    }
    return result;
}

TEST_CASE("World coordinates use floor division and bounded positions", "[world][coordinates]") {
    CHECK(ChunkOf({0, 0}) == ChunkCoord{0, 0});
    CHECK(ChunkOf({15, 16}) == ChunkCoord{0, 1});
    CHECK(ChunkOf({-1, -16}) == ChunkCoord{-1, -1});
    CHECK(ChunkOf({-17, 31}) == ChunkCoord{-2, 1});
    CHECK(ChunkOf({-17, 31, -10}) == ChunkCoord{-2, 1, -10});
    CHECK(TileIndex({-17, 31, 10}) == TileIndex({-17, 31, -10}));
    CHECK(ValidPosition({0, 0, world_min_y}));
    CHECK(ValidPosition({0, 0, world_max_y}));
    CHECK_FALSE(ValidPosition({0, 0, world_min_y - 1}));
    CHECK_FALSE(ValidPosition({0, 0, world_max_y + 1}));
    CHECK(TileIndex({-1, -1}) == 255);
    CHECK(TileIndex({-16, -16}) == 0);
    CHECK(TileIndex({-17, 16}) == 15);
    CHECK(TileIndex({INT_MIN, INT_MAX}) == 240);
    CHECK(ValidPosition({-world_coordinate_limit, world_coordinate_limit}));
    CHECK_FALSE(ValidPosition({world_coordinate_limit + 1, 0}));
    CHECK_FALSE(ValidPosition({0, -world_coordinate_limit - 1}));
    CHECK_FALSE(ValidPosition({INT_MIN, INT_MAX}));
    CHECK(Passable(TestContent(), TestTerrain("floor")));
    CHECK(Passable(TestContent(), TestTerrain("open_door")));
    CHECK_FALSE(Passable(TestContent(), TestTerrain("wall")));
    CHECK_FALSE(Passable(TestContent(), TestTerrain("closed_door")));

    World world(TestContent());
    load(world, {{-1, -1}, 0});
    REQUIRE(world.SetTile({-1, -1}, {TestTerrain("open_door"), 7}));
    CHECK(world.GetChunk({-1, -1})->tiles[255] == Tile{TestTerrain("open_door"), 7});
    CHECK(world.GetTile({0, 0}) == nullptr);
    CHECK(world.SetTile({0, 0}, {}) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.CreateActor({INT_MAX, 0}) == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.CreateObject({0, INT_MIN}) == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.CreateItem(TestItem("rock"), ItemLocation::Ground({world_coordinate_limit + 1, 0}))
        == eastl::unexpected(WorldError::invalid_position));

    load(world, {{62'500, -62'500}, 0});
    REQUIRE(world.GetTile({world_coordinate_limit, -world_coordinate_limit}) != nullptr);
    CHECK(world.GetTile({world_coordinate_limit + 1, -world_coordinate_limit}) == nullptr);
    REQUIRE(world.SetTile({world_coordinate_limit, -world_coordinate_limit}, {TestTerrain("floor"), 0}));
    take(world.CreateActor({world_coordinate_limit, -world_coordinate_limit}));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Chunk requests combine demand and reject invalid updates atomically", "[world][streaming]") {
    World world(TestContent());
    CHECK(world.RequestChunks({{0, 0}, -1}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestChunks({{0, 0}, 9}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestChunks({{INT_MAX, INT_MIN}, 1}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestChunks({{62'500, 0}, 1}, LoadMode::resident)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestChunks({{0, 0}, 0}, static_cast<LoadMode>(99))
        == eastl::unexpected(WorldError::invalid_request));
    const LoadRequest resident = take(world.RequestChunks({{0, 0}, 1}, LoadMode::resident));
    const LoadRequest active = take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    const LoadRequest overlap = take(world.RequestChunks({{1, 0}, 0}, LoadMode::active));
    CHECK(world.LoadedChunks().empty());
    world.ReconcileChunks();
    REQUIRE(world.LoadedChunks().size() == 9 * world_layer_count);
    for (int x = -1; x <= 1; ++x) {
        for (int z = -1; z <= 1; ++z) {
            for (int y = world_min_y; y <= world_max_y; ++y) { CHECK(world.GetChunk({x, z, y})); }
        }
    }
    CHECK(world.IsActive({0, 0}));
    CHECK(world.IsActive({16, 0}));
    CHECK_FALSE(world.IsActive({-1, -1}));
    CHECK(world.UpdateRequest(resident, {{0, 0}, 9}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.UpdateRequest(active, {{0, 0}, 0}, static_cast<LoadMode>(99))
        == eastl::unexpected(WorldError::invalid_request));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks().size() == 9 * world_layer_count);
    REQUIRE(world.ReleaseRequest(active));
    world.ReconcileChunks();
    CHECK_FALSE(world.IsActive({0, 0}));
    CHECK(world.GetChunk({0, 0}) != nullptr);
    CHECK(world.ReleaseRequest(active) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.UpdateRequest(active, {{0, 0}, 0}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.ReleaseRequest({}) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.UpdateRequest({UINT64_MAX}, {{0, 0}, 0}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_request));
    REQUIRE(world.UpdateRequest(overlap, {{-2, 0}, 0}, LoadMode::resident));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks().size() == 10 * world_layer_count);
    CHECK(world.GetChunk({-2, 0, world_min_y}));
    CHECK_FALSE(world.IsActive({16, 0}));
    REQUIRE(world.ReleaseRequest(resident));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks().size() == world_layer_count);
    for (int y = world_min_y; y <= world_max_y; ++y) {
        CHECK(world.LoadedChunks()[y - world_min_y] == ChunkCoord{-2, 0, y});
    }
    CHECK(world.ArchivedChunkCount() == 9 * world_layer_count);
    REQUIRE(world.ReleaseRequest(overlap));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks().empty());
    CHECK(world.ArchivedChunkCount() == 10 * world_layer_count);
    CHECK(world.CheckInvariants());

    // Exercise SlotMap request recycling, not just never-reused indices.
    for (int index = 0; index < 140; ++index) {
        const auto request = take(world.RequestChunks({{0, 0}, 0}, LoadMode::resident));
        CHECK(request != active);
        REQUIRE(world.ReleaseRequest(request));
    }
    CHECK(world.ReleaseRequest(active) == eastl::unexpected(WorldError::invalid_request));
}

TEST_CASE("Only active chunks accrue turns and enabled object updates", "[world][activity]") {
    World world(TestContent());
    const auto request = load(world, {{0, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.CreateActor({0, 0}, true));
    const auto object = take(world.CreateObject({1, 0}, true));
    CHECK(ActiveActors(world).empty());
    CHECK(ActiveObjects(world).empty());
    CHECK(world.RecordActorTurn(actor) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.RecordObjectUpdate(object) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.GetActor(actor)->turns == 0);
    CHECK(world.GetObject(object)->updates == 0);
    REQUIRE(world.UpdateRequest(request, {{0, 0}, 0}, LoadMode::active));
    world.ReconcileChunks();
    CHECK(ActiveActors(world) == Vector<ActorHandle>{actor});
    CHECK(ActiveObjects(world) == Vector<ObjectHandle>{object});
    REQUIRE(world.RecordActorTurn(actor));
    REQUIRE(world.RecordObjectUpdate(object));
    REQUIRE(world.SetObjectEnabled(object, false));
    REQUIRE(world.RecordObjectUpdate(object));
    CHECK(world.GetActor(actor)->turns == 1);
    CHECK(world.GetObject(object)->updates == 1);
    REQUIRE(world.UpdateRequest(request, {{0, 0}, 0}, LoadMode::resident));
    world.ReconcileChunks();
    CHECK(world.RecordObjectUpdate(object) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.RecordActorTurn({}) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.RecordObjectUpdate({UINT64_MAX}) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Activity-only reconciliation preserves archives handles and relative timers", "[world][streaming][activity]") {
    World world(TestContent());
    const auto archived = load(world, {{3, 0}, 0});
    REQUIRE(world.SetTile({48, 0}, {TestTerrain("floor"), 73}));
    REQUIRE(world.ReleaseRequest(archived));
    REQUIRE(world.ReconcileChunks());
    const auto request = load(world);
    const auto actor = take(world.CreateActor({0, 0}));
    const auto object = take(world.CreateObject({1, 0}, true));
    const auto item = take(world.CreateItem(TestItem("rock"), ItemLocation::Carried(actor)));
    REQUIRE(world.ScheduleActorAction(actor));
    world.ElapseTicks(23);
    const auto actorDelay = world.GetActor(actor)->remainingTicks;
    const auto objectDelay = world.GetObject(object)->remainingTicks;
    const auto loaded = world.LoadedChunks();
    const auto revision = world.GetRevision();
    const auto mapRevision = world.GetMapRevision();
    const auto lightInput = world.GetLightingInputRevision({0, 0});
    world.ResetPerceptionStats();
    REQUIRE(world.UpdateRequest(request, {{0, 0}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetRevision() == revision + world_layer_count);
    CHECK(world.GetMapRevision() == mapRevision + world_layer_count);
    CHECK(world.GetLightingInputRevision({0, 0}) == lightInput);
    CHECK(world.LoadedChunks() == loaded);
    CHECK(world.ArchivedChunkCount() == world_layer_count);
    REQUIRE(world.GetActor(actor));
    REQUIRE(world.GetObject(object));
    REQUIRE(world.GetItem(item));
    world.ElapseTicks(1000);
    CHECK(world.GetActor(actor)->remainingTicks == actorDelay);
    CHECK(world.GetObject(object)->remainingTicks == objectDelay);
    REQUIRE(world.UpdateRequest(request, {{0, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetActor(actor)->remainingTicks == actorDelay);
    CHECK(world.GetObject(object)->remainingTicks == objectDelay);
    CHECK(ActiveActors(world) == Vector<ActorHandle>{actor});
    CHECK(world.GetPerceptionStats().columnsLoaded == 0);
    CHECK(world.GetPerceptionStats().columnsUnloaded == 0);
    const auto committed = world.GetRevision();
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetRevision() == committed);
    load(world, {{3, 0}, 0});
    CHECK(*world.GetTile({48, 0}) == Tile{TestTerrain("floor"), 73});
    CHECK(world.GetActor(actor));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Tile circles use integer tile tangencies and ignore elevation", "[world][streaming][circle]") {
    World world(TestContent());
    CHECK(world.RequestCircle({{0, 0}, -1}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestCircle({{0, 0}, maxCircleResidencyRadius + 1}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestCircle({{INT_MIN, INT_MAX}, 0}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestCircle({{world_coordinate_limit, 0}, 1}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestCircle({{0, -world_coordinate_limit}, 1}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.RequestCircle({{0, 0}, 0}, static_cast<LoadMode>(99))
        == eastl::unexpected(WorldError::invalid_request));
    const auto maximum = take(world.RequestCircle(
        {{world_coordinate_limit - maxCircleResidencyRadius, 0}, maxCircleResidencyRadius}, LoadMode::resident));
    REQUIRE(world.ReleaseRequest(maximum));
    const auto edge = take(world.RequestCircle({{world_coordinate_limit, -world_coordinate_limit}, 0}, LoadMode::resident));
    REQUIRE(world.ReleaseRequest(edge));
    const auto request = take(world.RequestCircle({{-1, -1, INT_MIN}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    REQUIRE(world.LoadedChunks().size() == world_layer_count);
    CHECK(world.LoadedChunks().front() == ChunkCoord{-1, -1, world_min_y});
    CHECK(world.LoadedChunks().back() == ChunkCoord{-1, -1, world_max_y});
    CHECK_FALSE(world.IsActive({-1, -1}));
    REQUIRE(world.UpdateCircleRequest(request, {{0, 0, INT_MAX}, 1}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.LoadedChunks().size() == 3 * world_layer_count);
    CHECK(world.GetChunk({-1, 0}));
    CHECK(world.GetChunk({0, -1}));
    CHECK(world.GetChunk({0, 0}));
    CHECK_FALSE(world.GetChunk({-1, -1})); // Nearest tile is (-1,-1), outside radius one.
    REQUIRE(world.UpdateCircleRequest(request, {{0, 0}, 16}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetChunk({1, 0})); // Tile (16,0) is exactly tangent.
    CHECK(world.GetChunk({0, 1}));
    CHECK_FALSE(world.GetChunk({1, 1}));
    CHECK(world.LoadedChunks().size() == 6 * world_layer_count);
    CHECK(world.CheckInvariants());

    const auto before = world.LoadedChunks();
    CHECK(world.UpdateCircleRequest(request, {{0, 0}, 129}, LoadMode::resident)
        == eastl::unexpected(WorldError::invalid_region));
    CHECK(world.UpdateCircleRequest(request, {{0, 0}, 0}, static_cast<LoadMode>(99))
        == eastl::unexpected(WorldError::invalid_request));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.LoadedChunks() == before);
    CHECK(world.IsActive({0, 0}));
    REQUIRE(world.ReleaseRequest(request));
    CHECK(world.UpdateCircleRequest(request, {{0, 0}, 0}, LoadMode::active)
        == eastl::unexpected(WorldError::invalid_request));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.LoadedChunks().empty());
    CHECK(world.CheckInvariants());
}

TEST_CASE("Circle footprints change inside a chunk and combine with square activity", "[world][streaming][circle]") {
    World world(TestContent());
    const auto circle = take(world.RequestCircle({{7, 7}, 8}, LoadMode::resident));
    const auto active = take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
    const auto corner = take(world.RequestChunks({{1, 1}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetChunk({-1, 0}));
    CHECK_FALSE(world.GetChunk({1, 0}));
    const auto actor = take(world.CreateActor({1, 1}));
    const auto id = world.GetActor(actor)->id;
    for (int y = world_min_y; y <= world_max_y; ++y) {
        CHECK(world.GetChunk({0, 0, y})->active);
        CHECK(world.GetChunk({1, 1, y})->active); // Full active union, outside the circle.
        CHECK_FALSE(world.GetChunk({-1, 0, y})->active);
    }
    REQUIRE(world.UpdateCircleRequest(circle, {{8, 8}, 8}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    CHECK_FALSE(world.GetChunk({-1, 0}));
    CHECK(world.GetChunk({1, 0}));
    CHECK(world.FindActor(id) == actor);
    REQUIRE(world.ReleaseRequest(active));
    REQUIRE(world.ReconcileChunks());
    CHECK_FALSE(world.IsActive({1, 1}));
    CHECK(world.GetActor(actor));
    REQUIRE(world.ReleaseRequest(corner));
    REQUIRE(world.ReconcileChunks());
    CHECK_FALSE(world.GetChunk({1, 1}));
    CHECK(world.CheckInvariants());

    // Both update APIs replace a request's shape, preserving the handle.
    REQUIRE(world.UpdateRequest(circle, {{-2, -2}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.LoadedChunks().size() == world_layer_count);
    CHECK_FALSE(world.GetActor(actor));
    REQUIRE(world.UpdateCircleRequest(circle, {{1, 1}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.FindActor(id) != actor);
    REQUIRE(world.GetActor(world.FindActor(id)));
    CHECK(ActiveActors(world) == Vector<ActorHandle>{world.FindActor(id)});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Column erasure preserves grouped traversal and retained entity keys", "[world][streaming][determinism]") {
    World world(TestContent());
    Vector<LoadRequest> requests;
    for (const int x : {3, -2, 1, 0, -1, 2}) {
        requests.push_back(take(world.RequestChunks({{x, 0}, 0}, LoadMode::active)));
    }
    REQUIRE(world.ReconcileChunks());
    const auto checkGroups = [&]() {
        const auto chunks = world.LoadedChunks();
        REQUIRE(chunks.size() % world_layer_count == 0);
        for (std::size_t i = 0; i < chunks.size(); i += world_layer_count) {
            for (int y = world_min_y; y <= world_max_y; ++y) {
                CHECK(chunks[i + y - world_min_y] == ChunkCoord{chunks[i].x, chunks[i].z, y});
            }
            for (std::size_t j = 0; j < i; j += world_layer_count) {
                CHECK((chunks[i].x != chunks[j].x || chunks[i].z != chunks[j].z));
            }
        }
    };
    checkGroups();
    const auto actor = take(world.CreateActor({1, 1}));
    const auto id = world.GetActor(actor)->id;
    // Erases exercise relocation of the map's final value, rather than iterator stability.
    for (const int index : {2, 0, 1, 4, 5}) {
        REQUIRE(world.ReleaseRequest(requests[index]));
        REQUIRE(world.ReconcileChunks());
        checkGroups();
        CHECK(world.FindActor(id) == actor);
        CHECK(world.CheckInvariants());
    }
    REQUIRE(world.LoadedChunks().size() == world_layer_count);
    for (int y = world_min_y; y <= world_max_y; ++y) {
        CHECK(world.LoadedChunks()[y - world_min_y] == ChunkCoord{0, 0, y});
    }
}

TEST_CASE("Request insertion and recycled slots produce the same demand", "[world][streaming][determinism]") {
    World first(TestContent(), 91);
    World second(TestContent(), 91);
    const auto temporary = take(second.RequestChunks({{20, 20}, 0}, LoadMode::resident));
    REQUIRE(second.ReleaseRequest(temporary));
    for (const int x : {-2, 0, 3}) {
        take(first.RequestChunks({{x, 0}, 0}, x == 0 ? LoadMode::active : LoadMode::resident));
    }
    for (const int x : {3, 0, -2}) {
        take(second.RequestChunks({{x, 0}, 0}, x == 0 ? LoadMode::active : LoadMode::resident));
    }
    REQUIRE(first.ReconcileChunks());
    REQUIRE(second.ReconcileChunks());
    REQUIRE(first.LoadedChunks().size() == 3 * world_layer_count);
    REQUIRE(second.LoadedChunks().size() == first.LoadedChunks().size());
    for (const auto coord : first.LoadedChunks()) {
        const auto* other = second.GetChunk(coord);
        REQUIRE(other);
        CHECK(first.GetChunk(coord)->tiles == other->tiles);
        CHECK(first.GetChunk(coord)->active == other->active);
    }
    CHECK(first.CheckInvariants());
    CHECK(second.CheckInvariants());
}

TEST_CASE("Lighting refreshes source inputs after an unqueried same-coordinate reload", "[world][lighting][streaming]") {
    World world(TestContent());
    const auto request = load(world);
    world.SetSkyLight(0);
    const auto lamp = take(world.CreateObject({1, 1}, true));
    REQUIRE(world.SetObjectLight(lamp, {192, 4}));
    const auto lampId = world.GetObject(lamp)->id;
    CHECK(world.GetLightLevel({1, 1}) == 192);
    REQUIRE(world.SetObjectEnabled(lamp, false));
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    load(world);
    const auto fresh = world.FindObject(lampId);
    CHECK(fresh != lamp);
    REQUIRE(world.GetObject(fresh));
    CHECK_FALSE(world.GetObject(fresh)->enabled);
    CHECK(world.GetLightLevel({1, 1}) == 0);
    const auto levels = world.GetChunkLightLevels({0, 0});
    REQUIRE(levels.size() == chunk_size * chunk_size);
    CHECK(levels[TileIndex({1, 1})] == 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Perception ensures lazy neighbor output and invalidates before reusing its filter", "[world][lighting][perception]") {
    World world(TestContent());
    load(world, {{0, 0}, 1});
    REQUIRE(world.SetVisionProfile({8, 6, 1, 240, 3}));
    world.SetSkyLight(0);
    const auto observer = take(world.CreateActor({15, 7}, true));
    const auto lamp = take(world.CreateObject({18, 7}, true));
    REQUIRE(world.SetObjectLight(lamp, {192, 4}));
    CHECK(world.GetLightLevel({15, 7}) > 0); // Prepares inputs; only ensures observer's chunk.
    world.ResetPerceptionStats();
    CHECK(world.CanPlayerSee(observer, {18, 7})); // Requires output in the neighboring chunk.
    CHECK(world.GetPerceptionStats().filterRebuilds == 1);
    CHECK(world.CanPlayerSee(observer, {18, 7}));
    CHECK(world.GetPerceptionStats().filterRebuilds == 1);
    REQUIRE(world.SetObjectEnabled(lamp, false));
    CHECK_FALSE(world.CanPlayerSee(observer, {18, 7}));
    CHECK(world.GetPerceptionStats().filterRebuilds == 2);
    CHECK(world.GetPerceptionStats().lightingRebuilds == 1);
    CHECK(world.CheckInvariants());
}

TEST_CASE("World schedules committed actor delays and freezes inactive timers", "[world][timing]") {
    World world(TestContent());
    const auto request = load(world);
    const auto actor = take(world.CreateActor({0, 0}));
    const auto object = take(world.CreateObject({1, 0}, true));
    const auto actorId = world.GetActor(actor)->id;
    const auto objectId = world.GetObject(object)->id;
    CHECK(world.GetActor(actor)->speed == 100);
    CHECK(world.GetActor(actor)->remainingTicks == 0);
    CHECK(world.GetObject(object)->updateInterval == 100);
    CHECK(world.GetObject(object)->remainingTicks == 100);
    CHECK(world.SetActorSpeed({}, 100) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.ScheduleActorAction({}, 100) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.SetObjectUpdateInterval({}, 100) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.SetActorSpeed(actor, 0) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.SetObjectUpdateInterval(object, 0) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.ScheduleActorAction(actor, 0) == eastl::unexpected(WorldError::invalid_request));
    REQUIRE(world.SetActorSpeed(actor, 33));
    REQUIRE(world.ScheduleActorAction(actor, 101));
    CHECK(world.GetActor(actor)->remainingTicks == 34); // Ceiling, not truncation.
    CHECK(world.GetActor(actor)->turns == 1);
    CHECK(world.ScheduleActorAction(actor) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.GetActor(actor)->turns == 1);
    REQUIRE(world.SetActorSpeed(actor, 200));
    CHECK(world.GetActor(actor)->remainingTicks == 34); // Already committed.
    REQUIRE(world.SetObjectUpdateInterval(object, 75));
    world.ElapseTicks(20);
    CHECK(world.GetActor(actor)->remainingTicks == 14);
    CHECK(world.GetObject(object)->remainingTicks == 55);
    REQUIRE(world.SetObjectEnabled(object, false));
    REQUIRE(world.RecordObjectUpdate(object));
    world.ElapseTicks(0);
    CHECK(world.GetActor(actor)->remainingTicks == 14);
    world.ElapseTicks(4);
    CHECK(world.GetActor(actor)->remainingTicks == 10);
    CHECK(world.GetObject(object)->remainingTicks == 55);
    CHECK(world.GetObject(object)->updates == 0);
    REQUIRE(world.UpdateRequest(request, {{0, 0}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    world.ElapseTicks(UINT64_MAX);
    CHECK(world.GetActor(actor)->remainingTicks == 10);
    CHECK(world.GetObject(object)->remainingTicks == 55);
    CHECK(world.ScheduleActorAction(actor) == eastl::unexpected(WorldError::chunk_unavailable));
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    world.ElapseTicks(UINT64_MAX);
    load(world);
    const auto freshActor = world.FindActor(actorId);
    const auto freshObject = world.FindObject(objectId);
    CHECK(world.GetActor(freshActor)->speed == 200);
    CHECK(world.GetActor(freshActor)->remainingTicks == 10);
    CHECK(world.GetObject(freshObject)->updateInterval == 75);
    CHECK(world.GetObject(freshObject)->remainingTicks == 55);
    REQUIRE(world.SetObjectEnabled(freshObject, true));
    world.ElapseTicks(UINT64_MAX);
    CHECK(world.GetActor(freshActor)->remainingTicks == 0);
    CHECK(world.GetObject(freshObject)->remainingTicks == 0);
    REQUIRE(world.RecordObjectUpdate(freshObject));
    CHECK(world.GetObject(freshObject)->updates == 1);
    CHECK(world.GetObject(freshObject)->remainingTicks == 75);
    world.ElapseTicks(7);
    REQUIRE(world.SetObjectUpdateInterval(freshObject, 19));
    CHECK(world.GetObject(freshObject)->remainingTicks == 19); // Explicit restart.
    REQUIRE(world.SetActorSpeed(freshActor, UINT32_MAX));
    REQUIRE(world.ScheduleActorAction(freshActor, UINT32_MAX));
    CHECK(world.GetActor(freshActor)->remainingTicks == ActionTicks(UINT32_MAX, UINT32_MAX));
    CHECK(world.GetActor(freshActor)->remainingTicks > UINT32_MAX);
    CHECK(world.GetActor(freshActor)->turns == 2);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Actor movement keeps handles and inventory across chunk boundaries", "[world][actors]") {
    World world(TestContent());
    const auto origin = load(world);
    load(world, {{1, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.CreateActor({15, 1}, true));
    const auto backpack = take(world.CreateItem(TestItem("backpack"), ItemLocation::Carried(actor)));
    const auto rock = take(world.CreateItem(TestItem("rock"), ItemLocation::Inside(backpack)));
    const PersistentId id = world.GetActor(actor)->id;
    REQUIRE(world.MoveActor(actor, {16, 1}));
    CHECK(world.GetActor(actor)->id == id);
    CHECK(world.ActorAt({15, 1}) == ActorHandle{});
    CHECK(world.ActorAt({16, 1}) == actor);
    CHECK(world.GetChunk({0, 0})->actors.empty());
    CHECK(world.GetChunk({1, 0})->actors == Vector<ActorHandle>{actor});
    CHECK(world.GetItem(backpack)->location == ItemLocation::Carried(actor));
    CHECK(world.GetItem(rock)->location == ItemLocation::Inside(backpack));
    CHECK(ActiveActors(world).empty());
    REQUIRE(world.MoveActor(actor, {16, 1}));
    const auto other = take(world.CreateActor({17, 1}));
    CHECK(world.MoveActor(actor, {17, 1}) == eastl::unexpected(WorldError::occupied));
    CHECK(world.CreateActor({17, 1}) == eastl::unexpected(WorldError::occupied));
    REQUIRE(world.SetTile({18, 1}, {TestTerrain("closed_door"), 0}));
    CHECK(world.MoveActor(actor, {18, 1}) == eastl::unexpected(WorldError::blocked));
    CHECK(world.MoveActor(actor, {32, 1}) == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.MoveActor(actor, {INT_MAX, 1}) == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.MoveActor({}, {16, 1}) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.GetActor(actor)->position == TileCoord{16, 1});
    CHECK(world.GetActor(other)->position == TileCoord{17, 1});
    REQUIRE(world.ReleaseRequest(origin));
    world.ReconcileChunks();
    REQUIRE(world.GetActor(actor) != nullptr);
    REQUIRE(world.GetItem(rock) != nullptr);
    CHECK(world.FindActor(id) == actor);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Borrowed item ranges filter ground roots and handle unavailable positions", "[world][items][query]") {
    World world(TestContent());
    load(world);
    take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({2, 1})));
    const auto bag = take(world.CreateItem(TestItem("backpack"), ItemLocation::Ground({1, 1})));
    take(world.CreateItem(TestItem("rock"), ItemLocation::Inside(bag)));
    take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({1, 2})));
    const auto loose = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({1, 1})));
    take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({3, 1})));
    const auto actor = take(world.CreateActor({1, 1}));
    take(world.CreateItem(TestItem("rock"), ItemLocation::Carried(actor)));
    const auto object = take(world.CreateObject({1, 1}));
    take(world.CreateItem(TestItem("rock"), ItemLocation::Stored(object)));
    REQUIRE(world.SetTile({1, 1, 1}, {TestTerrain("floor"), 0}));
    const auto above = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({1, 1, 1})));

    const auto range = world.ItemsAt({1, 1});
    CHECK_FALSE(range.Empty());
    CHECK(world.CountItemsAt({1, 1}) == 2);
    // Repeated traversal and iterator copies do not consume or own the query.
    for (int pass = 0; pass < 2; ++pass) {
        int bags = 0;
        int rocks = 0;
        for (ItemHandle handle : range) {
            if (handle == bag) { ++bags; }
            if (handle == loose) { ++rocks; }
        }
        CHECK(bags == 1);
        CHECK(rocks == 1);
    }
    auto iterator = range.begin();
    const auto copy = iterator;
    CHECK(iterator++ == copy);
    CHECK(iterator != copy);
    REQUIRE(iterator != range.end());
    CHECK(*iterator != *copy);
    CHECK(++iterator == range.end());
    REQUIRE(world.CountItemsAt({1, 1, 1}) == 1);
    CHECK(*world.ItemsAt({1, 1, 1}).begin() == above);

    for (TileCoord position : {TileCoord{4, 4}, TileCoord{32, 0}, TileCoord{INT_MIN, INT_MAX},
             TileCoord{0, 0, world_min_y - 1}, TileCoord{0, 0, world_max_y + 1}}) {
        const auto empty = world.ItemsAt(position);
        CHECK(empty.Empty());
        CHECK(empty.begin() == empty.end());
        CHECK(world.CountItemsAt(position) == 0);
    }
    CHECK(world.CheckInvariants());
}

TEST_CASE("Item query snapshots retain capacity and survive mutations as handle values", "[world][items][query]") {
    World world(TestContent());
    const auto request = load(world);
    const auto first = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({1, 1})));
    const auto second = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({1, 1})));
    Vector<ItemHandle> snapshot{ItemHandle{}};
    snapshot.reserve(16);
    const auto capacity = snapshot.capacity();
    world.CollectItemsAt({1, 1}, snapshot);
    REQUIRE(snapshot.size() == 2);
    CHECK(snapshot.capacity() == capacity);
    CHECK(((snapshot[0] == first && snapshot[1] == second)
        || (snapshot[0] == second && snapshot[1] == first)));
    const auto original = snapshot;

    REQUIRE(world.TransferItem(first, ItemLocation::Ground({2, 1})));
    CHECK(snapshot == original);
    CHECK(world.CountItemsAt({1, 1}) == 1);
    // Mutate the chunk index while traversing the explicit snapshot.
    for (ItemHandle handle : snapshot) { REQUIRE(world.DestroyItem(handle)); }
    CHECK(snapshot == original);
    CHECK(world.GetItem(first) == nullptr);
    CHECK(world.GetItem(second) == nullptr);
    CHECK(world.ItemsAt({1, 1}).Empty());

    const auto fresh = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({1, 1})));
    world.CollectItemsAt({1, 1}, snapshot);
    REQUIRE(snapshot == Vector<ItemHandle>{fresh});
    CHECK(snapshot.capacity() == capacity);
    REQUIRE(world.ReleaseRequest(request));
    REQUIRE(world.ReconcileChunks());
    CHECK(snapshot == Vector<ItemHandle>{fresh});
    CHECK(world.GetItem(snapshot[0]) == nullptr);
    CHECK(world.ItemsAt({1, 1}).Empty());

    for (TileCoord position : {TileCoord{1, 1}, TileCoord{INT_MIN, 0}}) {
        snapshot.push_back(fresh);
        world.CollectItemsAt(position, snapshot);
        CHECK(snapshot.empty());
        CHECK(snapshot.capacity() == capacity);
    }
    CHECK(world.CheckInvariants());
}

TEST_CASE("Item transfers validate destinations and cycles before changing ownership", "[world][items]") {
    World world(TestContent());
    load(world);
    load(world, {{1, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.CreateActor({1, 1}));
    const auto object = take(world.CreateObject({2, 1}));
    const auto root = take(world.CreateItem(TestItem("backpack"), ItemLocation::Ground({0, 0})));
    const auto nested = take(world.CreateItem(TestItem("backpack"), ItemLocation::Inside(root)));
    const auto rock = take(world.CreateItem(TestItem("rock"), ItemLocation::Inside(nested)));
    CHECK(world.TransferItem(root, ItemLocation::Inside(root))
        == eastl::unexpected(WorldError::containment_cycle));
    CHECK(world.TransferItem(root, ItemLocation::Inside(nested))
        == eastl::unexpected(WorldError::containment_cycle));
    CHECK(world.TransferItem(nested, ItemLocation::Inside(rock))
        == eastl::unexpected(WorldError::not_container));
    CHECK(world.CreateItem(TestItem("bandage"), ItemLocation::Inside(rock))
        == eastl::unexpected(WorldError::not_container));
    CHECK(world.TransferItem(root, ItemLocation::Carried({UINT64_MAX}))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(root, ItemLocation::Stored({}))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(root, ItemLocation::Inside({}))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(root, ItemLocation::Ground({32, 0}))
        == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.TransferItem(root, ItemLocation::Ground({INT_MIN, 0}))
        == eastl::unexpected(WorldError::invalid_position));
    CHECK(world.TransferItem({}, ItemLocation::Carried(actor))
        == eastl::unexpected(WorldError::invalid_handle));
    REQUIRE(world.CountItemsAt({0, 0}) == 1);
    CHECK(*world.ItemsAt({0, 0}).begin() == root);
    CHECK(world.GetItem(root)->contents == Vector<ItemHandle>{nested});
    CHECK(world.GetItem(nested)->contents == Vector<ItemHandle>{rock});
    CHECK(world.GetItem(root)->location == ItemLocation::Ground({0, 0}));
    CHECK(world.GetActor(actor)->inventory.empty());
    CHECK(world.GetObject(object)->contents.empty());
    REQUIRE(world.TransferItem(root, ItemLocation::Carried(actor)));
    CHECK(world.ItemsAt({0, 0}).Empty());
    CHECK(world.GetActor(actor)->inventory == Vector<ItemHandle>{root});
    REQUIRE(world.TransferItem(root, ItemLocation::Carried(actor)));
    CHECK(world.GetActor(actor)->inventory.size() == 1);
    REQUIRE(world.TransferItem(root, ItemLocation::Stored(object)));
    CHECK(world.GetActor(actor)->inventory.empty());
    CHECK(world.GetObject(object)->contents == Vector<ItemHandle>{root});
    REQUIRE(world.TransferItem(root, ItemLocation::Ground({16, 2})));
    CHECK(world.GetObject(object)->contents.empty());
    CHECK(world.GetChunk({0, 0})->ground_items.empty());
    CHECK(world.GetChunk({1, 0})->ground_items == Vector<ItemHandle>{root});
    CHECK(world.GetItem(rock)->location == ItemLocation::Inside(nested));
    REQUIRE(world.TransferItem(rock, ItemLocation::Carried(actor)));
    CHECK(world.GetItem(nested)->contents.empty());
    CHECK(world.GetActor(actor)->inventory == Vector<ItemHandle>{rock});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Chunk archives preserve values and nested ownership with fresh runtime keys", "[world][streaming][items]") {
    World world(TestContent(), 123);
    const auto origin = load(world);
    load(world, {{2, 0}, 0}, LoadMode::resident);
    const auto actor = take(world.CreateActor({1, 1}, true));
    const auto object = take(world.CreateObject({2, 2}, true));
    const auto bag = take(world.CreateItem(TestItem("backpack"), ItemLocation::Carried(actor)));
    const auto nested = take(world.CreateItem(TestItem("backpack"), ItemLocation::Inside(bag)));
    const auto rock = take(world.CreateItem(TestItem("rock"), ItemLocation::Inside(nested)));
    const auto bandage = take(world.CreateItem(TestItem("bandage"), ItemLocation::Inside(bag)));
    const auto carried = take(world.CreateItem(TestItem("rock"), ItemLocation::Carried(actor)));
    const auto stored = take(world.CreateItem(TestItem("backpack"), ItemLocation::Stored(object)));
    const auto stored_child = take(world.CreateItem(TestItem("bandage"), ItemLocation::Inside(stored)));
    const auto ground = take(world.CreateItem(TestItem("backpack"), ItemLocation::Ground({0, 4})));
    const auto ground_child = take(world.CreateItem(TestItem("rock"), ItemLocation::Inside(ground)));
    // Reorder siblings so reload cannot accidentally rely on ID or slot order.
    REQUIRE(world.TransferItem(nested, ItemLocation::Ground({0, 0})));
    REQUIRE(world.TransferItem(nested, ItemLocation::Inside(bag)));
    const PersistentId actor_id = world.GetActor(actor)->id;
    const PersistentId object_id = world.GetObject(object)->id;
    const Vector<ItemHandle> old_items{bag, nested, rock, bandage, carried, stored,
        stored_child, ground, ground_child};
    Vector<PersistentId> item_ids;
    for (const auto handle : old_items) {
        item_ids.push_back(world.GetItem(handle)->id);
    }
    REQUIRE(world.RecordActorTurn(actor));
    REQUIRE(world.RecordActorTurn(actor));
    REQUIRE(world.RecordObjectUpdate(object));
    REQUIRE(world.SetObjectEnabled(object, false));
    REQUIRE(world.SetTile({3, 3}, {TestTerrain("closed_door"), 19}));
    CHECK(world.CheckInvariants());
    REQUIRE(world.ReleaseRequest(origin));
    world.ReconcileChunks();
    CHECK(world.ArchivedChunkCount() == world_layer_count);
    CHECK(world.GetActor(actor) == nullptr);
    CHECK(world.GetObject(object) == nullptr);
    CHECK(world.FindActor(actor_id) == ActorHandle{});
    CHECK(world.FindObject(object_id) == ObjectHandle{});
    for (std::size_t index = 0; index < old_items.size(); ++index) {
        CHECK(world.GetItem(old_items[index]) == nullptr);
        CHECK(world.FindItem(item_ids[index]) == ItemHandle{});
    }
    CHECK(world.DestroyActor(actor) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.DestroyObject(object) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.DestroyItem(bag) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.CheckInvariants());
    const auto newer = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({32, 0})));
    CHECK(world.GetItem(newer)->id > item_ids.back());
    CHECK(world.TransferItem(newer, ItemLocation::Carried(actor))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(newer, ItemLocation::Stored(object))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(newer, ItemLocation::Inside(bag))
        == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.TransferItem(newer, ItemLocation::Ground({0, 0}))
        == eastl::unexpected(WorldError::chunk_unavailable));
    CHECK(world.GetItem(newer)->location == ItemLocation::Ground({32, 0}));
    const auto reload = load(world);
    CHECK(world.ArchivedChunkCount() == 0);
    const auto fresh_actor = world.FindActor(actor_id);
    const auto fresh_object = world.FindObject(object_id);
    REQUIRE(world.GetActor(fresh_actor) != nullptr);
    REQUIRE(world.GetObject(fresh_object) != nullptr);
    CHECK(fresh_actor != actor);
    CHECK(fresh_object != object);
    CHECK(world.GetActor(fresh_actor)->player);
    CHECK(world.GetActor(fresh_actor)->turns == 2);
    CHECK(world.GetActor(fresh_actor)->position == TileCoord{1, 1});
    CHECK_FALSE(world.GetObject(fresh_object)->enabled);
    CHECK(world.GetObject(fresh_object)->updates == 1);
    CHECK(*world.GetTile({3, 3}) == Tile{TestTerrain("closed_door"), 19});
    Vector<ItemHandle> fresh;
    for (std::size_t index = 0; index < old_items.size(); ++index) {
        const auto handle = world.FindItem(item_ids[index]);
        REQUIRE(world.GetItem(handle) != nullptr);
        CHECK(handle != old_items[index]);
        CHECK(world.GetItem(handle)->id == item_ids[index]);
        CHECK(world.GetItem(old_items[index]) == nullptr);
        fresh.push_back(handle);
    }
    CHECK(world.GetActor(fresh_actor)->inventory == Vector<ItemHandle>{fresh[0], fresh[4]});
    CHECK(world.GetItem(fresh[0])->location == ItemLocation::Carried(fresh_actor));
    CHECK(world.GetItem(fresh[0])->contents == Vector<ItemHandle>{fresh[3], fresh[1]});
    CHECK(world.GetItem(fresh[1])->location == ItemLocation::Inside(fresh[0]));
    CHECK(world.GetItem(fresh[1])->contents == Vector<ItemHandle>{fresh[2]});
    CHECK(world.GetItem(fresh[2])->type == TestItem("rock"));
    CHECK(world.GetItem(fresh[2])->location == ItemLocation::Inside(fresh[1]));
    CHECK(world.GetItem(fresh[3])->type == TestItem("bandage"));
    CHECK(world.GetObject(fresh_object)->contents == Vector<ItemHandle>{fresh[5]});
    CHECK(world.GetItem(fresh[5])->location == ItemLocation::Stored(fresh_object));
    CHECK(world.GetItem(fresh[5])->contents == Vector<ItemHandle>{fresh[6]});
    CHECK(world.GetItem(fresh[6])->location == ItemLocation::Inside(fresh[5]));
    REQUIRE(world.CountItemsAt({0, 4}) == 1);
    CHECK(*world.ItemsAt({0, 4}).begin() == fresh[7]);
    CHECK(world.GetItem(fresh[7])->contents == Vector<ItemHandle>{fresh[8]});
    CHECK(world.GetItem(fresh[8])->location == ItemLocation::Inside(fresh[7]));
    CHECK(world.CheckInvariants());
    // A second archive replaces the first value snapshot, including mutations.
    REQUIRE(world.RecordActorTurn(fresh_actor));
    REQUIRE(world.SetObjectEnabled(fresh_object, true));
    REQUIRE(world.RecordObjectUpdate(fresh_object));
    REQUIRE(world.TransferItem(fresh[2], ItemLocation::Stored(fresh_object)));
    REQUIRE(world.ReleaseRequest(reload));
    world.ReconcileChunks();
    load(world);
    CHECK(world.GetActor(world.FindActor(actor_id))->turns == 3);
    CHECK(world.GetObject(world.FindObject(object_id))->updates == 2);
    CHECK(world.GetObject(world.FindObject(object_id))->enabled);
    CHECK(world.GetItem(world.FindItem(item_ids[2]))->location
        == ItemLocation::Stored(world.FindObject(object_id)));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Actor and object destruction drop trees while item destruction removes descendants", "[world][destruction]") {
    World world(TestContent());
    const auto request = load(world);
    const auto actor = take(world.CreateActor({1, 1}));
    const auto object = take(world.CreateObject({2, 2}));
    const auto bag = take(world.CreateItem(TestItem("backpack"), ItemLocation::Carried(actor)));
    const auto child = take(world.CreateItem(TestItem("backpack"), ItemLocation::Inside(bag)));
    const auto grandchild = take(world.CreateItem(TestItem("bandage"), ItemLocation::Inside(child)));
    const auto loose = take(world.CreateItem(TestItem("rock"), ItemLocation::Carried(actor)));
    const auto stored = take(world.CreateItem(TestItem("backpack"), ItemLocation::Stored(object)));
    const auto stored_child = take(world.CreateItem(TestItem("rock"), ItemLocation::Inside(stored)));
    const auto id = world.GetItem(grandchild)->id;
    REQUIRE(world.DestroyActor(actor));
    CHECK(world.ActorAt({1, 1}) == ActorHandle{});
    REQUIRE(world.CountItemsAt({1, 1}) == 2);
    for (ItemHandle dropped : world.ItemsAt({1, 1})) {
        CHECK((dropped == bag || dropped == loose));
    }
    CHECK(world.GetItem(bag)->location == ItemLocation::Ground({1, 1}));
    CHECK(world.GetItem(child)->location == ItemLocation::Inside(bag));
    CHECK(world.GetItem(grandchild)->location == ItemLocation::Inside(child));
    REQUIRE(world.DestroyObject(object));
    CHECK(world.ObjectAt({2, 2}) == ObjectHandle{});
    CHECK(world.GetItem(stored)->location == ItemLocation::Ground({2, 2}));
    CHECK(world.GetItem(stored_child)->location == ItemLocation::Inside(stored));
    REQUIRE(world.DestroyItem(child));
    CHECK(world.GetItem(child) == nullptr);
    CHECK(world.GetItem(grandchild) == nullptr);
    CHECK(world.FindItem(id) == ItemHandle{});
    CHECK(world.GetItem(bag)->contents.empty());
    REQUIRE(world.DestroyItem(stored));
    CHECK(world.GetItem(stored_child) == nullptr);
    CHECK(world.ItemsAt({2, 2}).Empty());
    CHECK(world.DestroyItem(child) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.DestroyActor(actor) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.DestroyObject(object) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.SetObjectEnabled(object, true) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.CheckInvariants());
    REQUIRE(world.ReleaseRequest(request));
    world.ReconcileChunks();
    load(world);
    CHECK(world.FindItem(id) == ItemHandle{});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Unordered active snapshots retain logical membership across different chunk reload orders", "[world][determinism]") {
    World world(TestContent());
    const auto left = load(world);
    const auto right = load(world, {{1, 0}, 0});
    take(world.CreateActor({17, 1}));
    take(world.CreateObject({17, 2}, false));
    take(world.CreateActor({1, 1}));
    take(world.CreateObject({1, 2}, true));
    const auto actors = actor_ids(world);
    const auto objects = object_ids(world);
    REQUIRE(actors.size() == 2);
    REQUIRE(objects.size() == 2);
    CHECK(actors[0] < actors[1]);
    CHECK(objects[0] < objects[1]);
    REQUIRE(world.ReleaseRequest(left));
    world.ReconcileChunks();
    load(world);
    CHECK(actor_ids(world) == actors);
    CHECK(object_ids(world) == objects);
    REQUIRE(world.ReleaseRequest(right));
    world.ReconcileChunks();
    load(world, {{1, 0}, 0});
    CHECK(actor_ids(world) == actors);
    CHECK(object_ids(world) == objects);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Full unload preserves moved item roots and never resets entity slot maps", "[world][streaming]") {
    World world(TestContent());
    const auto request = load(world, {{0, 0}, 1});
    const auto actor = take(world.CreateActor({1, 1}));
    const auto object = take(world.CreateObject({17, 1}));
    const auto bag = take(world.CreateItem(TestItem("backpack"), ItemLocation::Carried(actor)));
    const auto child = take(world.CreateItem(TestItem("rock"), ItemLocation::Inside(bag)));
    const auto actor_id = world.GetActor(actor)->id;
    const auto object_id = world.GetObject(object)->id;
    const auto bag_id = world.GetItem(bag)->id;
    const auto child_id = world.GetItem(child)->id;
    REQUIRE(world.TransferItem(bag, ItemLocation::Stored(object)));
    REQUIRE(world.ReleaseRequest(request));
    world.ReconcileChunks();
    CHECK(world.LoadedChunks().empty());
    CHECK(world.GetActor(actor) == nullptr);
    CHECK(world.GetObject(object) == nullptr);
    CHECK(world.GetItem(bag) == nullptr);
    CHECK(world.GetItem(child) == nullptr);
    CHECK(world.CheckInvariants());
    load(world); // The origin must not restore a tree transferred elsewhere.
    CHECK(world.FindActor(actor_id) != actor);
    REQUIRE(world.GetActor(world.FindActor(actor_id)) != nullptr);
    CHECK(world.GetActor(world.FindActor(actor_id))->inventory.empty());
    CHECK(world.FindItem(bag_id) == ItemHandle{});
    CHECK(world.FindItem(child_id) == ItemHandle{});
    load(world, {{1, 0}, 0});
    const auto fresh_object = world.FindObject(object_id);
    const auto fresh_bag = world.FindItem(bag_id);
    const auto fresh_child = world.FindItem(child_id);
    REQUIRE(world.GetObject(fresh_object) != nullptr);
    REQUIRE(world.GetItem(fresh_bag) != nullptr);
    REQUIRE(world.GetItem(fresh_child) != nullptr);
    CHECK(fresh_object != object);
    CHECK(fresh_bag != bag);
    CHECK(fresh_child != child);
    CHECK(world.GetItem(fresh_bag)->location == ItemLocation::Stored(fresh_object));
    CHECK(world.GetItem(fresh_child)->location == ItemLocation::Inside(fresh_bag));
    const auto newer = take(world.CreateItem(TestItem("bandage"), ItemLocation::Ground({0, 0})));
    CHECK(world.GetItem(newer)->id > child_id);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Layered requests overlap through full height and occupancy is layer local", "[world][layers]") {
    World world(TestContent());
    const auto resident = load(world, {{-1, -1, INT_MIN}, 0}, LoadMode::resident);
    const auto active = load(world, {{-1, -1, INT_MAX}, 0});
    REQUIRE(world.LoadedChunks().size() == world_layer_count);
    for (int y = world_min_y; y <= world_max_y; ++y) {
        CHECK(world.LoadedChunks()[y - world_min_y] == ChunkCoord{-1, -1, y});
        CHECK(world.IsActive({-1, -1, y}));
    }
    REQUIRE(world.SetTile({-1, -1, -1}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({-1, -1, 1}, {TestTerrain("floor"), 0}));
    const auto below = take(world.CreateActor({-1, -1, -1}));
    const auto above = take(world.CreateActor({-1, -1, 1}));
    const auto objectBelow = take(world.CreateObject({-1, -1, -1}));
    const auto objectAbove = take(world.CreateObject({-1, -1, 1}));
    const auto itemBelow = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({-1, -1, -1})));
    const auto itemAbove = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({-1, -1, 1})));
    CHECK(world.ActorAt({-1, -1, -1}) == below);
    CHECK(world.ActorAt({-1, -1, 1}) == above);
    CHECK(world.ObjectAt({-1, -1, -1}) == objectBelow);
    CHECK(world.ObjectAt({-1, -1, 1}) == objectAbove);
    REQUIRE(world.CountItemsAt({-1, -1, -1}) == 1);
    REQUIRE(world.CountItemsAt({-1, -1, 1}) == 1);
    CHECK(*world.ItemsAt({-1, -1, -1}).begin() == itemBelow);
    CHECK(*world.ItemsAt({-1, -1, 1}).begin() == itemAbove);
    CHECK(world.GetTile({-1, -1, world_min_y - 1}) == nullptr);
    CHECK(world.CreateActor({-1, -1, world_max_y + 1})
        == eastl::unexpected(WorldError::invalid_position));
    REQUIRE(world.ReleaseRequest(active));
    REQUIRE(world.ReconcileChunks());
    CHECK(ActiveActors(world).empty());
    CHECK(world.LoadedChunks().size() == world_layer_count);
    REQUIRE(world.ReleaseRequest(resident));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.ArchivedChunkCount() == world_layer_count);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Gravity settles actors bottom up and retries after blockers move or disappear", "[world][gravity]") {
    World world(TestContent());
    load(world);
    const Tile floor{TestTerrain("floor"), 0};
    const Tile air{TestTerrain("air"), 0};
    REQUIRE(world.SetTile({1, 1, -3}, floor));
    REQUIRE(world.SetTile({2, 1, -3}, floor));
    for (int y = -2; y <= 3; ++y) { REQUIRE(world.SetTile({1, 1, y}, air)); }
    REQUIRE(world.SetTile({1, 1, 1}, floor));
    REQUIRE(world.SetTile({1, 1, 0}, floor));
    const auto upper = take(world.CreateActor({1, 1, 1}, true));
    const auto lower = take(world.CreateActor({1, 1, 0}));
    const auto carried = take(world.CreateItem(TestItem("backpack"), ItemLocation::Carried(upper)));
    REQUIRE(world.ScheduleActorAction(upper, 37));
    const auto id = world.GetActor(upper)->id;
    REQUIRE(world.SetTile({1, 1, 1}, air));
    CHECK(world.GetActor(upper)->position == TileCoord{1, 1, 1});
    // Lower has the larger ID: height takes precedence over persistent ID.
    REQUIRE(world.SetTile({1, 1, 0}, air));
    CHECK(world.GetActor(lower)->position == TileCoord{1, 1, -3});
    CHECK(world.GetActor(upper)->position == TileCoord{1, 1, -2});
    REQUIRE(world.MoveActor(lower, {2, 1, -3}));
    CHECK(world.GetActor(upper)->position == TileCoord{1, 1, -3});
    CHECK(world.FindPlayerPosition(id) == TileCoord{1, 1, -3});
    CHECK(world.GetActor(upper)->turns == 1);
    CHECK(world.GetActor(upper)->remainingTicks == 37);
    CHECK(world.GetItem(carried)->location == ItemLocation::Carried(upper));
    const auto blocked = take(world.CreateActor({1, 1, 3}));
    CHECK(world.GetActor(blocked)->position == TileCoord{1, 1, -2});
    REQUIRE(world.DestroyActor(upper));
    CHECK(world.GetActor(blocked)->position == TileCoord{1, 1, -3});
    world.SettleGravity();
    CHECK(world.GetActor(blocked)->position == TileCoord{1, 1, -3});
    CHECK(world.CheckInvariants());
}

TEST_CASE("Resident spatial edits settle through air but walls and the bottom bound support", "[world][gravity]") {
    World world(TestContent());
    load(world, {{0, 0}, 0}, LoadMode::resident);
    const Tile air{TestTerrain("air"), 0};
    for (int y = world_min_y; y <= world_max_y; ++y) {
        REQUIRE(world.SetTile({3, 3, y}, air));
    }
    REQUIRE(world.SetTile({3, 3, -5}, {TestTerrain("wall"), 0}));
    const auto actor = take(world.CreateActor({3, 3, world_max_y}));
    CHECK(world.GetActor(actor)->position == TileCoord{3, 3, -4});
    REQUIRE(world.SetTile({3, 3, -5}, air));
    CHECK(world.GetActor(actor)->position == TileCoord{3, 3, world_min_y});
    CHECK(ActiveActors(world).empty());
    CHECK(world.CheckInvariants());
}

TEST_CASE("Equal height gravity ordering uses persistent IDs across actor and item roots", "[world][gravity][determinism]") {
    for (bool itemFirst : {false, true}) {
        World world(TestContent());
        load(world);
        REQUIRE(world.SetTile({4, 4, -2}, {TestTerrain("floor"), 0}));
        REQUIRE(world.SetTile({4, 4, -1}, {TestTerrain("air"), 0}));
        ActorHandle actor;
        ItemHandle item;
        if (itemFirst) {
            item = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({4, 4})));
            actor = take(world.CreateActor({4, 4}));
        } else {
            actor = take(world.CreateActor({4, 4}));
            item = take(world.CreateItem(TestItem("rock"), ItemLocation::Ground({4, 4})));
        }
        REQUIRE(world.SetTile({4, 4}, {TestTerrain("air"), 0}));
        CHECK(world.GetActor(actor)->position == TileCoord{4, 4, -2});
        CHECK(world.GetItem(item)->location == ItemLocation::Ground({4, 4, itemFirst ? -2 : -1}));
        world.SettleGravity();
        CHECK(world.CheckInvariants());
    }
}

TEST_CASE("Terrain generation depends only on seed and position and never issues entity IDs", "[world][determinism]") {
    World first(TestContent(), 91);
    World second(TestContent(), 91);
    const auto remote = load(first, {{10, -10}, 0});
    const auto terrain = first.GetChunk({10, -10})->tiles;
    load(first);
    load(second);
    load(second, {{10, -10}, 0});
    CHECK(second.GetChunk({10, -10})->tiles == terrain);
    CHECK(ActiveActors(first).empty());
    CHECK(ActiveObjects(first).empty());
    take(first.CreateActor({0, 0}));
    take(second.CreateActor({0, 0}));
    CHECK(first.GetActor(first.ActorAt({0, 0}))->id == 1);
    CHECK(second.GetActor(second.ActorAt({0, 0}))->id == 1);
    REQUIRE(first.ReleaseRequest(remote));
    first.ReconcileChunks();
    load(first, {{10, -10}, 0});
    CHECK(first.GetChunk({10, -10})->tiles == terrain);
    CHECK(first.CheckInvariants());
    CHECK(second.CheckInvariants());
}

}
