#include "terminal/render.h"
#include "client/text.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace {

TEST_CASE("Terminal preserves independently styled memory background and live overlay", "[terminal][render][perception]") {
    using namespace cataclysmx;
    ViewCell cell; cell.terrainVisibility = Visibility::Remembered;
    cell.tile.terrain = TestTerrain("floor"); cell.terrainDepth = 2;
    cell.overlayVisibility = Visibility::Visible; cell.overlayLight = LightBand::Dim;
    cell.actor = 7; cell.overlayDepth = 0;
    const auto appearance = CellAppearance(cell, TestContent());
    CHECK(appearance.symbol == "z");
    CHECK(appearance.foreground == RgbColor{0, 140, 0});
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(1));
    ftxui::Render(screen, RenderTerrainCell(appearance));
    const auto& pixel = screen.PixelAt(0, 0);
    CHECK(pixel.character == "z");
    CHECK(pixel.foreground_color == ftxui::Color::RGB(0, 140, 0));
    CHECK(pixel.background_color == ftxui::Color::RGB(appearance.background.red,
        appearance.background.green, appearance.background.blue));
}

TEST_CASE("Terminal terrain cells preserve colors and one-column alignment", "[terminal][render]") {
    struct GlyphCase {
        const char* name;
        const char* symbol;
        const char* expected;
    };
    const GlyphCase cases[] = {
        {"ASCII", "#", "#"},
        {"one-column U+2502", "\xe2\x94\x82", "\xe2\x94\x82"},
        {"wide CJK U+754C", "\xe7\x95\x8c", "?"},
        {"combining U+0301", "\xcc\x81", "?"},
        {"supplementary emoji U+1F600", "\xf0\x9f\x98\x80", "?"},
    };
    const cataclysmx::TerminalAppearance left{"L", {11, 22, 33}, {44, 55, 66}};
    const cataclysmx::TerminalAppearance right{"R", {77, 88, 99}, {111, 122, 133}};

    for (const auto& glyph : cases) {
        INFO(glyph.name);
        const cataclysmx::TerminalAppearance appearance{glyph.symbol, {18, 52, 86}, {120, 154, 188}};
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(3), ftxui::Dimension::Fixed(1));
        const auto row = ftxui::hbox({
            cataclysmx::RenderTerrainCell(left),
            cataclysmx::RenderTerrainCell(appearance),
            cataclysmx::RenderTerrainCell(right),
        });
        ftxui::Render(screen, row);

        // Both neighbors must remain in their exact columns, including after fallback.
        CHECK(screen.PixelAt(0, 0).character == "L");
        CHECK(screen.PixelAt(1, 0).character == glyph.expected);
        CHECK(screen.PixelAt(2, 0).character == "R");

        const cataclysmx::TerminalAppearance appearances[] = {left, appearance, right};
        for (int column = 0; column < 3; ++column) {
            INFO(column);
            const auto& pixel = screen.PixelAt(column, 0);
            const auto foreground = appearances[column].foreground;
            const auto background = appearances[column].background;
            CHECK(pixel.foreground_color == ftxui::Color::RGB(foreground.red, foreground.green, foreground.blue));
            CHECK(pixel.background_color == ftxui::Color::RGB(background.red, background.green, background.blue));
        }
    }
}

}
