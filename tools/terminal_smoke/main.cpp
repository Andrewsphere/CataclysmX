#include "terminal/render.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--render") {
        std::puts(cataclysmx::render_probe().c_str());
        return 0;
    }
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    int x = 0;
    int y = 0;
    auto component = ftxui::Renderer([&] {
        ftxui::Elements rows;
        for (int row = 0; row < 40; ++row) {
            std::string line;
            for (int column = 0; column < 120; ++column) {
                line += (row == ((y % 40 + 40) % 40) && column == ((x % 120 + 120) % 120))
                    ? '@' : ((row + column + x + y) % 7 == 0 ? '#' : '.');
            }
            rows.push_back(ftxui::text(line));
        }
        return ftxui::vbox({
            ftxui::text("CataclysmX: Привет | combining: é | wide: 界 | arrows move; Escape quits"),
            ftxui::text("x=" + std::to_string(x) + " y=" + std::to_string(y)),
            ftxui::vbox(std::move(rows)) | ftxui::color(ftxui::Color::RGB(90, 210, 130))
        });
    });
    component = ftxui::CatchEvent(component, [&](ftxui::Event event) {
        if (event == ftxui::Event::Escape || event == ftxui::Event::CtrlC) {
            screen.ExitLoopClosure()();
        } else if (event == ftxui::Event::ArrowLeft) { --x;
        } else if (event == ftxui::Event::ArrowRight) { ++x;
        } else if (event == ftxui::Event::ArrowUp) { --y;
        } else if (event == ftxui::Event::ArrowDown) { ++y;
        } else { return false; }
        return true;
    });
    screen.Loop(component);
}
