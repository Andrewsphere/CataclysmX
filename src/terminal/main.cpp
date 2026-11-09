#include "client/text.h"
#include "client/lan_profile.h"
#include "core/server.h"
#include "network/network.h"
#include "client/localization.h"
#include "terminal/debug_menu.h"
#include "terminal/settings_menu.h"
#include "terminal/gameplay_screen.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <EASTL/unique_ptr.h>
#include <EASTL/utility.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

using namespace cataclysmx;
eastl::string_view View(const String& text) { return {text.data(), text.size()}; }
std::string Frontend(const String& text) { return {text.data(), text.size()}; }
int Failure(const char* operation, const Diagnostics& diagnostics) {
    std::fprintf(stderr, "%s: %s\n", operation, FormatDiagnostics(diagnostics).c_str()); return 1;
}

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
    String savePath;
    String loadPath;
    String contentPath = CATACLYSMX_TERRAIN_FILE;
    String localizationDirectory = CATACLYSMX_LOC_DIRECTORY;
    String userRoot = DefaultUserRoot();
    String worldDirectory;
    bool render = false;
    bool openLan = false, browse = false;
    std::uint16_t port = 24680;
    std::string joinAddress, playerName = "Guest", profilePath = "cataclysmx-lan-profile.txt";
    for (int i = 1; i < argc; ++i) {
        const eastl::string_view argument(argv[i]);
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
        } else if (argument == "--user-dir" && i + 1 < argc) { userRoot = argv[++i];
        } else if (argument == "--world-dir" && i + 1 < argc) { worldDirectory = argv[++i];
        } else if (argument == "--loc-dir" && i + 1 < argc) { localizationDirectory = argv[++i];
        } else {
            std::fputs("Usage: cataclysmx_game [--content <path>] [--user-dir <path>] [--world-dir <path>] [--loc-dir <path>] [--render] [--save <path> | --load <path>] [--host] [--port n] [--join address[:port]] [--discover] [--name name] [--profile path]\n", stderr);
            return 2;
        }
    }
    if (!savePath.empty() && !loadPath.empty()) { std::fputs("Choose either --save or --load\n", stderr); return 2; }
    if (worldDirectory.empty() && (!savePath.empty() || !loadPath.empty())) {
        worldDirectory = WorldDirectoryForSave((savePath.empty() ? loadPath : savePath).c_str());
    }
    if ((openLan && !joinAddress.empty()) || playerName.empty() || playerName.size() > 32) { return 2; }
    Diagnostics diagnostics;
    SettingsStore settings(userRoot, worldDirectory);
    if (!settings.Load(&diagnostics)) { return Failure("Configuration load failed", diagnostics); }
    Localization localization;
    if (!localization.Load(localizationDirectory.c_str(), View(settings.Effective().language), &diagnostics)) {
        return Failure("Localization load failed", diagnostics);
    }
    // Content and configuration outlive the server and all terminal components.
    const auto content = LoadContentFile(contentPath.c_str(), &diagnostics);
    if (!content || !ValidateServerContent(*content, &diagnostics)) { return Failure("Content load/validation failed", diagnostics); }
    eastl::unique_ptr<Server> ownedServer;
    if (!loadPath.empty()) {
        auto loaded = Server::LoadSave(*content, loadPath.c_str(), &diagnostics);
        if (!loaded) { return Failure("Load failed", diagnostics); }
        ownedServer = eastl::move(*loaded);
    } else { ownedServer = eastl::make_unique<Server>(*content, settings.Effective().seed); }
    Server& server = *ownedServer;
    if (!ApplySettings(server, settings.Effective(), &diagnostics)) { return Failure("Settings application failed", diagnostics); }
    if (!savePath.empty()) {
        if (!server.CreateSave(savePath.c_str(), &diagnostics)) { return Failure("Save creation failed", diagnostics); }
        if (!settings.Save(SettingsScope::World, &diagnostics)) { return Failure("World configuration creation failed", diagnostics); }
    }
    if (render) {
        // Preserve the script-facing English snapshot contract.
        std::fputs(render_text(server.View(settings.Effective().viewRadius), *content).c_str(), stdout); return 0;
    }
    auto runtime = cataclysmx::network::NetworkRuntime::Create();
    if (!runtime) { std::fprintf(stderr, "LAN runtime failed: %d\n", static_cast<int>(runtime.error())); return 1; }
    cataclysmx::network::LanHost host(**runtime, server, *content);
    cataclysmx::network::LanClient guest(**runtime, *content);
    cataclysmx::network::LanDiscovery discovery(**runtime, *content);
    cataclysmx::client::LanProfile profile;
    bool profileUsable = true;
    std::string message = Frontend(GameplayWelcome(localization, settings.Keys()));
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
        cataclysmx::network::ConnectionSettings connectionSettings;
        connectionSettings.address = address.c_str();
        connectionSettings.port = remotePort;
        connectionSettings.playerName = playerName.c_str();
        connectionSettings.reconnectToken = newCharacter || !saved ? 0 : saved->token;
        guest.Disconnect();
        auto result = guest.Connect(connectionSettings);
        if (!result) { message = std::string("Connect failed: ") + cataclysmx::network::NetworkErrorMessage(result.error()); return; }
        persistedToken = 0;
        connectedAddress = address;
        connectedPort = remotePort;
        message = "Connecting to " + endpoint + "...";
    };
    if (openLan) {
        cataclysmx::network::HostSettings hostSettings;
        hostSettings.port = port;
        hostSettings.sessionName = playerName.c_str();
        if (auto result = host.Open(hostSettings); !result) {
            std::fprintf(stderr, "LAN host failed: %s\n", cataclysmx::network::NetworkErrorMessage(result.error())); return 1;
        }
    }
    if (!joinAddress.empty()) { Connect(joinAddress, false); }
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    TerminalKeyboard keyboard;
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
    bool settingsOpen = false;
    ftxui::Component debugMenu;
    ftxui::Component settingsMenu;
    GameplayUi gameplayUi;
    auto CurrentView = [&] {
        return guest.GetState() == cataclysmx::network::ConnectionState::Active && guest.HasView()
            ? guest.View() : server.View(settings.Effective().viewRadius);
    };
    auto lastUpdate = std::chrono::steady_clock::now();
    const auto exit = [&] { keyboard.Stop(); screen.ExitLoopClosure()(); };
    auto SubmitCommand = [&](Command command) {
        if (guest.GetState() == cataclysmx::network::ConnectionState::Connecting
            || guest.GetState() == cataclysmx::network::ConnectionState::Joining) {
            message = "Waiting for admission."; return false;
        }
        bool accepted = false;
        const std::string actionDescription = [&] {
            switch (command.kind) {
            case cataclysmx::CommandKind::move: return std::string("move ") + (command.dy < 0 ? "down" : command.dy > 0 ? "up" : command.dx < 0 ? "west" : command.dx > 0 ? "east" : command.dz < 0 ? "north" : "south");
            case cataclysmx::CommandKind::interact: return std::string("interact ") + (command.dy < 0 ? "down" : command.dy > 0 ? "up" : command.dx < 0 ? "west" : command.dx > 0 ? "east" : command.dz < 0 ? "north" : "south");
            case cataclysmx::CommandKind::wait: return std::string("wait");
            case cataclysmx::CommandKind::pick_up: return std::string("pick up");
            case cataclysmx::CommandKind::drop: return std::string("drop");
            case cataclysmx::CommandKind::use_item: return Frontend(localization.Text("action.use"));
            case cataclysmx::CommandKind::store_item: return Frontend(localization.Text("item.ui.store"));
            }
            return std::string("action");
        }();
        if (guest.GetState() == cataclysmx::network::ConnectionState::Active) {
            const auto submitted = guest.Submit(command);
            if (submitted) { accepted = true; pendingSequence = *submitted; pending = actionDescription + " #" + std::to_string(*submitted); message = "Sent; awaiting server receipt."; }
            else { message = std::string("Input rejected: ") + cataclysmx::network::NetworkErrorMessage(submitted.error()); }
        } else if (host.IsOpen()) {
            const auto submitted = host.QueueLocal(command);
            if (submitted == 0) { message = "Local input buffer is full."; }
            else { accepted = true; pendingSequence = submitted; pending = actionDescription + " #" + std::to_string(submitted); message = "Action pending."; }
        } else {
            const auto result = server.Send(command);
            accepted = result.advanced || result.outcome == Outcome::queued;
            message = Frontend(localization.ResultText(result));
            if (result.storageError) {
                message = Frontend(localization.Text(result.advanced ? "game.autosave_failed" : "game.load_failed"))
                    + " (error=" + std::to_string(static_cast<int>(*result.storageError)) + ")";
                std::fprintf(stderr, "%s\n", message.c_str());
            }
        }
        if (accepted) { interacting = false; }
        return accepted;
    };
    TerminalTimerWakeup timerWakeup;
    auto component = ftxui::Renderer([&] {
        if (settingsOpen) { return settingsMenu->Render(); }
        if (debugOpen) { return debugMenu->Render(); }
        const auto state = guest.GetState();
        GameplayPresentation presentation;
        const char* sessionKey = state == cataclysmx::network::ConnectionState::Active ? "game.ui.guest"
            : state == cataclysmx::network::ConnectionState::Connecting || state == cataclysmx::network::ConnectionState::Joining
                ? "game.ui.joining" : host.IsOpen() ? "game.ui.host" : "game.ui.local";
        presentation.session = localization.Text(sessionKey);
        if (host.IsOpen()) {
            presentation.session += ' '; presentation.session += UnsignedText(host.GetPort());
        }
        presentation.message = message.c_str(); presentation.pending = pending.c_str();
        presentation.interacting = interacting; presentation.enhancedKeyboard = keyboard.Enhanced();
        if (lanOpen) {
            ftxui::Elements rows;
            rows.push_back(ftxui::paragraph(Frontend(localization.Text("game.ui.lan_controls"))));
            rows.push_back(ftxui::paragraph("name=" + playerName + " address=" + directAddress
                + (editing ? " | editing: " + editText + "_ (Enter accepts, Escape cancels)" : "")));
            std::size_t index = 0;
            for (const auto& session : discovery.Sessions()) {
                if (++index > 9) { break; }
                rows.push_back(ftxui::paragraph(std::to_string(index) + ". " + session.sessionName.c_str() + " "
                    + session.address.c_str() + ":" + std::to_string(session.gamePort) + " "
                    + std::to_string(session.playerCount) + "/" + std::to_string(session.maxPlayers)
                    + (session.compatible ? "" : " (incompatible)")));
            }
            presentation.lan = ftxui::vbox(std::move(rows));
        }
        return gameplayUi.Render(CurrentView(), *content, localization, settings.Keys(), presentation);
    });
    component = ftxui::CatchEvent(component, [&](ftxui::Event event) {
        if (event == ftxui::Event::Custom) { timerWakeup.Consumed(); }
        const auto now = std::chrono::steady_clock::now();
        keyboard.Query();
        const eastl::string_view raw(event.input().data(), event.input().size());
        if (keyboard.HandleResponse(raw)) { return true; }
        // Terminal replies are not keyboard input (a cursor-position reply can
        // share bytes with a legacy modified function key).
        if (event.is_cursor_position() || event.is_cursor_shape() || event.IsTerminalNameVersion()
            || event.IsTerminalEmulator() || event.IsTerminalCapabilities()) { return true; }
        const auto chord = DecodeTerminalInput(raw);
        if (chord && chord->ctrl && chord->key == "c") { exit(); return true; }
        auto action = chord ? ResolveAction(settings.Keys(), interacting ? InputContext::Interaction : InputContext::Gameplay, *chord)
            : eastl::optional<InputAction>{};
        // Local modals discard elapsed time; an open LAN host continues serving guests.
        const bool pauseLocal = settingsOpen || debugOpen || gameplayUi.IsOpen()
            || (!lanOpen && (gameplayUi.PausesLocal(action) || action == InputAction::Settings
            || action == InputAction::Debug || action == InputAction::Save || action == InputAction::Cancel
            || action == InputAction::Quit || action == InputAction::ToggleRate || action == InputAction::ToggleIdle
            || action == InputAction::RateDown || action == InputAction::RateUp));
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
            message = "Receipt #" + std::to_string(result.sequence) + ": " + Frontend(localization.ResultText(result.result));
            if (result.sequence == pendingSequence && result.result.outcome != cataclysmx::Outcome::queued) { pending.clear(); }
        }
        if (guest.GetState() != cataclysmx::network::ConnectionState::Active
            && guest.GetState() != cataclysmx::network::ConnectionState::Connecting
            && guest.GetState() != cataclysmx::network::ConnectionState::Joining) {
            if (!pauseLocal || host.IsOpen()) {
                const auto update = host.Pump(delta);
                for (const auto& result : update.commandResults) {
                    if (result.player == server.GetHostPlayerId()) {
                        message = Frontend(localization.ResultText(result.result));
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
        // A changed endpoint must not reuse selected persistent IDs from another world.
        if (stateBefore != guest.GetState() && gameplayUi.IsOpen()) {
            gameplayUi.Close(); lastUpdate = std::chrono::steady_clock::now(); return true;
        }
        if (gameplayUi.IsOpen()) {
            eastl::optional<InputAction> activated;
            gameplayUi.HandleEvent(event, CurrentView(), localization, settings.Keys(), SubmitCommand,
                [&](InputAction selected) { activated = selected; }, &*content);
            lastUpdate = std::chrono::steady_clock::now();
            if (!activated) { return true; }
            // Dispatch the selected action once through the same handlers as a
            // bound gameplay key, after the modal has consumed its Enter event.
            action = activated;
        }
        if (settingsOpen || debugOpen) {
            if (event == ftxui::Event::Custom) { return true; }
            return settingsOpen ? settingsMenu->OnEvent(event) : debugMenu->OnEvent(event);
        }
        if (!lanOpen && action && gameplayUi.Open(*action)) {
            lastUpdate = now; return true;
        }
        if (!lanOpen && action == InputAction::Settings) {
            interacting = false; lastUpdate = now;
            settingsMenu = CreateSettingsMenu(settings, localization, server, keyboard, localizationDirectory.c_str(), [&] {
                settingsOpen = false; lastUpdate = std::chrono::steady_clock::now();
                message = Frontend(localization.Text(settings.Dirty(SettingsScope::Global) || settings.Dirty(SettingsScope::World)
                    ? "settings.unsaved" : "settings.title"));
            });
            settingsOpen = true; return true;
        }
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
                else { cataclysmx::network::HostSettings hostSettings; hostSettings.port = port; hostSettings.sessionName = playerName.c_str();
                    const auto result = host.Open(hostSettings);
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
        if (action == InputAction::Debug) {
            if (guest.GetState() == cataclysmx::network::ConnectionState::Active
                || guest.GetState() == cataclysmx::network::ConnectionState::Joining
                || guest.GetState() == cataclysmx::network::ConnectionState::Connecting) {
                message = "Worldgen debug view is available only in the local game."; return true;
            }
            interacting = false; lastUpdate = now;
            const auto position = server.GetWorld().GetActor(server.GetPlayer())->position;
            debugMenu = CreateWorldgenDebugMenu(server.GetWorld(), position,
                [&] { debugOpen = false; lastUpdate = std::chrono::steady_clock::now(); }, &localization, &settings.Keys());
            debugOpen = true; return true;
        }
        if (action == InputAction::Quit || (action == InputAction::Cancel && !interacting)) { exit(); return true; }
        if (action == InputAction::Cancel) { interacting = false; lastUpdate = now; return true; }
        if (action == InputAction::Save) {
            if (guest.GetState() == cataclysmx::network::ConnectionState::Active) {
                message = "Only the host can save."; return true;
            }
            diagnostics.Clear();
            if (!server.GetWorld().HasSave()) { message = Frontend(localization.Text("game.no_save")); }
            else if (const auto saved = server.Save(&diagnostics); !saved) {
                message = Frontend(localization.Text("game.save_failed")); message += ' '; message += Frontend(FormatDiagnostics(diagnostics));
            } else { message = Frontend(localization.Text("game.saved")); }
            lastUpdate = std::chrono::steady_clock::now(); return true;
        }
        if (action == InputAction::ToggleRate || action == InputAction::ToggleIdle || action == InputAction::RateDown || action == InputAction::RateUp) {
            if (guest.GetState() == cataclysmx::network::ConnectionState::Active) {
                message = "Only the host controls pacing."; return true;
            }
            auto pacing = server.GetPacing();
            if (action == InputAction::ToggleRate) { pacing.rateLimited = !pacing.rateLimited; }
            else if (action == InputAction::ToggleIdle) { pacing.pauseWhenIdle = !pacing.pauseWhenIdle; }
            else if (action == InputAction::RateDown) { pacing.ticksPerSecond = std::max(1U, pacing.ticksPerSecond / 2); }
            else { pacing.ticksPerSecond = std::min(1000000U, pacing.ticksPerSecond > 500000U ? 1000000U : pacing.ticksPerSecond * 2); }
            auto overrides = settings.Overrides(SettingsScope::World);
            if (action == InputAction::ToggleRate) { overrides.rateLimited = pacing.rateLimited; }
            else if (action == InputAction::ToggleIdle) { overrides.pauseWhenIdle = pacing.pauseWhenIdle; }
            else { overrides.ticksPerSecond = pacing.ticksPerSecond; }
            if (!settings.SetOverrides(SettingsScope::World, eastl::move(overrides), settings.KeyOverrides(SettingsScope::World))) { std::abort(); }
            if (!server.SetPacing(pacing)) { std::abort(); }
            lastUpdate = now; message = Frontend(localization.Text("settings.unsaved")); return true;
        }
        if (event == ftxui::Event::Custom || !action) { return true; }
        Command command;
        bool directional = true;
        switch (*action) {
        case InputAction::West: command.dx = -1; break;
        case InputAction::East: command.dx = 1; break;
        case InputAction::North: command.dz = -1; break;
        case InputAction::South: command.dz = 1; break;
        case InputAction::MoveDown: command.dy = -1; break;
        case InputAction::MoveUp: command.dy = 1; break;
        default: directional = false; break;
        }
        if (directional) { command.kind = interacting ? CommandKind::interact : CommandKind::move; }
        else if (*action == InputAction::Interact) { interacting = true; return true; }
        else if (*action == InputAction::Wait) { command.kind = CommandKind::wait; }
        else if (*action == InputAction::PickUp) { command.kind = CommandKind::pick_up; }
        else if (*action == InputAction::Drop) { command.kind = CommandKind::drop; }
        else { return true; }
        interacting = false;
        SubmitCommand(command);
        return true;
    });
    std::jthread timer([&screen, &timerWakeup](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (!stop.stop_requested() && timerWakeup.TryPost()) { screen.PostEvent(ftxui::Event::Custom); }
        }
    });
    screen.Loop(component);
    keyboard.Stop();
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
