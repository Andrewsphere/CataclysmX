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
         looks_like:'base',appearance:{symbol:'😀'}}
    ]})"};
    auto result = LoadContent({&source, 1});
    REQUIRE(result.has_value());
    return eastl::move(*result);
}

TEST_CASE("Client resolves terrain colors and inherited appearances from its catalog", "[client]") {
    const auto content = AppearanceContent();
    ViewCell cell;
    cell.visible = true;
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
    view.player_position = {-4, 7};
    for (const char* id : {"base", "wide", "combining", "supplementary"}) {
        ViewCell cell;
        cell.visible = true;
        cell.tile.terrain = content.FindTerrain(id);
        view.cells.push_back(cell);
    }
    CHECK(render_text(view, content) ==
        "tick=23 position=-4,7\n\xc3\xa9\xe7\x95\x8c\xcc\x81\xf0\x9f\x98\x80\nInventory: empty\n");
    view.inventory.push_back({17, ItemKind::rock, 2});
    CHECK(render_text(view, content).find("Inventory: 17:rock(2 inside)\n") != String::npos);
}

TEST_CASE("Client overlay precedence preserves the terrain background", "[client]") {
    const auto content = AppearanceContent();
    ViewCell cell;
    cell.visible = true;
    cell.tile.terrain = content.FindTerrain("base");
    const auto background = content.GetTerrain(cell.tile.terrain).appearance.background;
    cell.object = 3;
    auto appearance = CellAppearance(cell, content);
    CHECK(appearance.symbol == "&");
    CHECK(appearance.foreground == RgbColor{0, 128, 128});
    CHECK(appearance.background == background);
    cell.object_enabled = true;
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
    cell.object_enabled = true;
    const TerminalAppearance hidden{" ", {}, {}};
    CHECK(CellAppearance(cell, content) == hidden);
    // Hidden cells must not even resolve a terrain ID, which may be absent.
    cell.tile.terrain = {};
    CHECK(CellAppearance(cell, content) == hidden);
    WorldView view;
    view.width = 1;
    view.height = 1;
    view.cells.push_back(cell);
    CHECK(render_text(view, content) == "tick=0 position=0,0\n \nInventory: empty\n");
}

}
