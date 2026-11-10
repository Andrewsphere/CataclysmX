#include "sdl/map_packet.h"
#include "content_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <condition_variable>
#include <mutex>

using namespace cataclysmx;
using namespace cataclysmx::sdl;
namespace {
ContentCatalog Catalog() {
    const ContentSource source{"test", "{format_version:1,definitions:[{type:'terrain',id:'floor',name:'Floor',movement:{passable:true},blocks_sight:false,appearance:{symbol:'.',foreground:'light_gray'}}]}"};
    auto result = LoadContent({&source,1}); REQUIRE(result); return eastl::move(*result);
}
struct WakeState { std::mutex mutex; std::condition_variable condition; bool woken = false; };
void Wake(void* data) {
    auto& state = *static_cast<WakeState*>(data);
    { std::lock_guard lock(state.mutex); state.woken = true; }
    state.condition.notify_one();
}
}
TEST_CASE("map packet preserves overlay priority and hides unseen cells", "[sdl][map]") {
    auto content = Catalog(); TilesetDefinition tileset; tileset.terrain.resize(content.TerrainCount()+1);
    tileset.terrain[1] = {TilesetMappingKind::Sprite, 3};
    tileset.generic[static_cast<unsigned>(GenericSprite::Player)] = {TilesetMappingKind::Sprite, 4};
    WorldView view; view.width = 2; view.height = 1; view.playerId = 42; view.cells.resize(2);
    auto& cell = view.cells[0]; cell.terrainVisibility = cell.overlayVisibility = Visibility::Visible;
    cell.tile.terrain = content.FindTerrain("floor");
    cell.actor = 42; cell.ground_items = 2; cell.object = 4;
    view.cells[1].tile.terrain = TerrainId{999};
    MapIdentity id{1,2,3,4}; auto packet = PrepareMapPacket(view,content,tileset,id);
    CHECK(packet.identity == id); REQUIRE(packet.cells.size() == 2);
    CHECK(packet.cells[0].terrainSprite.index == 3); CHECK(packet.cells[0].overlaySprite.index == 4);
    CHECK(packet.cells[0].overlay.symbol == "@"); CHECK(packet.cells[0].terrain.background == packet.cells[0].overlay.background);
    CHECK_FALSE(packet.cells[1].visible); CHECK_FALSE(packet.cells[1].hasOverlay);
    view.cells[0].actor = 2; view.cells[0].player = true;
    packet = PrepareMapPacket(view,content,tileset,id); CHECK(packet.cells[0].overlay.symbol == "P");
    view.cells[0].actor = 0;
    packet = PrepareMapPacket(view,content,tileset,id); CHECK(packet.cells[0].overlay.symbol == "!");
}
TEST_CASE("map packets keep terrain and overlay perception independent", "[sdl][map][perception]") {
    const auto content = Catalog();
    TilesetDefinition tileset;
    tileset.terrain.resize(content.TerrainCount() + 1);
    tileset.terrain[1] = {TilesetMappingKind::Sprite, 3};
    tileset.generic[static_cast<unsigned>(GenericSprite::ObjectEnabled)] = {TilesetMappingKind::Sprite, 4};
    WorldView view;
    view.width = view.height = 1;
    view.cells.resize(1);
    auto& cell = view.cells[0];
    cell.tile.terrain = content.FindTerrain("floor");
    cell.terrainVisibility = cell.overlayVisibility = Visibility::Visible;
    cell.terrainLight = LightBand::Ordinary;
    cell.overlayLight = LightBand::Dim;
    cell.object = 17;
    cell.objectAppearance = {ObjectKind::Generic, ObjectFacing::North, true};

    SECTION("Object appearance does not replace the underlying terrain fallback") {
        const auto packet = PrepareMapPacket(view, content, tileset, {});
        CHECK(packet.cells[0].terrain.symbol == ".");
        CHECK(packet.cells[0].overlay.symbol == "*");
        CHECK(packet.cells[0].overlay == CellAppearance(cell, content));
        CHECK(packet.cells[0].terrainSprite.index == 3);
        CHECK(packet.cells[0].overlaySprite.index == 4);
    }
    SECTION("Remembered objects draw without a live entity ID or animation") {
        cell.terrainVisibility = cell.overlayVisibility = Visibility::Remembered;
        cell.terrainLight = cell.overlayLight = LightBand::Dark;
        cell.object = 0;
        const auto packet = PrepareMapPacket(view, content, tileset, {});
        CHECK(packet.cells[0].visible);
        CHECK(packet.cells[0].hasOverlay);
        CHECK(packet.cells[0].overlay == CellAppearance(cell, content));
        CHECK(packet.cells[0].terrainSprite.kind == TilesetMappingKind::Missing);
        CHECK(packet.cells[0].overlaySprite.kind == TilesetMappingKind::Missing);
        CHECK(packet.cells[0].itemVisual.item == 0);
        CHECK_FALSE(NextAnimationDelay(packet, tileset, 0));
    }
    SECTION("A known overlay does not expose unknown terrain") {
        cell.terrainVisibility = Visibility::Unknown;
        cell.tile.terrain = TerrainId{UINT32_MAX};
        const auto packet = PrepareMapPacket(view, content, tileset, {});
        CHECK(packet.cells[0].visible);
        CHECK(packet.cells[0].hasOverlay);
        CHECK(packet.cells[0].terrain.symbol == " ");
        CHECK(packet.cells[0].terrainSprite.kind == TilesetMappingKind::Missing);
        CHECK(packet.cells[0].overlaySprite.index == 4);
    }
    SECTION("Known terrain does not expose unknown overlays") {
        cell.overlayVisibility = Visibility::Unknown;
        cell.actor = 42;
        cell.ground_items = 1;
        const auto packet = PrepareMapPacket(view, content, tileset, {}, {{99, ItemTypeId{1}}});
        CHECK(packet.cells[0].visible);
        CHECK_FALSE(packet.cells[0].hasOverlay);
        CHECK(packet.cells[0].terrainSprite.index == 3);
        CHECK(packet.cells[0].overlaySprite.kind == TilesetMappingKind::Missing);
        CHECK(packet.cells[0].itemVisual.item == 0);
    }
}

TEST_CASE("map animation deadlines include only visible selected layers", "[sdl][map]") {
    TilesetDefinition tileset;
    AnimationClip clip; clip.frames.push_back({0,10}); clip.frames.push_back({1,30}); clip.totalMilliseconds = 30;
    tileset.animations.push_back(clip);
    MapPacket packet; packet.cells.resize(2); packet.cells[0].visible = true;
    packet.cells[0].terrainSprite = {TilesetMappingKind::Animation,0};
    packet.cells[1].terrainSprite = {TilesetMappingKind::Animation,0};
    CHECK(*NextAnimationDelay(packet,tileset,9) == 1);
    CHECK(*NextAnimationDelay(packet,tileset,10) == 20);
    packet.cells[0].terrainSprite = {}; packet.cells[0].overlaySprite = {TilesetMappingKind::Animation,0};
    CHECK_FALSE(NextAnimationDelay(packet,tileset,0));
    packet.cells[0].hasOverlay = true; CHECK(*NextAnimationDelay(packet,tileset,0) == 10);
}
TEST_CASE("map worker publishes owned packet and identity", "[sdl][map]") {
    const auto& content = TestContent(); TilesetDefinition tileset; WakeState state;
    tileset.items.resize(content.ItemCount() + 1);
    tileset.items[TestItem("toggle_lightbulb").value] = {TilesetMappingKind::Sprite, 7};
    MapPreparation worker(content,tileset,Wake,&state);
    WorldView view; view.width = 1; view.height = 1; view.cells.resize(1);
    view.cells[0].terrainVisibility = view.cells[0].overlayVisibility = Visibility::Visible;
    view.cells[0].tile.terrain = content.FindTerrain("floor");
    view.cells[0].ground_items = 1;
    Vector<MapItemVisual> items{{42, TestItem("toggle_lightbulb")}};
    worker.Submit(eastl::move(view),{7,8,9,10}, eastl::move(items));
    { std::unique_lock lock(state.mutex); REQUIRE(state.condition.wait_for(lock,std::chrono::seconds(5),[&] { return state.woken; })); }
    MapPacket packet; REQUIRE(worker.Take(packet)); CHECK(packet.identity == MapIdentity{7,8,9,10});
    CHECK(packet.cells.size() == 1); CHECK(packet.cells[0].visible); CHECK_FALSE(worker.Take(packet));
    CHECK(packet.cells[0].itemVisual.item == 42);
    CHECK(packet.cells[0].itemVisual.type == TestItem("toggle_lightbulb"));
    CHECK(packet.cells[0].overlaySprite.index == 7);
}
