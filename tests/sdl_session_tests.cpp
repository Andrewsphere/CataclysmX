#include "sdl/session.h"
#include "content_fixture.h"
#include "core/first_house_scenario.h"
#include "client/item_ui.h"

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

namespace {
bool WaitForItemPause(Session& session, WakeState& wake, SessionSnapshot& snapshot) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::unique_lock lock(wake.mutex);
    for (;;) {
        if (session.TakeSnapshot(snapshot) && snapshot.paused && !snapshot.view.remainingTicks && !snapshot.view.commandQueued) { return true; }
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

TEST_CASE("Paused session inspections and settings never advance time and resume command is ordered", "[sdl][session]") {
    WakeState wake;
    Session session(TestContent(),3,{},Wake,&wake);
    SessionReply reply;
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Started);
    SessionRequest request; request.generation = 3; request.id = 1; request.kind = RequestKind::Pause;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Paused);
    SessionSnapshot snapshot; REQUIRE(session.TakeSnapshot(snapshot));
    REQUIRE(snapshot.paused);
    const auto tick = snapshot.view.tick;
    request.id = 2; request.kind = RequestKind::Inspect; request.cursor = snapshot.view.player_position;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Inspected);
    CHECK_FALSE(reply.details.empty()); CHECK(reply.id == 2);
    CHECK(reply.inspectionView.origin.y == request.cursor.y);
    request.cursor.y = 1; request.id = 20;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Inspected);
    CHECK(reply.inspectionView.origin.y == 1); CHECK(reply.inspectionView.tick == tick);
    CHECK(reply.inspectionView.player_position == snapshot.view.player_position);
    request.id = 3; request.kind = RequestKind::ApplySettings; request.globalSettings.viewRadius = 4;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::SettingsApplied);
    CHECK(reply.runtimeSettings.viewRadius == 4);
    REQUIRE(session.TakeSnapshot(snapshot)); CHECK(snapshot.view.tick == tick); CHECK(snapshot.view.width == 9);
    request.id = 4; request.globalSettings.viewRadius = 65;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); CHECK(reply.kind == ReplyKind::SettingsFailed);
    CHECK_FALSE(reply.details.empty());
    request.id = 5; request.kind = RequestKind::ResumeAndCommand;
    request.player = snapshot.view.playerId; request.opportunity = snapshot.view.opportunity;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Admission);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Completed);
    CHECK(reply.result.outcome == Outcome::waited); CHECK(reply.result.cost == 100);
    CHECK(reply.result.tick == tick); // Admission commits the action before rate-capped recovery.
}

TEST_CASE("AI inspection is paused owner work with identified owned captures", "[sdl][session]") {
    WakeState wake;
    Session session(TestContent(),11,{},Wake,&wake);
    SessionReply reply;
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Started);
    SessionRequest request; request.generation = 11; request.id = 1; request.kind = RequestKind::InspectAi;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Cancelled);
    CHECK(reply.generation == 11); CHECK(reply.id == 1);
    CHECK(reply.aiDebug.image.cells.empty());

    request.kind = RequestKind::Pause; request.id = 2;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Paused);
    SessionSnapshot snapshot; REQUIRE(session.TakeSnapshot(snapshot)); REQUIRE(snapshot.paused);
    const auto tick = snapshot.view.tick;
    const auto player = snapshot.view.player_position;
    request.kind = RequestKind::InspectAi; request.id = 3; request.generation = 10; request.cursor = player;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Stale);
    CHECK(reply.generation == 11); CHECK(reply.id == 3);
    CHECK(reply.aiDebug.image.cells.empty());

    request.generation = 11; request.id = 4;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::AiInspected);
    CHECK(reply.generation == 11); CHECK(reply.id == 4);
    CHECK(reply.aiDebug.cursor == player); CHECK(reply.aiDebug.tick == tick);
    CHECK(reply.aiDebug.actor == snapshot.view.playerId);
    CHECK(reply.aiDebug.image.width == 17); CHECK(reply.aiDebug.image.height == 17);
    CHECK(reply.aiDebug.image.scale == 1);
    REQUIRE(reply.aiDebug.image.cells.size() == 17 * 17);
    REQUIRE_FALSE(reply.aiDebug.details.empty());
    auto retained = reply.aiDebug;
    const auto firstSymbol = retained.image.cells[0].symbol;
    const auto firstDetail = retained.details[0];
    // Mutating the receiver cannot change the saved value or the owner's next capture.
    reply.aiDebug.image.cells[0].symbol = "receiver edit";
    reply.aiDebug.details[0] = "receiver edit";
    request.id = 5;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::AiInspected);
    CHECK(reply.generation == 11); CHECK(reply.id == 5);
    CHECK(reply.aiDebug.cursor == player); CHECK(reply.aiDebug.tick == tick);
    CHECK(reply.aiDebug.image.cells[0].symbol == firstSymbol);
    CHECK(reply.aiDebug.details[0] == firstDetail);
    ++request.cursor.y; ++request.cursor.x; request.id = 6;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::AiInspected);
    CHECK(reply.generation == 11); CHECK(reply.id == 6);
    CHECK(reply.aiDebug.cursor == request.cursor); CHECK(reply.aiDebug.tick == tick);
    CHECK(reply.aiDebug.image.bounds.min.y == request.cursor.y);
    request.kind = RequestKind::Resume; request.id = 7;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Resumed);
    request.kind = RequestKind::InspectAi; request.id = 8;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); CHECK(reply.kind == ReplyKind::Cancelled);
    CHECK(reply.id == 8); CHECK(reply.aiDebug.image.cells.empty());
    session.Stop();
    CHECK(retained.cursor == player); CHECK(retained.tick == tick);
    CHECK(retained.image.cells[0].symbol == firstSymbol);
    CHECK(retained.details[0] == firstDetail);
}
TEST_CASE("First-house session rejects a demo-only catalog before construction", "[sdl][session][house]") {
    WakeState wake;
    SessionOptions options;
    options.scenario = NewGameScenario::FirstHouse;
    Session session(TestContent(), 1, options, Wake, &wake);
    SessionReply reply;
    REQUIRE(WaitForReply(session, wake, reply));
    CHECK(reply.kind == ReplyKind::StartupFailed);
    session.Stop();
    SessionSnapshot snapshot;
    CHECK_FALSE(session.TakeSnapshot(snapshot));
}

TEST_CASE("First-house session publishes the selected seed and planned start", "[sdl][session][house]") {
    const auto content = LoadContentPack(CATACLYSMX_FIRST_HOUSE_PACK);
    REQUIRE(content);
    for (const std::uint32_t seed : {0U, 13U}) {
        const auto start = FindFirstHouseStart(*content, seed);
        REQUIRE(start);
        WakeState wake;
        SessionOptions options;
        options.settings.seed = seed;
        options.scenario = NewGameScenario::FirstHouse;
        Session session(*content, 1, options, Wake, &wake);
        SessionReply reply;
        REQUIRE(WaitForReply(session, wake, reply));
        REQUIRE(reply.kind == ReplyKind::Started);
        session.Stop();
        SessionSnapshot snapshot;
        REQUIRE(session.TakeSnapshot(snapshot));
        CHECK(snapshot.seed == seed);
        CHECK(snapshot.runtimeSettings.seed == seed);
        CHECK(snapshot.view.player_position == start->playerPosition);
    }
}

TEST_CASE("Session completion publishes the next opportunity before accepting another frontend action", "[sdl][session]") {
    WakeState wake; SessionOptions options; options.settings.viewRadius = 4;
    Session session(TestContent(),1,options,Wake,&wake);
    SessionReply reply; REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Started);
    SessionRequest pause; pause.kind = RequestKind::Pause; pause.generation = 1; pause.id = 1;
    REQUIRE(session.TrySubmit(pause) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Paused);
    SessionSnapshot snapshot; REQUIRE(session.TakeSnapshot(snapshot));
    auto opportunity = snapshot.view.opportunity;
    for (std::uint64_t i = 0; i < 8; ++i) {
        SessionRequest command; command.kind = i == 0 ? RequestKind::ResumeAndCommand : RequestKind::Command;
        command.generation = 1; command.id = 2+i; command.player = snapshot.view.playerId;
        command.opportunity = opportunity;
        REQUIRE(session.TrySubmit(command) == EnqueueResult::Accepted);
        REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Admission);
        REQUIRE(WaitForReply(session,wake,reply)); REQUIRE(reply.kind == ReplyKind::Completed);
        REQUIRE(reply.result.outcome == Outcome::waited);
        REQUIRE(session.TakeSnapshot(snapshot));
        REQUIRE(snapshot.view.opportunity > opportunity);
        CHECK(snapshot.view.lastInputSequence == i+1);
        opportunity = snapshot.view.opportunity;
    }
}

TEST_CASE("SDL item owner consumes transfer recovery then pauses with the window retained", "[sdl][session][inventory]") {
    WakeState wake; SessionOptions options;
    options.settings.viewRadius = 4;
    options.settings.pacing.rateLimited = true;
    options.settings.pacing.pauseWhenIdle = false; // Browsing must still pause.
    options.settings.pacing.ticksPerSecond = 1000;
    Session session(TestContent(), 21, options, Wake, &wake);
    SessionReply reply; REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Started);
    SessionRequest request; request.kind = RequestKind::Pause; request.generation = 21; request.id = 1;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Paused);
    SessionSnapshot snapshot; REQUIRE(session.TakeSnapshot(snapshot));
    request.kind = RequestKind::PauseForItems; request.id = 10; request.player = snapshot.view.playerId;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Paused); REQUIRE(reply.id == 10);
    REQUIRE(session.TakeSnapshot(snapshot)); REQUIRE(snapshot.view.remainingTicks == 0);
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    const auto keys = DefaultKeymap();
    ItemUi ui; ui.Open(true); ui.Refresh(snapshot.view, TestContent(), localization, keys);
    ui.Handle({"Tab"}, keys, snapshot.view, TestContent(), localization);
    REQUIRE(ui.Frame().activePane == 1);
    REQUIRE_FALSE(ui.Frame().panes[1].rows.empty());
    ui.Handle({"Enter"}, keys, snapshot.view, TestContent(), localization);
    const auto transfer = ui.TakeCommand(snapshot.view); REQUIRE(transfer);
    REQUIRE(transfer->kind == CommandKind::transfer_item);
    const auto tick = snapshot.view.tick;
    request.kind = RequestKind::ItemCommand; request.id = 2; request.player = snapshot.view.playerId;
    request.opportunity = snapshot.view.opportunity; request.command = *transfer;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Admission);
    CHECK(ui.NeedsTime()); CHECK(ui.IsOpen());
    // Escape cancels the remainder, not the authoritative admitted item.
    ui.Handle({"Escape"}, keys, snapshot.view, TestContent(), localization);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Completed);
    REQUIRE(reply.id == 2); REQUIRE(reply.result.outcome == Outcome::transferred);
    REQUIRE(reply.result.cost > 0);
    const auto cost = reply.result.cost;
    ui.CommandFinished(reply.result);
    CHECK(ui.IsOpen()); CHECK(ui.IsAdvanced()); CHECK(ui.NeedsTime());
    REQUIRE(WaitForItemPause(session, wake, snapshot));
    CHECK(snapshot.view.tick >= tick + cost);
    CHECK(snapshot.runtimeSettings.pacing.pauseWhenIdle == false);
    CHECK(snapshot.view.remainingTicks == 0);
    ui.Refresh(snapshot.view, TestContent(), localization, keys);
    CHECK_FALSE(ui.NeedsTime()); CHECK_FALSE(ui.TakeCommand(snapshot.view));
    const auto paidTick = snapshot.view.tick;
    // An identified read round-trip proves the retained modal stays paused.
    request.kind = RequestKind::Inspect; request.id = 3; request.cursor = snapshot.view.player_position;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Inspected);
    CHECK(reply.inspectionView.tick == paidTick);

    ui.Close(); ui.Open(); ui.Refresh(snapshot.view, TestContent(), localization, keys);
    REQUIRE_FALSE(ui.Frame().panes[0].rows.empty());
    ui.Handle({"d"}, keys, snapshot.view, TestContent(), localization);
    const auto drop = ui.TakeCommand(snapshot.view); REQUIRE(drop);
    request.kind = RequestKind::ItemCommand; request.id = 4; request.command = *drop;
    request.opportunity = snapshot.view.opportunity;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Admission);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Completed);
    REQUIRE(reply.result.outcome == Outcome::dropped);
    ui.CommandFinished(reply.result);
    CHECK_FALSE(ui.IsOpen()); CHECK(ui.NeedsTime());
    REQUIRE(WaitForItemPause(session, wake, snapshot));
    ui.Refresh(snapshot.view, TestContent(), localization, keys);
    CHECK_FALSE(ui.NeedsTime()); CHECK(snapshot.view.tick > paidTick);

    // Stale opportunities and wrong players never leave browsing running.
    request.id = 5; request.opportunity = 0;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Completed);
    CHECK(reply.result.outcome == Outcome::stale_input); CHECK(reply.id == 5);
    REQUIRE(session.TakeSnapshot(snapshot)); CHECK(snapshot.paused);
    request.id = 6; request.player = 0;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); CHECK(reply.kind == ReplyKind::Cancelled); CHECK(reply.id == 6);
}

TEST_CASE("Opening an SDL item window waits for an already paid action before acknowledging pause", "[sdl][session][inventory]") {
    WakeState wake; SessionOptions options;
    options.settings.pacing.rateLimited = true; options.settings.pacing.ticksPerSecond = 1000;
    Session session(TestContent(), 22, options, Wake, &wake);
    SessionReply reply; REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Started);
    SessionRequest request; request.kind = RequestKind::Pause; request.generation = 22; request.id = 1;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Paused);
    SessionSnapshot snapshot; REQUIRE(session.TakeSnapshot(snapshot));
    request.kind = RequestKind::ResumeAndCommand; request.id = 2;
    request.player = snapshot.view.playerId; request.opportunity = snapshot.view.opportunity;
    const auto tick = snapshot.view.tick;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Admission);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Completed);
    REQUIRE(reply.result.cost > 0); const auto cost = reply.result.cost;
    request.kind = RequestKind::PauseForItems; request.id = 3;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Paused); CHECK(reply.id == 3);
    REQUIRE(session.TakeSnapshot(snapshot)); CHECK(snapshot.paused);
    CHECK(snapshot.view.remainingTicks == 0); CHECK(snapshot.view.tick >= tick + cost);
}

TEST_CASE("SDL item recovery rejects controls and replacement commands by receipt identity", "[sdl][session][inventory]") {
    WakeState wake; SessionOptions options;
    options.settings.pacing.rateLimited = true; options.settings.pacing.ticksPerSecond = 1;
    Session session(TestContent(), 23, options, Wake, &wake);
    SessionReply reply; REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Started);
    SessionRequest request; request.generation = 23; request.kind = RequestKind::Pause; request.id = 1;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Paused);
    SessionSnapshot snapshot; REQUIRE(session.TakeSnapshot(snapshot));
    request.player = snapshot.view.playerId; request.opportunity = snapshot.view.opportunity;
    request.kind = RequestKind::ItemCommand; request.id = 2; // A long paid wait isolates recovery from admission.
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Admission); CHECK(reply.id == 2);
    REQUIRE(WaitForReply(session, wake, reply)); REQUIRE(reply.kind == ReplyKind::Completed); CHECK(reply.id == 2);
    REQUIRE(reply.result.cost == ticks_per_turn);
    request.kind = RequestKind::Pause; request.id = 3;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    request.kind = RequestKind::Resume; request.id = 4;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    request.kind = RequestKind::ItemCommand; request.id = 5;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    request.kind = RequestKind::Pause; request.id = 6; request.generation = 22;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    for (std::uint64_t id = 3; id <= 6; ++id) {
        REQUIRE(WaitForReply(session, wake, reply)); CHECK(reply.id == id); CHECK(reply.generation == 23);
        CHECK(reply.kind == (id == 6 ? ReplyKind::Stale : ReplyKind::Cancelled));
    }
    REQUIRE(session.TakeSnapshot(snapshot)); CHECK_FALSE(snapshot.paused);
    CHECK(snapshot.view.remainingTicks > 0);
    session.Stop();
}
