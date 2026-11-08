#include "client/text.h"
#include "client/lan_profile.h"
#include "core/server.h"
#include "network/network.h"
#include "terminal/render.h"
#include "terminal/debug_menu.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <EASTL/unique_ptr.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <chrono>
#include <string>
#include <thread>

namespace {
bool Endpoint(const std::string& input, std::string& address, std::uint16_t& port, std::uint16_t defaultPort) {
    const auto colon = input.rfind(':');
    address = input.substr(0, colon);
    port = defaultPort;
    if (address.empty() || address.size() > 255 || input.find_first_of(" \t\r\n") != std::string::npos) { return false; }
    if (colon != std::string::npos) {
        const auto value = input.substr(colon + 1);
        if (value.empty()) { return false; }
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || port == 0) { return false; }
    }
    return true;
}
}

int main(int argc, char** argv) {
    std::string savePath;
    std::string loadPath;
    std::string contentPath = CATACLYSMX_TERRAIN_FILE;
    bool render = false;
    bool openLan = false, browse = false;
    std::uint16_t port = 24680;
    std::string joinAddress, playerName = "Guest", profilePath = "cataclysmx-lan-profile.txt";
    for (int i = 1; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--render") { render = true;
        } else if (argument == "--save" && i + 1 < argc) { savePath = argv[++i];
        } else if (argument == "--load" && i + 1 < argc) { loadPath = argv[++i];
        } else if (argument == "--content" && i + 1 < argc) { contentPath = argv[++i];
        } else if (argument == "--host") { openLan = true;
        } else if (argument == "--discover") { browse = true;
        } else if (argument == "--join" && i + 1 < argc) { joinAddress = argv[++i];
        } else if (argument == "--name" && i + 1 < argc) { playerName = argv[++i];
        } else if (argument == "--profile" && i + 1 < argc) { profilePath = argv[++i];
        } else if (argument == "--port" && i + 1 < argc) {
            const std::string value(argv[++i]);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || port == 0) { return 2; }
        } else {
            std::fputs("Usage: cataclysmx_game [--content path] [--save path | --load path] [--render] [--host] [--port n] [--join address[:port]] [--discover] [--name name] [--profile path]\n", stderr);
            return 2;
        }
    }
    if (!savePath.empty() && !loadPath.empty()) {
        std::fputs("Choose either --save or --load\n", stderr);
        return 2;
    }
    if ((openLan && !joinAddress.empty()) || playerName.empty() || playerName.size() > 32) { return 2; }
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
    auto runtime = cataclysmx::network::NetworkRuntime::Create();
    if (!runtime) { std::fprintf(stderr, "LAN runtime failed: %d\n", static_cast<int>(runtime.error())); return 1; }
    cataclysmx::network::LanHost host(**runtime, server, *content);
    cataclysmx::network::LanClient guest(**runtime, *content);
    cataclysmx::network::LanDiscovery discovery(**runtime, *content);
    cataclysmx::client::LanProfile profile;
    bool profileUsable = true;
    std::string message = "Pick up the backpack with g. The machine (&) is east.";
    if (auto loaded = profile.Load(profilePath.c_str()); !loaded) {
        profileUsable = false;
        message = "LAN profile cannot be loaded; use --profile with a valid file to join. Local play and hosting are available.";
        std::fprintf(stderr, "LAN profile error=%d: %s\n", static_cast<int>(loaded.error()), profilePath.c_str());
    }
    std::string connectedAddress;
    std::uint16_t connectedPort = 0;
    std::uint64_t persistedToken = 0;
    auto Connect = [&](const std::string& endpoint, bool newCharacter) {
        if (!profileUsable) { message = "Cannot join with an unreadable LAN profile: " + profilePath; return; }
        if (host.IsOpen()) { message = "Close LAN hosting before joining another game."; return; }
        std::string address;
        std::uint16_t remotePort;
        if (!Endpoint(endpoint, address, remotePort, port)) { message = "Invalid address or port."; return; }
        const auto* saved = profile.Find(address.c_str(), remotePort);
        cataclysmx::network::ConnectionSettings settings;
        settings.address = address.c_str();
        settings.port = remotePort;
        settings.playerName = playerName.c_str();
        settings.reconnectToken = newCharacter || !saved ? 0 : saved->token;
        guest.Disconnect();
        auto result = guest.Connect(settings);
        if (!result) { message = std::string("Connect failed: ") + cataclysmx::network::NetworkErrorMessage(result.error()); return; }
        persistedToken = 0;
        connectedAddress = address;
        connectedPort = remotePort;
        message = "Connecting to " + endpoint + "...";
    };
    if (openLan) {
        cataclysmx::network::HostSettings settings;
        settings.port = port;
        settings.sessionName = playerName.c_str();
        if (auto result = host.Open(settings); !result) {
            std::fprintf(stderr, "LAN host failed: %s\n", cataclysmx::network::NetworkErrorMessage(result.error())); return 1;
        }
    }
    if (!joinAddress.empty()) { Connect(joinAddress, false); }
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    bool interacting = false;
    bool debugOpen = false;
    bool lanOpen = browse;
    auto StartDiscovery = [&] {
        const auto opened = discovery.Open();
        if (!opened) { message = std::string("LAN discovery unavailable: ") + cataclysmx::network::NetworkErrorMessage(opened.error()); return; }
        if (auto refreshed = discovery.Refresh(); !refreshed) {
            message = std::string("LAN refresh failed: ") + cataclysmx::network::NetworkErrorMessage(refreshed.error());
        }
    };
    if (lanOpen) { StartDiscovery(); }
    char editing = 0;
    std::string editText, directAddress = joinAddress;
    std::string pending;
    std::uint64_t pendingSequence = 0;
    std::uint64_t discoveryElapsed = 0;
    auto lastUpdate = std::chrono::steady_clock::now();
    ftxui::Component debugMenu;
    auto component = ftxui::Renderer([&] {
        if (debugOpen) { return debugMenu->Render(); }
        ftxui::Elements rows;
        rows.push_back(ftxui::text("CataclysmX | arrows/hjkl move | . wait | g pickup | d drop | o + direction interact | F9 LAN | [/] rate | r limit | p idle pause | S save | F11 debug | q quit"));
        const auto state = guest.GetState();
        rows.push_back(ftxui::text(std::string("Session: ") + (state == cataclysmx::network::ConnectionState::Active ? "guest"
            : state == cataclysmx::network::ConnectionState::Connecting || state == cataclysmx::network::ConnectionState::Joining ? "joining"
            : host.IsOpen() ? "hosting on port " + std::to_string(host.GetPort()) : "local")
            + (pending.empty() ? "" : " | pending: " + pending)));
        rows.push_back(ftxui::text(interacting ? "Interact in which direction? (Escape cancels)" : message));
        if (lanOpen) {
            rows.push_back(ftxui::text("LAN: F9/Esc close | O open | C close | R refresh | j edit address (Enter accepts) | Enter join | n name | 1-9 join | D leave"));
            rows.push_back(ftxui::text("X new character at entered address (replaces saved identity for that endpoint)"));
            rows.push_back(ftxui::text("name=" + playerName + " address=" + directAddress + (editing ? " | editing: " + editText + "_ (Enter accepts)" : "")));
            std::size_t index = 0;
            for (const auto& session : discovery.Sessions()) {
                if (++index > 9) { break; }
                rows.push_back(ftxui::text(std::to_string(index) + ". " + session.sessionName.c_str() + " "
                    + session.address.c_str() + ":" + std::to_string(session.gamePort) + " "
                    + std::to_string(session.playerCount) + "/" + std::to_string(session.maxPlayers)
                    + (session.compatible ? "" : " (incompatible)")));
            }
        }
        const cataclysmx::WorldView view = guest.GetState() == cataclysmx::network::ConnectionState::Active && guest.HasView()
            ? guest.View() : server.View();
        rows.push_back(ftxui::text("tick=" + std::to_string(view.tick) + " position="
            + std::to_string(view.player_position.x) + "," + std::to_string(view.player_position.z)));
        rows.push_back(ftxui::text("rate=" + std::to_string(view.pacing.ticksPerSecond) + " ticks/s limit="
            + (view.pacing.rateLimited ? "on" : "off") + " idle-pause="
            + (view.pacing.pauseWhenIdle ? "on" : "off") + " speed=" + std::to_string(view.speed)
            + " delay=" + std::to_string(view.remainingTicks)
            + " queued=" + (view.commandQueued ? "yes" : "no")));
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
        for (const auto& player : view.players) {
            rows.push_back(ftxui::text(std::string(player.name.c_str()) + " #" + std::to_string(player.id)
                + (player.state == cataclysmx::PlayerConnectionState::Parked ? " parked" :
                   player.state == cataclysmx::PlayerConnectionState::Joining ? " joining" : " active")
                + (player.graceMicroseconds ? " grace=" + std::to_string(player.graceMicroseconds / 1000000)
                    + "." + std::to_string((player.graceMicroseconds / 100000) % 10) + "s" : "")
                + (player.commandQueued ? " queued" : player.state == cataclysmx::PlayerConnectionState::Active
                    && player.ready ? " waiting for input" : "")));
        }
        return ftxui::vbox(std::move(rows));
    });
    component = ftxui::CatchEvent(component, [&](ftxui::Event event) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - lastUpdate).count();
        lastUpdate = now;
        const auto delta = static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed));
        if (lanOpen) {
            discovery.Pump(delta);
            discoveryElapsed += delta;
            if (discoveryElapsed >= 2000000) {
                if (auto refreshed = discovery.Refresh(); !refreshed) {
                    message = std::string("LAN refresh failed: ") + cataclysmx::network::NetworkErrorMessage(refreshed.error());
                }
                discoveryElapsed = 0;
            }
        }
        const auto stateBefore = guest.GetState();
        guest.Pump(delta);
        bool profileFailed = false;
        const auto token = guest.GetReconnectToken();
        if (token && (guest.GetState() == cataclysmx::network::ConnectionState::Joining
            || guest.GetState() == cataclysmx::network::ConnectionState::Active) && token != persistedToken) {
            persistedToken = token;
            const auto* saved = profile.Find(connectedAddress.c_str(), connectedPort);
            if (!saved || saved->token != token) {
                auto updated = profile;
                if (auto set = updated.Set({connectedAddress.c_str(), connectedPort, token, playerName.c_str()}); !set) {
                    message = "Reconnect profile full or invalid (error=" + std::to_string(static_cast<int>(set.error())) + ")";
                    std::fprintf(stderr, "%s\n", message.c_str());
                    profileFailed = true;
                    guest.Disconnect();
                } else if (auto savedFile = updated.Save(profilePath.c_str()); !savedFile) {
                    message = "Reconnect profile write failed (error=" + std::to_string(static_cast<int>(savedFile.error())) + "): " + profilePath;
                    std::fprintf(stderr, "%s\n", message.c_str());
                    profileFailed = true;
                    guest.Disconnect();
                } else { profile = eastl::move(updated); message = "Reconnect identity saved; joining..."; }
            }
        }
        if (guest.GetState() == cataclysmx::network::ConnectionState::Active) {
            if (stateBefore != cataclysmx::network::ConnectionState::Active) {
                message = "Joined LAN game as player #" + std::to_string(guest.View().playerId);
            }
        }
        if ((guest.GetState() == cataclysmx::network::ConnectionState::Rejected
            || guest.GetState() == cataclysmx::network::ConnectionState::Disconnected)
            && stateBefore != guest.GetState() && stateBefore != cataclysmx::network::ConnectionState::Disconnected
            && !profileFailed) {
            message = std::string("LAN connection ended: ") + cataclysmx::network::NetworkErrorMessage(guest.GetError())
                + "; local game resumed. Unknown token? F9 then X to start a new character.";
            pending.clear();
        }
        for (const auto& result : guest.TakeResults()) {
            message = "Receipt #" + std::to_string(result.sequence) + ": " + cataclysmx::OutcomeMessage(result.result.outcome);
            if (result.sequence == pendingSequence && result.result.outcome != cataclysmx::Outcome::queued) { pending.clear(); }
        }
        if (guest.GetState() != cataclysmx::network::ConnectionState::Active
            && guest.GetState() != cataclysmx::network::ConnectionState::Connecting
            && guest.GetState() != cataclysmx::network::ConnectionState::Joining) {
            if (!debugOpen || host.IsOpen()) {
                const auto update = host.Pump(delta);
                for (const auto& result : update.commandResults) {
                    if (result.player == server.GetHostPlayerId()) {
                        message = cataclysmx::OutcomeMessage(result.result.outcome);
                        if (result.sequence == pendingSequence && result.result.outcome != cataclysmx::Outcome::queued) { pending.clear(); }
                    }
                }
                if (update.storageError) {
                    message = "Autosave/world load failed (error=" + std::to_string(static_cast<int>(*update.storageError)) + ")";
                    std::fprintf(stderr, "%s\n", message.c_str());
                }
                if (update.networkError) {
                    message = std::string("LAN transport: ") + cataclysmx::network::NetworkErrorMessage(*update.networkError);
                    std::fprintf(stderr, "%s\n", message.c_str());
                }
            }
        }
        if (event == ftxui::Event::Custom) { return true; }
        if (event == ftxui::Event::CtrlC) {
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == ftxui::Event::F11) {
            if (guest.GetState() == cataclysmx::network::ConnectionState::Active
                || guest.GetState() == cataclysmx::network::ConnectionState::Joining
                || guest.GetState() == cataclysmx::network::ConnectionState::Connecting) {
                message = "Worldgen debug view is available only in the local game."; return true;
            }
            if (debugOpen) {
                debugOpen = false;
            } else {
                interacting = false;
                const auto position = server.GetWorld().GetActor(server.GetPlayer())->position;
                debugMenu = cataclysmx::CreateWorldgenDebugMenu(server.GetWorld(), position,
                    [&] { debugOpen = false; });
                debugOpen = true;
            }
            return true;
        }
        if (debugOpen) { return debugMenu->OnEvent(event); }
        if (event == ftxui::Event::F9) {
            lanOpen = !lanOpen; editing = 0; interacting = false;
            if (lanOpen) { StartDiscovery(); discoveryElapsed = 0; }
            else { discovery.Close(); }
            return true;
        }
        if (lanOpen) {
            if (editing) {
                if (event == ftxui::Event::Escape) { editing = 0; return true; }
                if (event == ftxui::Event::Backspace && !editText.empty()) { editText.pop_back(); return true; }
                if (event == ftxui::Event::Return) {
                    if (editing == 'n') { if (!editText.empty() && editText.size() <= 32) { playerName = editText; } }
                    else { directAddress = editText; }
                    editing = 0;
                    return true;
                }
                if (event.is_character() && editText.size() < 255) { editText += event.character(); return true; }
                return true;
            }
            if (event == ftxui::Event::Escape) { lanOpen = false; discovery.Close(); return true; }
            if (event == ftxui::Event::Character('j') || event == ftxui::Event::Character('n')) {
                editing = event == ftxui::Event::Character('j') ? 'j' : 'n';
                editText = editing == 'j' ? directAddress : playerName;
                return true;
            }
            if (event == ftxui::Event::Character('R')) {
                if (auto refreshed = discovery.Refresh(); !refreshed) {
                    message = std::string("LAN refresh failed: ") + cataclysmx::network::NetworkErrorMessage(refreshed.error());
                }
                return true;
            }
            if (event == ftxui::Event::Character('O')) {
                if (guest.GetState() == cataclysmx::network::ConnectionState::Active
                    || guest.GetState() == cataclysmx::network::ConnectionState::Connecting
                    || guest.GetState() == cataclysmx::network::ConnectionState::Joining) { message = "Leave guest session first."; }
                else { cataclysmx::network::HostSettings settings; settings.port = port; settings.sessionName = playerName.c_str();
                    const auto result = host.Open(settings);
                    message = result ? "LAN open on port " + std::to_string(host.GetPort()) :
                        std::string("Open failed: ") + cataclysmx::network::NetworkErrorMessage(result.error()); }
                return true;
            }
            if (event == ftxui::Event::Character('C')) { host.Close(); message = "LAN closed; guests parked."; return true; }
            if (event == ftxui::Event::Character('D')) { guest.Disconnect(); pending.clear(); message = "Left LAN session; local game resumed."; return true; }
            if (event == ftxui::Event::Character('X')) { Connect(directAddress, true); return true; }
            if (event == ftxui::Event::Return) { Connect(directAddress, false); return true; }
            for (int i = 1; i <= 9; ++i) {
                if (event == ftxui::Event::Character(static_cast<char>('0' + i))
                    && discovery.Sessions().size() >= static_cast<std::size_t>(i)) {
                    const auto& found = discovery.Sessions()[i - 1];
                    if (!found.compatible) { message = "Incompatible content."; }
                    else { directAddress = std::string(found.address.c_str()) + ":" + std::to_string(found.gamePort); Connect(directAddress, false); }
                    return true;
                }
            }
            return true;
        }
        if (event == ftxui::Event::Escape && interacting) { interacting = false; return true; }
        if (event == ftxui::Event::Escape || event == ftxui::Event::Character('q')) {
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
            if (guest.GetState() == cataclysmx::network::ConnectionState::Active) { message = "Only the host can save."; return true; }
            if (!server.GetWorld().HasSave()) {
                message = "Use --save <path> to enable saving for this game.";
            } else {
                diagnostics.Clear();
                const auto saved = server.Save(&diagnostics);
                message = saved ? "Game saved." : "Save failed (error=" + std::to_string(static_cast<int>(saved.error()))
                    + "): " + cataclysmx::FormatDiagnostics(diagnostics).c_str();
            }
            return true;
        } else if (event == ftxui::Event::Character('r') || event == ftxui::Event::Character('p')
            || event == ftxui::Event::Character('[') || event == ftxui::Event::Character(']')) {
            if (guest.GetState() == cataclysmx::network::ConnectionState::Active) { message = "Only the host controls pacing."; return true; }
            auto pacing = server.GetPacing();
            if (event == ftxui::Event::Character('r')) { pacing.rateLimited = !pacing.rateLimited; }
            else if (event == ftxui::Event::Character('p')) { pacing.pauseWhenIdle = !pacing.pauseWhenIdle; }
            else if (event == ftxui::Event::Character('[')) { pacing.ticksPerSecond = std::max(1U, pacing.ticksPerSecond / 2); }
            else { pacing.ticksPerSecond = std::min(1000000U, pacing.ticksPerSecond > 500000U
                ? 1000000U : pacing.ticksPerSecond * 2); }
            server.SetPacing(pacing);
            return true;
        } else if (event == ftxui::Event::Character('.')) { command.kind = cataclysmx::CommandKind::wait;
        } else if (event == ftxui::Event::Character('g')) { command.kind = cataclysmx::CommandKind::pick_up;
        } else if (event == ftxui::Event::Character('d')) { command.kind = cataclysmx::CommandKind::drop;
        } else { return false; }
        interacting = false;
        if (guest.GetState() == cataclysmx::network::ConnectionState::Connecting
            || guest.GetState() == cataclysmx::network::ConnectionState::Joining) { message = "Waiting for admission."; return true; }
        const std::string action = [&] {
            switch (command.kind) {
            case cataclysmx::CommandKind::move: return std::string("move ") + (command.dx < 0 ? "west" : command.dx > 0 ? "east" : command.dz < 0 ? "north" : "south");
            case cataclysmx::CommandKind::interact: return std::string("interact ") + (command.dx < 0 ? "west" : command.dx > 0 ? "east" : command.dz < 0 ? "north" : "south");
            case cataclysmx::CommandKind::wait: return std::string("wait");
            case cataclysmx::CommandKind::pick_up: return std::string("pick up");
            case cataclysmx::CommandKind::drop: return std::string("drop");
            }
            return std::string("action");
        }();
        if (guest.GetState() == cataclysmx::network::ConnectionState::Active) {
            const auto submitted = guest.Submit(command);
            if (submitted) { pendingSequence = *submitted; pending = action + " #" + std::to_string(*submitted); message = "Sent; awaiting server receipt."; }
            else { message = std::string("Input rejected: ") + cataclysmx::network::NetworkErrorMessage(submitted.error()); }
        } else {
            const auto submitted = host.QueueLocal(command);
            if (submitted == 0) { message = "Local input buffer is full."; }
            else { pendingSequence = submitted; pending = action + " #" + std::to_string(submitted); message = "Action pending."; }
        }
        return true;
    });
    std::jthread timer([&screen](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (!stop.stop_requested()) { screen.PostEvent(ftxui::Event::Custom); }
        }
    });
    screen.Loop(component);
    timer.request_stop();
    timer.join();
    guest.Disconnect();
    host.Close();
    if (server.GetWorld().HasSave()) {
        diagnostics.Clear();
        if (auto saved = server.Save(&diagnostics); !saved) {
            std::fprintf(stderr, "Final save failed (error=%d): %s\n", static_cast<int>(saved.error()),
                cataclysmx::FormatDiagnostics(diagnostics).c_str());
            return 1;
        }
    }
    return 0;
}
