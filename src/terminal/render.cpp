#include "terminal/render.h"

#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

#include <string_view>

namespace cataclysmx {

ftxui::Element RenderTerrainCell(const TerminalAppearance& appearance) {
    const std::string_view symbol(appearance.symbol.data(), appearance.symbol.size());
    // Content validates a single scalar; FTXUI decides whether it occupies one column.
    const std::string glyph = ftxui::string_width(symbol) == 1 ? std::string(symbol) : "?";
    const auto foreground = appearance.foreground;
    const auto background = appearance.background;
    return ftxui::text(glyph)
        | ftxui::color(ftxui::Color::RGB(foreground.red, foreground.green, foreground.blue))
        | ftxui::bgcolor(ftxui::Color::RGB(background.red, background.green, background.blue));
}

std::string render_probe() {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(40));
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 120; ++x) {
            screen.PixelAt(x, y).character = ((x + y) % 7 == 0) ? "#" : ".";
        }
    }
    return screen.ToString();
}

}
