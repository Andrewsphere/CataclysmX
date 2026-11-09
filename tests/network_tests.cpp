#include "network/network.h"
#include "network/wire.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <enet/enet.h>

#include <chrono>
#include <climits>
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
    bad[12] = 0; // width must be a positive odd square dimension
    wire::Reader badWidth{{bad.data(), bad.size()}};
    CHECK_FALSE(badWidth.View(result));
    bad = snapshot.bytes;
    for (int i = 34; i < 42; ++i) bad[i] = 0; // player ID
    wire::Reader badId{{bad.data(), bad.size()}};
    CHECK_FALSE(badId.View(result));
    bad = snapshot.bytes;
    for (int i = 50; i < 58; ++i) bad[i] = 0; // opportunity
    wire::Reader badOpportunity{{bad.data(), bad.size()}};
    CHECK_FALSE(badOpportunity.View(result));
    bad = snapshot.bytes;
    for (int i = 74; i < 78; ++i) bad[i] = 0; // speed
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
    // This synthetic inventory replaces the snapshot's real item identities.
    view.groundItems.clear();
    view.inventory.resize(129);
    for (std::size_t i = 0; i < view.inventory.size(); ++i) {
        view.inventory[i].id = i + 1;
        view.inventory[i].type = TestItem("rock");
    }
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

TEST_CASE("Protocol six preserves signed six-axis commands and authoritative shapes", "[network][codec][3d]") {
    CHECK(wire::protocol == 0x43584e06);
    for (const auto kind : {CommandKind::move, CommandKind::interact}) {
        for (const TileCoord direction : {TileCoord{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
            {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, 0, 0}, {1, 0, 1}, {0, 1, -1}, {0, 0, INT_MIN}}) {
            Command command{kind, direction.x, direction.z}; command.dy = direction.y;
            wire::Writer writer;
            CHECK(writer.CommandValue(command) == ValidCommandShape(command));
            if (!ValidCommandShape(command)) continue;
            wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
            Command decoded;
            REQUIRE(reader.CommandValue(decoded)); CHECK(reader.Done());
            CHECK(decoded.kind == kind); CHECK(decoded.dx == command.dx);
            CHECK(decoded.dz == command.dz); CHECK(decoded.dy == command.dy);
            // dy is appended to the command, and the reader must validate it too.
            writer.bytes.back() = 2;
            wire::Reader malformed{{writer.bytes.data(), writer.bytes.size()}};
            CHECK_FALSE(malformed.CommandValue(decoded));
        }
    }
}

TEST_CASE("Actual elevated server projections roundtrip independent terrain and overlay depths", "[network][codec][3d]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    for (int y : {-1, 2}) {
        CAPTURE(y);
        REQUIRE(world.SetTile({0, 0, y}, {TestTerrain("floor"), 0}));
        REQUIRE(world.MoveActor(server.GetPlayer(), {0, 0, y}));
        for (int x = 1; x <= 2; ++x) {
            REQUIRE(world.SetTile({x, 0, y - 1}, {TestTerrain("floor"), 19}));
            REQUIRE(world.SetTile({x, 0, y}, {TestTerrain("air"), 0}));
        }
        // A solid wall supports the foreground actor without filling its air cell.
        REQUIRE(world.SetTile({2, 0, y - 1}, {TestTerrain("wall"), 19}));
        // Use a separate open ray: the immediately adjacent lower cell is hidden
        // by the conservative edge of the player's own floor.
        for (int z = 1; z <= 3; ++z) {
            REQUIRE(world.SetTile({0, z, y - 1}, {TestTerrain("floor"), 19}));
            REQUIRE(world.SetTile({0, z, y}, {TestTerrain("air"), 0}));
        }
        const auto foreground = world.CreateActor({2, 0, y}); REQUIRE(foreground);
        const auto item = world.CreateItem(TestItem("rock"), ItemLocation::Ground({0, 3, y - 1})); REQUIRE(item);
        const auto view = server.View(4);
        const auto& actorCell = view.cells[4 * view.width + 6];
        REQUIRE(actorCell.visible); CHECK(actorCell.terrainDepth == 1); CHECK(actorCell.overlayDepth == 0);
        CHECK(actorCell.actor == world.GetActor(*foreground)->id);
        const auto& itemCell = view.cells[7 * view.width + 4];
        REQUIRE(itemCell.visible); CHECK(itemCell.terrainDepth == 1); CHECK(itemCell.overlayDepth == 1);
        CHECK(itemCell.ground_items != 0);
        wire::Writer writer; REQUIRE(writer.View(view));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        WorldView decoded; REQUIRE(reader.View(decoded)); CHECK(reader.Done());
        CHECK(decoded.origin == view.origin); CHECK(decoded.player_position == TileCoord{0, 0, y});
        CHECK(decoded.cells == view.cells);
        REQUIRE(world.DestroyActor(*foreground));
    }
}

WorldView SingleCellView(int y = 0) {
    WorldView view;
    view.width = 1; view.height = 1; view.playerId = 1;
    view.origin.y = y; view.player_position.y = y;
    ViewCell cell; cell.visible = true; cell.tile.terrain = TestTerrain("floor");
    view.cells.push_back(cell);
    return view;
}

TEST_CASE("Snapshot codecs reject invalid elevation, source depths and visible bounds", "[network][codec][3d]") {
    const auto baseline = SingleCellView();
    auto invalid = baseline;
    wire::Writer writer; REQUIRE(writer.View(baseline));
    // Protocol-six view header: origin XYZ, dimensions, player XYZ, timing/status, cell count.
    constexpr std::size_t originY = 8, playerY = 22, firstCell = 87;
    constexpr std::size_t terrainDepth = firstCell + 28, overlayDepth = firstCell + 29;
    std::size_t offset = 0;
    wire::Writer replacement;
    SECTION("origin mismatch") { invalid.origin.y = 1; offset = originY; replacement.I32(1); }
    SECTION("above world") {
        invalid.player_position.y = 11; invalid.origin.y = 11;
        writer.bytes[originY + 3] = 11;
        offset = playerY; replacement.I32(11);
    }
    SECTION("below world") {
        invalid.player_position.y = -11; invalid.origin.y = -11;
        wire::Writer origin; origin.I32(-11);
        for (std::size_t i = 0; i < 4; ++i) writer.bytes[originY + i] = origin.bytes[i];
        offset = playerY; replacement.I32(-11);
    }
    SECTION("extreme Y") { invalid.player_position.y = INT_MIN; offset = playerY; replacement.I32(INT_MIN); }
    SECTION("extreme X") { invalid.player_position.x = INT_MAX; offset = 14; replacement.I32(INT_MAX); }
    SECTION("terrain below bottom") { invalid.cells[0].terrainDepth = 11; offset = terrainDepth; replacement.U8(11); }
    SECTION("overlay below bottom") {
        invalid.cells[0].actor = 7; invalid.cells[0].overlayDepth = 11;
        writer.bytes[firstCell + 13] = 7; // actor low byte
        offset = overlayDepth; replacement.U8(11);
    }
    SECTION("terrain beyond byte contract") { invalid.cells[0].terrainDepth = 21; offset = terrainDepth; replacement.U8(21); }
    SECTION("overlay beyond byte contract") { invalid.cells[0].overlayDepth = 255; offset = overlayDepth; replacement.U8(255); }
    SECTION("unsigned depth cannot wrap on encode") { invalid.cells[0].terrainDepth = 256; offset = terrainDepth; replacement.U8(255); }
    SECTION("depth without overlay") { invalid.cells[0].overlayDepth = 1; offset = overlayDepth; replacement.U8(1); }
    SECTION("zero terrain") { invalid.cells[0].tile.terrain = {}; offset = firstCell + 1; replacement.U32(0); }
    SECTION("invalid visibility") { invalid.cells[0].visible = false; offset = firstCell; replacement.U8(2); }
    SECTION("player without actor") { invalid.cells[0].player = true; offset = firstCell + 14; replacement.U8(1); }
    SECTION("enabled without object") { invalid.cells[0].object_enabled = true; offset = firstCell + 27; replacement.U8(1); }
    wire::Writer rejected; CHECK_FALSE(rejected.View(invalid));
    REQUIRE_FALSE(replacement.bytes.empty());
    for (std::size_t i = 0; i < replacement.bytes.size(); ++i) writer.bytes[offset + i] = replacement.bytes[i];
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded; CHECK_FALSE(reader.View(decoded));
}

TEST_CASE("Hidden cells are default and projection origins may extend past horizontal boundaries", "[network][codec][3d]") {
    auto view = SingleCellView(world_max_y);
    view.cells[0].terrainDepth = 20; view.cells[0].overlayDepth = 20; view.cells[0].actor = 9;
    wire::Writer deepest; REQUIRE(deepest.View(view));
    wire::Reader deepestReader{{deepest.bytes.data(), deepest.bytes.size()}};
    WorldView decoded; REQUIRE(deepestReader.View(decoded)); CHECK(decoded.cells == view.cells);
    auto leaked = deepest.bytes; leaked[87] = 0; // Hidden cells have no payload on the wire.
    wire::Reader leakedReader{{leaked.data(), leaked.size()}};
    CHECK_FALSE((leakedReader.View(decoded) && leakedReader.Done()));
    view = SingleCellView(world_min_y);
    wire::Writer bottom; REQUIRE(bottom.View(view));
    view.cells[0].terrainDepth = 1;
    wire::Writer below; CHECK_FALSE(below.View(view));
    for (int boundary : {-world_coordinate_limit, world_coordinate_limit}) {
        view = {}; view.width = 25; view.height = 25; view.playerId = 1;
        view.player_position = {boundary, boundary, -3}; view.origin = {boundary - 12, boundary - 12, -3};
        view.cells.resize(625);
        wire::Writer writer; REQUIRE(writer.View(view));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        REQUIRE(reader.View(decoded)); CHECK(reader.Done()); CHECK(decoded.cells == view.cells);
        CHECK(decoded.origin == view.origin);
    }
    // A visible cell beyond the world edge is invalid even when the view is centered correctly.
    view = {}; view.width = 3; view.height = 3; view.playerId = 1;
    view.player_position = {world_coordinate_limit, 0}; view.origin = {world_coordinate_limit - 1, -1};
    view.cells.resize(9);
    wire::Writer edge; REQUIRE(edge.View(view));
    view.cells[5] = SingleCellView().cells[0];
    wire::Writer outside; CHECK_FALSE(outside.View(view));
    // Move a valid center-cell payload to the adjacent, out-of-world column on the wire.
    view.cells[4] = view.cells[5]; view.cells[5] = {};
    wire::Writer centered; REQUIRE(centered.View(view));
    centered.bytes.insert(centered.bytes.begin() + 87 + 4, 0);
    centered.bytes.erase(centered.bytes.begin() + 87 + 4 + 1 + 30);
    wire::Reader malformed{{centered.bytes.data(), centered.bytes.size()}};
    CHECK_FALSE((malformed.View(decoded) && malformed.Done()));
    view = SingleCellView(); view.cells[0] = {};
    wire::Writer hidden; REQUIRE(hidden.View(view));
    wire::Reader hiddenReader{{hidden.bytes.data(), hidden.bytes.size()}};
    REQUIRE(hiddenReader.View(decoded)); CHECK(hiddenReader.Done()); CHECK(decoded.cells[0] == ViewCell{});
    view = SingleCellView(); view.cells[0] = {};
    SECTION("hidden terrain") { view.cells[0].tile.terrain = TestTerrain("floor"); }
    SECTION("hidden damage") { view.cells[0].tile.damage = 1; }
    SECTION("hidden actor") { view.cells[0].actor = 1; }
    SECTION("hidden player") { view.cells[0].player = true; }
    SECTION("hidden items") { view.cells[0].ground_items = 1; }
    SECTION("hidden object") { view.cells[0].object = 1; }
    SECTION("hidden enabled") { view.cells[0].object_enabled = true; }
    SECTION("hidden terrain depth") { view.cells[0].terrainDepth = 1; }
    SECTION("hidden overlay depth") { view.cells[0].overlayDepth = 1; }
    wire::Writer invalid; CHECK_FALSE(invalid.View(view));
}

TEST_CASE("Terrain fingerprint includes floor support and stair connectivity", "[network][fingerprint][3d]") {
    const auto fingerprint = [](const char* flags) {
        String text = "{format_version:1,definitions:[{type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'white',background:'black'},";
        text += flags; text += "}]}";
        const ContentSource source{"terrain-network.json5", {text.data(), text.size()}};
        Diagnostics diagnostics;
        auto content = LoadContent({&source, 1}, &diagnostics);
        INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(content);
        return ContentFingerprint(*content);
    };
    const auto floor = fingerprint("open_space:false,stairs:false");
    const auto air = fingerprint("open_space:true,stairs:false");
    const auto stairs = fingerprint("open_space:false,stairs:true");
    CHECK(floor != air); CHECK(floor != stairs); CHECK(air != stairs);
}

TEST_CASE("Loopback rejects protocol-five hello before admission", "[network][loopback][3d]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent()); LanHost host(**runtime, server, TestContent()); REQUIRE(host.Open({0, 0, "Protocol"}));
    ENetHost* client = enet_host_create(nullptr, 1, 2, 0, 0); REQUIRE(client);
    struct Cleanup { ENetHost* host; ~Cleanup() { enet_host_destroy(host); } } cleanup{client};
    ENetAddress address{}; address.port = host.GetPort(); REQUIRE(enet_address_set_host(&address, "127.0.0.1") == 0);
    REQUIRE(enet_host_connect(client, &address, 2, 0));
    bool rejected = false;
    for (int i = 0; i < 500 && !rejected; ++i) {
        host.Pump(10000);
        ENetEvent event{};
        while (enet_host_service(client, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                wire::Writer hello; hello.U32(0x43584e05); hello.U8(static_cast<std::uint8_t>(wire::Type::Hello));
                hello.U64(ContentFingerprint(TestContent())); REQUIRE(hello.Text("Old guest", 32)); hello.U64(0);
                auto* packet = enet_packet_create(hello.bytes.data(), hello.bytes.size(), ENET_PACKET_FLAG_RELIABLE);
                REQUIRE(packet); REQUIRE(enet_peer_send(event.peer, 0, packet) == 0); enet_host_flush(client);
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                wire::Reader reader{{event.packet->data, event.packet->dataLength}};
                std::uint32_t protocol; std::uint8_t type, reason;
                rejected = reader.U32(protocol) && protocol == wire::protocol && reader.U8(type) &&
                    type == static_cast<std::uint8_t>(wire::Type::Rejection) && reader.U8(reason) &&
                    reason == static_cast<std::uint8_t>(NetworkError::Protocol) && reader.Done();
                enet_packet_destroy(event.packet);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(rejected); CHECK(server.GetPlayers().size() == 1);
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
        REQUIRE(server.GetWorld().CreateItem(TestItem("rock"), ItemLocation::Carried(aliceActor)));
    REQUIRE(server.GetWorld().CreateItem(TestItem("rock"), ItemLocation::Ground(a.View().player_position)));
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
