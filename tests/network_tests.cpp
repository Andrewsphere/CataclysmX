#include "network/network.h"
#include "network/wire.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;

TEST_CASE("Network wire rejects truncated values, invalid enums and oversized snapshots", "[network][codec]") {
    CHECK(NetworkErrorMessage(NetworkError::UnknownToken) != nullptr);
    wire::Writer w;
    PlayerCommandResult receipt{17, 4, 2, {Outcome::moved, true, 100}};
    w.Result(receipt);
    wire::Reader reader{{w.bytes.data(), w.bytes.size()}};
    PlayerCommandResult decoded;
    REQUIRE(reader.Result(decoded));
    CHECK(reader.Done());
    CHECK(decoded.player == 17);
    CHECK(decoded.sequence == 4);
    CHECK(decoded.result.outcome == Outcome::moved);
    for (std::size_t n = 0; n < w.bytes.size(); ++n) {
        wire::Reader truncated{{w.bytes.data(), n}};
        CHECK_FALSE(truncated.Result(decoded));
    }
    w.bytes[24] = 255; // outcome byte follows three u64s
    wire::Reader malformed{{w.bytes.data(), w.bytes.size()}};
    CHECK_FALSE(malformed.Result(decoded));

    Server server(TestContent());
    auto view = server.ViewForPlayer(server.GetHostPlayerId());
    wire::Writer snapshot;
    REQUIRE(snapshot.View(view));
    wire::Reader roundtrip{{snapshot.bytes.data(), snapshot.bytes.size()}};
    WorldView result;
    REQUIRE(roundtrip.View(result));
    CHECK(roundtrip.Done());
    CHECK(result.cells == view.cells);
    CHECK(result.playerId == view.playerId);
    wire::Reader cut{{snapshot.bytes.data(), snapshot.bytes.size() - 1}};
    CHECK_FALSE(cut.View(result));
    auto bad = snapshot.bytes;
    bad[8] = 0; // width must be a positive odd square dimension
    wire::Reader badWidth{{bad.data(), bad.size()}};
    CHECK_FALSE(badWidth.View(result));
    bad = snapshot.bytes;
    for (int i = 26; i < 34; ++i) bad[i] = 0; // player ID
    wire::Reader badId{{bad.data(), bad.size()}};
    CHECK_FALSE(badId.View(result));
    bad = snapshot.bytes;
    for (int i = 42; i < 50; ++i) bad[i] = 0; // opportunity
    wire::Reader badOpportunity{{bad.data(), bad.size()}};
    CHECK_FALSE(badOpportunity.View(result));
    bad = snapshot.bytes;
    for (int i = 66; i < 70; ++i) bad[i] = 0; // speed
    wire::Reader badSpeed{{bad.data(), bad.size()}};
    CHECK_FALSE(badSpeed.View(result));
    bad = snapshot.bytes;
    bad[bad.size() - 19] = 255; // player connection state
    wire::Reader badState{{bad.data(), bad.size()}};
    CHECK_FALSE(badState.View(result));
    wire::Writer commandBytes;
    commandBytes.CommandValue({CommandKind::wait});
    commandBytes.bytes[0] = 255;
    wire::Reader badCommand{{commandBytes.bytes.data(), commandBytes.bytes.size()}};
    Command decodedCommand;
    CHECK_FALSE(badCommand.CommandValue(decodedCommand));
    wire::Writer text;
    CHECK_FALSE(text.Text("bad\nname", 32));
    view.inventory.resize(129);
    for (std::size_t i = 0; i < view.inventory.size(); ++i) view.inventory[i].id = i + 1;
    wire::Writer many;
    REQUIRE(many.View(view));
    wire::Reader manyReader{{many.bytes.data(), many.bytes.size()}};
    REQUIRE(manyReader.View(result));
    CHECK(result.inventory.size() == 129);
    view.inventory.resize(2049);
    wire::Writer excessive;
    CHECK_FALSE(excessive.View(view));
    view.inventory.clear();
    view.cells.push_back({});
    wire::Writer invalid;
    CHECK_FALSE(invalid.View(view));
}

template<class Predicate>
bool Poll(LanHost& host, LanClient& a, LanClient& b, Predicate predicate) {
    for (int i = 0; i < 500; ++i) {
        host.Pump(10000); a.Pump(10000); b.Pump(10000);
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

TEST_CASE("Real loopback joins two guests and preserves identity across reconnect", "[network][loopback]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent(), 73);
    LanHost host(**runtime, server, TestContent());
    REQUIRE(host.Open({0, 0, "Loopback"}));
    LanClient a(**runtime, TestContent()), b(**runtime, TestContent());
    REQUIRE(a.Connect({"127.0.0.1", host.GetPort(), "Alice", 0}));
    REQUIRE(b.Connect({"127.0.0.1", host.GetPort(), "Bob", 0}));
    REQUIRE(Poll(host, a, b, [&] { return a.HasView() && b.HasView() &&
        a.GetState() == ConnectionState::Active && b.GetState() == ConnectionState::Active; }));
    const auto alice = a.View().playerId;
    const auto bob = b.View().playerId;
    REQUIRE(alice != bob);
    const auto token = a.GetReconnectToken(); REQUIRE(token != 0);
    const auto aliceActor = server.GetWorld().FindActor(alice);
    REQUIRE(aliceActor.value != 0);
    for (int i = 0; i < 129; ++i)
        REQUIRE(server.GetWorld().CreateItem(ItemKind::rock, ItemLocation::Carried(aliceActor)));
    REQUIRE(server.GetWorld().CreateItem(ItemKind::rock, ItemLocation::Ground(a.View().player_position)));
    REQUIRE(a.Submit({CommandKind::pick_up}));
    REQUIRE(b.Submit({CommandKind::wait}));
    host.QueueLocal({CommandKind::wait});
    bool aliceResolved = false;
    REQUIRE(Poll(host, a, b, [&] {
        for (const auto& receipt : a.TakeResults()) {
            CHECK(receipt.player == alice);
            if (receipt.result.outcome == Outcome::picked_up) aliceResolved = true;
        }
        return a.HasView() && a.View().inventory.size() == 130 && aliceResolved;
    }));
    CHECK(aliceResolved);
    for (const auto& receipt : b.TakeResults()) CHECK(receipt.player == bob);
    const auto inventory = a.View().inventory;
    const auto earlierRevision = a.View().revision;
    auto pacing = server.GetPacing(); pacing.pauseWhenIdle = false;
    REQUIRE(server.SetPacing(pacing));
    for (int i = 0; i < 100; ++i) {
        host.Pump(20000); a.Pump(20000); b.Pump(20000);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(a.GetState() == ConnectionState::Active);
    CHECK(b.GetState() == ConnectionState::Active);
    CHECK(a.View().revision > earlierRevision);
    REQUIRE(a.View().inventory == inventory);
    LanClient duplicate(**runtime, TestContent());
    REQUIRE(duplicate.Connect({"127.0.0.1", host.GetPort(), "Duplicate", token}));
    REQUIRE(Poll(host, duplicate, b, [&] { return duplicate.GetState() == ConnectionState::Rejected; }));
    CHECK(duplicate.GetError() == NetworkError::AlreadyConnected);
    // A queued packet from the old connection cannot be assigned to the new one.
    REQUIRE(a.Submit({CommandKind::move, -1, 0}));
    a.Disconnect();
    REQUIRE(Poll(host, a, b, [&] {
        for (const auto& p : server.GetPlayers()) if (p.id == alice) return p.state == PlayerConnectionState::Parked;
        return false;
    }));
    REQUIRE(a.Connect({"127.0.0.1", host.GetPort(), "Alice", token}));
    REQUIRE(Poll(host, a, b, [&] { return a.HasView() && a.GetState() == ConnectionState::Active; }));
    CHECK(a.View().playerId == alice);
    CHECK(a.View().inventory == inventory);
    CHECK(a.TakeResults().empty());
    CHECK(b.View().playerId == bob);
    for (int reconnect = 0; reconnect < 2; ++reconnect) {
        a.Disconnect();
        REQUIRE(Poll(host, a, b, [&] {
            for (const auto& p : server.GetPlayers())
                if (p.id == alice) return p.state == PlayerConnectionState::Parked;
            return false;
        }));
        REQUIRE(a.Connect({"127.0.0.1", host.GetPort(), "Alice", token}));
        REQUIRE(Poll(host, a, b, [&] { return a.GetState() == ConnectionState::Active; }));
        CHECK(a.View().playerId == alice);
    }
    host.Close();
    CHECK_FALSE(host.IsOpen());
    for (int i = 0; i < 100 && b.GetState() != ConnectionState::Disconnected; ++i) {
        b.Pump(10000);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(b.GetState() == ConnectionState::Disconnected);
}

TEST_CASE("Directed UDP discovery finds a compatible loopback host and expires", "[network][discovery]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent());
    LanHost host(**runtime, server, TestContent());
    REQUIRE(host.Open({0, 0, "Find me"}));
    LanDiscovery discovery(**runtime, TestContent());
    REQUIRE(discovery.Open());
    REQUIRE(discovery.Refresh(host.GetDiscoveryPort(), "127.0.0.1"));
    bool found = false;
    for (int i = 0; i < 200 && !found; ++i) {
        host.Pump(10000); discovery.Pump(10000);
        found = !discovery.Sessions().empty();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(found);
    CHECK(discovery.Sessions()[0].compatible);
    CHECK(discovery.Sessions()[0].sessionName == "Find me");
    CHECK(discovery.Sessions()[0].gamePort == host.GetPort());
    host.Close();
    // Explicit simulation time drives expiry even if no OS packets arrive.
    discovery.Pump(4000000);
    CHECK(discovery.Sessions().empty());
}

TEST_CASE("Execution receipt permits the next input before the next world snapshot", "[network][receipt]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent());
    LanHost host(**runtime, server, TestContent());
    REQUIRE(host.Open({0, 0, "Receipts"}));
    LanClient a(**runtime, TestContent()), idle(**runtime, TestContent());
    REQUIRE(a.Connect({"127.0.0.1", host.GetPort(), "Alice", 0}));
    REQUIRE(Poll(host, a, idle, [&] { return a.GetState() == ConnectionState::Active; }));
    // Ack the activation snapshot without advancing the snapshot cadence clock.
    a.Pump(0); host.Pump(0);
    const auto oldOpportunity = a.View().opportunity;
    REQUIRE(a.Submit({CommandKind::wait}));
    auto invalid = a.Submit({CommandKind::move, 0, 0});
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error() == NetworkError::InvalidCommand);
    invalid = a.Submit({CommandKind::wait, 0, 0, 0, 0});
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error() == NetworkError::InvalidCommand);
    host.QueueLocal({CommandKind::wait});
    bool resolved = false;
    for (int i = 0; i < 150 && !resolved; ++i) {
        a.Pump(0); host.Pump(0); a.Pump(0);
        for (const auto& receipt : a.TakeResults())
            if (receipt.result.outcome == Outcome::waited) resolved = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(resolved);
    CHECK(a.View().opportunity == oldOpportunity); // snapshot was throttled
    REQUIRE(a.Submit({CommandKind::wait}));
    host.QueueLocal({CommandKind::wait});
    bool second = false;
    for (int i = 0; i < 150 && !second; ++i) {
        a.Pump(0); host.Pump(0); a.Pump(0);
        for (const auto& receipt : a.TakeResults())
            if (receipt.result.outcome == Outcome::waited) second = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(second);
}

TEST_CASE("Joining view stays private until host activation relocates a blocked spawn", "[network][join]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent());
    LanHost host(**runtime, server, TestContent());
    REQUIRE(host.Open({0, 0, "Joining"}));
    LanClient a(**runtime, TestContent()), idle(**runtime, TestContent());
    REQUIRE(a.Connect({"127.0.0.1", host.GetPort(), "Guest", 0}));
    PersistentId id = 0;
    TileCoord staged{};
    for (int i = 0; i < 200 && !id; ++i) {
        a.Pump(10000); host.Pump(10000);
        for (const auto& p : server.GetPlayers()) if (p.state == PlayerConnectionState::Joining) {
            id = p.id;
            staged = server.GetWorld().GetActor(server.GetWorld().FindActor(id))->position;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(id != 0);
    REQUIRE_FALSE(a.HasView());
    REQUIRE(server.GetWorld().CreateActor(staged)); // parked actor does not occupy its saved tile
    REQUIRE(Poll(host, a, idle, [&] { return a.GetState() == ConnectionState::Active; }));
    CHECK(a.View().player_position != staged);
    CHECK(a.View().playerId == id);
}

TEST_CASE("Incompatible content and unknown reconnect tokens are rejected on loopback", "[network][admission]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent());
    LanHost host(**runtime, server, TestContent());
    REQUIRE(host.Open({0, 0, "Admission"}));
    ContentCatalog different;
    LanClient wrong(**runtime, different), unknown(**runtime, TestContent());
    REQUIRE(wrong.Connect({"127.0.0.1", host.GetPort(), "Other", 0}));
    REQUIRE(unknown.Connect({"127.0.0.1", host.GetPort(), "Unknown", 12345}));
    REQUIRE(Poll(host, wrong, unknown, [&] {
        return wrong.GetState() == ConnectionState::Rejected &&
            unknown.GetState() == ConnectionState::Rejected;
    }));
    CHECK(wrong.GetError() == NetworkError::IncompatibleContent);
    CHECK(unknown.GetError() == NetworkError::UnknownToken);
    CHECK(server.GetPlayers().size() == 1);
    for (int i = 0; i < 12; ++i) { host.Pump(10000); wrong.Pump(10000); unknown.Pump(10000); }
    CHECK(wrong.GetState() == ConnectionState::Rejected);
    CHECK(unknown.GetState() == ConnectionState::Rejected);
    CHECK_FALSE(wrong.HasView());
    CHECK_FALSE(unknown.HasView());
}
}
