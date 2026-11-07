#include "terminal/render.h"

#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

namespace cataclysmx {

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
