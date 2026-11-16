#include "core/presentation.h"
#include "network/network.h"
#include "network/wire.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <atomic>
#include <enet/enet.h>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;

TEST_CASE("Presentation sidecars roundtrip and reject hidden item identities and budgets", "[network][codec][presentation]") {
    Server server(TestContent());
    const auto view = server.View(8);
    PlayerPresentation source;
    REQUIRE(CapturePlayerPresentation(server.GetWorld(), view, source));
    REQUIRE(source.mapItems.size() == view.cells.size());
    wire::Writer writer; REQUIRE(writer.Presentation(view, source));
    PlayerPresentation decoded;
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.Presentation(view, decoded)); CHECK(reader.Done());
    CHECK(decoded.mapItems == source.mapItems);
    REQUIRE(decoded.interactions.size() == source.interactions.size());
    for (std::size_t i = 0; i < source.interactions.size(); ++i) {
        CHECK(decoded.interactions[i].request == source.interactions[i].request);
        CHECK(decoded.interactions[i].label == source.interactions[i].label);
        CHECK(decoded.interactions[i].reason == source.interactions[i].reason);
    }
    for (std::size_t n = 0; n < writer.bytes.size(); ++n) {
        wire::Reader truncated{{writer.bytes.data(), n}};
        CHECK_FALSE(truncated.Presentation(view, decoded));
    }
    auto hidden = view;
    hidden.cells[0] = {};
    source.mapItems[0] = {17, TestItem("rock")};
    CHECK_FALSE(wire::Writer{}.Presentation(hidden, source));
    source.mapItems[0] = {};
    source.interactions.resize(maxPresentationInteractions + 1);
    CHECK_FALSE(wire::Writer{}.Presentation(view, source));
    source.interactions.clear(); source.mapItems.pop_back();
    CHECK_FALSE(wire::Writer{}.Presentation(view, source));
}

TEST_CASE("Inspection codec distinguishes selected layers and bounds typed observations", "[network][codec][presentation]") {
    Server server(TestContent());
    auto cursor = server.View(0).player_position;
    cursor.y = cursor.y == world_max_y ? cursor.y - 1 : cursor.y + 1;
    PlayerInspection source;
    REQUIRE(CapturePlayerInspection(server, server.GetHostPlayerId(), 9, cursor, 3, source));
    CHECK_FALSE(wire::Writer{}.View(source.view));
    wire::Writer writer; REQUIRE(writer.Inspection(source));
    PlayerInspection decoded;
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.Inspection(decoded)); CHECK(reader.Done());
    CHECK(decoded.requestId == 9); CHECK(decoded.cursor == cursor);
    CHECK(decoded.view.origin.y == cursor.y); CHECK(decoded.view.cells == source.view.cells);
    CHECK(decoded.mapItems == source.mapItems);
    CHECK(wire::ValidInspectionDefinitions(decoded, TestContent()));
    for (std::size_t n = 0; n < writer.bytes.size(); ++n) {
        wire::Reader truncated{{writer.bytes.data(), n}};
        CHECK_FALSE(truncated.Inspection(decoded));
    }
    const auto index = static_cast<std::size_t>(source.view.width * (source.view.height / 2) + source.view.width / 2);
    source.view.cells[index] = {}; source.details.actor = 42; source.details.speed = 100;
    CHECK_FALSE(wire::Writer{}.Inspection(source));
    source.details = {}; source.details.items.resize(maxInspectionItems + 1);
    CHECK_FALSE(wire::Writer{}.Inspection(source));
    source = {}; source.requestId = 10; source.cursor = cursor; source.status = InspectionStatus::TooLarge;
    wire::Writer failure; REQUIRE(failure.Inspection(source));
    wire::Reader failureReader{{failure.bytes.data(), failure.bytes.size()}};
    REQUIRE(failureReader.Inspection(decoded)); CHECK(failureReader.Done());
    CHECK(decoded.status == InspectionStatus::TooLarge); CHECK(decoded.view.cells.empty());
}

TEST_CASE("Inspection transports visible status and nested items with canonical absence", "[network][codec][presentation]") {
    Server server(TestContent());
    const auto cursor = server.View(0).player_position;
    PlayerInspection source;
    REQUIRE(CapturePlayerInspection(server, server.GetHostPlayerId(), 1, cursor, 2, source));
    REQUIRE(source.details.actor == server.GetHostPlayerId());
    REQUIRE(source.details.items.size() >= 2);
    CHECK(source.details.items[0].depth == 0);
    CHECK(source.details.items[1].depth == 1);
    wire::Writer writer; REQUIRE(writer.Inspection(source));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    PlayerInspection result; REQUIRE(reader.Inspection(result)); REQUIRE(reader.Done());
    CHECK(result.details.actor == source.details.actor); CHECK(result.details.speed == source.details.speed);
    CHECK(result.details.combat == source.details.combat);
    REQUIRE(result.details.items.size() == source.details.items.size());
    for (std::size_t i = 0; i < result.details.items.size(); ++i) {
        CHECK(result.details.items[i].item == source.details.items[i].item);
        CHECK(result.details.items[i].type == source.details.items[i].type);
        CHECK(result.details.items[i].depth == source.details.items[i].depth);
    }
    auto invalid = source; invalid.details.items[1].item = invalid.details.items[0].item;
    CHECK_FALSE(wire::Writer{}.Inspection(invalid));
    invalid = source; invalid.details.items[1].depth = maxInspectionDepth + 1;
    CHECK_FALSE(wire::Writer{}.Inspection(invalid));
    invalid = source; invalid.details.actor = 0;
    CHECK_FALSE(wire::Writer{}.Inspection(invalid));
    invalid = source; invalid.details.objectType.value = 1;
    CHECK_FALSE(wire::Writer{}.Inspection(invalid));
    invalid = source; invalid.details.items[0].type.value = static_cast<std::uint32_t>(TestContent().ItemCount() + 1);
    CHECK_FALSE(wire::ValidInspectionDefinitions(invalid, TestContent()));
}

template<class Predicate>
bool PollPresentation(LanHost& host, LanClient& client, Predicate predicate) {
    for (int i = 0; i < 500; ++i) {
        host.Pump(10000); client.Pump(10000);
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
TEST_CASE("Loopback inspection is terminal isolated and single flight", "[network][loopback][presentation]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent()); LanHost host(**runtime, server, TestContent());
    HostSettings settings; settings.port = 0; settings.discoveryPort = 0; REQUIRE(host.Open(settings));
    LanClient client(**runtime, TestContent());
    ConnectionSettings connection; connection.port = host.GetPort(); REQUIRE(client.Connect(connection));
    REQUIRE(PollPresentation(host, client, [&] { return client.HasView(); }));
    const auto original = client.View(); const auto context = client.GetInputContext();
    CHECK(client.Presentation().mapItems.size() == original.cells.size());
    const auto wrongOpportunity = context.opportunity + 100;
    REQUIRE(client.Submit({CommandKind::wait}, wrongOpportunity));
    Vector<PlayerCommandResult> receipts;
    REQUIRE(PollPresentation(host, client, [&] { receipts = client.TakeResults(); return !receipts.empty(); }));
    REQUIRE(receipts.size() == 1);
    CHECK(receipts[0].opportunity == wrongOpportunity); CHECK(receipts[0].result.outcome == Outcome::stale_input);
    CHECK(client.GetInputContext().lastInputSequence == context.lastInputSequence);
    CHECK(client.View().revision == original.revision);
    const auto localContext = server.GetInputContext(server.GetHostPlayerId());
    const auto localSequence = host.QueueLocal({CommandKind::wait}, localContext.opportunity + 100);
    REQUIRE(localSequence != 0);
    const auto localUpdate = host.Pump(0);
    bool found = false;
    for (const auto& receipt : localUpdate.commandResults) if (receipt.sequence == localSequence && receipt.player == server.GetHostPlayerId()) {
        found = true; CHECK(receipt.result.outcome == Outcome::stale_input);
    }
    CHECK(found);
    auto cursor = original.player_position; ++cursor.y;
    REQUIRE(client.Inspect(1, cursor, 4));
    CHECK(client.Inspect(2, cursor, 4) == eastl::unexpected(NetworkError::Busy));
    PlayerInspection result;
    REQUIRE(PollPresentation(host, client, [&] { return client.TryTakeInspection(result); }));
    CHECK(result.requestId == 1); CHECK(result.cursor == cursor); CHECK(result.status == InspectionStatus::Complete);
    CHECK(result.view.origin.y == cursor.y);
    CHECK(client.View().origin.y == original.origin.y);
    CHECK(client.View().cells == original.cells);
    CHECK(client.GetInputContext().opportunity == context.opportunity);
    CHECK_FALSE(client.TryTakeInspection(result));
    CHECK(client.Inspect(1, cursor, 4) == eastl::unexpected(NetworkError::InvalidSettings));
    REQUIRE(client.Inspect(2, cursor, 4));
    // The prior reply ACK and the next request share reliable channel zero.
    REQUIRE(PollPresentation(host, client, [&] { return client.TryTakeInspection(result); }));
    CHECK(result.requestId == 2); CHECK(result.status == InspectionStatus::Complete);
    REQUIRE(client.Inspect(3, cursor, 4));
    client.Disconnect();
    REQUIRE(client.TryTakeInspection(result)); CHECK(result.requestId == 3);
    CHECK(result.status == InspectionStatus::Disconnected);
    CHECK(client.Presentation().mapItems.empty());
}

TEST_CASE("Inspection timeout disconnects in the same pump and reconnect resets request IDs", "[network][loopback][presentation]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent()); LanHost host(**runtime, server, TestContent());
    HostSettings settings; settings.port = 0; settings.discoveryPort = 0; REQUIRE(host.Open(settings));
    LanClient client(**runtime, TestContent()); ConnectionSettings connection; connection.port = host.GetPort();
    REQUIRE(client.Connect(connection));
    REQUIRE(PollPresentation(host, client, [&] { return client.HasView(); }));
    const auto player = client.View().playerId; const auto cursor = client.View().player_position;
    connection.reconnectToken = client.GetReconnectToken();
    REQUIRE(client.Inspect(1, cursor, 4));
    // Do not pump the host: no reply can have been generated.
    client.Pump(5000001);
    CHECK(client.GetState() == ConnectionState::Rejected); CHECK(client.GetError() == NetworkError::Timeout);
    PlayerInspection result; REQUIRE(client.TryTakeInspection(result));
    CHECK(result.requestId == 1); CHECK(result.status == InspectionStatus::Disconnected);
    CHECK_FALSE(client.TryTakeInspection(result)); CHECK_FALSE(client.HasView());
    bool parked = false;
    for (int i = 0; i < 500 && !parked; ++i) {
        host.Pump(0); // Client must not require another pump to send its disconnect.
        for (const auto& status : server.GetPlayers()) if (status.id == player) parked = status.state == PlayerConnectionState::Parked;
        if (!parked) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(parked);
    client.Pump(0); CHECK(client.GetError() == NetworkError::Timeout);
    REQUIRE(client.Connect(connection));
    REQUIRE(PollPresentation(host, client, [&] { return client.HasView(); }));
    REQUIRE(client.Inspect(1, client.View().player_position, 4));
    REQUIRE(PollPresentation(host, client, [&] { return client.TryTakeInspection(result); }));
    CHECK(result.status == InspectionStatus::Complete);
}

TEST_CASE("Independent runtime lifetimes on another thread preserve active sockets", "[network][loopback][presentation]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent()); LanHost host(**runtime, server, TestContent());
    HostSettings settings; settings.port = 0; settings.discoveryPort = 0; REQUIRE(host.Open(settings));
    LanClient client(**runtime, TestContent()); ConnectionSettings connection; connection.port = host.GetPort();
    REQUIRE(client.Connect(connection));
    REQUIRE(PollPresentation(host, client, [&] { return client.HasView(); }));
    std::atomic<bool> succeeded{true};
    {
        std::jthread other([&] {
            for (int i = 0; i < 32; ++i) if (!NetworkRuntime::Create()) succeeded.store(false);
        });
        for (int i = 0; i < 32; ++i) { host.Pump(0); client.Pump(0); }
    }
    REQUIRE(succeeded.load());
    REQUIRE(client.Inspect(1, client.View().player_position, 4));
    PlayerInspection result;
    REQUIRE(PollPresentation(host, client, [&] { return client.TryTakeInspection(result); }));
    CHECK(result.status == InspectionStatus::Complete);
}

struct RawPresentationHost {
    ENetHost* host = nullptr;
    ENetPeer* peer = nullptr;
    ~RawPresentationHost() { if (host) enet_host_destroy(host); }
    void Open() {
        ENetAddress address{ENET_HOST_ANY, 0}; host = enet_host_create(&address, 1, 2, 0, 0); REQUIRE(host);
    }
    std::uint16_t Port() const {
        ENetAddress address{}; REQUIRE(enet_socket_get_address(host->socket, &address) == 0); return address.port;
    }
    void Pump() {
        ENetEvent event{};
        while (enet_host_service(host, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) peer = event.peer;
            if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
        }
    }
    void Send(const wire::Writer& writer, enet_uint8 channel = 0) {
        REQUIRE(peer);
        auto* packet = enet_packet_create(writer.bytes.data(), writer.bytes.size(), ENET_PACKET_FLAG_RELIABLE);
        REQUIRE(packet); REQUIRE(enet_peer_send(peer, channel, packet) == 0); enet_host_flush(host);
    }
};
wire::Writer PresentationMessage(wire::Type type) {
    wire::Writer writer; writer.U32(wire::protocol); writer.U8(static_cast<std::uint8_t>(type)); return writer;
}
template<class Predicate>
bool PollRawPresentation(RawPresentationHost& host, LanClient& client, Predicate predicate) {
    for (int i = 0; i < 500; ++i) {
        host.Pump(); client.Pump(1000);
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
TEST_CASE("Inspection reply validation and channel ordering preserve authoritative context", "[network][loopback][presentation]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    RawPresentationHost host; host.Open(); LanClient client(**runtime, TestContent());
    ConnectionSettings connection; connection.port = host.Port(); REQUIRE(client.Connect(connection));
    REQUIRE(PollRawPresentation(host, client, [&] { return host.peer && client.GetState() == ConnectionState::Joining; }));
    Server server(TestContent()); const auto view = server.View(4);
    PlayerPresentation presentation; REQUIRE(CapturePlayerPresentation(server.GetWorld(), view, presentation));
    constexpr std::uint64_t nonce = 99;
    auto accepted = PresentationMessage(wire::Type::Accepted);
    accepted.U64(view.playerId); accepted.U64(42); accepted.U64(nonce); host.Send(accepted);
    auto snapshot = PresentationMessage(wire::Type::Snapshot); snapshot.U64(nonce);
    REQUIRE(snapshot.View(view)); REQUIRE(snapshot.Presentation(view, presentation)); host.Send(snapshot);
    REQUIRE(PollRawPresentation(host, client, [&] { return client.HasView(); }));
    REQUIRE(client.Inspect(1, view.player_position, 4));
    PlayerInspection inspection; REQUIRE(CapturePlayerInspection(server, view.playerId, 1, view.player_position, 4, inspection));
    SECTION("A newer receipt and inspection may precede an older channel one snapshot") {
        const auto sequence = client.Submit({CommandKind::wait}); REQUIRE(sequence);
        auto receipt = PresentationMessage(wire::Type::Receipt); receipt.U64(nonce);
        REQUIRE(receipt.Result({view.playerId, *sequence, view.opportunity, {Outcome::queued, false, view.tick}}));
        receipt.U64(view.opportunity + 1); receipt.U64(view.revision + 10); receipt.U64(*sequence); host.Send(receipt);
        inspection.view.revision = view.revision + 100;
        auto reply = PresentationMessage(wire::Type::Inspection); reply.U64(nonce); REQUIRE(reply.Inspection(inspection)); host.Send(reply);
        PlayerInspection result;
        REQUIRE(PollRawPresentation(host, client, [&] { return client.TryTakeInspection(result); }));
        CHECK(client.GetInputContext().revision == view.revision + 10);
        CHECK(client.View().revision == view.revision);
        auto delayed = view; ++delayed.revision;
        auto update = PresentationMessage(wire::Type::Snapshot); update.U64(nonce);
        REQUIRE(update.View(delayed)); REQUIRE(update.Presentation(delayed, presentation)); host.Send(update, 1);
        REQUIRE(PollRawPresentation(host, client, [&] { return client.View().revision == delayed.revision; }));
        CHECK(client.GetInputContext().opportunity == view.opportunity + 1);
        CHECK(client.GetInputContext().lastInputSequence == *sequence);
        CHECK(client.GetInputContext().revision == view.revision + 10);
    }
    SECTION("Wrong session nonce is terminal even with a queued Closing message") {
        auto reply = PresentationMessage(wire::Type::Inspection); reply.U64(nonce + 1); REQUIRE(reply.Inspection(inspection)); host.Send(reply);
        host.Send(PresentationMessage(wire::Type::Closing));
        REQUIRE(PollRawPresentation(host, client, [&] { return client.GetState() == ConnectionState::Rejected; }));
        client.Pump(0); CHECK(client.GetError() == NetworkError::Protocol);
        PlayerInspection result; REQUIRE(client.TryTakeInspection(result)); CHECK(result.status == InspectionStatus::Disconnected);
        CHECK_FALSE(client.TryTakeInspection(result));
    }
    SECTION("A valid layer view with the wrong requested radius is rejected") {
        REQUIRE(CapturePlayerInspection(server, view.playerId, 1, view.player_position, 3, inspection));
        auto reply = PresentationMessage(wire::Type::Inspection); reply.U64(nonce); REQUIRE(reply.Inspection(inspection)); host.Send(reply);
        REQUIRE(PollRawPresentation(host, client, [&] { return client.GetState() == ConnectionState::Rejected; }));
        CHECK(client.GetError() == NetworkError::Protocol);
    }
    SECTION("An inspection on the snapshot channel cannot bypass reliable ACK ordering") {
        auto reply = PresentationMessage(wire::Type::Inspection); reply.U64(nonce); REQUIRE(reply.Inspection(inspection)); host.Send(reply, 1);
        REQUIRE(PollRawPresentation(host, client, [&] { return client.GetState() == ConnectionState::Rejected; }));
        CHECK(client.GetError() == NetworkError::Protocol);
    }
    SECTION("A mismatched request identity cannot complete the current query") {
        inspection.requestId = 2;
        auto reply = PresentationMessage(wire::Type::Inspection); reply.U64(nonce); REQUIRE(reply.Inspection(inspection)); host.Send(reply);
        REQUIRE(PollRawPresentation(host, client, [&] { return client.GetState() == ConnectionState::Rejected; }));
        PlayerInspection result; REQUIRE(client.TryTakeInspection(result));
        CHECK(result.requestId == 1); CHECK(result.status == InspectionStatus::Disconnected);
    }
}
}
