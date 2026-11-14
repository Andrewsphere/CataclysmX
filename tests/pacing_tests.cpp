#include "core/pacing.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {

using namespace cataclysmx;

constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();

PlayerReadiness Ready(std::uint64_t handle = 1, PersistentId id = 1,
    std::uint32_t speed = 100) {
    return {ActorHandle{handle}, id, speed, true, false};
}

std::uint64_t DeadlineWait(const TickPacer& pacer) {
    const auto deadline = pacer.NextDeadlineMicroseconds();
    REQUIRE(deadline.has_value());
    return *deadline;
}

TEST_CASE("Input grace follows 50 75 80 and 100 tick actions", "[pacing]") {
    for (std::uint32_t speed : {50u, 75u, 80u, 100u}) {
        TickPacer pacer;
        REQUIRE(pacer.SetSettings({100, false, false}));
        pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(1, 1, speed)});
        const std::uint64_t grace = static_cast<std::uint64_t>(speed) * 10'000;
        REQUIRE(DeadlineWait(pacer) == grace);
        pacer.ElapseMicroseconds(grace - 1);
        CHECK(pacer.ExpiredPlayers().empty());
        CHECK(DeadlineWait(pacer) == 1);
        pacer.ElapseMicroseconds(1);
        CHECK(pacer.ExpiredPlayers() == SmallVector<ActorHandle, 4>{ActorHandle{1}});
        CHECK(DeadlineWait(pacer) == 0);
        pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(1, 1, speed)});
        CHECK(DeadlineWait(pacer) == 0);
    }
}

TEST_CASE("Runtime speed and rate changes preserve partial input budgets", "[pacing]") {
    TickPacer pacer;
    REQUIRE(pacer.SetSettings({100, true, false}));
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready()});
    pacer.ElapseMicroseconds(250'001);
    CHECK(pacer.AvailableTicks() == 25);
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(1, 1, 50)});
    CHECK(DeadlineWait(pacer) == 749'999);
    REQUIRE(pacer.SetSettings({200, true, false}));
    CHECK(DeadlineWait(pacer) == 375'000);
    CHECK(pacer.AvailableTicks() == 25);
    CHECK(pacer.MicrosecondsUntilTicks(26) == 5'000);
    const PacingSettings previous = pacer.GetSettings();
    CHECK_FALSE(pacer.SetSettings({0, false, true}));
    CHECK(pacer.GetSettings() == previous);
    CHECK(DeadlineWait(pacer) == 375'000);
    pacer.ElapseMicroseconds(374'999);
    CHECK(DeadlineWait(pacer) == 1);
    pacer.ElapseMicroseconds(1);
    CHECK(pacer.ExpiredPlayers().size() == 1);
}

TEST_CASE("Cap changes clear credit but retain deadlines", "[pacing]") {
    TickPacer pacer;
    REQUIRE(pacer.SetSettings({100, true, false}));
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready()});
    pacer.ElapseMicroseconds(25'001);
    CHECK(pacer.AvailableTicks() == 2);
    pacer.ConsumeTicks(2);
    CHECK(pacer.AvailableTicks() == 0);
    CHECK(pacer.MicrosecondsUntilTicks(1) == 4'999);
    REQUIRE(pacer.SetSettings({100, false, false}));
    CHECK(pacer.AvailableTicks() == maximum);
    CHECK(pacer.MicrosecondsUntilTicks(maximum) == 0);
    pacer.ConsumeTicks(maximum);
    pacer.ElapseMicroseconds(10'000);
    CHECK(DeadlineWait(pacer) == 964'999);
    REQUIRE(pacer.SetSettings({100, true, false}));
    CHECK(pacer.AvailableTicks() == 0);
    CHECK(pacer.MicrosecondsUntilTicks(1) == 10'000);
    CHECK(DeadlineWait(pacer) == 964'999);
}

TEST_CASE("Idle pauses preserve grace and discard catchup credit", "[pacing]") {
    TickPacer pacer;
    REQUIRE(pacer.SetSettings({100, true, true}));
    CHECK(pacer.IsIdle());
    pacer.ElapseMicroseconds(maximum);
    CHECK(pacer.AvailableTicks() == 0);
    Vector<PlayerReadiness> players{Ready(), Ready(2, 2)};
    players[1].ready = false;
    pacer.Synchronize(players);
    CHECK_FALSE(pacer.IsIdle());
    pacer.ElapseMicroseconds(250'001);
    CHECK(pacer.AvailableTicks() == 25);
    players[1].ready = true;
    pacer.Synchronize(players);
    CHECK(pacer.IsIdle());
    CHECK(pacer.AvailableTicks() == 0);
    CHECK_FALSE(pacer.NextDeadlineMicroseconds().has_value());
    pacer.ElapseMicroseconds(maximum);
    CHECK(pacer.ExpiredPlayers().empty());
    players[1].hasCommand = true;
    pacer.Synchronize(players);
    CHECK_FALSE(pacer.IsIdle());
    CHECK(DeadlineWait(pacer) == 749'999);
    CHECK(pacer.MicrosecondsUntilTicks(1) == 10'000);
    pacer.ElapseMicroseconds(749'999);
    REQUIRE(pacer.ExpiredPlayers().size() == 1);
    players[1].hasCommand = false;
    pacer.Synchronize(players);
    CHECK(pacer.ExpiredPlayers().empty());
    players[1].hasCommand = true;
    pacer.Synchronize(players);
    CHECK(pacer.ExpiredPlayers() == SmallVector<ActorHandle, 4>{ActorHandle{1}});
    pacer.Synchronize({});
    CHECK(pacer.IsIdle());
    CHECK(pacer.AvailableTicks() == 0);
}

TEST_CASE("Queued future commands and busy players count as activity", "[pacing]") {
    TickPacer pacer;
    REQUIRE(pacer.SetSettings({100, true, true}));
    PlayerReadiness player = Ready();
    player.ready = false;
    player.hasCommand = true;
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{player});
    CHECK_FALSE(pacer.IsIdle());
    CHECK_FALSE(pacer.NextDeadlineMicroseconds().has_value());
    pacer.ElapseMicroseconds(10'000);
    CHECK(pacer.AvailableTicks() == 1);
    player.ready = true;
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{player});
    CHECK_FALSE(pacer.IsIdle());
    CHECK_FALSE(pacer.NextDeadlineMicroseconds().has_value());
    player.hasCommand = false;
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{player});
    CHECK(pacer.IsIdle());
    REQUIRE(pacer.SetSettings({100, true, false}));
    CHECK(DeadlineWait(pacer) == 1'000'000);
}

TEST_CASE("Simultaneous deadlines are returned in persistent ID order", "[pacing]") {
    TickPacer pacer;
    REQUIRE(pacer.SetSettings({100, false, false}));
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(7, 30), Ready(9, 10), Ready(2, 20)});
    pacer.ElapseMicroseconds(1'000'000);
    CHECK(pacer.ExpiredPlayers() == SmallVector<ActorHandle, 4>{ActorHandle{9}, ActorHandle{2}, ActorHandle{7}});
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(2, 20), Ready(7, 30), Ready(9, 10)});
    CHECK(pacer.ExpiredPlayers() == SmallVector<ActorHandle, 4>{ActorHandle{9}, ActorHandle{2}, ActorHandle{7}});
}

TEST_CASE("More than four players retain ordered deadlines across resynchronization", "[pacing]") {
    TickPacer pacer;
    REQUIRE(pacer.SetSettings({100, false, false}));
    SmallVector<PlayerReadiness, 4> players{
        Ready(6, 40), Ready(5, 20), Ready(4, 30),
        Ready(3, 20), Ready(2, 10), Ready(1, 50)};
    pacer.Synchronize(players);
    pacer.ElapseMicroseconds(250'000);
    players = {Ready(1, 50, 50), Ready(2, 10, 50), Ready(3, 20, 50),
        Ready(4, 30, 50), Ready(5, 20, 50), Ready(6, 40, 50)};
    pacer.Synchronize(players);
    for (const PlayerReadiness& player : players) {
        CHECK(pacer.GraceMicroseconds(player.actor) == 750'000);
    }
    pacer.ElapseMicroseconds(750'000);
    CHECK(pacer.ExpiredPlayers() == SmallVector<ActorHandle, 4>{
        ActorHandle{2}, ActorHandle{3}, ActorHandle{5}, ActorHandle{4}, ActorHandle{6}, ActorHandle{1}});

    pacer.RestartGrace(ActorHandle{3});
    CHECK(pacer.GraceMicroseconds(ActorHandle{3}) == 0);
    players.erase(players.begin() + 4); // Prune actor 5 while still above inline capacity.
    pacer.Synchronize(players);
    CHECK(pacer.GraceMicroseconds(ActorHandle{3}) == 500'000);
    CHECK(pacer.ExpiredPlayers() == SmallVector<ActorHandle, 4>{
        ActorHandle{2}, ActorHandle{4}, ActorHandle{6}, ActorHandle{1}});
    players.push_back(Ready(5, 20, 50));
    pacer.Synchronize(players);
    CHECK(pacer.GraceMicroseconds(ActorHandle{5}) == 500'000);
    pacer.ElapseMicroseconds(500'000);
    CHECK(pacer.ExpiredPlayers() == SmallVector<ActorHandle, 4>{
        ActorHandle{2}, ActorHandle{3}, ActorHandle{5}, ActorHandle{4}, ActorHandle{6}, ActorHandle{1}});
}

TEST_CASE("Readiness registration pruning and identity changes restart grace", "[pacing]") {
    TickPacer pacer;
    REQUIRE(pacer.SetSettings({100, false, false}));
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready()});
    pacer.ElapseMicroseconds(1'000'000);
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(2, 1)}); // Same persistent ID, replaced handle.
    CHECK(DeadlineWait(pacer) == 1'000'000);
    pacer.ElapseMicroseconds(1'000'000);
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(2, 2)}); // Same handle, distinct persistent ID.
    CHECK(DeadlineWait(pacer) == 1'000'000);
    pacer.Synchronize({});
    CHECK_FALSE(pacer.NextDeadlineMicroseconds().has_value());
    CHECK(pacer.ExpiredPlayers().empty());
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(2, 2, 75)});
    CHECK(DeadlineWait(pacer) == 750'000);
    PlayerReadiness player = Ready(2, 2, 50);
    player.ready = false;
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{player});
    CHECK_FALSE(pacer.NextDeadlineMicroseconds().has_value());
    player.ready = true;
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{player});
    CHECK(DeadlineWait(pacer) == 500'000);
    player.hasCommand = true;
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{player});
    CHECK_FALSE(pacer.NextDeadlineMicroseconds().has_value());
    player.hasCommand = false;
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{player});
    CHECK(DeadlineWait(pacer) == 500'000);
}

TEST_CASE("Fractional pacing accumulates exactly and rounds waits upward", "[pacing]") {
    TickPacer pacer;
    REQUIRE(pacer.SetSettings({3, true, false}));
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(1, 1, 50)});
    CHECK(DeadlineWait(pacer) == 16'666'667);
    CHECK(pacer.MicrosecondsUntilTicks(1) == 333'334);
    pacer.ElapseMicroseconds(333'333);
    CHECK(pacer.AvailableTicks() == 0);
    CHECK(pacer.MicrosecondsUntilTicks(1) == 1);
    REQUIRE(pacer.SetSettings({1, true, false}));
    CHECK(pacer.MicrosecondsUntilTicks(1) == 1);
    pacer.ElapseMicroseconds(1);
    CHECK(pacer.AvailableTicks() == 1);
    pacer.ConsumeTicks(1);
    for (int i = 0; i < 10; ++i) { pacer.ElapseMicroseconds(100'000); }
    CHECK(pacer.AvailableTicks() == 1);
    CHECK(pacer.MicrosecondsUntilTicks(2) == 1'000'000);
}

TEST_CASE("Extreme elapsed ticks and rates saturate without overflow", "[pacing]") {
    TickPacer pacer;
    REQUIRE(pacer.SetSettings({1, true, false}));
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(1, 1, std::numeric_limits<std::uint32_t>::max())});
    CHECK(pacer.MicrosecondsUntilTicks(maximum) == maximum);
    pacer.ElapseMicroseconds(maximum);
    CHECK(pacer.AvailableTicks() == maximum / 1'000'000);
    CHECK(pacer.ExpiredPlayers().size() == 1);
    pacer.ConsumeTicks(pacer.AvailableTicks());
    CHECK(pacer.MicrosecondsUntilTicks(1) == 1'000'000 - maximum % 1'000'000);
    REQUIRE(pacer.SetSettings({std::numeric_limits<std::uint32_t>::max(), true, false}));
    // Full-width target tick conversion remains representable at this rate.
    CHECK(pacer.MicrosecondsUntilTicks(maximum) == 4'294'967'297'000'000ULL);
    pacer.ElapseMicroseconds(maximum);
    CHECK(pacer.AvailableTicks() == maximum);
    CHECK(pacer.MicrosecondsUntilTicks(maximum) == 0);
    pacer.ElapseMicroseconds(maximum);
    CHECK(pacer.AvailableTicks() == maximum);
    pacer.ConsumeTicks(maximum);
    CHECK(pacer.AvailableTicks() == 0);
}

TEST_CASE("Reset clears transient pacing state and preserves settings", "[pacing]") {
    TickPacer pacer;
    const PacingSettings settings{80, true, true};
    REQUIRE(pacer.SetSettings(settings));
    PlayerReadiness busy = Ready(2, 2);
    busy.ready = false;
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(), busy});
    pacer.ElapseMicroseconds(100'001);
    pacer.Reset();
    CHECK(pacer.GetSettings() == settings);
    CHECK(pacer.IsIdle());
    CHECK(pacer.AvailableTicks() == 0);
    CHECK_FALSE(pacer.NextDeadlineMicroseconds().has_value());
    CHECK(pacer.ExpiredPlayers().empty());
    pacer.Synchronize(SmallVector<PlayerReadiness, 4>{Ready(), busy});
    CHECK(DeadlineWait(pacer) == 1'250'000);
    CHECK(pacer.MicrosecondsUntilTicks(1) == 12'500);
}

}
