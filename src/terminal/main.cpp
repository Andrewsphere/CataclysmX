#include "client/text.h"
#include "core/server.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <cstdio>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    cataclysmx::Server server;
    if (argc == 2 && std::string(argv[1]) == "--render") {
        std::fputs(cataclysmx::render_text(server.view()).c_str(), stdout);
        return 0;
    }
    if (argc != 1) {
        std::fputs("Usage: cataclysmx_game [--render]\n", stderr);
        return 2;
    }
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    bool interacting = false;
    std::string message = "Pick up the backpack with g. The machine (&) is east.";
    auto component = ftxui::Renderer([&] {
        ftxui::Elements rows;
        rows.push_back(ftxui::text("CataclysmX | arrows/hjkl move | . wait | g pickup | d drop | o + direction interact | q quit"));
        rows.push_back(ftxui::text(interacting ? "Interact in which direction? (Escape cancels)" : message));
        const cataclysmx::String rendered = cataclysmx::render_text(server.view());
        std::istringstream text(std::string(rendered.data(), rendered.size()));
        std::string line;
        while (std::getline(text, line)) { rows.push_back(ftxui::text(line)); }
        return ftxui::vbox(std::move(rows));
    });
    component = ftxui::CatchEvent(component, [&](ftxui::Event event) {
        if (event == ftxui::Event::Escape && interacting) { interacting = false; return true; }
        if (event == ftxui::Event::Escape || event == ftxui::Event::CtrlC || event == ftxui::Event::Character('q')) {
            screen.ExitLoopClosure()();
            return true;
        }
        cataclysmx::Command command;
        bool directional = true;
        if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::Character('h')) { command.dx = -1;
        } else if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Character('l')) { command.dx = 1;
        } else if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k')) { command.dz = -1;
        } else if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j')) { command.dz = 1;
        } else { directional = false; }
        if (directional) {
            command.kind = interacting ? cataclysmx::CommandKind::interact : cataclysmx::CommandKind::move;
        } else if (event == ftxui::Event::Character('o')) {
            interacting = true;
            return true;
        } else if (interacting) {
            return false;
        } else if (event == ftxui::Event::Character('.')) { command.kind = cataclysmx::CommandKind::wait;
        } else if (event == ftxui::Event::Character('g')) { command.kind = cataclysmx::CommandKind::pick_up;
        } else if (event == ftxui::Event::Character('d')) { command.kind = cataclysmx::CommandKind::drop;
        } else { return false; }
        interacting = false;
        const auto result = server.send(command);
        message = cataclysmx::outcome_message(result.outcome);
        return true;
    });
    screen.Loop(component);
    return 0;
}
