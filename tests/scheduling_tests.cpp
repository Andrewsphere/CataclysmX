#include "core/server.h"
#include "core/simulation.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {

using namespace cataclysmx;
constexpr Tick forever = std::numeric_limits<Tick>::max();

template<class T>
T Take(WorldResult<T> result) {
    REQUIRE(result.has_value());
    return *result;
}

LoadRequest LoadFloor(World& world, ChunkCoord chunk = {}) {
    const auto request = Take(world.RequestChunks({chunk, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            REQUIRE(world.SetTile({chunk.x * chunk_size + x, chunk.z * chunk_size + z, chunk.y},
                {TestTerrain("floor"), 0}));
        }
    }
    return request;
}

Command Wait(std::uint32_t cost) {
    Command command;
    command.waitCost = cost;
    return command;
}

void SameScheduledState(const Server& first, const Server& second) {
    CHECK(first.GetTick() == second.GetTick());
    CHECK(first.GetPacing() == second.GetPacing());
    CHECK(first.View().cells == second.View().cells);
    CHECK(first.View().remainingTicks == second.View().remainingTicks);
    CHECK(first.View().commandQueued == second.View().commandQueued);
    const World& a = first.GetWorld();
    const World& b = second.GetWorld();
    Vector<ActorHandle> actorsA, actorsB;
    a.CollectActiveActors(actorsA);
    b.CollectActiveActors(actorsB);
    REQUIRE(actorsA.size() == actorsB.size());
    for (ActorHandle handle : actorsA) {
        const Actor* actor = a.GetActor(handle);
        const Actor* other = b.GetActor(b.FindActor(actor->id));
        REQUIRE(other);
        CHECK(actor->position == other->position);
        CHECK(actor->turns == other->turns);
        CHECK(actor->speed == other->speed);
        CHECK(a.GetActorRemainingTicks(handle) == b.GetActorRemainingTicks(b.FindActor(actor->id)));
    }
    Vector<ObjectHandle> objectsA, objectsB;
    a.CollectActiveObjects(objectsA);
    b.CollectActiveObjects(objectsB);
    REQUIRE(objectsA.size() == objectsB.size());
    for (ObjectHandle handle : objectsA) {
        const auto* object = a.GetObject(handle);
        const auto* other = b.GetObject(b.FindObject(object->id));
        REQUIRE(other);
        CHECK(object->updates == other->updates);
        CHECK(a.GetObjectRemainingTicks(handle) == b.GetObjectRemainingTicks(b.FindObject(object->id)));
        CHECK(object->updateInterval == other->updateInterval);
    }
    CHECK(a.CheckInvariants());
    CHECK(b.CheckInvariants());
}

TEST_CASE("Elapsed time changes the World clock without rewriting deadlines", "[scheduling][deadlines]") {
    World world(TestContent());
    LoadFloor(world);
    world.ElapseTicks(123);
    const auto player = Take(world.CreateActor({1, 1}, true));
    const auto object = Take(world.CreateObject({2, 1}, true));
    REQUIRE(world.ScheduleActorAction(player));
    CHECK(world.GetActor(player)->actionDeadline.dueTick == 223);
    CHECK(world.GetObject(object)->updateDeadline.dueTick == 223);
    for (int step = 0; step < 10; ++step) {
        world.ElapseTicks(10);
        CHECK(world.GetActor(player)->actionDeadline.dueTick == 223);
        CHECK(world.GetObject(object)->updateDeadline.dueTick == 223);
    }
    CHECK(world.GetTick() == 223);
    CHECK(world.NextActorEventIn() == eastl::optional<Tick>{0});
    CHECK(world.NextObjectEventIn() == eastl::optional<Tick>{0});
    world.ElapseTicks(7); // Overdue is ready too; no unsigned subtraction wrap.
    CHECK(world.GetActorRemainingTicks(player) == 0);
    CHECK(world.GetObjectRemainingTicks(object) == 0);
    Vector<ActorHandle> ready;
    world.CollectReadyPlayers(ready);
    CHECK(ready == Vector<ActorHandle>{player});
    CHECK(world.ProcessObjectEvents());
    CHECK_FALSE(world.ProcessObjectEvents());
    CHECK(world.GetObject(object)->updateDeadline.dueTick == 330);
    Tick mirror = 999; // Compatibility time is output, never a second clock.
    std::uint32_t rng = 0;
    CHECK(AdvanceUntilInput(world, mirror, rng, world.GetTick()).stop == SimulationStop::InputRequired);
    CHECK(mirror == 230);
}

TEST_CASE("Large absolute ticks retain exact positive deadlines", "[scheduling][deadlines]") {
    World world(TestContent());
    LoadFloor(world);
    const Tick start = forever - 1000;
    world.ElapseTicks(start);
    const auto player = Take(world.CreateActor({1, 1}, true));
    const auto object = Take(world.CreateObject({2, 1}, true));
    REQUIRE(world.ScheduleActorAction(player));
    CHECK(world.GetActor(player)->actionDeadline.dueTick == start + 100);
    CHECK(world.GetObject(object)->updateDeadline.dueTick == start + 100);
    world.ElapseTicks(99);
    CHECK(world.GetActorRemainingTicks(player) == 1);
    CHECK(world.GetObjectRemainingTicks(object) == 1);
    world.ElapseTicks(1);
    CHECK(world.GetActorRemainingTicks(player) == 0);
    CHECK(world.GetObjectRemainingTicks(object) == 0);
    CHECK(world.ProcessObjectEvents());
    CHECK(world.GetObject(object)->updateDeadline.dueTick == start + 200);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Overlapping suspension reasons rebase only on final reactivation", "[scheduling][streaming][parking]") {
    World world(TestContent());
    const auto request = LoadFloor(world);
    const auto player = Take(world.CreateActor({1, 1}, true));
    const auto npc = Take(world.CreateActor({3, 1}));
    const auto object = Take(world.CreateObject({2, 1}, true));
    REQUIRE(world.ScheduleActorAction(player));
    REQUIRE(world.ScheduleActorAction(npc));
    world.ElapseTicks(30);
    REQUIRE(world.SetActorParked(player, true));
    REQUIRE(world.SetActorController(npc, AiControllerKind::None));
    REQUIRE(world.SetObjectEnabled(object, false));
    world.ElapseTicks(20);
    REQUIRE(world.UpdateRequest(request, {{}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    // Clearing entity-level reasons in an inactive chunk must not resume time.
    REQUIRE(world.SetActorParked(player, false));
    REQUIRE(world.SetActorController(npc, AiControllerKind::Wander));
    REQUIRE(world.SetObjectEnabled(object, true));
    world.ElapseTicks(500);
    CHECK(world.GetActor(player)->actionDeadline.suspended);
    CHECK(world.GetActor(npc)->actionDeadline.suspended);
    CHECK(world.GetObject(object)->updateDeadline.suspended);
    CHECK(world.GetActorRemainingTicks(player) == 70);
    CHECK(world.GetActorRemainingTicks(npc) == 70);
    CHECK(world.GetObjectRemainingTicks(object) == 70);
    REQUIRE(world.UpdateRequest(request, {{}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    CHECK(world.GetActor(player)->actionDeadline.dueTick == 620);
    CHECK(world.GetActor(npc)->actionDeadline.dueTick == 620);
    CHECK(world.GetObject(object)->updateDeadline.dueTick == 620);
    // Inverse overlap: chunk reactivation cannot clear the parked reason.
    REQUIRE(world.SetActorParked(player, true));
    REQUIRE(world.UpdateRequest(request, {{}, 0}, LoadMode::resident));
    REQUIRE(world.ReconcileChunks());
    world.ElapseTicks(100);
    REQUIRE(world.UpdateRequest(request, {{}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    world.ElapseTicks(10);
    CHECK(world.GetActorRemainingTicks(player) == 70);
    REQUIRE(world.SetActorParked(player, false));
    CHECK(world.GetActor(player)->actionDeadline.dueTick == 730);
    CHECK(world.CheckInvariants());
}

TEST_CASE("NPC due batches follow stable IDs after dense storage relocation", "[scheduling][determinism]") {
    World world(TestContent());
    LoadFloor(world);
    const auto retired = Take(world.CreateActor({8, 8}));
    const auto first = Take(world.CreateActor({10, 10}));
    const auto second = Take(world.CreateActor({2, 2}));
    REQUIRE(world.DestroyActor(retired)); // Relocates second before first.
    const auto third = Take(world.CreateActor({6, 6}));
    std::uint32_t rng = 0;
    world.ProcessNpcActions(rng);
    CHECK(world.GetActor(first)->position == TileCoord{11, 10});
    CHECK(world.GetActor(second)->position == TileCoord{2, 3});
    CHECK(world.GetActor(third)->position == TileCoord{6, 5});
    CHECK(rng == 3'519'870'697U);
    CHECK(world.GetActor(first)->actionDeadline.dueTick == 100);
    CHECK(world.GetActor(second)->actionDeadline.dueTick == 100);
    CHECK(world.GetActor(third)->actionDeadline.dueTick == 100);
}

TEST_CASE("NPC horizontal wandering preserves elevation and the legacy draw policy", "[scheduling][3d][determinism]") {
    World world(TestContent());
    LoadFloor(world, {0, 0, 2});
    const auto npc = Take(world.CreateActor({5, 5, 2}));
    const auto player = Take(world.CreateActor({1, 1, 2}, true));
    std::uint32_t rng = 0;
    cataclysmx::Advance(world, player, rng);
    CHECK(world.GetActor(npc)->position == TileCoord{6, 5, 2});
    CHECK(world.GetActor(npc)->turns == 1);
    CHECK(rng == 1'013'904'223U);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Walking into a deep hole settles immediately for only the walking cost", "[scheduling][server][3d]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({0, 0, 2}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({1, 0, 0}, {TestTerrain("floor"), 0}));
    REQUIRE(world.SetTile({1, 0, 1}, {TestTerrain("air"), 0}));
    REQUIRE(world.SetTile({1, 0, 2}, {TestTerrain("air"), 0}));
    REQUIRE(world.MoveActor(server.GetPlayer(), {0, 0, 2}));
    REQUIRE(server.SetBubbleRadii(0, 0));
    const auto chunks = world.LoadedChunks();
    REQUIRE(world.SetActorSpeed(server.GetPlayer(), 70));
    REQUIRE(server.SetPacing({100, true, true}));
    const auto result = server.Send({CommandKind::move, 1, 0});
    CHECK(result.outcome == Outcome::moved);
    CHECK(result.advanced);
    CHECK(result.cost == 70);
    CHECK(result.tick == 0);
    CHECK(world.GetActor(server.GetPlayer())->position == TileCoord{1, 0, 0});
    CHECK(world.GetActor(server.GetPlayer())->turns == 1);
    CHECK(world.GetActorRemainingTicks(server.GetPlayer()) == 70);
    CHECK(world.LoadedChunks() == chunks);
    CHECK(server.Update(700'000).tick == 70);
    CHECK(server.View().remainingTicks == 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Buffered stairs resolve from current elevation and validate the current pair", "[scheduling][server][3d]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    for (int y = 0; y <= 2; ++y) {
        REQUIRE(world.SetTile({0, 0, y}, {TestTerrain("stairs"), 0}));
    }
    REQUIRE(server.SetPacing({100, true, true}));
    Command up{CommandKind::move};
    up.dy = 1;
    REQUIRE(server.Send(up).advanced);
    CHECK(server.Send(up).outcome == Outcome::queued);
    REQUIRE(world.SetTile({0, 0, 2}, {TestTerrain("floor"), 0}));
    const auto update = server.Update(1'000'000);
    REQUIRE(update.commandResult);
    CHECK(update.commandResult->outcome == Outcome::blocked);
    CHECK_FALSE(update.commandResult->advanced);
    CHECK(update.commandResult->cost == 0);
    CHECK(server.View().player_position == TileCoord{0, 0, 1});
    CHECK(server.View().origin.y == 1);
    CHECK(server.View().remainingTicks == 0);
    CHECK(server.GetTick() == 100);
}

TEST_CASE("Player durations scale waits while object time stays independent", "[scheduling][simulation]") {
    for (std::uint32_t speed : {50u, 70u, 100u}) {
        CAPTURE(speed);
        World world(TestContent());
        LoadFloor(world);
        const auto player = Take(world.CreateActor({1, 1}, true));
        const auto object = Take(world.CreateObject({2, 1}, true));
        REQUIRE(world.SetActorSpeed(player, speed));
        CHECK(world.GetObject(object)->updateInterval == 100);
        Tick tick = 0;
        std::uint32_t rng = 42;
        for (std::uint32_t base : {100u, 150u, 250u}) {
            CAPTURE(base);
            const Tick before = tick;
            REQUIRE(world.ScheduleActorAction(player, base));
            const auto result = AdvanceUntilInput(world, tick, rng, forever);
            CHECK(result.stop == SimulationStop::InputRequired);
            CHECK(result.readyPlayers == Vector<ActorHandle>{player});
            CHECK(tick - before == static_cast<Tick>(base) * speed / 100);
            CHECK(world.GetObject(object)->updates == tick / 100);
            CHECK(world.GetObjectRemainingTicks(object) == 100 - tick % 100);
            CHECK(rng == 42);
        }
        CHECK(world.GetActor(player)->turns == 3);
    }
}

TEST_CASE("Positive action durations round up and invalid scheduling preserves cooldown", "[scheduling]") {
    World world(TestContent());
    LoadFloor(world);
    const auto player = Take(world.CreateActor({1, 1}, true));
    REQUIRE(world.SetActorSpeed(player, 70));
    REQUIRE(world.ScheduleActorAction(player, 1));
    CHECK(world.GetActorRemainingTicks(player) == 1);
    CHECK(world.ScheduleActorAction(player, 150) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.GetActor(player)->turns == 1);
    Tick tick = 0;
    std::uint32_t rng = 0;
    REQUIRE(AdvanceUntilInput(world, tick, rng, forever).stop == SimulationStop::InputRequired);
    CHECK(tick == 1);
    REQUIRE(world.ScheduleActorAction(player, 151));
    CHECK(world.GetActorRemainingTicks(player) == 106);
    REQUIRE(world.SetActorSpeed(player, 50));
    CHECK(world.GetActorRemainingTicks(player) == 106); // Committed duration is stable.
    REQUIRE(AdvanceUntilInput(world, tick, rng, forever).stop == SimulationStop::InputRequired);
    CHECK(tick == 107);
    CHECK(world.ScheduleActorAction(player, 0) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.SetActorSpeed(player, 0) == eastl::unexpected(WorldError::invalid_request));
    CHECK(world.GetActorRemainingTicks(player) == 0);
    CHECK(world.GetActor(player)->turns == 2);
}

TEST_CASE("Exact limits process due objects and return input before the actor phase", "[scheduling][simulation]") {
    World world(TestContent());
    LoadFloor(world);
    const auto player = Take(world.CreateActor({1, 1}, true));
    const auto npc = Take(world.CreateActor({5, 5}));
    const auto object = Take(world.CreateObject({2, 1}, true));
    REQUIRE(world.SetActorSpeed(npc, 50));
    REQUIRE(world.ScheduleActorAction(player));
    Tick tick = 0;
    std::uint32_t rng = 0;
    const auto partial = AdvanceUntilInput(world, tick, rng, 99);
    CHECK(partial.stop == SimulationStop::LimitReached);
    CHECK(partial.nextEventIn == 1);
    CHECK(tick == 99);
    CHECK(world.GetActor(npc)->turns == 2);
    CHECK(world.GetObject(object)->updates == 0);
    const auto boundary = AdvanceUntilInput(world, tick, rng, 100);
    CHECK(boundary.stop == SimulationStop::InputRequired);
    CHECK(boundary.readyPlayers == Vector<ActorHandle>{player});
    CHECK(world.GetObject(object)->updates == 1);
    CHECK(world.GetActor(npc)->turns == 2);
    REQUIRE(world.ScheduleActorAction(player));
    const auto resumed = AdvanceUntilInput(world, tick, rng, 100);
    CHECK(resumed.stop == SimulationStop::LimitReached);
    CHECK(resumed.nextEventIn == 50);
    CHECK(world.GetActor(npc)->turns == 3);
    CHECK(world.GetObject(object)->updates == 1);
}

TEST_CASE("Singleplayer Advance commits a normal wait and stops at the next input", "[scheduling][simulation]") {
    World world(TestContent());
    LoadFloor(world);
    const auto player = Take(world.CreateActor({1, 1}, true));
    const auto object = Take(world.CreateObject({2, 1}, true));
    REQUIRE(world.SetActorSpeed(player, 50));
    std::uint32_t rng = 42;
    cataclysmx::Advance(world, player, rng);
    CHECK(world.GetActor(player)->turns == 1);
    CHECK(world.GetActorRemainingTicks(player) == 0);
    CHECK(world.GetObject(object)->updates == 0);
    CHECK(world.GetObjectRemainingTicks(object) == 50);
    cataclysmx::Advance(world, player, rng);
    CHECK(world.GetActor(player)->turns == 2);
    CHECK(world.GetActorRemainingTicks(player) == 0);
    CHECK(world.GetObject(object)->updates == 1);
    CHECK(world.GetObjectRemainingTicks(object) == 100);
    CHECK(rng == 42);
}

TEST_CASE("All ready players form a persistent ID barrier before NPCs without repeating objects", "[scheduling][barrier]") {
    World world(TestContent());
    LoadFloor(world);
    const auto retired = Take(world.CreateActor({9, 9}, true));
    const auto first = Take(world.CreateActor({8, 1}, true));
    REQUIRE(world.DestroyActor(retired));
    const auto second = Take(world.CreateActor({1, 1}, true)); // Reuses an older slot.
    const auto busy = Take(world.CreateActor({2, 1}, true));
    const auto npc = Take(world.CreateActor({5, 5}));
    const auto object = Take(world.CreateObject({3, 1}, true));
    REQUIRE(world.ScheduleActorAction(busy, 250));
    Tick tick = 0;
    std::uint32_t rng = 0;
    auto result = AdvanceUntilInput(world, tick, rng, forever);
    CHECK(result.readyPlayers == Vector<ActorHandle>{first, second});
    CHECK(world.GetActor(npc)->turns == 0);
    REQUIRE(world.ScheduleActorAction(first));
    result = AdvanceUntilInput(world, tick, rng, forever);
    CHECK(result.readyPlayers == Vector<ActorHandle>{second});
    CHECK(tick == 0);
    CHECK(rng == 0);
    REQUIRE(world.ScheduleActorAction(second));
    result = AdvanceUntilInput(world, tick, rng, forever);
    CHECK(result.readyPlayers == Vector<ActorHandle>{first, second});
    CHECK(tick == 100);
    CHECK(world.GetActorRemainingTicks(busy) == 150);
    CHECK(world.GetActor(npc)->turns == 1); // Tick-100 NPC phase is still behind the barrier.
    CHECK(world.GetObject(object)->updates == 1); // Due objects precede that barrier.
    const auto savedRng = rng;
    for (int repeat = 0; repeat < 3; ++repeat) {
        CHECK(AdvanceUntilInput(world, tick, rng, tick).readyPlayers == result.readyPlayers);
        CHECK(world.GetObject(object)->updates == 1);
        CHECK(world.GetActor(npc)->turns == 1);
        CHECK(rng == savedRng);
    }
}

TEST_CASE("Blocked NPC actions consume duration and exactly one ordered explicit RNG draw", "[scheduling][determinism]") {
    World world(TestContent());
    LoadFloor(world);
    const auto first = Take(world.CreateActor({10, 10}));
    const auto second = Take(world.CreateActor({2, 2}));
    REQUIRE(world.SetActorSpeed(first, 70));
    REQUIRE(world.SetTile({11, 10}, {TestTerrain("wall"), 0}));
    Tick tick = 0;
    std::uint32_t rng = 0;
    const auto result = AdvanceUntilInput(world, tick, rng, 0);
    CHECK(result.stop == SimulationStop::LimitReached);
    CHECK(result.nextEventIn == 70);
    CHECK(rng == 1'196'435'762U);
    CHECK(world.GetActor(first)->position == TileCoord{10, 10});
    CHECK(world.GetActorRemainingTicks(first) == 70);
    CHECK(world.GetActor(first)->turns == 1);
    CHECK(world.GetActor(second)->position == TileCoord{2, 3});
    CHECK(world.GetActorRemainingTicks(second) == 100);
    CHECK(AdvanceUntilInput(world, tick, rng, 69).nextEventIn == 1);
    CHECK(world.GetActor(first)->turns == 1);
    CHECK(rng == 1'196'435'762U);
    REQUIRE(AdvanceUntilInput(world, tick, rng, 70).stop == SimulationStop::LimitReached);
    CHECK(world.GetActor(first)->turns == 2);
    CHECK(world.GetActor(second)->turns == 1);
    CHECK(rng == 3'519'870'697U);
}

TEST_CASE("Deleted scheduled entities leave no events and reused slots start fresh", "[scheduling][lifetime]") {
    World world(TestContent());
    LoadFloor(world);
    const auto actor = Take(world.CreateActor({1, 1}));
    const auto object = Take(world.CreateObject({2, 1}, true));
    REQUIRE(world.ScheduleActorAction(actor, 250));
    REQUIRE(world.DestroyActor(actor));
    REQUIRE(world.DestroyObject(object));
    world.ElapseTicks(17);
    Tick tick = world.GetTick();
    std::uint32_t rng = 9;
    auto result = AdvanceUntilInput(world, tick, rng, 1000);
    CHECK(result.stop == SimulationStop::NoEvents);
    CHECK(result.nextEventIn == 0);
    CHECK(tick == 17);
    CHECK(rng == 9);
    CHECK(world.GetActor(actor) == nullptr);
    CHECK(world.GetObject(object) == nullptr);
    const auto replacement = Take(world.CreateActor({1, 1}, true));
    CHECK(replacement != actor);
    result = AdvanceUntilInput(world, tick, rng, 1000);
    CHECK(result.readyPlayers == Vector<ActorHandle>{replacement});
    CHECK(world.ScheduleActorAction(actor) == eastl::unexpected(WorldError::invalid_handle));
    CHECK(world.CheckInvariants());
}

TEST_CASE("Inactive and archived chunks freeze partial actor and object delays", "[scheduling][streaming]") {
    World world(TestContent());
    LoadFloor(world);
    const auto remoteRequest = LoadFloor(world, {1, 0});
    const auto actor = Take(world.CreateActor({18, 2}));
    const auto object = Take(world.CreateObject({19, 2}, true));
    const auto actorId = world.GetActor(actor)->id;
    const auto objectId = world.GetObject(object)->id;
    REQUIRE(world.ScheduleActorAction(actor, 250));
    Tick tick = 0;
    std::uint32_t rng = 0;
    REQUIRE(AdvanceUntilInput(world, tick, rng, 40).stop == SimulationStop::LimitReached);
    SECTION("Resident only") {
        REQUIRE(world.UpdateRequest(remoteRequest, {{1, 0}, 0}, LoadMode::resident));
    }
    SECTION("Archived and rematerialized") {
        REQUIRE(world.ReleaseRequest(remoteRequest));
    }
    REQUIRE(world.ReconcileChunks());
    Take(world.CreateObject({3, 3}, true));
    REQUIRE(AdvanceUntilInput(world, tick, rng, 500).stop == SimulationStop::LimitReached);
    Take(world.RequestChunks({{1, 0}, 0}, LoadMode::active));
    REQUIRE(world.ReconcileChunks());
    const auto restoredActor = world.FindActor(actorId);
    const auto restoredObject = world.FindObject(objectId);
    REQUIRE(world.GetActor(restoredActor));
    REQUIRE(world.GetObject(restoredObject));
    CHECK(world.GetActorRemainingTicks(restoredActor) == 210);
    CHECK(world.GetObjectRemainingTicks(restoredObject) == 60);
    CHECK(world.GetObject(restoredObject)->updates == 0);
    REQUIRE(AdvanceUntilInput(world, tick, rng, 559).stop == SimulationStop::LimitReached);
    CHECK(world.GetObjectRemainingTicks(restoredObject) == 1);
    REQUIRE(AdvanceUntilInput(world, tick, rng, 560).stop == SimulationStop::LimitReached);
    CHECK(world.GetObject(restoredObject)->updates == 1);
    CHECK(world.GetActorRemainingTicks(restoredActor) == 150);
    CHECK(world.GetActor(restoredActor)->turns == 1);
    CHECK(rng == 0);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Server capped commands commit immediately and buffer busy input without spending time", "[scheduling][server][buffering]") {
    Server server(TestContent());
    REQUIRE(server.SetPacing({100, true, true}));
    REQUIRE(server.GetWorld().SetActorSpeed(server.GetPlayer(), 70));
    const auto result = server.Send(Wait(150));
    CHECK(result.outcome == Outcome::waited);
    CHECK(result.advanced);
    CHECK(result.tick == 0);
    CHECK(result.cost == 105);
    CHECK(server.View().remainingTicks == 105);
    const auto busy = server.Send({CommandKind::move, 1, 0});
    CHECK(busy.outcome == Outcome::queued);
    CHECK_FALSE(busy.advanced);
    CHECK(busy.cost == 0);
    CHECK(busy.tick == 0);
    CHECK(server.View().commandQueued);
    CHECK(server.View().player_position == TileCoord{0, 0});
    CHECK(server.Update(499'999).tick == 49);
    CHECK(server.View().remainingTicks == 56);
    CHECK(server.Update(1).tick == 50);
    CHECK(server.View().remainingTicks == 55);
    const auto resolved = server.Update(550'000);
    CHECK_FALSE(resolved.needsInput);
    REQUIRE(resolved.commandResult);
    CHECK(resolved.commandResult->outcome == Outcome::moved);
    CHECK(resolved.commandResult->tick == 105);
    CHECK(resolved.commandResult->cost == 70);
    CHECK_FALSE(server.View().commandQueued);
    CHECK(server.View().remainingTicks == 70);
    CHECK(server.View().player_position == TileCoord{1, 0});
    CHECK(server.GetTick() == 105);
    const auto invalid = server.Send(Wait(0));
    CHECK(invalid.outcome == Outcome::invalid_command);
    CHECK_FALSE(invalid.advanced);
    CHECK(server.GetTick() == 105);
    Command move{CommandKind::move, -1, 0};
    move.waitCost = 250; // Only wait honors the caller's base cost.
    CHECK(server.Send(move).outcome == Outcome::queued);
    const auto next = server.Update(700'000);
    REQUIRE(next.commandResult);
    CHECK(next.commandResult->cost == 70);
    CHECK(server.View().player_position == TileCoord{0, 0});
}

TEST_CASE("The latest valid intent replaces one buffered slot and executes exactly once", "[scheduling][server][buffering]") {
    Server server(TestContent());
    REQUIRE(server.SetPacing({100, true, true}));
    REQUIRE(server.Send({CommandKind::move, 1, 0}).advanced);
    for (int repeat = 0; repeat < 8; ++repeat) {
        CHECK(server.Send({CommandKind::move, 1, 0}).outcome == Outcome::queued);
    }
    CHECK(server.Send({CommandKind::move, 0, 1}).outcome == Outcome::queued);
    for (Command invalid : {Command{CommandKind::move, 0, 0},
             Command{CommandKind::interact, 1, 2}, Command{static_cast<CommandKind>(999)}, Wait(0)}) {
        CHECK(server.Send(invalid).outcome == Outcome::invalid_command);
        CHECK(server.View().commandQueued);
    }
    CHECK(server.GetTick() == 0);
    CHECK(server.GetWorld().GetActor(server.GetPlayer())->turns == 1);
    CHECK(server.View().remainingTicks == 100);
    const auto before = server.Update(999'999);
    CHECK_FALSE(before.commandResult);
    CHECK(server.View().player_position == TileCoord{1, 0});
    const auto due = server.Update(1);
    REQUIRE(due.commandResult);
    CHECK(due.commandResult->outcome == Outcome::moved);
    CHECK(due.commandResult->advanced);
    CHECK(due.commandResult->tick == 100);
    CHECK(server.View().player_position == TileCoord{1, 1});
    CHECK(server.GetWorld().GetActor(server.GetPlayer())->turns == 2);
    CHECK_FALSE(server.View().commandQueued);
    const auto finish = server.Update(5'000'000);
    CHECK(finish.tick == 200);
    CHECK(finish.needsInput);
    CHECK_FALSE(finish.commandResult);
    CHECK(finish.skippedActions == 0);
    CHECK(server.View().player_position == TileCoord{1, 1});
    CHECK(server.GetWorld().GetActor(server.GetPlayer())->turns == 2);
}

TEST_CASE("Buffered movement validates the destination when ready and consumes failed intent", "[scheduling][server][buffering]") {
    Server server(TestContent());
    REQUIRE(server.SetPacing({100, true, true}));
    REQUIRE(server.Send({}).advanced);
    World& world = server.GetWorld();
    REQUIRE(world.SetTile({1, 0}, {TestTerrain("wall"), 0}));
    CHECK(server.Send({CommandKind::move, 1, 0}).outcome == Outcome::queued);
    SECTION("A newly available destination succeeds") {
        REQUIRE(world.SetTile({1, 0}, {TestTerrain("floor"), 0}));
        const auto update = server.Update(1'000'000);
        REQUIRE(update.commandResult);
        CHECK(update.commandResult->outcome == Outcome::moved);
        CHECK(update.commandResult->advanced);
        CHECK(world.GetActor(server.GetPlayer())->position == TileCoord{1, 0});
        CHECK(world.GetActor(server.GetPlayer())->turns == 2);
        CHECK_FALSE(update.needsInput);
    }
    SECTION("A blocked destination fails once without spending action time") {
        const auto update = server.Update(1'000'000);
        REQUIRE(update.commandResult);
        CHECK(update.commandResult->outcome == Outcome::blocked);
        CHECK_FALSE(update.commandResult->advanced);
        CHECK(update.commandResult->cost == 0);
        CHECK(update.commandResult->tick == 100);
        CHECK(update.needsInput);
        CHECK(world.GetActorRemainingTicks(server.GetPlayer()) == 0);
        REQUIRE(world.SetTile({1, 0}, {TestTerrain("floor"), 0}));
        CHECK(server.Update(5'000'000).tick == 100);
        CHECK(world.GetActor(server.GetPlayer())->position == TileCoord{0, 0});
        CHECK(world.GetActor(server.GetPlayer())->turns == 1);
    }
    CHECK_FALSE(server.View().commandQueued);
    CHECK(server.GetWorld().CheckInvariants());
}

TEST_CASE("Buffered movement is a relative intent resolved from the current position", "[scheduling][server][buffering]") {
    Server server(TestContent());
    REQUIRE(server.SetPacing({100, true, true}));
    REQUIRE(server.Send({CommandKind::move, 1, 0}).advanced);
    CHECK(server.Send({CommandKind::move, 0, 1}).outcome == Outcome::queued);
    REQUIRE(server.GetWorld().MoveActor(server.GetPlayer(), {2, 1}));
    const auto update = server.Update(1'000'000);
    REQUIRE(update.commandResult);
    CHECK(update.commandResult->outcome == Outcome::moved);
    CHECK(server.View().player_position == TileCoord{2, 2});
}

TEST_CASE("Buffered actions use current speed and ownership at execution", "[scheduling][server][buffering]") {
    Server server(TestContent());
    REQUIRE(server.SetPacing({100, true, true}));
    REQUIRE(server.Send({}).advanced);
    SECTION("A wait uses speed when the intent is resolved") {
        CHECK(server.Send(Wait(150)).outcome == Outcome::queued);
        REQUIRE(server.GetWorld().SetActorSpeed(server.GetPlayer(), 70));
        const auto update = server.Update(1'000'000);
        REQUIRE(update.commandResult);
        CHECK(update.commandResult->outcome == Outcome::waited);
        CHECK(update.commandResult->cost == 105);
        CHECK(server.View().remainingTicks == 105);
    }
    SECTION("A stale pickup target fails at execution") {
        REQUIRE_FALSE(server.GetWorld().ItemsAt({0, 0}).Empty());
        const auto bag = *server.GetWorld().ItemsAt({0, 0}).begin();
        const auto bagId = server.GetWorld().GetItem(bag)->id;
        CHECK(server.Send({CommandKind::pick_up, 0, 0, bagId}).outcome == Outcome::queued);
        REQUIRE(server.GetWorld().DestroyItem(bag));
        const auto update = server.Update(1'000'000);
        REQUIRE(update.commandResult);
        CHECK(update.commandResult->outcome == Outcome::nothing_here);
        CHECK_FALSE(update.commandResult->advanced);
        CHECK(update.needsInput);
        CHECK(server.GetWorld().GetActor(server.GetPlayer())->turns == 1);
    }
    CHECK_FALSE(server.View().commandQueued);
}

TEST_CASE("Queued input preserves elapsed credit across readiness and pacing changes", "[scheduling][server][buffering][determinism]") {
    Server whole(TestContent(), 918);
    Server split(TestContent(), 918);
    for (Server* server : {&whole, &split}) {
        REQUIRE(server->SetPacing({137, true, true}));
        REQUIRE(server->GetWorld().SetActorSpeed(server->GetPlayer(), 80));
        REQUIRE(server->Send({CommandKind::move, 1, 0}).advanced);
        CHECK(server->Send({CommandKind::move, 0, 1}).outcome == Outcome::queued);
    }
    const auto one = whole.Update(1'000'000);
    eastl::optional<CommandResult> resolved;
    for (std::uint64_t elapsed : {1ULL, 99'999ULL, 400'001ULL, 250'000ULL, 249'999ULL}) {
        const auto update = split.Update(elapsed);
        if (update.commandResult) { resolved = update.commandResult; }
    }
    REQUIRE(one.commandResult);
    REQUIRE(resolved);
    CHECK(one.commandResult->tick == 80);
    CHECK(resolved->tick == 80);
    CHECK(whole.GetTick() == 137);
    CHECK(whole.View().remainingTicks == 23);
    CHECK(whole.View().player_position == TileCoord{1, 1});
    SameScheduledState(whole, split);

    CHECK(whole.Send({CommandKind::move, -1, 0}).outcome == Outcome::queued);
    REQUIRE(whole.SetPacing({200, false, true}));
    const auto uncapped = whole.Update(0);
    REQUIRE(uncapped.commandResult);
    CHECK(uncapped.commandResult->tick == 160);
    CHECK(uncapped.tick == 240);
    CHECK(uncapped.needsInput);
    CHECK(whole.View().player_position == TileCoord{0, 1});
    CHECK_FALSE(whole.View().commandQueued);
}

TEST_CASE("Runtime cap switching resumes the existing cooldown and idle time never catches up", "[scheduling][server][pacing]") {
    Server server(TestContent());
    REQUIRE(server.SetPacing({100, true, true}));
    CHECK(server.Update(9'000'000).tick == 0);
    REQUIRE(server.Send(Wait(250)).advanced);
    CHECK(server.GetTick() == 0);
    CHECK(server.Update(250'000).tick == 25);
    REQUIRE(server.SetPacing({200, false, true}));
    CHECK(server.Update(0).tick == 250);
    CHECK(server.View().remainingTicks == 0);
    CHECK(server.Send(Wait(150)).tick == 400);
    REQUIRE(server.SetPacing({200, true, true}));
    CHECK(server.Update(30'000'000).tick == 400);
    REQUIRE(server.Send({}).advanced);
    CHECK(server.GetTick() == 400);
    CHECK(server.Update(5'000).tick == 401);
    const auto settings = server.GetPacing();
    CHECK_FALSE(server.SetPacing({0, false, false}));
    CHECK(server.GetPacing() == settings);
}

TEST_CASE("Continuous missing input gets speed scaled grace then a normal wait", "[scheduling][server][pacing]") {
    for (bool capped : {false, true}) {
        CAPTURE(capped);
        Server server(TestContent());
        REQUIRE(server.GetWorld().SetActorSpeed(server.GetPlayer(), 70));
        REQUIRE(server.Send({}).tick == 70); // Begin a fresh speed-70 readiness episode.
        REQUIRE(server.SetPacing({200, capped, false}));
        const auto before = server.Update(349'999);
        CHECK(before.skippedActions == 0);
        CHECK(before.needsInput);
        CHECK(before.tick == 70);
        const auto deadline = server.Update(1);
        CHECK(deadline.skippedActions == 1);
        CHECK(deadline.tick == 140);
        CHECK(deadline.needsInput);
        CHECK(server.GetWorld().GetActor(server.GetPlayer())->turns == 2);
        CHECK(server.Update(0).skippedActions == 0);
        REQUIRE(server.SetPacing({200, capped, true}));
        CHECK(server.Update(20'000'000).skippedActions == 0);
        CHECK(server.GetTick() == 140);
    }
}

TEST_CASE("Elapsed update partitioning preserves events deadlines and fractional credit", "[scheduling][server][determinism]") {
    for (bool capped : {false, true}) {
        for (bool pause : {false, true}) {
            CAPTURE(capped, pause);
            Server whole(TestContent(), 918);
            Server split(TestContent(), 918);
            for (Server* server : {&whole, &split}) {
                REQUIRE(server->GetWorld().SetActorSpeed(server->GetPlayer(), 70));
                const auto object = server->GetWorld().ObjectAt({2, 0});
                REQUIRE(server->GetWorld().SetObjectEnabled(object, true));
                REQUIRE(server->SetPacing({137, capped, pause}));
                REQUIRE(server->Send(Wait(250)).advanced);
            }
            const auto one = whole.Update(7'123'457);
            std::uint64_t skipped = 0;
            for (std::uint64_t elapsed : {1ULL, 7'299ULL, 333'333ULL, 1'000'001ULL,
                     5'000'003ULL, 782'820ULL}) {
                skipped += split.Update(elapsed).skippedActions;
            }
            CHECK(one.skippedActions == skipped);
            SameScheduledState(whole, split);
            // A follow-up crosses the next boundary if fractional credit survived.
            const auto a = whole.Update(987'654);
            const auto b = split.Update(987'654);
            CHECK(a.skippedActions == b.skippedActions);
            CHECK(a.needsInput == b.needsInput);
            SameScheduledState(whole, split);
        }
    }
}

TEST_CASE("A busy player keeps shared pacing active while a ready player reaches its deadline", "[scheduling][barrier][pacing]") {
    World world(TestContent());
    LoadFloor(world);
    const auto ready = Take(world.CreateActor({1, 1}, true));
    const auto busy = Take(world.CreateActor({2, 1}, true));
    REQUIRE(world.SetActorSpeed(ready, 50));
    REQUIRE(world.ScheduleActorAction(busy, 250));
    TickPacer pacer;
    REQUIRE(pacer.SetSettings({100, true, true}));
    const PlayerReadiness players[] = {{busy, world.GetActor(busy)->id, 100, false, false},
        {ready, world.GetActor(ready)->id, 50, true, false}};
    pacer.Synchronize(players);
    CHECK_FALSE(pacer.IsIdle());
    Tick tick = 0;
    std::uint32_t rng = 0;
    CHECK(AdvanceUntilInput(world, tick, rng, 50).readyPlayers == Vector<ActorHandle>{ready});
    pacer.ElapseMicroseconds(499'999);
    CHECK(pacer.ExpiredPlayers().empty());
    pacer.ElapseMicroseconds(1);
    CHECK(pacer.ExpiredPlayers() == SmallVector<ActorHandle, 4>{ready});
    REQUIRE(world.ScheduleActorAction(ready)); // Controller supplies normal wait on expiry.
    const auto result = AdvanceUntilInput(world, tick, rng, pacer.AvailableTicks());
    pacer.ConsumeTicks(tick);
    CHECK(tick == 50);
    CHECK(result.readyPlayers == Vector<ActorHandle>{ready});
    CHECK(world.GetActorRemainingTicks(busy) == 200);
    CHECK(world.GetActor(ready)->turns == 1);
}

}
