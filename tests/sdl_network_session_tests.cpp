#include "sdl/session.h"
#include "content_fixture.h"
#include "core/first_house_scenario.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::sdl;

struct SessionFiles {
    std::filesystem::path directory;
    std::string save;
    std::string profile;
    SessionFiles() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; ; ++attempt) {
            directory = parent / ("cataclysmx-sdl-network-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(directory, error);
            REQUIRE_FALSE(error);
            if (created) { break; }
        }
        save = (directory / "world.sqlite").string();
        profile = (directory / "profile.txt").string();
    }
    ~SessionFiles() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

template<class Predicate>
bool Until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do {
        if (predicate()) { return true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

SessionReply Next(Session& session) {
    SessionReply reply;
    REQUIRE(Until([&] { return session.TryPopReply(reply); }));
    INFO("Reply id=" << reply.id << " kind=" << static_cast<int>(reply.kind));
    return reply;
}

SessionSnapshot Snapshot(Session& session) {
    SessionSnapshot snapshot;
    REQUIRE(Until([&] { return session.TakeSnapshot(snapshot); }));
    return snapshot;
}

SessionRequest Make(RequestKind kind, std::uint64_t generation, std::uint64_t id) {
    SessionRequest request;
    request.kind = kind; request.generation = generation; request.id = id;
    return request;
}

SessionReply Control(Session& session, SessionRequest request) {
    const auto id = request.id;
    REQUIRE(session.TrySubmit(eastl::move(request)) == EnqueueResult::Accepted);
    auto reply = Next(session);
    REQUIRE(reply.id == id);
    return reply;
}

SessionSnapshot Active(Session& session) {
    SessionSnapshot snapshot;
    REQUIRE(Until([&] {
        session.TakeSnapshot(snapshot);
        return snapshot.mode == SessionMode::Guest && snapshot.connectionState == network::ConnectionState::Active;
    }));
    return snapshot;
}

SessionReply Completion(Session& session) {
    auto reply = Next(session);
    if (reply.kind == ReplyKind::Admission) { reply = Next(session); }
    REQUIRE(reply.kind == ReplyKind::Completed);
    return reply;
}

SessionOptions HostOptions(const SessionFiles& files) {
    SessionOptions options;
    options.host = true;
    options.hostSettings = {0, 0, "SDL loopback"};
    options.settings.viewRadius = 4;
    options.settings.pacing.ticksPerSecond = 1000;
    options.profilePath = files.profile.c_str();
    return options;
}

SessionOptions GuestOptions(const SessionFiles& files, std::uint16_t port) {
    SessionOptions options;
    options.join = true;
    // This catalog has no first-house start. A remote join must not validate it.
    options.scenario = NewGameScenario::FirstHouse;
    options.settings.viewRadius = 4;
    options.profilePath = files.profile.c_str();
    options.connectionSettings = {"127.0.0.1", port, "SDL guest", 0};
    return options;
}

TEST_CASE("SDL LAN menus keep transport and authoritative rounds running", "[sdl][session][network][loopback]") {
    SessionFiles hostFiles, guestFiles;
    Session host(TestContent(), 10, HostOptions(hostFiles));
    REQUIRE(Next(host).kind == ReplyKind::Started);
    auto hostView = Snapshot(host);
    REQUIRE(hostView.mode == SessionMode::Host);
    REQUIRE(hostView.hostPort != 0);
    Session guest(TestContent(), 20, GuestOptions(guestFiles, hostView.hostPort));
    REQUIRE(Next(guest).kind == ReplyKind::Started);
    auto guestView = Active(guest);
    client::LanProfile persisted;
    REQUIRE(persisted.Load(guestFiles.profile.c_str()));
    const auto* identity = persisted.Find("127.0.0.1", hostView.hostPort);
    REQUIRE(identity);
    CHECK(identity->token != 0);
    const auto hostGeneration = hostView.generation;
    const auto guestGeneration = guestView.generation;
    CHECK(guestView.input.opportunity != 0);
    CHECK_FALSE(guestView.hasSave);
    REQUIRE(Control(host, Make(RequestKind::Pause, hostGeneration, 1)).kind == ReplyKind::MenuOpened);
    hostView = Snapshot(host);
    CHECK(hostView.menuOpen); CHECK_FALSE(hostView.paused);
    REQUIRE(Control(guest, Make(RequestKind::PauseForItems, guestGeneration, 1)).kind == ReplyKind::MenuOpened);
    guestView = Snapshot(guest);
    CHECK(guestView.menuOpen); CHECK_FALSE(guestView.paused);

    const auto oldOpportunity = guestView.input.opportunity;
    auto guestCommand = Make(RequestKind::ItemCommand, guestGeneration, 2);
    guestCommand.player = guestView.view.playerId;
    guestCommand.opportunity = oldOpportunity;
    guestCommand.command.waitCost = 1;
    auto hostCommand = Make(RequestKind::Command, hostGeneration, 2);
    hostCommand.player = hostView.view.playerId;
    hostCommand.opportunity = hostView.input.opportunity;
    hostCommand.command.waitCost = 1;
    REQUIRE(guest.TrySubmit(guestCommand) == EnqueueResult::Accepted);
    REQUIRE(host.TrySubmit(hostCommand) == EnqueueResult::Accepted);
    auto admission = Next(guest);
    REQUIRE(admission.kind == ReplyKind::Admission);
    auto completion = Next(guest);
    REQUIRE(completion.kind == ReplyKind::Completed);
    CHECK(completion.result.outcome == Outcome::waited);
    CHECK(completion.input.opportunity > oldOpportunity);
    REQUIRE(Next(host).kind == ReplyKind::Admission);
    REQUIRE(Next(host).kind == ReplyKind::Completed);
    hostView = Snapshot(host);
    CHECK(hostView.menuOpen); CHECK_FALSE(hostView.paused);

    // Explicit opportunity stays stale even when the receipt has newer context.
    guestCommand.id = 3;
    REQUIRE(guest.TrySubmit(guestCommand) == EnqueueResult::Accepted);
    auto stale = Next(guest);
    REQUIRE(stale.kind == ReplyKind::Completed);
    CHECK(stale.result.outcome == Outcome::stale_input);
    CHECK(stale.input.opportunity >= completion.input.opportunity);

    auto inspect = Make(RequestKind::Inspect, guestGeneration, 4);
    inspect.cursor = guestView.view.player_position;
    auto inspected = Control(guest, inspect);
    REQUIRE(inspected.kind == ReplyKind::Inspected);
    CHECK_FALSE(inspected.networkError);
    CHECK_FALSE(inspected.details.empty());
    CHECK(inspected.inspectionView.playerId == guestCommand.player);
    CHECK(inspected.inspectionView.origin.y == inspect.cursor.y);
    CHECK_FALSE(inspected.inspectionItems.empty());
    auto ai = Control(guest, Make(RequestKind::InspectAi, guestGeneration, 5));
    CHECK(ai.kind == ReplyKind::Cancelled);

    auto preferences = Make(RequestKind::ApplySettings, guestGeneration, 6);
    preferences.globalSettings.viewRadius = 3;
    preferences.globalSettings.language = "en";
    preferences.globalSettings.keyboardRepeatRate = 15;
    preferences.globalSettings.messageHistorySize = 20;
    const auto applied = Control(guest, preferences);
    REQUIRE(applied.kind == ReplyKind::SettingsApplied);
    const auto resized = Snapshot(guest);
    CHECK(resized.view.width == 7);
    CHECK(resized.view.height == 7);
    CHECK(resized.mapItems.size() == resized.view.cells.size());
    CHECK(resized.view.origin.x == resized.view.player_position.x - 3);
    CHECK(applied.runtimeSettings.viewRadius == 3);
    CHECK(applied.runtimeSettings.keyboardRepeatRate == 15);
    CHECK(applied.runtimeSettings.messageHistorySize == 20);
    CHECK(applied.runtimeSettings.pacing == hostView.runtimeSettings.pacing);
    preferences.id = 7; preferences.globalSettings.pauseWhenIdle = false;
    REQUIRE(Control(guest, preferences).kind == ReplyKind::SettingsFailed);
    preferences.globalSettings.pauseWhenIdle.reset();
    preferences.worldSettings.seed = hostView.seed + 1;
    preferences.id = 11;
    REQUIRE(Control(guest, preferences).kind == ReplyKind::SettingsFailed);
    preferences.worldSettings.seed.reset(); preferences.worldSettings.activeRadius = 2;
    preferences.id = 12;
    REQUIRE(Control(guest, preferences).kind == ReplyKind::SettingsFailed);

    const auto returned = Control(guest, Make(RequestKind::Disconnect, guestGeneration, 8));
    REQUIRE(returned.kind == ReplyKind::ControlCompleted);
    REQUIRE(returned.generation > guestGeneration);
    auto old = Make(RequestKind::ResumeAndCommand, guestGeneration, 9);
    old.player = guestCommand.player; old.opportunity = completion.input.opportunity;
    const auto obsolete = Control(guest, old);
    REQUIRE(obsolete.kind == ReplyKind::Stale);
    CHECK(obsolete.generation == guestGeneration);
    REQUIRE(Control(guest, Make(RequestKind::Pause, returned.generation, 10)).kind == ReplyKind::Paused);
    auto local = Snapshot(guest);
    CHECK(local.mode == SessionMode::Local); CHECK(local.paused);
    CHECK(local.status.find("Demo fallback") != String::npos);
    CHECK_FALSE(guest.Stop()); CHECK_FALSE(host.Stop());
}

TEST_CASE("SDL host shutdown persists parked guest inventory and reconnect identity", "[sdl][session][network][save]") {
    SessionFiles hostFiles, guestFiles;
    constexpr std::uint64_t token = 73129;
    PersistentId player = 0;
    Vector<InventoryEntry> inventory;
    {
        Server seed(TestContent());
        REQUIRE(seed.SetCircleResidency(1, CircleResidency{48}));
        auto joined = seed.JoinPlayer("SDL guest", 0, token);
        REQUIRE(joined);
        player = joined->player;
        REQUIRE(seed.ActivatePlayer(player));
        const auto actor = seed.GetWorld().FindActor(player);
        REQUIRE(actor.value != 0);
        REQUIRE(seed.GetWorld().CreateItem(TestItem("rock"), ItemLocation::Carried(actor)));
        inventory = seed.ViewForPlayer(player).inventory;
        REQUIRE_FALSE(inventory.empty());
        REQUIRE(seed.DisconnectPlayer(player));
        REQUIRE(seed.CreateSave(hostFiles.save.c_str()));
    }
    auto options = HostOptions(hostFiles);
    options.loadPath = hostFiles.save.c_str();
    options.settingsOverridesProvided = true;
    options.globalSettings.viewRadius = 4;
    options.globalSettings.ticksPerSecond = 1000;
    std::uint16_t port = 0;
    {
        Session host(TestContent(), 30, options);
        REQUIRE(Next(host).kind == ReplyKind::Started);
        auto view = Snapshot(host);
        CHECK(view.runtimeSettings.residentRadiusTiles == 48);
        REQUIRE(view.hasSave);
        port = view.hostPort;
        client::LanProfile identity;
        REQUIRE(identity.Set({"127.0.0.1", port, token, "SDL guest"}));
        REQUIRE(identity.Save(guestFiles.profile.c_str()));
        Session guest(TestContent(), 40, GuestOptions(guestFiles, port));
        REQUIRE(Next(guest).kind == ReplyKind::Started);
        auto joined = Active(guest);
        CHECK(joined.view.playerId == player);
        CHECK(joined.view.inventory == inventory);
        REQUIRE(Control(host, Make(RequestKind::CloseLan, view.generation, 1)).kind == ReplyKind::ControlCompleted);
        SessionSnapshot returned;
        REQUIRE(Until([&] {
            guest.TakeSnapshot(returned);
            return returned.mode == SessionMode::Local && returned.generation > joined.generation;
        }));
        CHECK(returned.statusVersion > joined.statusVersion);
        auto open = Make(RequestKind::OpenLan, view.generation, 2);
        open.hostSettings = {port, 0, "Reopened SDL"};
        REQUIRE(Control(host, open).kind == ReplyKind::ControlCompleted);
        auto connect = Make(RequestKind::Connect, returned.generation, 1);
        connect.connectionSettings = {"127.0.0.1", port, "SDL guest", 0};
        REQUIRE(Control(guest, connect).kind == ReplyKind::ControlCompleted);
        auto rejoined = Active(guest);
        CHECK(rejoined.view.playerId == player); CHECK(rejoined.view.inventory == inventory);
        // Stop host while guest is active: Close must park it before final Save.
        CHECK_FALSE(host.Stop());
        CHECK_FALSE(guest.Stop());
    }
    {
        auto loaded = Server::LoadSave(TestContent(), hostFiles.save.c_str());
        REQUIRE(loaded);
        bool parked = false;
        for (const auto& status : (*loaded)->GetPlayers()) {
            if (status.id == player) { parked = status.state == PlayerConnectionState::Parked; }
        }
        REQUIRE(parked);
    }
    options.hostSettings.port = port;
    Session reloaded(TestContent(), 50, options);
    REQUIRE(Next(reloaded).kind == ReplyKind::Started);
    Session guest(TestContent(), 60, GuestOptions(guestFiles, port));
    REQUIRE(Next(guest).kind == ReplyKind::Started);
    auto joined = Active(guest);
    CHECK(joined.view.playerId == player); CHECK(joined.view.inventory == inventory);
    CHECK_FALSE(guest.Stop()); CHECK_FALSE(reloaded.Stop());
}

TEST_CASE("SDL profile failure blocks guest join but permits local hosting", "[sdl][session][network][profile]") {
    SessionFiles files;
    auto options = HostOptions(files);
    // A directory is not a readable profile file.
    options.profilePath = files.directory.string().c_str();
    Session host(TestContent(), 70, options);
    REQUIRE(Next(host).kind == ReplyKind::Started);
    const auto view = Snapshot(host);
    CHECK(view.mode == SessionMode::Host); REQUIRE(view.profileError);
    options.host = false; options.join = true;
    options.connectionSettings.port = view.hostPort;
    Session guest(TestContent(), 80, options);
    auto failed = Next(guest);
    CHECK(failed.kind == ReplyKind::StartupFailed);
    CHECK(failed.startupError == StartupError::Network);
    REQUIRE(failed.profileError);
    CHECK_FALSE(guest.Stop()); CHECK_FALSE(host.Stop());
}

TEST_CASE("SDL discovery continues while the local world is paused and stops explicitly", "[sdl][session][network][discovery]") {
    SessionFiles hostFiles, localFiles;
    Session host(TestContent(), 90, HostOptions(hostFiles));
    REQUIRE(Next(host).kind == ReplyKind::Started);
    const auto hosted = Snapshot(host);
    SessionOptions options;
    options.settings.viewRadius = 4;
    options.profilePath = localFiles.profile.c_str();
    Session local(TestContent(), 100, options);
    REQUIRE(Next(local).kind == ReplyKind::Started);
    REQUIRE(Control(local, Make(RequestKind::Pause, 100, 1)).kind == ReplyKind::Paused);
    const auto paused = Snapshot(local);
    auto refresh = Make(RequestKind::RefreshDiscovery, 100, 2);
    refresh.hostSettings.discoveryPort = hosted.discoveryPort;
    refresh.discoveryAddress = "127.0.0.1";
    REQUIRE(Control(local, refresh).kind == ReplyKind::ControlCompleted);
    SessionSnapshot discovered;
    REQUIRE(Until([&] {
        local.TakeSnapshot(discovered);
        return !discovered.sessions.empty();
    }));
    REQUIRE(discovered.sessions.size() == 1);
    CHECK(discovered.sessions[0].gamePort == hosted.hostPort);
    CHECK(discovered.sessions[0].compatible);
    CHECK(discovered.view.tick == paused.view.tick);
    CHECK(discovered.paused);
    REQUIRE(Control(local, Make(RequestKind::StopDiscovery, 100, 3)).kind == ReplyKind::ControlCompleted);
    const auto stopped = Snapshot(local);
    CHECK(stopped.sessions.empty()); CHECK(stopped.paused);
    CHECK_FALSE(local.Stop()); CHECK_FALSE(host.Stop());
}

TEST_CASE("SDL local save creation and shutdown checkpoint retain authoritative state", "[sdl][session][save]") {
    SessionFiles files;
    SessionOptions options;
    options.settings.viewRadius = 4;
    options.settings.seed = 198734;
    options.settingsOverridesProvided = true;
    options.globalSettings.viewRadius = 4;
    options.globalSettings.seed = 42; // App's explicit --seed takes creation precedence.
    options.savePath = files.save.c_str();
    options.profilePath = files.profile.c_str();
    Session session(TestContent(), 110, options);
    REQUIRE(Next(session).kind == ReplyKind::Started);
    REQUIRE(Control(session, Make(RequestKind::Pause, 110, 1)).kind == ReplyKind::Paused);
    const auto before = Snapshot(session);
    REQUIRE(before.hasSave);
    CHECK(before.seed == 198734);
    CHECK(before.runtimeSettings.seed == 198734);
    auto request = Make(RequestKind::ResumeAndCommand, 110, 2);
    request.player = before.view.playerId; request.opportunity = before.input.opportunity;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(Next(session).kind == ReplyKind::Admission);
    const auto completed = Next(session);
    REQUIRE(completed.kind == ReplyKind::Completed);
    REQUIRE(completed.input.opportunity > before.input.opportunity);
    REQUIRE(Control(session, Make(RequestKind::Pause, 110, 3)).kind == ReplyKind::Paused);
    const auto checkpoint = Snapshot(session);
    REQUIRE(Control(session, Make(RequestKind::Save, 110, 4)).kind == ReplyKind::ControlCompleted);
    CHECK_FALSE(session.Stop());
    auto loaded = Server::LoadSave(TestContent(), files.save.c_str());
    REQUIRE(loaded);
    CHECK((*loaded)->GetWorld().GetSeed() == 198734);
    CHECK((*loaded)->View(4).tick == checkpoint.view.tick);
    CHECK((*loaded)->View(4).inventory == checkpoint.view.inventory);
    CHECK((*loaded)->View(4).remainingTicks == checkpoint.view.remainingTicks);
}

TEST_CASE("SDL host pacing settings apply live and are replicated read-only to guests", "[sdl][session][network][settings]") {
    SessionFiles hostFiles, guestFiles;
    Session host(TestContent(), 300, HostOptions(hostFiles));
    REQUIRE(Next(host).kind == ReplyKind::Started);
    const auto hosted = Snapshot(host);
    Session guest(TestContent(), 400, GuestOptions(guestFiles, hosted.hostPort));
    REQUIRE(Next(guest).kind == ReplyKind::Started);
    auto joined = Active(guest);
    REQUIRE(Control(host, Make(RequestKind::Pause, hosted.generation, 1)).kind == ReplyKind::MenuOpened);
    REQUIRE(Control(guest, Make(RequestKind::Pause, joined.generation, 1)).kind == ReplyKind::MenuOpened);
    auto request = Make(RequestKind::ApplySettings, hosted.generation, 2);
    request.worldSettings.ticksPerSecond = 250;
    request.worldSettings.rateLimited = true;
    request.worldSettings.pauseWhenIdle = true;
    const auto applied = Control(host, request);
    REQUIRE(applied.kind == ReplyKind::SettingsApplied);
    const PacingSettings expected{250, true, true};
    CHECK(applied.runtimeSettings.pacing == expected);
    auto current = Snapshot(host);
    CHECK(current.menuOpen); CHECK_FALSE(current.paused);
    REQUIRE(Until([&] {
        guest.TakeSnapshot(joined);
        return joined.view.pacing == expected;
    }));
    CHECK(joined.menuOpen); CHECK_FALSE(joined.paused);
    request.generation = joined.generation; request.id = 2;
    request.worldSettings.ticksPerSecond = 999;
    REQUIRE(Control(guest, request).kind == ReplyKind::SettingsFailed);
    request.generation = hosted.generation; request.id = 3;
    request.worldSettings.ticksPerSecond = 0;
    REQUIRE(Control(host, request).kind == ReplyKind::SettingsFailed);
    CHECK_FALSE(guest.Stop()); CHECK_FALSE(host.Stop());
}

TEST_CASE("SDL persistence startup failures carry typed errors", "[sdl][session][save]") {
    SessionFiles files;
    SessionOptions options;
    options.profilePath = files.profile.c_str();
    SECTION("Remote join cannot create a local save") {
        options.join = true; options.savePath = files.save.c_str();
        Session session(TestContent(), 120, options);
        const auto failure = Next(session);
        REQUIRE(failure.kind == ReplyKind::StartupFailed);
        CHECK(failure.startupError == StartupError::InvalidSettings);
        CHECK_FALSE(failure.details.empty());
    }
    SECTION("Missing save is a storage failure") {
        options.loadPath = files.save.c_str();
        Session session(TestContent(), 120, options);
        const auto failure = Next(session);
        REQUIRE(failure.kind == ReplyKind::StartupFailed);
        CHECK(failure.startupError == StartupError::Storage);
        CHECK(failure.storageError.has_value());
        CHECK_FALSE(failure.details.empty());
    }
}

TEST_CASE("SDL returning from a guest preserves an existing paused world exactly", "[sdl][session][network][loopback]") {
    SessionFiles hostFiles, localFiles;
    Session host(TestContent(), 130, HostOptions(hostFiles));
    REQUIRE(Next(host).kind == ReplyKind::Started);
    const auto hosted = Snapshot(host);
    SessionOptions options;
    options.settings.seed = 9137;
    options.settings.viewRadius = 4;
    options.settings.pacing.rateLimited = false;
    options.profilePath = localFiles.profile.c_str();
    Session local(TestContent(), 140, options);
    REQUIRE(Next(local).kind == ReplyKind::Started);
    REQUIRE(Control(local, Make(RequestKind::Pause, 140, 1)).kind == ReplyKind::Paused);
    auto before = Snapshot(local);
    auto command = Make(RequestKind::ResumeAndCommand, 140, 2);
    command.player = before.view.playerId; command.opportunity = before.input.opportunity;
    command.command.waitCost = 7;
    REQUIRE(local.TrySubmit(command) == EnqueueResult::Accepted);
    REQUIRE(Completion(local).result.outcome == Outcome::waited);
    auto pause = Make(RequestKind::PauseForItems, 140, 3);
    pause.player = before.view.playerId;
    REQUIRE(Control(local, pause).kind == ReplyKind::Paused);
    before = Snapshot(local);
    REQUIRE(before.view.tick >= 7);
    REQUIRE(before.paused);
    const auto pacing = before.runtimeSettings.pacing;
    auto connect = Make(RequestKind::Connect, 140, 4);
    connect.connectionSettings = {"127.0.0.1", hosted.hostPort, "Visitor", 0};
    REQUIRE(Control(local, connect).kind == ReplyKind::ControlCompleted);
    const auto remote = Active(local);
    REQUIRE(remote.mode == SessionMode::Guest);
    REQUIRE(Control(local, Make(RequestKind::Pause, remote.generation, 5)).kind == ReplyKind::MenuOpened);
    const auto returned = Control(local, Make(RequestKind::Disconnect, remote.generation, 6));
    REQUIRE(returned.kind == ReplyKind::ControlCompleted);
    const auto after = Snapshot(local);
    CHECK(after.mode == SessionMode::Local);
    CHECK(after.paused); CHECK(after.menuOpen);
    CHECK(after.seed == before.seed);
    CHECK(after.view.tick == before.view.tick);
    CHECK(after.view.player_position == before.view.player_position);
    CHECK(after.view.playerId == before.view.playerId);
    CHECK(after.view.inventory == before.view.inventory);
    CHECK(after.input.opportunity == before.input.opportunity);
    CHECK(after.runtimeSettings.pacing == pacing);
    CHECK_FALSE(local.Stop()); CHECK_FALSE(host.Stop());
}

TEST_CASE("SDL startup join returns to the valid selected local scenario", "[sdl][session][network][house]") {
    auto scenario = NewGameScenario::FirstHouse;
    const char* pack = CATACLYSMX_FIRST_HOUSE_PACK;
    SECTION("First house") {}
    SECTION("Town") { scenario = NewGameScenario::Town; pack = CATACLYSMX_CONTENT_PACK; }
    const auto content = LoadContentPack(pack);
    REQUIRE(content);
    constexpr std::uint32_t seed = 13;
    const auto start = scenario == NewGameScenario::Town
        ? FindTownStart(*content, seed) : FindFirstHouseStart(*content, seed);
    REQUIRE(start);
    SessionFiles hostFiles, guestFiles;
    Session host(*content, 150, HostOptions(hostFiles));
    REQUIRE(Next(host).kind == ReplyKind::Started);
    const auto hosted = Snapshot(host);
    auto options = GuestOptions(guestFiles, hosted.hostPort);
    options.scenario = scenario;
    options.settings.seed = seed;
    Session guest(*content, 160, options);
    REQUIRE(Next(guest).kind == ReplyKind::Started);
    const auto remote = Active(guest);
    const auto returned = Control(guest, Make(RequestKind::Disconnect, remote.generation, 1));
    REQUIRE(returned.kind == ReplyKind::ControlCompleted);
    REQUIRE(Control(guest, Make(RequestKind::Pause, returned.generation, 2)).kind == ReplyKind::Paused);
    const auto local = Snapshot(guest);
    CHECK(local.seed == seed);
    CHECK(local.view.player_position == start->playerPosition);
    CHECK(local.status.find("Demo fallback") == String::npos);
    CHECK_FALSE(guest.Stop()); CHECK_FALSE(host.Stop());
}

TEST_CASE("SDL unknown profile tokens survive rejection until new-character admission", "[sdl][session][network][profile]") {
    SessionFiles hostFiles, guestFiles;
    Session host(TestContent(), 170, HostOptions(hostFiles));
    REQUIRE(Next(host).kind == ReplyKind::Started);
    const auto hosted = Snapshot(host);
    constexpr std::uint64_t unknownToken = 912831;
    client::LanProfile profile;
    REQUIRE(profile.Set({"127.0.0.1", hosted.hostPort, unknownToken, "Earlier identity"}));
    REQUIRE(profile.Save(guestFiles.profile.c_str()));
    Session guest(TestContent(), 180, GuestOptions(guestFiles, hosted.hostPort));
    REQUIRE(Next(guest).kind == ReplyKind::Started);
    SessionSnapshot rejected;
    REQUIRE(Until([&] {
        guest.TakeSnapshot(rejected);
        return rejected.mode == SessionMode::Local && rejected.networkError == network::NetworkError::UnknownToken;
    }));
    REQUIRE(profile.Load(guestFiles.profile.c_str()));
    REQUIRE(profile.Find("127.0.0.1", hosted.hostPort));
    CHECK(profile.Find("127.0.0.1", hosted.hostPort)->token == unknownToken);
    CHECK(profile.Find("127.0.0.1", hosted.hostPort)->name == "Earlier identity");

    // An explicit fresh-character attempt that never reaches admission must
    // leave the earlier identity intact as well.
    auto connect = Make(RequestKind::Connect, rejected.generation, 1);
    connect.newCharacter = true;
    connect.connectionSettings = {"127.0.0.1", hosted.hostPort, String(33, 'x'), 0};
    const auto invalid = Control(guest, connect);
    REQUIRE(invalid.kind == ReplyKind::ControlFailed);
    REQUIRE(profile.Load(guestFiles.profile.c_str()));
    REQUIRE(profile.Find("127.0.0.1", hosted.hostPort));
    CHECK(profile.Find("127.0.0.1", hosted.hostPort)->token == unknownToken);
    connect.id = 2; connect.generation = invalid.generation;
    connect.connectionSettings.playerName = "Replacement";
    REQUIRE(Control(guest, connect).kind == ReplyKind::ControlCompleted);
    const auto joined = Active(guest);
    REQUIRE(profile.Load(guestFiles.profile.c_str()));
    const auto* replacement = profile.Find("127.0.0.1", hosted.hostPort);
    REQUIRE(replacement);
    CHECK_FALSE(joined.profileError);
    CHECK(replacement->token != 0); CHECK(replacement->token != unknownToken);
    CHECK(replacement->name == "Replacement");
    CHECK(joined.view.playerId != 0);
    CHECK_FALSE(guest.Stop()); CHECK_FALSE(host.Stop());
}

TEST_CASE("SDL item transfers resolve while both LAN peers retain live menus", "[sdl][session][network][inventory]") {
    SessionFiles hostFiles, guestFiles;
    constexpr std::uint64_t token = 592318;
    PersistentId item = 0;
    {
        Server seed(TestContent());
        auto joined = seed.JoinPlayer("Carrier", 0, token);
        REQUIRE(joined);
        REQUIRE(seed.ActivatePlayer(joined->player));
        const auto actor = seed.GetWorld().FindActor(joined->player);
        const auto created = seed.GetWorld().CreateItem(TestItem("rock"), ItemLocation::Carried(actor));
        REQUIRE(created);
        item = seed.GetWorld().GetItem(*created)->id;
        REQUIRE(seed.DisconnectPlayer(joined->player));
        REQUIRE(seed.CreateSave(hostFiles.save.c_str()));
    }
    auto options = HostOptions(hostFiles);
    options.loadPath = hostFiles.save.c_str();
    Session host(TestContent(), 190, options);
    REQUIRE(Next(host).kind == ReplyKind::Started);
    auto hosted = Snapshot(host);
    client::LanProfile profile;
    REQUIRE(profile.Set({"127.0.0.1", hosted.hostPort, token, "Carrier"}));
    REQUIRE(profile.Save(guestFiles.profile.c_str()));
    Session guest(TestContent(), 200, GuestOptions(guestFiles, hosted.hostPort));
    REQUIRE(Next(guest).kind == ReplyKind::Started);
    const auto joined = Active(guest);
    REQUIRE(Control(host, Make(RequestKind::Pause, hosted.generation, 1)).kind == ReplyKind::MenuOpened);
    REQUIRE(Control(guest, Make(RequestKind::PauseForItems, joined.generation, 1)).kind == ReplyKind::MenuOpened);
    auto transfer = Make(RequestKind::ItemCommand, joined.generation, 2);
    transfer.player = joined.view.playerId; transfer.opportunity = joined.input.opportunity;
    transfer.command.kind = CommandKind::transfer_item;
    transfer.command.item = item;
    transfer.command.source = {};
    transfer.command.destination = {ItemStorageKind::Ground, joined.view.player_position, 0};
    hosted = Snapshot(host);
    auto wait = Make(RequestKind::Command, hosted.generation, 2);
    wait.player = hosted.view.playerId; wait.opportunity = hosted.input.opportunity;
    REQUIRE(guest.TrySubmit(transfer) == EnqueueResult::Accepted);
    REQUIRE(host.TrySubmit(wait) == EnqueueResult::Accepted);
    const auto completed = Completion(guest);
    REQUIRE(completed.result.outcome == Outcome::transferred);
    CHECK(completed.result.cost > 0);
    CHECK(completed.input.opportunity > transfer.opportunity);
    REQUIRE(Completion(host).result.outcome == Outcome::waited);
    SessionSnapshot delivered;
    REQUIRE(Until([&] {
        guest.TakeSnapshot(delivered);
        for (const auto& entry : delivered.view.groundItems) { if (entry.id == item) { return true; } }
        return false;
    }));
    CHECK(delivered.menuOpen); CHECK_FALSE(delivered.paused);
    CHECK(delivered.runtimeSettings.pacing.pauseWhenIdle); // Hosting policy stays authoritative.
    bool retained = false;
    for (const auto& entry : delivered.view.inventory) { if (entry.id == item) { retained = true; } }
    CHECK_FALSE(retained);
    hosted = Snapshot(host);
    CHECK(hosted.menuOpen); CHECK_FALSE(hosted.paused);
    CHECK_FALSE(guest.Stop()); CHECK_FALSE(host.Stop());
}

TEST_CASE("SDL pending local inputs cannot be trapped by pause and Stop drains receipts", "[sdl][session]") {
    SessionFiles files;
    SessionOptions options;
    options.profilePath = files.profile.c_str();
    options.settings.viewRadius = 4;
    options.settings.pacing.ticksPerSecond = 1;
    options.settings.pacing.rateLimited = true;
    Session session(TestContent(), 210, options);
    REQUIRE(Next(session).kind == ReplyKind::Started);
    REQUIRE(Control(session, Make(RequestKind::Pause, 210, 1)).kind == ReplyKind::Paused);
    const auto before = Snapshot(session);
    auto request = Make(RequestKind::ResumeAndCommand, 210, 2);
    request.player = before.view.playerId; request.opportunity = before.input.opportunity;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    const auto paid = Completion(session);
    request.id = 3; request.kind = RequestKind::Command; request.opportunity = paid.input.opportunity;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(Next(session).kind == ReplyKind::Admission);
    REQUIRE(Control(session, Make(RequestKind::Pause, 210, 4)).kind == ReplyKind::Cancelled);
    const auto busy = Control(session, Make(RequestKind::Connect, 210, 5));
    REQUIRE(busy.kind == ReplyKind::ControlFailed);
    CHECK(busy.networkError == network::NetworkError::Busy);
    CHECK_FALSE(Snapshot(session).paused);
    for (std::uint64_t id = 10; id < 10 + SessionChannels::requestCapacity; ++id) {
        REQUIRE(session.TrySubmit(Make(RequestKind::Pause, 210, id)) == EnqueueResult::Accepted);
    }
    CHECK_FALSE(session.Stop());
    bool cancelled = false;
    std::size_t replies = 0;
    SessionReply reply;
    while (session.TryPopReply(reply)) {
        CHECK(reply.generation == 210);
        CHECK(reply.kind == ReplyKind::Cancelled);
        if (reply.id == 3) { cancelled = true; }
        ++replies;
    }
    CHECK(cancelled);
    CHECK(replies == SessionChannels::requestCapacity + 1);
}

TEST_CASE("SDL shutdown restores user pacing during paid item recovery", "[sdl][session][save][inventory]") {
    SessionFiles files;
    SessionOptions options;
    options.profilePath = files.profile.c_str(); options.savePath = files.save.c_str();
    options.settings.viewRadius = 4;
    options.settings.pacing.ticksPerSecond = 1;
    options.settings.pacing.rateLimited = true;
    Session session(TestContent(), 220, options);
    REQUIRE(Next(session).kind == ReplyKind::Started);
    REQUIRE(Control(session, Make(RequestKind::Pause, 220, 1)).kind == ReplyKind::Paused);
    // Enable non-idle pacing only after pausing so startup cannot purchase an
    // unrelated automatic wait before the item action under test.
    auto settings = Make(RequestKind::ApplySettings, 220, 3);
    settings.globalSettings.viewRadius = 4;
    settings.globalSettings.ticksPerSecond = 1;
    settings.globalSettings.rateLimited = true;
    settings.globalSettings.pauseWhenIdle = false;
    REQUIRE(Control(session, settings).kind == ReplyKind::SettingsApplied);
    const auto before = Snapshot(session);
    auto request = Make(RequestKind::ItemCommand, 220, 2);
    request.player = before.view.playerId; request.opportunity = before.input.opportunity;
    REQUIRE(session.TrySubmit(request) == EnqueueResult::Accepted);
    REQUIRE(Completion(session).result.cost == ticks_per_turn);
    CHECK_FALSE(session.Stop());
    auto loaded = Server::LoadSave(TestContent(), files.save.c_str());
    REQUIRE(loaded);
    CHECK((*loaded)->GetPacing() == before.runtimeSettings.pacing);
    CHECK_FALSE((*loaded)->GetPacing().pauseWhenIdle);
    CHECK((*loaded)->View(4).remainingTicks > 0);
}
}
