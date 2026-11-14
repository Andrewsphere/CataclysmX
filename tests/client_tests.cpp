#include "client/text.h"
#include "client/message_history.h"
#include "client/perception_style.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <EASTL/utility.h>

#include <initializer_list>
#include <cmath>
#include <limits>

namespace {

using namespace cataclysmx;

TEST_CASE("Message history retains events in order and trims its configurable capacity", "[client][messages]") {
    MessageHistory history;
    history.Add("");
    CHECK(history.Entries().empty());
    for (int i = 0; i < 12; ++i) { history.Add(String(1, static_cast<char>('A' + i))); }
    REQUIRE(history.Entries().size() == 10);
    CHECK(history.Entries().front() == "C");
    CHECK(history.Entries().back() == "L");
    history.SetCapacity(2);
    REQUIRE(history.Entries().size() == 2);
    CHECK(history.Entries().front() == "K");
    history.Add("L");
    CHECK(history.Entries()[0] == "L");
    CHECK(history.Entries()[1] == "L");
    history.SetCapacity(1);
    history.Add("Moved east.");
    REQUIRE(history.Entries().size() == 1);
    CHECK(history.Entries().back() == "Moved east.");
    history.SetCapacity(100);
    history.Add("You wait.");
    CHECK(history.Entries().size() == 2);
    CHECK(history.Entries().front() == "Moved east.");
}

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

TEST_CASE("Shared perception colors retain exact terminal integer rules", "[client][perception]") {
    const RgbColor color{18, 52, 86};
    CHECK(PerceptionColor(color, Visibility::Visible, LightBand::Dark, 0, false) == RgbColor{28, 37, 45});
    CHECK(PerceptionColor(color, Visibility::Visible, LightBand::Dark, 0, true) == RgbColor{4, 13, 21});
    CHECK(PerceptionColor(color, Visibility::Visible, LightBand::Dim, 0, false) == RgbColor{9, 28, 47});
    CHECK(PerceptionColor(color, Visibility::Visible, LightBand::Ordinary, 0, false) == color);
    CHECK(PerceptionColor(color, Visibility::Visible, LightBand::Bright, 0, false) == color);
    CHECK(PerceptionColor(color, Visibility::Visible, LightBand::Ordinary, 1, false) == RgbColor{25, 63, 112});
    CHECK(PerceptionColor(color, Visibility::Visible, LightBand::Ordinary, 1, true) == RgbColor{21, 57, 98});
    for (const auto light : {LightBand::Dark, LightBand::Dim, LightBand::Ordinary, LightBand::Bright}) {
        CHECK(PerceptionColor(color, Visibility::Remembered, light, 0, false) == RgbColor{64, 49, 32});
        CHECK(PerceptionColor(color, Visibility::Remembered, light, 1, false) == RgbColor{63, 51, 41});
        CHECK(PerceptionColor(color, Visibility::Remembered, light, 1, true) == RgbColor{63, 50, 36});
        for (const auto visibility : {Visibility::Visible, Visibility::Remembered}) {
            for (const bool background : {false, true}) {
                CHECK(PerceptionColor(color, visibility, light, 6, background)
                    == PerceptionColor(color, visibility, light, std::numeric_limits<unsigned>::max(), background));
            }
        }
    }
}

TEST_CASE("Sprite perception transforms approximate exact colors within two byte levels", "[client][perception]") {
    CHECK(MakePerceptionTransform(Visibility::Visible, LightBand::Ordinary, 0) == PerceptionTransform{});
    CHECK(MakePerceptionTransform(Visibility::Visible, LightBand::Bright, 0) == PerceptionTransform{});
    // Asymmetric colors exercise all luminance coefficients, including cross-channel mixing.
    const RgbColor samples[] = {{0, 0, 0}, {255, 255, 255}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255},
        {18, 52, 86}, {120, 154, 188}, {1, 2, 3}, {254, 127, 63}, {37, 209, 151}};
    for (const auto visibility : {Visibility::Visible, Visibility::Remembered}) {
        for (const auto light : {LightBand::Dark, LightBand::Dim, LightBand::Ordinary, LightBand::Bright}) {
            for (const unsigned depth : {0U, 1U, 2U, 5U, 6U, 100U, std::numeric_limits<unsigned>::max()}) {
                for (const bool background : {false, true}) {
                    const auto transform = MakePerceptionTransform(visibility, light, depth, background);
                    for (const auto color : samples) {
                        const auto exact = PerceptionColor(color, visibility, light, depth, background);
                        const auto channel = [color](const eastl::array<float, 4>& row) {
                            return std::lround(row[0] * color.red + row[1] * color.green + row[2] * color.blue + row[3] * 255.0f);
                        };
                        CHECK(std::abs(channel(transform.red) - exact.red) <= 2);
                        CHECK(std::abs(channel(transform.green) - exact.green) <= 2);
                        CHECK(std::abs(channel(transform.blue) - exact.blue) <= 2);
                    }
                    CHECK(MakePerceptionTransform(visibility, light, 6, background)
                        == MakePerceptionTransform(visibility, light, std::numeric_limits<unsigned>::max(), background));
                    if (visibility == Visibility::Remembered) {
                        CHECK(transform == MakePerceptionTransform(visibility, LightBand::Ordinary, depth, background));
                    }
                }
            }
        }
    }
    const auto memory = MakePerceptionTransform(Visibility::Remembered, LightBand::Dark, 0);
    CHECK(memory.red[1] > memory.red[0]);
    CHECK(memory.red[2] > 0);
    CHECK(memory.red[3] > memory.green[3]);
    CHECK(MakePerceptionTransform(Visibility::Visible, LightBand::Dark, 0).red[3] > 0);
    CHECK(MakePerceptionTransform(Visibility::Visible, LightBand::Dark, 0, true).red[3] == 0);
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
    CHECK(bright == ordinary);
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
