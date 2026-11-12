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
    bad[bad.size() - 148] = 255; // player state, followed by player light and 128-byte combat view
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

TEST_CASE("Current protocol preserves signed six-axis commands and authoritative shapes", "[network][codec][3d]") {
    CHECK(wire::protocol == 0x43584e0a);
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
            // dy precedes the appended target and style.
            writer.bytes[writer.bytes.size() - 10] = 2;
            wire::Reader malformed{{writer.bytes.data(), writer.bytes.size()}};
            CHECK_FALSE(malformed.CommandValue(decoded));
        }
    }
}

TEST_CASE("Combat commands and outcomes roundtrip with bounded style", "[network][codec][combat]") {
    for (const auto target : {PersistentId{0}, PersistentId{123456789}}) {
        Command command; command.kind = CommandKind::attack; command.target = target;
        wire::Writer writer; REQUIRE(writer.CommandValue(command));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        Command decoded; REQUIRE(reader.CommandValue(decoded)); CHECK(reader.Done());
        CHECK(decoded.kind == command.kind); CHECK(decoded.target == target);
        for (std::size_t n = 0; n < writer.bytes.size(); ++n) {
            wire::Reader truncated{{writer.bytes.data(), n}};
            CHECK_FALSE(truncated.CommandValue(decoded));
        }
    }
    for (const auto style : {CombatStyle::Balanced, CombatStyle::Aggressive, CombatStyle::Defensive}) {
        Command command; command.kind = CommandKind::combat_style; command.style = style;
        wire::Writer writer; REQUIRE(writer.CommandValue(command));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        Command decoded; REQUIRE(reader.CommandValue(decoded)); CHECK(reader.Done());
        CHECK(decoded.kind == command.kind); CHECK(decoded.style == style);
        writer.bytes.back() = 255;
        wire::Reader malformed{{writer.bytes.data(), writer.bytes.size()}};
        CHECK_FALSE(malformed.CommandValue(decoded));
    }
    Command invalid; invalid.kind = CommandKind::combat_style; invalid.style = static_cast<CombatStyle>(255);
    wire::Writer invalidWriter; CHECK_FALSE(invalidWriter.CommandValue(invalid));
    for (const auto outcome : {Outcome::attacked, Outcome::attack_missed, Outcome::attack_parried,
        Outcome::attack_blocked, Outcome::attack_windup, Outcome::target_defeated, Outcome::no_target,
        Outcome::no_combat_move, Outcome::combat_style_changed}) {
        PlayerCommandResult result; result.player = 1; result.sequence = 1; result.opportunity = 1;
        result.result.outcome = outcome;
        wire::Writer writer; REQUIRE(writer.Result(result));
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        PlayerCommandResult decoded; REQUIRE(reader.Result(decoded)); CHECK(reader.Done());
        CHECK(decoded.result.outcome == outcome);
    }
}

TEST_CASE("Combat snapshot roundtrips and validates all fields", "[network][codec][combat]") {
    Server server(TestContent());
    auto view = server.View(0);
    auto& combat = view.combat;
    combat = {};
    combat.style = CombatStyle::Defensive; combat.action = ActorActionKind::MeleeWindup;
    combat.stamina = 47; combat.accuracyPenalty = 30;
    combat.parts[static_cast<std::size_t>(BodyPart::LeftHand)] = {1234, 8000, true};
    combat.feedback.result = {AttackOutcome::Hit, CombatMove::HeavyAttack, 123, BodyPart::LeftHand,
        17, 50, true, true, true, DisruptionResponse::Canceled};
    combat.feedback.incoming = true; combat.feedback.tick = 999;
    wire::Writer writer; REQUIRE(writer.View(view));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded; REQUIRE(reader.View(decoded)); CHECK(reader.Done()); CHECK(decoded.combat == combat);
    const auto start = writer.bytes.size() - 128;
    // Each entry names an invalid byte: enums, limits, flags, and absent simple-body parts.
    for (const auto [offset, value] : {eastl::pair<std::size_t, std::uint8_t>{0, 2}, {1, 3}, {2, 5},
        {6, 101}, {7, 31}, {16, 2}, {89, 11}, {93, 5}, {102, 10},
        {115, 2}, {116, 2}, {117, 2}, {118, 3}, {119, 2}}) {
        auto bytes = writer.bytes; bytes[start + offset] = value;
        wire::Reader invalid{{bytes.data(), bytes.size()}};
        INFO(offset); CHECK_FALSE(invalid.View(decoded));
    }
    for (std::size_t n = start; n < writer.bytes.size(); ++n) {
        wire::Reader truncated{{writer.bytes.data(), n}}; CHECK_FALSE(truncated.View(decoded));
    }
    SECTION("invalid style") { combat.style = static_cast<CombatStyle>(3); }
    SECTION("invalid action") { combat.action = static_cast<ActorActionKind>(-1); }
    SECTION("invalid body") { combat.bodyKind = static_cast<BodyKind>(2); }
    SECTION("invalid stamina") { combat.stamina = 101; }
    SECTION("invalid penalty") { combat.accuracyPenalty = 31; }
    SECTION("hp exceeds maximum") { combat.parts[2].hp = 8001; }
    SECTION("maximum exceeds limit") { combat.parts[2].maximum = UnreducedMax(BodyPart::LeftHand) + 1; }
    SECTION("absent part") { combat.bodyKind = BodyKind::Simple; }
    SECTION("invalid outcome") { combat.feedback.result.outcome = static_cast<AttackOutcome>(11); }
    SECTION("invalid move") { combat.feedback.result.move = static_cast<CombatMove>(5); }
    SECTION("invalid part") { combat.feedback.result.part = static_cast<BodyPart>(10); }
    SECTION("invalid disruption") { combat.feedback.result.disruption = static_cast<DisruptionResponse>(-1); }
    wire::Writer invalidWriter; CHECK_FALSE(invalidWriter.View(view));
}

TEST_CASE("Radius 64 snapshots roundtrip and reject oversized dimensions counts packets and coordinate overflow", "[network][codec][view]") {
    Server server(TestContent());
    auto view = server.View(0);
    const auto player = view.cells[0];
    view.width = 129; view.height = 129;
    view.origin = {-64, -64, view.player_position.y};
    view.cells.assign(129 * 129, ViewCell{});
    view.cells[64 * 129 + 64] = player;
    wire::Writer writer;
    REQUIRE(writer.View(view));
    CHECK(writer.bytes.size() < 1024); // Two unknown runs around the player cell.
    CHECK(writer.bytes.size() < wire::maxPacket);
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded;
    REQUIRE(reader.View(decoded)); REQUIRE(reader.Done());
    CHECK(decoded.width == 129); CHECK(decoded.height == 129);
    CHECK(decoded.origin == view.origin); CHECK(decoded.cells == view.cells);
    for (const auto dimension : {130, 131, INT_MAX}) {
        auto invalid = view; invalid.width = dimension; invalid.height = dimension;
        wire::Writer rejected; CHECK_FALSE(rejected.View(invalid));
    }
    auto invalid = view;
    invalid.cells.pop_back();
    wire::Writer wrongCount; CHECK_FALSE(wrongCount.View(invalid));
    invalid = view; invalid.player_position.x = INT_MAX; invalid.origin.x = INT_MAX - 64;
    wire::Writer overflow; CHECK_FALSE(overflow.View(invalid));
    for (const int offset : {12, 13}) {
        auto bad = writer.bytes; bad[offset] = 131;
        wire::Reader rejected{{bad.data(), bad.size()}};
        CHECK_FALSE(rejected.View(decoded));
    }
    auto bad = writer.bytes;
    bad[85] = 0; bad[86] = 0; // u16 cell count after pacing and command flags
    wire::Reader badCount{{bad.data(), bad.size()}};
    CHECK_FALSE(badCount.View(decoded));
    bad = writer.bytes; bad.resize(wire::maxPacket + 1);
    wire::Reader oversized{{bad.data(), bad.size()}};
    CHECK_FALSE(oversized.View(decoded));
}

TEST_CASE("Run encoded snapshots bound expansion and preserve repeated and varying cells", "[network][codec][view]") {
    Server server(TestContent());
    auto view = server.View(0);
    view.width = 129; view.height = 129; view.origin = {-64, -64};
    view.cells.assign(129 * 129, ViewCell{});
    wire::Writer compact; REQUIRE(compact.View(view));
    CHECK(compact.bytes.size() < 1024);
    WorldView decoded;
    wire::Reader reader{{compact.bytes.data(), compact.bytes.size()}};
    REQUIRE(reader.View(decoded)); REQUIRE(reader.Done());
    CHECK(decoded.cells == view.cells);
    CHECK(decoded.opportunity == view.opportunity);
    for (const std::size_t cut : {std::size_t{0}, std::size_t{86}, std::size_t{87}, std::size_t{88},
        std::size_t{89}, std::size_t{124}, compact.bytes.size() - 1}) {
        wire::Reader truncated{{compact.bytes.data(), cut}};
        CHECK_FALSE(truncated.View(decoded));
    }
    for (const std::uint16_t length : {std::uint16_t{0}, std::uint16_t{16642}, std::uint16_t{65535}}) {
        auto bad = compact.bytes;
        bad[87] = static_cast<std::uint8_t>(length >> 8); bad[88] = static_cast<std::uint8_t>(length);
        wire::Reader invalid{{bad.data(), bad.size()}};
        CHECK_FALSE(invalid.View(decoded));
        CHECK(decoded.cells.size() <= 129 * 129);
    }
    // Worst case: every cell starts a run. This still fits the packet cap and
    // retains all fields, rather than truncating perception to reduce traffic.
    for (std::size_t i = 0; i < view.cells.size(); ++i) {
        auto& cell = view.cells[i];
        cell.tile = {TestTerrain("floor"), static_cast<std::uint8_t>(i % 256)};
        cell.terrainVisibility = Visibility::Visible; cell.terrainLight = LightBand::Ordinary;
        cell.terrainDepth = static_cast<unsigned>(i % 3);
        cell.overlayVisibility = Visibility::Visible; cell.actor = 77;
        cell.overlayDepth = static_cast<unsigned>(i % 2); cell.overlayLight = LightBand::Dim;
    }
    wire::Writer varied; REQUIRE(varied.View(view));
    CHECK(varied.bytes.size() > 32768); // Cannot collapse into a small uniform run.
    CHECK(varied.bytes.size() <= wire::maxPacket);
    wire::Reader variedReader{{varied.bytes.data(), varied.bytes.size()}};
    REQUIRE(variedReader.View(decoded)); CHECK(variedReader.Done());
    CHECK(decoded.cells == view.cells);

    // A repeated visible payload must be checked at every expanded coordinate.
    view.width = 3; view.height = 3; view.origin = {-1, -1};
    view.cells.assign(9, view.cells[0]);
    wire::Writer edge; REQUIRE(edge.View(view));
    wire::Writer coordinate; coordinate.I32(world_coordinate_limit - 1);
    for (int i = 0; i < 4; ++i) { edge.bytes[i] = coordinate.bytes[i]; }
    coordinate.bytes.clear(); coordinate.I32(world_coordinate_limit);
    for (int i = 0; i < 4; ++i) { edge.bytes[14 + i] = coordinate.bytes[i]; }
    wire::Reader outside{{edge.bytes.data(), edge.bytes.size()}};
    CHECK_FALSE(outside.View(decoded));
}

TEST_CASE("Actual elevated server projections roundtrip independent terrain and overlay depths", "[network][codec][3d]") {
    Server server(TestContent());
    auto& world = server.GetWorld();
    // This test isolates projection/codec geometry, including sealed underground
    // layers where sky cannot supply illumination.
    REQUIRE(world.SetActorLight(server.GetPlayer(), {192, 12}));
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
        REQUIRE(actorCell.terrainVisibility == Visibility::Visible); CHECK(actorCell.terrainDepth == 1); CHECK(actorCell.overlayDepth == 0);
        CHECK(actorCell.actor == world.GetActor(*foreground)->id);
        const auto& itemCell = view.cells[7 * view.width + 4];
        REQUIRE(itemCell.terrainVisibility == Visibility::Visible); CHECK(itemCell.terrainDepth == 1); CHECK(itemCell.overlayDepth == 1);
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
    ViewCell cell; cell.terrainVisibility = Visibility::Visible; cell.tile.terrain = TestTerrain("floor");
    view.cells.push_back(cell);
    return view;
}

TEST_CASE("Snapshot codecs reject invalid elevation, source depths and visible bounds", "[network][codec][3d]") {
    const auto baseline = SingleCellView();
    auto invalid = baseline;
    wire::Writer writer; REQUIRE(writer.View(baseline));
    // Protocol-nine header ends at byte 87, followed by the u16 run length
    // and the run's 36-byte cell payload. This fixture contains one cell.
    constexpr std::size_t originY = 8, playerY = 22, firstCell = 89;
    constexpr std::size_t terrainDepth = firstCell + 31, overlayDepth = firstCell + 32;
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
        invalid.cells[0].overlayVisibility = Visibility::Visible;
        writer.bytes[firstCell + 13] = 7; // actor low byte
        writer.bytes[firstCell + 33] = 2;
        offset = overlayDepth; replacement.U8(11);
    }
    SECTION("terrain beyond byte contract") { invalid.cells[0].terrainDepth = 21; offset = terrainDepth; replacement.U8(21); }
    SECTION("overlay beyond byte contract") { invalid.cells[0].overlayDepth = 255; offset = overlayDepth; replacement.U8(255); }
    SECTION("unsigned depth cannot wrap on encode") { invalid.cells[0].terrainDepth = 256; offset = terrainDepth; replacement.U8(255); }
    SECTION("depth without overlay") { invalid.cells[0].overlayDepth = 1; offset = overlayDepth; replacement.U8(1); }
    SECTION("zero terrain") { invalid.cells[0].tile.terrain = {}; offset = firstCell + 1; replacement.U32(0); }
    SECTION("invalid visibility") { invalid.cells[0].terrainVisibility = static_cast<Visibility>(3); offset = firstCell; replacement.U8(3); }
    SECTION("player without actor") { invalid.cells[0].player = true; offset = firstCell + 14; replacement.U8(1); }
    SECTION("active without object") { invalid.cells[0].objectAppearance.active = true; offset = firstCell + 29; replacement.U8(1); }
    wire::Writer rejected; CHECK_FALSE(rejected.View(invalid));
    REQUIRE_FALSE(replacement.bytes.empty());
    for (std::size_t i = 0; i < replacement.bytes.size(); ++i) writer.bytes[offset + i] = replacement.bytes[i];
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded; CHECK_FALSE(reader.View(decoded));
}

TEST_CASE("Snapshot runs roundtrip independent memory and live contributions", "[network][codec][perception]") {
    auto view = SingleCellView(2);
    auto& cell = view.cells[0];
    cell.terrainDepth = 3;
    cell.overlayDepth = 1;
    cell.objectAppearance = {ObjectKind::Chair, ObjectFacing::West, false, true};
    cell.overlayVisibility = Visibility::Remembered;
    cell.terrainLight = LightBand::Bright;
    view.playerLight = LightBand::Dim;
    SECTION("live terrain remembered furniture") {}
    SECTION("remembered terrain live actor and furniture") {
        cell.terrainVisibility = Visibility::Remembered; cell.terrainLight = LightBand::Dark;
        cell.overlayVisibility = Visibility::Visible; cell.overlayLight = LightBand::Ordinary;
        cell.actor = 42; cell.ground_items = 2; cell.object = 99;
    }
    SECTION("unknown terrain remembered furniture") {
        cell.terrainVisibility = Visibility::Unknown; cell.tile = {}; cell.terrainDepth = 0;
        cell.terrainLight = LightBand::Dark;
    }
    SECTION("remembered terrain live items") {
        cell.terrainVisibility = Visibility::Remembered; cell.terrainLight = LightBand::Dark;
        cell.overlayVisibility = Visibility::Visible; cell.overlayLight = LightBand::Dark;
        cell.objectAppearance = {}; cell.ground_items = 1;
    }
    wire::Writer writer; REQUIRE(writer.View(view));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded; REQUIRE(reader.View(decoded)); CHECK(reader.Done());
    CHECK(decoded.cells == view.cells); CHECK(decoded.playerLight == LightBand::Dim);
}

TEST_CASE("Snapshot runs reject hidden information and malformed perception states", "[network][codec][perception]") {
    auto view = SingleCellView();
    auto& cell = view.cells[0];
    cell.terrainVisibility = Visibility::Remembered;
    cell.overlayVisibility = Visibility::Remembered;
    cell.objectAppearance = {ObjectKind::Lamp, ObjectFacing::East, true, false};
    wire::Writer writer; REQUIRE(writer.View(view));
    std::size_t field = 0; std::uint8_t value = 0;
    // Cell payload offsets exclude the u16 run length and are not native structure offsets.
    SECTION("terrain visibility") { cell.terrainVisibility = static_cast<Visibility>(3); field = 0; value = 3; }
    SECTION("overlay visibility") { cell.overlayVisibility = static_cast<Visibility>(3); field = 33; value = 3; }
    SECTION("terrain band") { cell.terrainLight = static_cast<LightBand>(4); field = 34; value = 4; }
    SECTION("overlay band") { cell.overlayLight = static_cast<LightBand>(4); field = 35; value = 4; }
    SECTION("current light on memory terrain") { cell.terrainLight = LightBand::Dim; field = 34; value = 1; }
    SECTION("current light on memory object") { cell.overlayLight = LightBand::Bright; field = 35; value = 3; }
    SECTION("memory damage") { cell.tile.damage = 1; field = 5; value = 1; }
    SECTION("memory actor") { cell.actor = 1; field = 13; value = 1; }
    SECTION("memory items") { cell.ground_items = 1; field = 18; value = 1; }
    SECTION("memory object handle") { cell.object = 1; field = 26; value = 1; }
    SECTION("empty remembered object") { cell.objectAppearance.kind = ObjectKind::None; field = 27; value = 0; }
    SECTION("object kind") { cell.objectAppearance.kind = static_cast<ObjectKind>(5); field = 27; value = 5; }
    SECTION("object facing") { cell.objectAppearance.facing = static_cast<ObjectFacing>(4); field = 28; value = 4; }
    SECTION("active boolean") { field = 29; value = 2; }
    SECTION("broken boolean") { field = 30; value = 2; }
    SECTION("terrain depth") { cell.terrainDepth = 21; field = 31; value = 21; }
    SECTION("overlay depth") { cell.overlayDepth = 21; field = 32; value = 21; }
    SECTION("live descriptor without handle") { cell.overlayVisibility = Visibility::Visible; field = 33; value = 2; }
    if (field != 29 && field != 30) {
        wire::Writer invalid; CHECK_FALSE(invalid.View(view));
    }
    writer.bytes[89 + field] = value;
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded; CHECK_FALSE(reader.View(decoded));
}

TEST_CASE("Snapshot runs validate the appended player light band", "[network][codec][perception]") {
    auto view = SingleCellView();
    wire::Writer writer; REQUIRE(writer.View(view));
    writer.bytes[writer.bytes.size() - 129] = 4;
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded; CHECK_FALSE(reader.View(decoded));
    view.playerLight = static_cast<LightBand>(4);
    wire::Writer invalid; CHECK_FALSE(invalid.View(view));
}

TEST_CASE("Receiving catalog validation rejects unknown terrain including memory", "[network][codec][perception]") {
    for (const auto visibility : {Visibility::Visible, Visibility::Remembered}) {
        auto view = SingleCellView(); view.cells[0].terrainVisibility = visibility;
        CHECK(wire::ValidTerrainDefinitions(view, TestContent()));
        view.cells[0].tile.terrain = {UINT32_MAX};
        wire::Writer writer; REQUIRE(writer.View(view)); // Definition-independent structural codec.
        wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
        WorldView decoded; REQUIRE(reader.View(decoded));
        CHECK_FALSE(wire::ValidTerrainDefinitions(decoded, TestContent()));
    }
}

TEST_CASE("Hidden cells are default and projection origins may extend past horizontal boundaries", "[network][codec][3d]") {
    auto view = SingleCellView(world_max_y);
    view.cells[0].terrainDepth = 20; view.cells[0].overlayDepth = 20; view.cells[0].actor = 9;
    view.cells[0].overlayVisibility = Visibility::Visible;
    wire::Writer deepest; REQUIRE(deepest.View(view));
    wire::Reader deepestReader{{deepest.bytes.data(), deepest.bytes.size()}};
    WorldView decoded; REQUIRE(deepestReader.View(decoded)); CHECK(decoded.cells == view.cells);
    auto leaked = deepest.bytes; leaked[89] = 0; // Unknown terrain must have a default payload.
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
    // Move the visible run from center cell 4 to out-of-world cell 5 by shifting
    // one unknown cell from the final run into the first run (4,1,4 -> 5,1,3).
    view.cells[4] = view.cells[5]; view.cells[5] = {};
    wire::Writer centered; REQUIRE(centered.View(view));
    constexpr std::size_t firstRun = 87, runRecordSize = 38;
    REQUIRE(centered.bytes[firstRun + 1] == 4);
    REQUIRE(centered.bytes[firstRun + runRecordSize + 1] == 1);
    REQUIRE(centered.bytes[firstRun + 2 * runRecordSize + 1] == 4);
    centered.bytes[firstRun + 1] = 5;
    centered.bytes[firstRun + 2 * runRecordSize + 1] = 3;
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
    SECTION("hidden active") { view.cells[0].objectAppearance.active = true; }
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
    CHECK(floor != fingerprint("open_space:false,stairs:false,light:{intensity:120,range:4}"));
    CHECK(fingerprint("light:{intensity:120,range:4}") != fingerprint("light:{intensity:121,range:4}"));
    CHECK(fingerprint("light:{intensity:120,range:4}") != fingerprint("light:{intensity:120,range:5}"));
}

TEST_CASE("Loopback rejects protocol-eight hello before admission", "[network][loopback][view]") {
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
                wire::Writer hello; hello.U32(0x43584e08); hello.U8(static_cast<std::uint8_t>(wire::Type::Hello));
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

TEST_CASE("Host close cancels a partially sent large snapshot before disconnect notification", "[network][loopback][view]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent());
    // Alternating visible payloads keep this a genuinely fragmented snapshot
    // even after run compression; the unknown joining view now fits one run.
    for (int z = -32; z <= 32; ++z) {
        for (int x = -32; x <= 32; ++x) {
            REQUIRE(server.GetWorld().SetTile({x, z}, {TestTerrain("floor"), static_cast<std::uint8_t>(x & 1)}));
        }
    }
    LanHost host(**runtime, server, TestContent()); REQUIRE(host.Open({0, 0, "Closing snapshot"}));
    ENetHost* client = enet_host_create(nullptr, 1, 2, 0, 0); REQUIRE(client);
    struct Cleanup { ENetHost* host; ~Cleanup() { enet_host_destroy(host); } } cleanup{client};
    client->maximumPacketSize = wire::maxPacket;
    client->maximumWaitingData = 2 * wire::maxPacket;
    ENetAddress address{}; address.port = host.GetPort();
    REQUIRE(enet_address_set_host(&address, "127.0.0.1") == 0);
    REQUIRE(enet_host_connect(client, &address, 2, 0));
    bool helloSent = false;
    std::uint64_t nonce = 0;
    const auto send = [](ENetPeer* peer, wire::Writer& message) {
        auto* packet = enet_packet_create(message.bytes.data(), message.bytes.size(), ENET_PACKET_FLAG_RELIABLE);
        REQUIRE(packet); REQUIRE(enet_peer_send(peer, 0, packet) == 0);
        enet_host_flush(peer->host);
    };
    for (int i = 0; i < 500; ++i) {
        const auto update = host.Pump(0); REQUIRE_FALSE(update.networkError);
        // Stop before servicing/ACKing the active view queued by Ready handling.
        if (server.GetPlayers().size() == 2 && server.GetPlayers()[1].state == PlayerConnectionState::Active) { break; }
        ENetEvent event{};
        while (enet_host_service(client, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                wire::Writer hello; hello.U32(wire::protocol);
                hello.U8(static_cast<std::uint8_t>(wire::Type::Hello));
                hello.U64(ContentFingerprint(TestContent()));
                REQUIRE(hello.Text("Closing guest", 32)); hello.U64(0);
                send(event.peer, hello); helloSent = true;
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                wire::Reader reader{{event.packet->data, event.packet->dataLength}};
                std::uint32_t protocol; std::uint8_t type;
                REQUIRE(reader.U32(protocol)); REQUIRE(protocol == wire::protocol); REQUIRE(reader.U8(type));
                if (type == static_cast<std::uint8_t>(wire::Type::Accepted)) {
                    std::uint64_t player, token;
                    REQUIRE(reader.U64(player)); REQUIRE(reader.U64(token)); REQUIRE(reader.U64(nonce));
                } else if (type == static_cast<std::uint8_t>(wire::Type::Snapshot)) {
                    std::uint64_t receivedNonce; REQUIRE(reader.U64(receivedNonce)); REQUIRE(receivedNonce == nonce);
                    WorldView view; REQUIRE(reader.View(view)); REQUIRE(reader.Done());
                    wire::Writer ack; ack.U32(wire::protocol); ack.U8(static_cast<std::uint8_t>(wire::Type::SnapshotAck));
                    ack.U64(nonce); ack.U64(view.revision); send(event.peer, ack);
                    wire::Writer ready; ready.U32(wire::protocol); ready.U8(static_cast<std::uint8_t>(wire::Type::Ready));
                    ready.U64(nonce); ready.U64(view.playerId); send(event.peer, ready);
                }
                enet_packet_destroy(event.packet);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(helloSent); REQUIRE(server.GetPlayers().size() == 2);
    REQUIRE(server.GetPlayers()[1].state == PlayerConnectionState::Active);
    wire::Writer snapshot;
    REQUIRE(snapshot.View(server.ViewForPlayer(server.GetPlayers()[1].id, 64)));
    REQUIRE(snapshot.bytes.size() > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE);
    host.Close(); REQUIRE_FALSE(host.IsOpen());
    bool disconnected = false;
    for (int i = 0; i < 100 && !disconnected; ++i) {
        ENetEvent event{};
        while (enet_host_service(client, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_DISCONNECT) { disconnected = true; }
            else if (event.type == ENET_EVENT_TYPE_RECEIVE) { enet_packet_destroy(event.packet); }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(disconnected);
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
    CHECK(a.View().width == 129); CHECK(a.View().height == 129);
    CHECK(b.View().width == 129); CHECK(b.View().height == 129);
    CHECK(a.View().cells.size() == 129 * 129);
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

TEST_CASE("Full radius LAN admission and reconnect deliver successful first inputs at frontend poll cadence", "[network][loopback][view][receipt]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent());
    LanHost host(**runtime, server, TestContent()); REQUIRE(host.Open({0, 0, "Frontend cadence"}));
    LanClient guest(**runtime, TestContent());
    std::uint64_t token = 0;
    PersistentId player = 0;
    for (int round = 0; round < 2; ++round) {
        REQUIRE(guest.Connect({"127.0.0.1", host.GetPort(), "Alice", token}));
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = started + std::chrono::milliseconds(2500);
        auto previous = started;
        std::uint64_t sequence = 0;
        eastl::optional<Outcome> outcome;
        while (std::chrono::steady_clock::now() < deadline && !outcome) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count());
            previous = now;
            const auto update = host.Pump(elapsed); REQUIRE_FALSE(update.networkError);
            guest.Pump(elapsed);
            if (!sequence && guest.GetState() == ConnectionState::Active) {
                REQUIRE(guest.View().width == 129); REQUIRE(guest.View().height == 129);
                if (round == 0) { player = guest.View().playerId; token = guest.GetReconnectToken(); }
                CHECK(guest.View().playerId == player);
                const auto sent = guest.Submit({CommandKind::wait}); REQUIRE(sent); sequence = *sent;
            }
            for (const auto& receipt : guest.TakeResults()) {
                REQUIRE(receipt.sequence == sequence);
                if (receipt.result.outcome != Outcome::queued) { outcome = receipt.result.outcome; }
            }
            // Match the frontend's normal service cadence rather than a tight
            // loop that hides multi-window snapshot admission latency.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        INFO("round=" << round << " submitted=" << sequence);
        REQUIRE(outcome); CHECK(*outcome == Outcome::waited);
        guest.Disconnect();
        bool parked = false;
        for (int i = 0; i < 100 && !parked; ++i) {
            host.Pump(10000);
            for (const auto& status : server.GetPlayers()) {
                if (status.id == player) { parked = status.state == PlayerConnectionState::Parked; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        REQUIRE(parked);
    }
}

TEST_CASE("Reconnected guest input waits for the idle host's full grace", "[network][loopback][receipt]") {
    auto runtime = NetworkRuntime::Create(); REQUIRE(runtime);
    Server server(TestContent());
    LanHost host(**runtime, server, TestContent());
    REQUIRE(host.Open({0, 0, "Reconnect grace"}));
    LanClient guest(**runtime, TestContent()), idle(**runtime, TestContent());
    REQUIRE(guest.Connect({"127.0.0.1", host.GetPort(), "Alice", 0}));
    REQUIRE(Poll(host, guest, idle, [&] { return guest.GetState() == ConnectionState::Active; }));
    const auto player = guest.View().playerId;
    const auto token = guest.GetReconnectToken();
    guest.Disconnect();
    REQUIRE(Poll(host, guest, idle, [&] {
        for (const auto& p : server.GetPlayers())
            if (p.id == player) return p.state == PlayerConnectionState::Parked;
        return false;
    }));
    REQUIRE(guest.Connect({"127.0.0.1", host.GetPort(), "Alice", token}));
    REQUIRE(Poll(host, guest, idle, [&] { return guest.GetState() == ConnectionState::Active; }));
    REQUIRE(guest.View().playerId == player);
    // Idle time during admission does not spend the host's input grace.
    const auto hostActor = server.GetWorld().FindActor(server.GetHostPlayerId());
    REQUIRE(server.View().players[0].graceMicroseconds == 1000000);
    const auto sequence = guest.Submit({CommandKind::wait}); REQUIRE(sequence);
    bool queued = false;
    bool resolved = false;
    const auto receive = [&] {
        for (const auto& receipt : guest.TakeResults()) {
            CHECK(receipt.sequence == *sequence);
            if (receipt.result.outcome == Outcome::queued) queued = true;
            else { CHECK(receipt.result.outcome == Outcome::waited); resolved = true; }
        }
    };
    for (int i = 0; i < 200 && !queued; ++i) {
        guest.Pump(0); host.Pump(0); guest.Pump(0); receive();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(queued); REQUIRE_FALSE(resolved);
    host.Pump(999999);
    REQUIRE(server.GetWorld().GetActorRemainingTicks(hostActor) == 0);
    for (const auto& p : server.GetPlayers())
        if (p.id == server.GetHostPlayerId()) REQUIRE(p.graceMicroseconds == 1);
    const auto update = host.Pump(1);
    REQUIRE(update.commandResults.size() == 1);
    CHECK(update.commandResults[0].player == player);
    CHECK(update.commandResults[0].sequence == *sequence);
    CHECK(update.commandResults[0].result.outcome == Outcome::waited);
    for (int i = 0; i < 200 && !resolved; ++i) {
        guest.Pump(0); host.Pump(0); guest.Pump(0); receive();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(resolved);
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
