#include "core/server.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

PersistentId Join(Server& server, const char* name, std::uint64_t token) {
    const auto joined = server.JoinPlayer(name, 0, token);
    REQUIRE(joined);
    REQUIRE(server.ActivatePlayer(joined->player));
    return joined->player;
}

TEST_CASE("Joining and occupied reconnect fallback preserve the saved layer", "[multiplayer][server][3d]") {
    Server server(TestContent());
    World& world = server.GetWorld();
    for (int z = -2; z <= 2; ++z) {
        for (int x = -2; x <= 2; ++x) {
            REQUIRE(world.SetTile({x, z, 3}, {TestTerrain("floor"), 0}));
        }
    }
    REQUIRE(world.MoveActor(server.GetPlayer(), {0, 0, 3}));
    REQUIRE(server.SetBubbleRadii(0, 1));
    const auto guest = Join(server, "Elevated", 399);
    const auto original = server.ViewForPlayer(guest).player_position;
    CHECK(original.y == 3);
    CHECK(server.ViewForPlayer(guest).origin.y == 3);
    REQUIRE(server.DisconnectPlayer(guest));
    REQUIRE(world.CreateActor(original));
    REQUIRE(server.JoinPlayer("Elevated", 399, 0));
    REQUIRE(server.ActivatePlayer(guest));
    const auto resumed = server.ViewForPlayer(guest);
    CHECK(resumed.player_position != original);
    CHECK(resumed.player_position.y == 3);
    CHECK(resumed.origin.y == 3);
    CHECK(world.FindPlayerPosition(guest)->y == 3);
    CHECK(world.CheckInvariants());
}

TEST_CASE("Rounds resolve reverse arrival in persistent ID order and isolate personal views",
    "[multiplayer][server]") {
    Server server(TestContent());
    const PersistentId guest = Join(server, "Guest", 555);
    const PersistentId host = server.GetHostPlayerId();
    const TileCoord guestPosition = server.ViewForPlayer(guest).player_position;
    REQUIRE(server.GetWorld().SetTile({guestPosition.x - 2, guestPosition.z},
        {TestTerrain("floor"), 0}));
    REQUIRE(server.GetWorld().MoveActor(server.GetPlayer(), {guestPosition.x - 2, guestPosition.z}));
    REQUIRE(server.GetWorld().SetTile({guestPosition.x - 1, guestPosition.z},
        {TestTerrain("floor"), 0}));
    const auto hostView = server.View();
    const auto guestView = server.ViewForPlayer(guest);
    CHECK(hostView.playerId == host);
    CHECK(guestView.playerId == guest);
    CHECK(hostView.player_position != guestView.player_position);
    CHECK(hostView.players.size() == 2);
    CHECK(server.Submit(guest, {1, guestView.opportunity, {CommandKind::move, -1, 0}}).outcome
        == Outcome::queued);
    CHECK(server.Update(0).commandResults.empty());
    CHECK(server.Submit(host, {1, hostView.opportunity, {CommandKind::move, 1, 0}}).outcome
        == Outcome::queued);
    const auto round = server.Update(0);
    REQUIRE(round.commandResults.size() == 2);
    CHECK(round.commandResults[0].player == host);
    CHECK(round.commandResults[0].result.outcome == Outcome::moved);
    CHECK(round.commandResults[1].player == guest);
    CHECK(round.commandResults[1].result.outcome == Outcome::occupied);
    CHECK(server.ViewForPlayer(guest).opportunity == guestView.opportunity + 1);
    CHECK(server.GetWorld().CheckInvariants());
}

TEST_CASE("Joining is outside the barrier and buffered input keeps its slot token",
    "[multiplayer][server]") {
    Server server(TestContent());
    const auto joining = server.JoinPlayer("Guest", 0, 55);
    REQUIRE(joining);
    CHECK(server.GetPlayers()[1].state == PlayerConnectionState::Joining);
    CHECK(server.Send({}).advanced);
    REQUIRE(server.ActivatePlayer(joining->player));
    const auto view = server.ViewForPlayer(joining->player);
    CHECK(server.Submit(joining->player, {1, view.opportunity, {}}).outcome == Outcome::queued);
    CHECK(server.Submit(joining->player, {1, view.opportunity, {}}).outcome == Outcome::duplicate_input);
    CHECK(server.Submit(joining->player, {2, view.opportunity, {CommandKind::move, 0, 0}}).outcome
        == Outcome::invalid_command);
    CHECK(server.Submit(joining->player, {2, view.opportunity, {}}).outcome == Outcome::queued);
    CHECK(server.Submit(joining->player, {3, view.opportunity - 1, {}}).outcome == Outcome::stale_input);
    CHECK(server.ViewForPlayer(joining->player).lastInputSequence == 2);
    CHECK(server.DisconnectPlayer(joining->player));
    CHECK(server.GetPlayers()[1].state == PlayerConnectionState::Parked);
    CHECK(server.JoinPlayer("Other", 55, 0)->player == joining->player);
    CHECK(server.ActivatePlayer(joining->player));
    CHECK(server.ViewForPlayer(joining->player).lastInputSequence == 0);
}

TEST_CASE("Concurrent grace waits for missing players and all-idle pauses", "[multiplayer][pacing]") {
    Server server(TestContent());
    const PersistentId guest = Join(server, "Guest", 100);
    REQUIRE(server.SetPacing({100, false, true}));
    const auto host = server.View();
    CHECK(server.Update(10'000'000).tick == 0);
    CHECK(server.Submit(host.playerId, {1, host.opportunity, {}}).outcome == Outcome::queued);
    CHECK(server.Update(999'999).commandResults.empty());
    const auto round = server.Update(1);
    REQUIRE(round.commandResults.size() == 1);
    CHECK(round.commandResults[0].player == host.playerId);
    CHECK(round.skippedActions == 1);
    CHECK(server.ViewForPlayer(guest).opportunity == 2);
    CHECK(server.GetTick() == 100);
}

TEST_CASE("Parked guests leave occupancy and freeze cooldown until reconnect", "[multiplayer][server]") {
    Server server(TestContent());
    const PersistentId guest = Join(server, "Guest", 200);
    REQUIRE(server.SetPacing({100, true, true}));
    const TileCoord position = server.ViewForPlayer(guest).player_position;
    REQUIRE(server.GetWorld().SetActorSpeed(server.GetWorld().FindActor(guest), 50));
    Command longWait;
    longWait.waitCost = 250;
    const auto host = server.View();
    const auto visitor = server.ViewForPlayer(guest);
    REQUIRE(server.Submit(guest, {1, visitor.opportunity, longWait}).outcome == Outcome::queued);
    REQUIRE(server.Submit(host.playerId, {1, host.opportunity, longWait}).outcome == Outcome::queued);
    REQUIRE(server.Update(0).commandResults.size() == 2);
    const Tick cooldown = server.GetWorld().GetActorRemainingTicks(server.GetWorld().FindActor(guest));
    CHECK(cooldown == 125);
    REQUIRE(server.DisconnectPlayer(guest));
    CHECK(server.GetWorld().ActorAt(position).value == 0);
    CHECK(server.Update(1'000'000).tick == 100);
    const Actor* parked = server.GetWorld().GetActor(server.GetWorld().FindActor(guest));
    REQUIRE(parked);
    CHECK(server.GetWorld().GetActorRemainingTicks(server.GetWorld().FindActor(guest)) == cooldown);
    CHECK(parked->parked);
    const auto resumed = server.JoinPlayer("Guest", 200, 0);
    REQUIRE(resumed);
    REQUIRE(server.ActivatePlayer(guest));
    CHECK(server.GetWorld().GetActorRemainingTicks(server.GetWorld().FindActor(guest)) == cooldown);
    CHECK_FALSE(server.GetWorld().GetActor(server.GetWorld().FindActor(guest))->parked);
    CHECK(server.GetWorld().CheckInvariants());
}

TEST_CASE("Reconnection selects deterministic free fallback when original tile is occupied",
    "[multiplayer][server]") {
    Server server(TestContent());
    const PersistentId guest = Join(server, "Guest", 501);
    const TileCoord original = server.ViewForPlayer(guest).player_position;
    REQUIRE(server.DisconnectPlayer(guest));
    REQUIRE(server.GetWorld().CreateActor(original));
    const auto resumed = server.JoinPlayer("Guest", 501, 0);
    REQUIRE(resumed);
    REQUIRE(server.ActivatePlayer(guest));
    CHECK(server.ViewForPlayer(guest).player_position != original);
    CHECK(server.GetWorld().GetActor(server.GetWorld().ActorAt(original))->player == false);
    CHECK(server.GetWorld().CheckInvariants());
}

TEST_CASE("Expired opportunity rejects late packets without replaying an action", "[multiplayer][pacing]") {
    Server server(TestContent());
    const PersistentId guest = Join(server, "Guest", 654);
    REQUIRE(server.SetPacing({100, false, false}));
    const auto before = server.ViewForPlayer(guest);
    const auto elapsed = server.Update(1'000'000);
    CHECK(elapsed.skippedActions == 2);
    CHECK(server.ViewForPlayer(guest).opportunity == before.opportunity + 1);
    CHECK(server.Submit(guest, {1, before.opportunity, {CommandKind::move, 1, 0}}).outcome
        == Outcome::stale_input);
    CHECK(server.ViewForPlayer(guest).player_position == before.player_position);
}

TEST_CASE("Personal bubbles keep distinct chunks active and release on parking",
    "[multiplayer][streaming]") {
    Server server(TestContent());
    const PersistentId guest = Join(server, "Guest", 901);
    const TileCoord position = server.ViewForPlayer(guest).player_position;
    REQUIRE(ChunkOf(position) != ChunkOf(server.View().player_position));
    REQUIRE(server.SetBubbleRadii(0, 0));
    CHECK(server.GetWorld().IsActive(position));
    CHECK(server.GetWorld().IsActive(server.View().player_position));
    REQUIRE(server.DisconnectPlayer(guest));
    CHECK_FALSE(server.GetWorld().IsActive(position));
    CHECK(server.GetWorld().IsActive(server.View().player_position));
    CHECK(server.GetWorld().CheckInvariants());
}

TEST_CASE("Mixed grace lengths wait for the later deadline across elapsed partitions",
    "[multiplayer][pacing]") {
    Server server(TestContent());
    const auto joined = server.JoinPlayer("Fast", 0, 902);
    REQUIRE(joined);
    const PersistentId guest = joined->player;
    REQUIRE(server.GetWorld().SetActorSpeed(server.GetWorld().FindActor(guest), 50));
    REQUIRE(server.ActivatePlayer(guest));
    REQUIRE(server.SetPacing({100, true, false}));
    CHECK(server.Update(500'000).skippedActions == 0);
    CHECK(server.GetPlayers()[1].graceMicroseconds == 0);
    CHECK(server.GetPlayers()[0].graceMicroseconds == 500'000);
    const auto expired = server.ViewForPlayer(guest);
    CHECK(server.Submit(guest, {1, expired.opportunity, {CommandKind::move, 1, 0}}).outcome
        == Outcome::stale_input);
    CHECK_FALSE(server.ViewForPlayer(guest).commandQueued);
    CHECK(server.Update(499'999).skippedActions == 0);
    CHECK(server.Update(1).skippedActions == 2);
    CHECK(server.ViewForPlayer(guest).opportunity == 2);
}

TEST_CASE("Staggered joining grace is partition independent while host remains busy",
    "[multiplayer][pacing]") {
    Server whole(TestContent(), 72);
    Server split(TestContent(), 72);
    PersistentId guest = 0;
    for (Server* server : {&whole, &split}) {
        REQUIRE(server->SetPacing({100, true, false}));
        Command longWait;
        longWait.waitCost = 250;
        REQUIRE(server->Send(longWait).advanced);
        REQUIRE(server->Update(250'000).tick == 25);
        const auto joined = server->JoinPlayer("Fast", 0, 907);
        REQUIRE(joined);
        guest = joined->player;
        REQUIRE(server->GetWorld().SetActorSpeed(server->GetWorld().FindActor(guest), 50));
        REQUIRE(server->ActivatePlayer(guest));
    }
    const auto single = whole.Update(500'000);
    const auto first = split.Update(200'000);
    const auto second = split.Update(300'000);
    CHECK(single.skippedActions == first.skippedActions + second.skippedActions);
    CHECK(single.skippedActions == 1);
    CHECK(whole.GetTick() == split.GetTick());
    CHECK(whole.ViewForPlayer(guest).opportunity == split.ViewForPlayer(guest).opportunity);
    CHECK(whole.GetPlayers()[1].graceMicroseconds == split.GetPlayers()[1].graceMicroseconds);
}

TEST_CASE("Failed inputs keep their original deadline while another player is busy",
    "[multiplayer][pacing]") {
    Server server(TestContent());
    REQUIRE(server.SetPacing({100, true, true}));
    Command longWait;
    longWait.waitCost = 500;
    REQUIRE(server.Send(longWait).advanced); // Begin before the guest enters the barrier.
    const auto joined = server.JoinPlayer("Fast", 0, 903);
    REQUIRE(joined);
    const PersistentId guest = joined->player;
    REQUIRE(server.GetWorld().SetActorSpeed(server.GetWorld().FindActor(guest), 50));
    REQUIRE(server.ActivatePlayer(guest));
    for (std::uint64_t sequence = 1; sequence <= 4; ++sequence) {
        const auto view = server.ViewForPlayer(guest);
        REQUIRE(server.Submit(guest, {sequence, view.opportunity, {CommandKind::drop}}).outcome
            == Outcome::queued);
        const auto update = server.Update(100'000);
        REQUIRE(update.commandResults.size() == 1);
        CHECK(update.commandResults[0].result.outcome == Outcome::not_carried);
        CHECK(update.skippedActions == 0);
        CHECK(server.GetPlayers()[1].graceMicroseconds == 500'000 - sequence * 100'000);
    }
    const auto before = server.ViewForPlayer(guest);
    REQUIRE(server.Submit(guest, {5, before.opportunity, {CommandKind::drop}}).outcome
        == Outcome::queued);
    const auto expired = server.Update(100'000);
    REQUIRE(expired.commandResults.size() == 1);
    CHECK(expired.commandResults[0].result.outcome == Outcome::not_carried);
    CHECK(expired.skippedActions == 1); // Failed intent cannot renew expired grace.
    CHECK(server.ViewForPlayer(guest).opportunity == before.opportunity + 2);
}

TEST_CASE("Disconnect and joining cancellation invalidate captured opportunity tokens",
    "[multiplayer][server]") {
    Server server(TestContent());
    const PersistentId guest = Join(server, "Guest", 904);
    const std::uint64_t captured = server.ViewForPlayer(guest).opportunity;
    REQUIRE(server.DisconnectPlayer(guest));
    REQUIRE(server.JoinPlayer("Guest", 904, 0));
    REQUIRE(server.ActivatePlayer(guest));
    CHECK(server.Submit(guest, {1, captured, {}}).outcome == Outcome::stale_input);
    const std::uint64_t joiningToken = server.ViewForPlayer(guest).opportunity;
    REQUIRE(server.DisconnectPlayer(guest));
    REQUIRE(server.JoinPlayer("Guest", 904, 0));
    REQUIRE(server.DisconnectPlayer(guest)); // Joining never entered the scheduler.
    REQUIRE(server.JoinPlayer("Guest", 904, 0));
    REQUIRE(server.ActivatePlayer(guest));
    CHECK(server.Submit(guest, {1, joiningToken, {}}).outcome == Outcome::stale_input);
    CHECK(server.Submit(guest, {1, server.ViewForPlayer(guest).opportunity, {}}).outcome
        == Outcome::queued);
}

TEST_CASE("Zero-radius reconnect searches the loaded chunk before failing",
    "[multiplayer][streaming]") {
    Server server(TestContent());
    const PersistentId guest = Join(server, "Guest", 905);
    const TileCoord original = server.ViewForPlayer(guest).player_position;
    REQUIRE(server.SetBubbleRadii(0, 0));
    REQUIRE(server.DisconnectPlayer(guest));
    const auto request = server.GetWorld().RequestChunks({ChunkOf(original), 0}, LoadMode::active);
    REQUIRE(request);
    REQUIRE(server.GetWorld().ReconcileChunks());
    REQUIRE(server.GetWorld().CreateActor(original));
    REQUIRE(server.JoinPlayer("Guest", 905, 0));
    REQUIRE(server.ActivatePlayer(guest));
    REQUIRE(server.GetWorld().ReleaseRequest(*request));
    REQUIRE(server.GetWorld().ReconcileChunks());
    CHECK(server.ViewForPlayer(guest).player_position != original);
    CHECK(server.GetWorld().CheckInvariants());
}

TEST_CASE("Chunk-edge reconnect stages destination before relocating parked guest",
    "[multiplayer][streaming]") {
    Server server(TestContent());
    const auto joined = server.JoinPlayer("Guest", 0, 906);
    REQUIRE(joined);
    const PersistentId guest = joined->player;
    const ActorHandle actor = server.GetWorld().FindActor(guest);
    REQUIRE(server.GetWorld().SetTile({15, 1}, {TestTerrain("floor"), 0}));
    REQUIRE(server.GetWorld().SetTile({16, 0}, {TestTerrain("floor"), 0}));
    for (TileCoord wall : {TileCoord{14, 0}, TileCoord{15, 0}, TileCoord{14, 1},
             TileCoord{14, 2}, TileCoord{15, 2}}) {
        REQUIRE(server.GetWorld().SetTile(wall, {TestTerrain("wall"), 0}));
    }
    REQUIRE(server.GetWorld().MoveActor(actor, {15, 1}));
    REQUIRE(server.DisconnectPlayer(guest));
    REQUIRE(server.SetBubbleRadii(0, 0));
    REQUIRE(server.GetWorld().CreateActor({15, 1}));
    REQUIRE(server.JoinPlayer("Guest", 906, 0));
    REQUIRE(server.ActivatePlayer(guest));
    CHECK(server.ViewForPlayer(guest).player_position == TileCoord{16, 0});
    CHECK(server.GetWorld().CheckInvariants());
}

TEST_CASE("Local convenience input shares sequence context and due-object ordering",
    "[multiplayer][scheduling]") {
    Server server(TestContent());
    const auto host = server.GetHostPlayerId();
    REQUIRE(server.Submit(host, {23, server.View().opportunity, {}}).outcome == Outcome::queued);
    const auto updated = server.Update(0);
    REQUIRE(updated.commandResult);
    REQUIRE(updated.commandResult->advanced);
    CHECK(server.Send({}).advanced);
    CHECK(server.View().lastInputSequence == 24);

    World& world = server.GetWorld();
    REQUIRE(world.MoveActor(server.GetPlayer(), {1, 0}));
    const auto object = world.ObjectAt({2, 0});
    REQUIRE(world.SetObjectEnabled(object, true));
    world.ElapseTicks(100); // A restored/admin-created due effect at the input tick.
    const auto updates = world.GetObject(object)->updates;
    REQUIRE(server.Send({CommandKind::interact, 1, 0}).outcome == Outcome::toggled);
    CHECK(world.GetObject(object)->updates == updates + 1);
    CHECK_FALSE(world.GetObject(object)->enabled);
}
}
