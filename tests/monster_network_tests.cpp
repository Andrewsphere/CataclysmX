#include "network/wire.h"
#include "content/content.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/utility.h>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;

constexpr std::size_t viewHeaderSize = 87;
constexpr std::size_t runRecordSize = 54; // Protocol 23 appends the meter-only grid ID.
constexpr std::size_t firstMonsterType = viewHeaderSize + 2 + 36;

ContentCatalog MonsterContent() {
    constexpr ContentSource source{"monster-network.json5", R"({format_version:1,
      ai_profiles:[{id:'hunter'}],definitions:[
        {type:'monster',id:'firefighter',name:'Firefighter zombie',ai_profile:'hunter'},
        {type:'monster',id:'nurse',name:'Nurse zombie',ai_profile:'hunter'}
      ]})"};
    auto content = LoadContent({&source, 1});
    REQUIRE(content);
    return eastl::move(*content);
}

WorldView MonsterView(MonsterTypeId type) {
    WorldView view;
    view.width = 1; view.height = 1; view.playerId = 1;
    ViewCell cell;
    cell.overlayVisibility = Visibility::Visible;
    cell.actor = 77;
    cell.monsterType = type;
    view.cells.push_back(cell);
    return view;
}

TEST_CASE("Typed visible monster cells roundtrip and require admitted catalog IDs", "[monster][network][codec]") {
    const auto content = MonsterContent();
    const auto type = content.FindMonster("firefighter");
    REQUIRE(type.value != 0);
    auto view = MonsterView(type);
    REQUIRE(wire::ValidMonsterDefinitions(view, content));
    wire::Writer writer;
    REQUIRE(writer.View(view));
    WorldView decoded;
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.View(decoded)); REQUIRE(reader.Done());
    CHECK(decoded.cells == view.cells);
    CHECK(wire::ValidMonsterDefinitions(decoded, content));
    for (std::size_t size = 0; size < writer.bytes.size(); ++size) {
        wire::Reader truncated{{writer.bytes.data(), size}};
        WorldView partial;
        CHECK_FALSE(truncated.View(partial));
    }
    // Unknown catalog IDs are structurally valid, but cannot pass publication validation.
    for (std::size_t i = 0; i < 4; ++i) writer.bytes[firstMonsterType + i] = 255;
    wire::Reader malformed{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(malformed.View(decoded)); REQUIRE(malformed.Done());
    CHECK(decoded.cells[0].monsterType == MonsterTypeId{UINT32_MAX});
    CHECK_FALSE(wire::ValidMonsterDefinitions(decoded, content));
    view.cells[0].monsterType = {};
    CHECK(wire::ValidMonsterDefinitions(view, content));
    CHECK(wire::Writer{}.View(view));
}

TEST_CASE("Monster identity cannot be encoded or decoded on hidden remembered empty or player cells",
    "[monster][network][codec][visibility]") {
    auto view = MonsterView({});
    auto& cell = view.cells[0];
    SECTION("unknown overlay") { cell = {}; }
    SECTION("remembered object") {
        cell = {};
        cell.overlayVisibility = Visibility::Remembered;
        cell.objectAppearance.kind = ObjectKind::Chair;
    }
    SECTION("visible player") { cell.player = true; }
    SECTION("visible empty actor slot") { cell.actor = 0; cell.ground_items = 1; }
    wire::Writer writer;
    REQUIRE(writer.View(view));
    cell.monsterType = {1};
    CHECK_FALSE(wire::Writer{}.View(view));
    // Inject identity into an otherwise canonical cell. Reject before appending it.
    writer.bytes[firstMonsterType + 3] = 1;
    WorldView decoded;
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    CHECK_FALSE(reader.View(decoded));
    CHECK(decoded.cells.empty());
}

TEST_CASE("Sparse monster RLE distinguishes adjacent types with identical actor metadata", "[monster][network][codec][view]") {
    const auto content = MonsterContent();
    auto view = MonsterView(content.FindMonster("firefighter"));
    const auto first = view.cells[0];
    auto second = first;
    second.monsterType = content.FindMonster("nurse");
    REQUIRE(first.monsterType != second.monsterType);
    view.width = 129; view.height = 129; view.origin = {-64, -64};
    view.cells.assign(129 * 129, ViewCell{});
    constexpr std::size_t center = 64 * 129 + 64;
    view.cells[center] = first;
    view.cells[center + 1] = second;
    wire::Writer writer;
    REQUIRE(writer.View(view));
    // Unknown prefix, two distinct actor runs, unknown suffix.
    CHECK(writer.bytes[viewHeaderSize + runRecordSize + 1] == 1);
    CHECK(writer.bytes[viewHeaderSize + 2 * runRecordSize + 1] == 1);
    auto same = view;
    same.cells[center + 1] = first;
    wire::Writer merged;
    REQUIRE(merged.View(same));
    CHECK(writer.bytes.size() == merged.bytes.size() + runRecordSize);
    WorldView decoded;
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.View(decoded)); REQUIRE(reader.Done());
    CHECK(decoded.cells == view.cells);
    CHECK(wire::ValidMonsterDefinitions(decoded, content));
}

const ViewCell& Cell(const WorldView& view, int x, int z) {
    const auto index = static_cast<std::size_t>((z - view.origin.z) * view.width + x - view.origin.x);
    REQUIRE(index < view.cells.size());
    return view.cells[index];
}

TEST_CASE("Server projects only the visible monster identity including lower overlays", "[monster][network][server][3d]") {
    // Keep this geometry fixture independent of the expanded town catalog.
    Diagnostics diagnostics;
    const auto loaded = LoadContentPack(CATACLYSMX_FIRST_HOUSE_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str());
    REQUIRE(loaded);
    const auto& content = *loaded;
    const auto type = content.FindMonster("cellar_zombie");
    REQUIRE(type.value != 0);
    const auto floor = content.FindTerrain("floor");
    const auto air = content.FindTerrain("air");
    REQUIRE(floor.value != 0);
    REQUIRE(air.value != 0);
    Server server(content);
    auto& world = server.GetWorld();
    REQUIRE(world.SetTile({0, 0, 1}, {floor, 0}));
    for (int x = 1; x <= 8; ++x) {
        REQUIRE(world.SetTile({x, 0, 0}, {floor, 0}));
        REQUIRE(world.SetTile({x, 0, 1}, {air, 0}));
    }
    REQUIRE(world.MoveActor(server.GetPlayer(), {0, 0, 1}));
    const auto monster = world.CreateMonster(type, {8, 0, 0});
    REQUIRE(monster);
    REQUIRE(world.SetActorLight(*monster, {64, 1}));
    world.SetSkyLight(0);
    const auto view = server.View(12);
    const auto& visible = Cell(view, 8, 0);
    REQUIRE(visible.overlayVisibility == Visibility::Visible);
    CHECK(visible.actor == world.GetActor(*monster)->id);
    CHECK(visible.overlayDepth == 1);
    CHECK(visible.monsterType == type);
    CHECK(Cell(view, 0, 0).player);
    CHECK(Cell(view, 0, 0).monsterType == MonsterTypeId{});
    wire::Writer writer;
    REQUIRE(writer.View(view));
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    WorldView decoded;
    REQUIRE(reader.View(decoded)); REQUIRE(reader.Done());
    CHECK(decoded.cells == view.cells);
    CHECK(wire::ValidMonsterDefinitions(decoded, content));

    world.UpdatePlayerObservations();
    REQUIRE(world.SetActorLight(*monster, {}));
    const auto hidden = server.View(12);
    // Once the open column is dark, the upper view cannot project a remembered
    // lower floor through unknown air. Memory still belongs to the lower layer.
    CHECK(Cell(hidden, 8, 0).overlayVisibility != Visibility::Visible);
    CHECK(Cell(hidden, 8, 0).actor == 0);
    for (const auto& cell : hidden.cells) {
        if (!cell.actor || cell.player || cell.overlayVisibility != Visibility::Visible) {
            CHECK(cell.monsterType == MonsterTypeId{});
        }
    }
    const auto remembered = server.ViewForPlayerAtLayer(server.GetHostPlayerId(), 12, 0);
    CHECK(Cell(remembered, 8, 0).terrainVisibility == Visibility::Remembered);
    CHECK(Cell(remembered, 8, 0).actor == 0);
    CHECK(Cell(remembered, 8, 0).monsterType == MonsterTypeId{});
}
}
