#include "core/presentation.h"
#include "client/tile_inspection.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
TEST_CASE("Repeated presentation capture reuses buffers and clears obsolete visuals", "[presentation][inspection]") {
    Server server(TestContent()); const auto view = server.View(8);
    PlayerPresentation presentation;
    presentation.mapItems.reserve(view.cells.size());
    presentation.interactions.reserve(maxPresentationInteractions);
    const auto* mapBuffer = presentation.mapItems.data();
    const auto* interactionBuffer = presentation.interactions.data();
    REQUIRE(CapturePlayerPresentation(server.GetWorld(), view, presentation));
    CHECK(presentation.mapItems.data() == mapBuffer);
    CHECK(presentation.interactions.data() == interactionBuffer);
    const auto expected = presentation.mapItems;
    REQUIRE(CapturePlayerPresentation(server.GetWorld(), view, presentation));
    CHECK(presentation.mapItems == expected); CHECK(presentation.mapItems.data() == mapBuffer);
    CHECK(presentation.interactions.data() == interactionBuffer);
    auto hidden = view;
    for (auto& cell : hidden.cells) cell = {};
    CaptureMapItems(server.GetWorld(), hidden, presentation.mapItems);
    CHECK(presentation.mapItems.data() == mapBuffer);
    REQUIRE(presentation.mapItems.size() == view.cells.size());
    for (const auto& item : presentation.mapItems) CHECK(item == MapItemVisual{});
}
TEST_CASE("Typed inspection preserves local formatting and read only world state", "[presentation][inspection]") {
    Server server(TestContent());
    const auto view = server.View(8);
    const auto revision = server.GetRevision();
    for (int z = 0; z < view.height; ++z) for (int x = 0; x < view.width; ++x) {
        const TileCoord cursor{view.origin.x + x, view.origin.z + z, view.origin.y};
        TileInspection details;
        REQUIRE(CaptureTileInspection(server.GetWorld(), view, cursor, details));
        Vector<String> local, remote;
        DescribeTile(server.GetWorld(), view, cursor, local);
        DescribeTile(TestContent(), view, cursor, details, remote);
        CHECK(local == remote);
        const auto& cell = view.cells[static_cast<std::size_t>(z * view.width + x)];
        if (cell.overlayVisibility != Visibility::Visible) {
            CHECK(details.actor == 0); CHECK(details.object == 0); CHECK(details.items.empty());
        }
    }
    CHECK(server.GetRevision() == revision);
    CHECK(server.View(8).cells == view.cells);
    TileInspection outside;
    REQUIRE(CaptureTileInspection(server.GetWorld(), view, {view.origin.x - 1, view.origin.z, view.origin.y}, outside));
    CHECK(outside.items.empty()); CHECK(outside.actor == 0); CHECK(outside.object == 0);
}
TEST_CASE("Inspection observes memories without exposing subsequently hidden contents", "[presentation][inspection]") {
    Server server(TestContent()); auto& world = server.GetWorld();
    REQUIRE(world.SetObjectAppearance(world.ObjectAt({2,0}), ObjectKind::Chair));
    REQUIRE(server.Send({}).advanced);
    REQUIRE(world.SetTile({1,0}, {TestTerrain("wall"),0}));
    REQUIRE(world.CreateItem(TestItem("bandage"), ItemLocation::Ground({2,0})));
    const auto memories = world.GetPlayerMemories();
    const auto chunks = world.LoadedChunks();
    PlayerInspection result;
    REQUIRE(CapturePlayerInspection(server, server.GetHostPlayerId(), 1, {2,0}, 8, result));
    CHECK(result.details.actor == 0); CHECK(result.details.object == 0); CHECK(result.details.items.empty());
    const auto index = static_cast<std::size_t>((result.cursor.z - result.view.origin.z) * result.view.width + result.cursor.x - result.view.origin.x);
    CHECK(result.mapItems[index].item == 0);
    Vector<String> lines;
    DescribeTile(TestContent(), result.view, result.cursor, result.details, lines);
    bool remembered = false;
    for (const auto& line : lines) {
        remembered |= line.find("remembered") != String::npos;
        CHECK(line.find("bandage") == String::npos);
    }
    CHECK(remembered); CHECK(world.GetPlayerMemories() == memories);
    CHECK(world.LoadedChunks() == chunks);
}
}
