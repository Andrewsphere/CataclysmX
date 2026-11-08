#include "client/text.h"
#include "core/server.h"
#include "terminal/render.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <EASTL/unique_ptr.h>

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    std::string savePath;
    std::string loadPath;
    std::string contentPath = CATACLYSMX_TERRAIN_FILE;
    bool render = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--render") { render = true;
        } else if (argument == "--save" && i + 1 < argc) { savePath = argv[++i];
        } else if (argument == "--load" && i + 1 < argc) { loadPath = argv[++i];
        } else if (argument == "--content" && i + 1 < argc) { contentPath = argv[++i];
        } else {
            std::fputs("Usage: cataclysmx_game [--content <path>] [--render] [--save <path> | --load <path>]\n", stderr);
            return 2;
        }
    }
    if (!savePath.empty() && !loadPath.empty()) {
        std::fputs("Choose either --save or --load\n", stderr);
        return 2;
    }
    cataclysmx::Diagnostics diagnostics;
    // Declared before the server so the referenced catalog outlives it.
    const auto content = cataclysmx::LoadContentFile(contentPath.c_str(), &diagnostics);
    if (!content) {
        std::fprintf(stderr, "Content load failed (error=%d): %s\n", static_cast<int>(content.error()),
            cataclysmx::FormatDiagnostics(diagnostics).c_str());
        return 1;
    }
    const auto validated = cataclysmx::ValidateServerContent(*content, &diagnostics);
    if (!validated) {
        std::fprintf(stderr, "Content validation failed: %s\n",
            cataclysmx::FormatDiagnostics(diagnostics).c_str());
        return 1;
    }
    eastl::unique_ptr<cataclysmx::Server> ownedServer;
    diagnostics.Clear();
    if (!loadPath.empty()) {
        auto loaded = cataclysmx::Server::LoadSave(*content, loadPath.c_str(), &diagnostics);
        if (!loaded) {
            std::fprintf(stderr, "Load failed (error=%d): %s\n", static_cast<int>(loaded.error()),
                cataclysmx::FormatDiagnostics(diagnostics).c_str());
            return 1;
        }
        ownedServer = eastl::move(*loaded);
    } else {
        ownedServer = eastl::make_unique<cataclysmx::Server>(*content);
        if (!savePath.empty()) {
            ownedServer->SetAutosaveInterval(1000);
            diagnostics.Clear();
            const auto saved = ownedServer->CreateSave(savePath.c_str(), &diagnostics);
            if (!saved) {
                std::fprintf(stderr, "Save creation failed (error=%d): %s\n", static_cast<int>(saved.error()),
                    cataclysmx::FormatDiagnostics(diagnostics).c_str());
                return 1;
            }
        }
    }
    cataclysmx::Server& server = *ownedServer;
    if (render) {
        std::fputs(cataclysmx::render_text(server.View(), *content).c_str(), stdout);
        return 0;
    }
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    bool interacting = false;
    std::string message = "Pick up the backpack with g. The machine (&) is east.";
    auto component = ftxui::Renderer([&] {
        ftxui::Elements rows;
        rows.push_back(ftxui::text("CataclysmX | arrows/hjkl move | . wait | g pickup | d drop | o + direction interact | S save | q quit"));
        rows.push_back(ftxui::text(interacting ? "Interact in which direction? (Escape cancels)" : message));
        const cataclysmx::WorldView view = server.View();
        rows.push_back(ftxui::text("tick=" + std::to_string(view.tick) + " position="
            + std::to_string(view.player_position.x) + "," + std::to_string(view.player_position.z)));
        for (int row = 0; row < view.height; ++row) {
            ftxui::Elements cells;
            for (int column = 0; column < view.width; ++column) {
                cells.push_back(cataclysmx::RenderTerrainCell(cataclysmx::CellAppearance(
                    view.cells[static_cast<std::size_t>(row * view.width + column)], *content)));
            }
            rows.push_back(ftxui::hbox(std::move(cells)));
        }
        std::string inventory = "Inventory:";
        for (const cataclysmx::InventoryEntry& item : view.inventory) {
            inventory += " " + std::to_string(item.id) + ":" + cataclysmx::item_name(item.kind);
            if (item.contained_items != 0) {
                inventory += "(" + std::to_string(item.contained_items) + " inside)";
            }
        }
        if (view.inventory.empty()) { inventory += " empty"; }
        rows.push_back(ftxui::text(inventory));
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
                diagnostics.Clear();
                const auto saved = server.Save(&diagnostics);
                message = saved ? "Game saved." : "Save failed (error=" + std::to_string(static_cast<int>(saved.error()))
                    + "): " + cataclysmx::FormatDiagnostics(diagnostics).c_str();
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
            message += " (error=" + std::to_string(static_cast<int>(*result.storageError)) + ")";
            std::fprintf(stderr, "%s\n", message.c_str());
        }
        return true;
    });
    screen.Loop(component);
    return 0;
}
