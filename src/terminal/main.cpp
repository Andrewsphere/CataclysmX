#include "client/text.h"
#include "core/server.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <EASTL/unique_ptr.h>

#include <cstdio>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    std::string savePath;
    std::string loadPath;
    bool render = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--render") { render = true;
        } else if (argument == "--save" && i + 1 < argc) { savePath = argv[++i];
        } else if (argument == "--load" && i + 1 < argc) { loadPath = argv[++i];
        } else {
            std::fputs("Usage: cataclysmx_game [--render] [--save <path> | --load <path>]\n", stderr);
            return 2;
        }
    }
    if (!savePath.empty() && !loadPath.empty()) {
        std::fputs("Choose either --save or --load\n", stderr);
        return 2;
    }
    eastl::unique_ptr<cataclysmx::Server> ownedServer;
    cataclysmx::Diagnostics diagnostics;
    if (!loadPath.empty()) {
        auto loaded = cataclysmx::Server::LoadSave(loadPath.c_str(), &diagnostics);
        if (!loaded) { std::fprintf(stderr, "Load failed: %s\n", diagnostics.message.c_str()); return 1; }
        ownedServer = eastl::move(*loaded);
    } else {
        ownedServer = eastl::make_unique<cataclysmx::Server>();
        if (!savePath.empty()) {
            ownedServer->SetAutosaveInterval(1000);
            if (!ownedServer->CreateSave(savePath.c_str(), &diagnostics)) {
                std::fprintf(stderr, "Save creation failed: %s\n", diagnostics.message.c_str());
                return 1;
            }
        }
    }
    cataclysmx::Server& server = *ownedServer;
    if (render) {
        std::fputs(cataclysmx::render_text(server.View()).c_str(), stdout);
        return 0;
    }
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    bool interacting = false;
    std::string message = "Pick up the backpack with g. The machine (&) is east.";
    auto component = ftxui::Renderer([&] {
        ftxui::Elements rows;
        rows.push_back(ftxui::text("CataclysmX | arrows/hjkl move | . wait | g pickup | d drop | o + direction interact | S save | q quit"));
        rows.push_back(ftxui::text(interacting ? "Interact in which direction? (Escape cancels)" : message));
        const cataclysmx::String rendered = cataclysmx::render_text(server.View());
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
        } else if (event == ftxui::Event::Character('S')) {
            if (!server.GetWorld().HasSave()) {
                message = "Use --save <path> to enable saving for this game.";
            } else {
                const auto saved = server.Save(&diagnostics);
                message = saved ? "Game saved." : std::string("Save failed: ") + diagnostics.message.c_str();
            }
            return true;
        } else if (event == ftxui::Event::Character('.')) { command.kind = cataclysmx::CommandKind::wait;
        } else if (event == ftxui::Event::Character('g')) { command.kind = cataclysmx::CommandKind::pick_up;
        } else if (event == ftxui::Event::Character('d')) { command.kind = cataclysmx::CommandKind::drop;
        } else { return false; }
        interacting = false;
        const auto result = server.Send(command);
        message = cataclysmx::OutcomeMessage(result.outcome);
        if (result.storageError) {
            message = result.advanced ? "Autosave failed." : "World load failed.";
            std::fprintf(stderr, "%s\n", message.c_str());
        }
        return true;
    });
    screen.Loop(component);
    return 0;
}
