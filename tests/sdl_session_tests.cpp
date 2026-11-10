#include "sdl/session.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <latch>

using namespace cataclysmx;
using namespace cataclysmx::sdl;

TEST_CASE("Session channels reserve replies and keep only the latest snapshot", "[sdl][session]") {
    SessionChannels channels;
    channels.PublishReply({ReplyKind::Started});
    for (std::size_t i = 0; i < SessionChannels::requestCapacity; ++i) {
        SessionRequest request;
        request.id = i + 1;
        REQUIRE(channels.TrySubmit(request) == EnqueueResult::Accepted);
    }
    CHECK(channels.TrySubmit({}) == EnqueueResult::Busy);
    SessionRequest request;
    for (std::size_t i = 0; i < SessionChannels::requestCapacity; ++i) {
        REQUIRE(channels.TakeRequest(request));
        CHECK(request.id == i + 1);
        channels.PublishReply({ReplyKind::Admission, 1, request.id});
        channels.PublishReply({ReplyKind::Completed, 1, request.id});
    }
    CHECK(channels.TrySubmit({}) == EnqueueResult::Busy);
    SessionReply reply;
    REQUIRE(channels.TryPopReply(reply));
    CHECK(reply.kind == ReplyKind::Started);
    for (std::size_t i = 0; i < SessionChannels::requestCapacity; ++i) {
        REQUIRE(channels.TryPopReply(reply));
        CHECK(reply.id == i + 1);
        CHECK(reply.kind == ReplyKind::Admission);
        REQUIRE(channels.TryPopReply(reply));
        CHECK(reply.id == i + 1);
        CHECK(reply.kind == ReplyKind::Completed);
    }
    CHECK(channels.TrySubmit({}) == EnqueueResult::Accepted);
    channels.Stop();
    CHECK(channels.TrySubmit({}) == EnqueueResult::Stopped);

    SessionSnapshot building;
    building.view.tick = 1;
    building.mapItems.push_back({17, ItemTypeId{1}});
    channels.PublishSnapshot(building);
    building.view.tick = 2;
    building.mapItems = {{18, ItemTypeId{1}}};
    channels.PublishSnapshot(building);
    SessionSnapshot current;
    REQUIRE(channels.TakeSnapshot(current));
    CHECK(current.view.tick == 2);
    REQUIRE(current.mapItems.size() == 1);
    CHECK(current.mapItems[0].item == 18);
    CHECK_FALSE(channels.TakeSnapshot(current));
    building.view.tick = 3;
    building.mapItems = {{19, ItemTypeId{1}}};
    channels.PublishSnapshot(building);
    CHECK(current.view.tick == 2);
    CHECK(current.mapItems[0].item == 18);
    REQUIRE(channels.TakeSnapshot(current));
    CHECK(current.view.tick == 3);
    CHECK(current.mapItems[0].item == 19);
}

namespace {
struct WakeState {
    std::mutex mutex;
    std::condition_variable condition;
    unsigned count = 0;
};
void Wake(void* data) {
    auto& state = *static_cast<WakeState*>(data);
    std::lock_guard lock(state.mutex);
    ++state.count;
    state.condition.notify_one();
}
bool WaitForReply(Session& session, WakeState& wake, SessionReply& reply) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::unique_lock lock(wake.mutex);
    for (;;) {
        if (session.TryPopReply(reply)) { return true; }
        if (wake.condition.wait_until(lock, deadline) == std::cv_status::timeout) { return false; }
    }
}
}

TEST_CASE("Session acknowledges controls and drains accepted work on stop", "[sdl][session]") {
    WakeState wake;
    Session session(TestContent(), 7, {}, Wake, &wake);
    SessionReply reply;
    REQUIRE(WaitForReply(session, wake, reply));
    REQUIRE(reply.kind == ReplyKind::Started);
    SessionRequest pause;
    pause.kind = RequestKind::Pause;
    pause.generation = 7;
    pause.id = 1;
    REQUIRE(session.TrySubmit(pause) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply));
    CHECK(reply.kind == ReplyKind::Paused);
    CHECK(reply.id == 1);
    SessionRequest stale = pause;
    stale.generation = 6;
    stale.id = 2;
    REQUIRE(session.TrySubmit(stale) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply));
    CHECK(reply.kind == ReplyKind::Stale);
    SessionRequest resume = pause;
    resume.kind = RequestKind::Resume;
    resume.id = 3;
    REQUIRE(session.TrySubmit(resume) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply));
    CHECK(reply.kind == ReplyKind::Resumed);
    for (std::uint64_t i = 4; i < 20; ++i) {
        pause.id = i;
        REQUIRE(session.TrySubmit(pause) == EnqueueResult::Accepted);
    }
    session.Stop();
    unsigned count = 0;
    while (session.TryPopReply(reply)) { ++count; }
    CHECK(count == 16);
    CHECK(session.TrySubmit(pause) == EnqueueResult::Stopped);
}

TEST_CASE("Session reports startup failures and rejects old gameplay opportunities", "[sdl][session]") {
    SECTION("Invalid content is an explicit startup failure") {
        ContentCatalog content;
        WakeState wake;
        Session session(content, 1, {}, Wake, &wake);
        SessionReply reply;
        REQUIRE(WaitForReply(session, wake, reply));
        CHECK(reply.kind == ReplyKind::StartupFailed);
        CHECK(reply.startupError == StartupError::InvalidContent);
        session.Stop();
        CHECK(session.TrySubmit({}) == EnqueueResult::Stopped);
    }
    SECTION("Invalid settings are an explicit startup failure") {
        WakeState wake;
        SessionOptions options;
        options.settings.viewRadius = 99;
        Session session(TestContent(), 1, options, Wake, &wake);
        SessionReply reply;
        REQUIRE(WaitForReply(session, wake, reply));
        CHECK(reply.kind == ReplyKind::StartupFailed);
        CHECK(reply.startupError == StartupError::InvalidSettings);
    }
    SECTION("Completed input cannot be retargeted to a new opportunity") {
        WakeState wake;
        Session session(TestContent(), 1, {}, Wake, &wake);
        SessionReply reply;
        REQUIRE(WaitForReply(session, wake, reply));
        REQUIRE(reply.kind == ReplyKind::Started);
        // The owner publishes its first snapshot before acknowledging this pause.
        SessionRequest pause;
        pause.kind = RequestKind::Pause; pause.generation = 1; pause.id = 1;
        REQUIRE(session.TrySubmit(pause) == EnqueueResult::Accepted);
        REQUIRE(WaitForReply(session, wake, reply));
        REQUIRE(reply.kind == ReplyKind::Paused);
        SessionSnapshot snapshot;
        REQUIRE(session.TakeSnapshot(snapshot));
        pause.kind = RequestKind::Resume; pause.id = 2;
        REQUIRE(session.TrySubmit(pause) == EnqueueResult::Accepted);
        REQUIRE(WaitForReply(session, wake, reply));
        REQUIRE(reply.kind == ReplyKind::Resumed);
        SessionRequest command;
        command.generation = 1; command.id = 3;
        command.player = snapshot.view.playerId;
        command.opportunity = snapshot.view.opportunity;
        REQUIRE(session.TrySubmit(command) == EnqueueResult::Accepted);
        REQUIRE(WaitForReply(session, wake, reply));
        REQUIRE(reply.kind == ReplyKind::Admission);
        REQUIRE(reply.id == 3);
        REQUIRE(WaitForReply(session, wake, reply));
        REQUIRE(reply.kind == ReplyKind::Completed);
        CHECK(reply.result.outcome == Outcome::waited);
        command.id = 4;
        REQUIRE(session.TrySubmit(command) == EnqueueResult::Accepted);
        REQUIRE(WaitForReply(session, wake, reply));
        CHECK(reply.kind == ReplyKind::Completed);
        CHECK(reply.id == 4);
        CHECK(reply.result.outcome == Outcome::stale_input);
        CHECK_FALSE(reply.result.advanced);
        CHECK(reply.result.cost == 0);
    }
}

namespace {
struct StalledWake {
    std::latch entered{1};
    std::latch release{1};
    bool first = true; // Only touched by the session thread.
};
void StallOnce(void* data) {
    auto& wake = *static_cast<StalledWake*>(data);
    if (wake.first) {
        wake.first = false;
        wake.entered.count_down();
        wake.release.wait();
    }
}
}

TEST_CASE("Frontend can read a snapshot and fill the inbox while the owner stalls", "[sdl][session]") {
    StalledWake wake;
    Session session(TestContent(), 9, {}, StallOnce, &wake);
    wake.entered.wait();
    SessionReply reply;
    CHECK(session.TryPopReply(reply));
    CHECK(reply.kind == ReplyKind::Started);
    SessionSnapshot current;
    CHECK(session.TakeSnapshot(current));
    CHECK(current.generation == 9);
    CHECK(current.view.width > 0);
    SessionRequest request;
    request.kind = RequestKind::Pause;
    request.generation = 9;
    for (std::size_t i = 0; i < SessionChannels::requestCapacity; ++i) {
        request.id = i + 1;
        CHECK(session.TrySubmit(request) == EnqueueResult::Accepted);
    }
    CHECK(session.TrySubmit(request) == EnqueueResult::Busy);
    std::latch stopping{1};
    std::jthread stopThread([&] { stopping.count_down(); session.Stop(); });
    stopping.wait();
    wake.release.count_down();
    stopThread.join();
    unsigned replies = 0;
    while (session.TryPopReply(reply)) { ++replies; }
    CHECK(replies == SessionChannels::requestCapacity);
}
