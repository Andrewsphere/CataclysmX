#include "client/text.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <EASTL/utility.h>

#include <initializer_list>

namespace {

using namespace cataclysmx;

ContentCatalog AppearanceContent() {
    const ContentSource source{"client.json5", R"({format_version:1,definitions:[
        {type:'terrain',id:'base',name:'Base',movement:{passable:true},blocks_sight:false,
         appearance:{symbol:'\u00e9',foreground:'#123456',background:'#789abc'}},
        {type:'terrain',id:'child',name:'Child',movement:{passable:true},blocks_sight:false,
         looks_like:'base',appearance:{foreground:'#abcdef'}},
        {type:'terrain',id:'wide',name:'Wide',movement:{passable:true},blocks_sight:false,
         looks_like:'base',appearance:{symbol:'\u754c'}},
        {type:'terrain',id:'combining',name:'Combining',movement:{passable:true},blocks_sight:false,
         looks_like:'base',appearance:{symbol:'\u0301'}},
        {type:'terrain',id:'supplementary',name:'Supplementary',movement:{passable:true},blocks_sight:false,
         looks_like:'base',appearance:{symbol:'😀'}},
        {type:'item',id:'rock',name:'rock',mass_g:500,volume_ml:250}
    ]})"};
    auto result = LoadContent({&source, 1});
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

TEST_CASE("Client resolves terrain colors and inherited appearances from its catalog", "[client]") {
    const auto content = AppearanceContent();
    ViewCell cell;
    cell.terrainVisibility = Visibility::Visible; cell.terrainLight = LightBand::Ordinary;
    cell.tile.terrain = content.FindTerrain("child");
    const auto appearance = CellAppearance(cell, content);
    CHECK(appearance.symbol == "\xc3\xa9");
    CHECK(appearance.foreground == RgbColor{171, 205, 239});
    CHECK(appearance.background == RgbColor{120, 154, 188});

    cell.tile.terrain = TestContent().FindTerrain("wall");
    CHECK(CellAppearance(cell, TestContent()) == TestContent().GetTerrain(cell.tile.terrain).appearance);
}

TEST_CASE("Client text preserves complete Unicode scalars and textual status", "[client]") {
    const auto content = AppearanceContent();
    WorldView view;
    view.width = 4;
    view.height = 1;
    view.tick = 23;
    view.player_position = {-4, 7, -2};
    for (const char* id : {"base", "wide", "combining", "supplementary"}) {
        ViewCell cell;
        cell.terrainVisibility = Visibility::Visible; cell.terrainLight = LightBand::Ordinary;
        cell.tile.terrain = content.FindTerrain(id);
        view.cells.push_back(cell);
    }
    CHECK(render_text(view, content) ==
        "tick=23 position=-4,7,-2\n\xc3\xa9\xe7\x95\x8c\xcc\x81\xf0\x9f\x98\x80\nInventory: empty\n");
    view.inventory.push_back({17, content.FindItem("rock"), 2});
    CHECK(render_text(view, content).find("Inventory: 17:rock(2 inside)\n") != String::npos);
}

TEST_CASE("Client overlay precedence preserves the terrain background", "[client]") {
    const auto content = AppearanceContent();
    ViewCell cell;
    cell.terrainVisibility = Visibility::Visible; cell.terrainLight = LightBand::Ordinary;
    cell.overlayVisibility = Visibility::Visible; cell.overlayLight = LightBand::Ordinary;
    cell.tile.terrain = content.FindTerrain("base");
    const auto background = content.GetTerrain(cell.tile.terrain).appearance.background;
    cell.object = 3;
    cell.objectAppearance.kind = ObjectKind::Generic;
    auto appearance = CellAppearance(cell, content);
    CHECK(appearance.symbol == "&");
    CHECK(appearance.foreground == RgbColor{0, 128, 128});
    CHECK(appearance.background == background);
    cell.objectAppearance.active = true;
    appearance = CellAppearance(cell, content);
    CHECK(appearance.symbol == "*");
    CHECK(appearance.foreground == RgbColor{0, 255, 255});
    CHECK(appearance.background == background);
    cell.ground_items = 1;
    appearance = CellAppearance(cell, content);
    CHECK(appearance.symbol == "!");
    CHECK(appearance.foreground == RgbColor{255, 255, 0});
    CHECK(appearance.background == background);
    cell.actor = 5;
    appearance = CellAppearance(cell, content);
    CHECK(appearance.symbol == "z");
    CHECK(appearance.foreground == RgbColor{0, 255, 0});
    CHECK(appearance.background == background);
    cell.player = true;
    appearance = CellAppearance(cell, content);
    CHECK(appearance.symbol == "@");
    CHECK(appearance.foreground == RgbColor{255, 255, 255});
    CHECK(appearance.background == background);
}

TEST_CASE("Client hidden cells reveal neither terrain nor overlays", "[client]") {
    const auto content = AppearanceContent();
    ViewCell cell;
    cell.tile.terrain = content.FindTerrain("base");
    cell.actor = 5;
    cell.player = true;
    cell.ground_items = 2;
    cell.object = 7;
    cell.objectAppearance = {ObjectKind::Generic, ObjectFacing::North, true, false};
    const TerminalAppearance hidden{" ", {}, {}};
    CHECK(CellAppearance(cell, content) == hidden);
    // Hidden cells must not even resolve a terrain ID, which may be absent.
    cell.tile.terrain = {};
    CHECK(CellAppearance(cell, content) == hidden);
    WorldView view;
    view.width = 1;
    view.height = 1;
    view.cells.push_back(cell);
    CHECK(render_text(view, content) == "tick=0 position=0,0,0\n \nInventory: empty\n");
}

TEST_CASE("Depth tint distinguishes terrain background and overlay depth", "[client]") {
    const auto content = AppearanceContent();
    ViewCell cell;
    cell.terrainVisibility = Visibility::Visible; cell.terrainLight = LightBand::Ordinary;
    cell.tile.terrain = content.FindTerrain("base");
    cell.terrainDepth = 1;
    auto terrain = CellAppearance(cell, content);
    CHECK(terrain.foreground == RgbColor{25, 63, 112});
    CHECK(terrain.background == RgbColor{111, 147, 188});
    cell.actor = 1;
    cell.overlayVisibility = Visibility::Visible; cell.overlayLight = LightBand::Ordinary;
    cell.overlayDepth = 0;
    auto overlay = CellAppearance(cell, content);
    CHECK(overlay.foreground == RgbColor{0, 255, 0});
    CHECK(overlay.background == terrain.background);
    cell.overlayDepth = 1;
    CHECK(CellAppearance(cell, content).foreground == RgbColor{12, 215, 48});
    cell.overlayDepth = 20;
    auto deepest = CellAppearance(cell, content);
    CHECK(deepest.foreground == RgbColor{33, 143, 134});
    cell.overlayDepth = 100;
    CHECK(CellAppearance(cell, content).foreground == deepest.foreground);
}

TEST_CASE("Item command parser accepts vertical direction symbols", "[client]") {
    Command command;
    REQUIRE(ParseItemCommand("use 1 2 activate <", command));
    CHECK(command.dy == -1);
    CHECK(command.dx == 0);
    CHECK(command.dz == 0);
    REQUIRE(ParseItemCommand("use 1 2 activate up", command));
    CHECK(command.dy == 1);
}

TEST_CASE("Memory furniture renders without handles and channels style independently", "[client][perception]") {
    const auto content = AppearanceContent();
    ViewCell cell;
    cell.terrainVisibility = Visibility::Visible; cell.terrainLight = LightBand::Ordinary;
    cell.tile.terrain = content.FindTerrain("base"); cell.terrainDepth = 2;
    const auto terrain = CellAppearance(cell, content);
    cell.overlayVisibility = Visibility::Remembered;
    cell.objectAppearance.kind = ObjectKind::Chair;
    cell.overlayDepth = 1;
    const auto memory = CellAppearance(cell, content);
    CHECK(memory.symbol == "h"); CHECK(cell.object == 0);
    CHECK(memory.background == terrain.background);
    cell.actor = 99; cell.ground_items = 5; cell.player = true;
    CHECK(CellAppearance(cell, content) == memory); // Never display actors/items from memory.
    cell.overlayLight = LightBand::Bright;
    CHECK(CellAppearance(cell, content) == memory); // Current light cannot recolor memory.
    cell.actor = 0; cell.ground_items = 0; cell.player = false;
    cell.terrainVisibility = Visibility::Remembered;
    cell.overlayVisibility = Visibility::Visible; cell.overlayLight = LightBand::Ordinary;
    const auto mixed = CellAppearance(cell, content);
    CHECK(mixed.foreground != memory.foreground);
    CHECK(mixed.background != memory.background);
    cell.terrainLight = LightBand::Bright;
    CHECK(CellAppearance(cell, content) == mixed);
    cell.objectAppearance.kind = ObjectKind::Table; CHECK(CellAppearance(cell, content).symbol == "T");
    cell.objectAppearance.kind = ObjectKind::Lamp; CHECK(CellAppearance(cell, content).symbol == "i");
    cell.objectAppearance.active = true; CHECK(CellAppearance(cell, content).symbol == "*");
    cell.objectAppearance.kind = ObjectKind::Generic; cell.objectAppearance.active = false;
    CHECK(CellAppearance(cell, content).symbol == "&");
}

TEST_CASE("Live light bands preserve ordinary colors and keep dark near vision readable", "[client][perception]") {
    const auto content = AppearanceContent();
    ViewCell cell; cell.terrainVisibility = Visibility::Visible; cell.tile.terrain = content.FindTerrain("base");
    cell.terrainLight = LightBand::Ordinary;
    const auto ordinary = CellAppearance(cell, content);
    CHECK(ordinary == content.GetTerrain(cell.tile.terrain).appearance);
    cell.terrainLight = LightBand::Bright; const auto bright = CellAppearance(cell, content);
    cell.terrainLight = LightBand::Dim; const auto dim = CellAppearance(cell, content);
    cell.terrainLight = LightBand::Dark; const auto dark = CellAppearance(cell, content);
    CHECK(bright.foreground.red > ordinary.foreground.red);
    CHECK(bright.background.blue > ordinary.background.blue);
    CHECK(dim.foreground.blue < ordinary.foreground.blue);
    CHECK(dark.foreground.red >= 24);
    CHECK(dark.foreground != dark.background);
    cell.terrainVisibility = Visibility::Remembered; cell.terrainDepth = 0;
    const auto memory = CellAppearance(cell, content);
    cell.terrainDepth = 1; const auto below = CellAppearance(cell, content);
    // Foreground depth-one blend is 25% / 4 = 6%, after sepia conversion.
    CHECK(below.foreground.blue == (unsigned(memory.foreground.blue) * 94 + 192 * 6) / 100);
}

}
