#include "headless/lan.h"
#include "client/lan_profile.h"
#include "client/text.h"
#include "network/network.h"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <thread>

namespace cataclysmx::headless {
namespace {
bool Endpoint(const std::string& endpoint, std::string& address, std::uint16_t& port, std::uint16_t defaultPort) {
    const auto colon = endpoint.rfind(':');
    address = endpoint.substr(0, colon);
    port = defaultPort;
    if (address.empty() || address.size() > 255 || endpoint.find_first_of(" \t\r\n") != std::string::npos) { return false; }
    if (colon != std::string::npos) {
        const auto digits = endpoint.substr(colon + 1);
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), port);
        if (digits.empty() || parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || !port) { return false; }
    }
    return true;
}
bool ParseCommand(const std::string& text, Command& command) {
    if (ParseItemCommand({text.data(), text.size()}, command)) { return true; }
    if (text == "wait") { command.kind = CommandKind::wait; return true; }
    if (text == "pickup") { command.kind = CommandKind::pick_up; return true; }
    if (text == "drop") { command.kind = CommandKind::drop; return true; }
    const auto space = text.find(' ');
    if (space == std::string::npos) { return false; }
    if (text.substr(0, space) == "move") { command.kind = CommandKind::move; }
    else if (text.substr(0, space) == "interact") { command.kind = CommandKind::interact; }
    else { return false; }
    const auto direction = text.substr(space + 1);
    if (direction == "down" || direction == "<") { command.dy = -1; return true; }
    if (direction == "up" || direction == ">") { command.dy = 1; return true; }
    if (direction.size() != 1) { return false; }
    switch (direction[0]) {
    case 'n': command.dz = -1; break;
    case 's': command.dz = 1; break;
    case 'e': command.dx = 1; break;
    case 'w': command.dx = -1; break;
    default: return false;
    }
    return true;
}
}

int RunLan(Server& server, const ContentCatalog& content, const LanOptions& options) {
    if (options.host + options.discover + !options.join.empty() != 1 || options.runForMs > 86400000
        || (options.newCharacter && options.join.empty()) || (!options.discoveryAddress.empty() && !options.discover)
        || options.name.empty() || options.name.size() > 32) { return 2; }
    std::string address;
    std::uint16_t remotePort = 0;
    if (!options.join.empty() && !Endpoint(options.join, address, remotePort, options.port)) { return 2; }
    Vector<Command> commands;
    for (const auto& text : options.commands) {
        Command command;
        if (!ParseCommand(text, command)) { std::fprintf(stderr, "Invalid LAN command: %s\n", text.c_str()); return 2; }
        commands.push_back(command);
    }
    auto runtime = network::NetworkRuntime::Create();
    if (!runtime) { std::fprintf(stderr, "LAN runtime: %s\n", network::NetworkErrorMessage(runtime.error())); return 1; }
    network::LanHost host(**runtime, server, content);
    network::LanClient client(**runtime, content);
    network::LanDiscovery discovery(**runtime, content);
    cataclysmx::client::LanProfile profile;
    if (!options.join.empty()) {
        if (auto loaded = profile.Load(options.profile.c_str()); !loaded) {
            std::fprintf(stderr, "LAN profile read error=%d: %s\n", static_cast<int>(loaded.error()), options.profile.c_str()); return 1;
        }
    }
    if (options.host) {
        network::HostSettings settings;
        settings.port = options.port;
        settings.discoveryPort = options.discoveryPort;
        settings.sessionName = options.name.c_str();
        if (auto opened = host.Open(settings); !opened) { std::fprintf(stderr, "Open: %s\n", network::NetworkErrorMessage(opened.error())); return 1; }
        std::printf("hosting port=%u discovery-port=%u\n", host.GetPort(), host.GetDiscoveryPort());
        std::fflush(stdout);
    }
    if (!options.join.empty()) {
        network::ConnectionSettings settings;
        settings.address = address.c_str(); settings.port = remotePort; settings.playerName = options.name.c_str();
        if (!options.newCharacter) {
            if (const auto* saved = profile.Find(address.c_str(), remotePort)) { settings.reconnectToken = saved->token; }
        }
        if (auto connected = client.Connect(settings); !connected) { std::fprintf(stderr, "Connect: %s\n", network::NetworkErrorMessage(connected.error())); return 1; }
    }
    if (options.discover) {
        if (auto opened = discovery.Open(); !opened) { std::fprintf(stderr, "Discovery: %s\n", network::NetworkErrorMessage(opened.error())); return 1; }
        if (auto refreshed = discovery.Refresh(options.discoveryPort,
            options.discoveryAddress.empty() ? nullptr : options.discoveryAddress.c_str()); !refreshed) {
            std::fprintf(stderr, "Discovery refresh: %s\n", network::NetworkErrorMessage(refreshed.error())); return 1;
        }
    }
    auto last = std::chrono::steady_clock::now();
    const auto end = last + std::chrono::milliseconds(options.runForMs);
    std::size_t sent = 0;
    std::size_t resolved = 0;
    bool accepted = false;
    std::uint64_t savedToken = 0;
    std::uint64_t discoveryElapsed = 0;
    bool hostFailure = false;
    while (std::chrono::steady_clock::now() < end) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last).count();
        last = now;
        if (options.host) {
            // Queue one new command per poll; every input shares the authoritative host pipeline.
            if (sent == resolved && sent < commands.size()) { std::printf("submitted sequence=%llu\n", static_cast<unsigned long long>(host.QueueLocal(commands[sent++]))); }
            auto update = host.Pump(static_cast<std::uint64_t>(elapsed));
            for (const auto& receipt : update.commandResults) {
                if (receipt.player == server.GetHostPlayerId() && receipt.result.outcome != Outcome::queued) { ++resolved; }
                std::printf("receipt player=%llu sequence=%llu outcome=%s tick=%llu\n",
                    static_cast<unsigned long long>(receipt.player), static_cast<unsigned long long>(receipt.sequence),
                    CommandResultMessage(receipt.result), static_cast<unsigned long long>(receipt.result.tick));
            }
            if (update.storageError) { std::fprintf(stderr, "Storage error=%d\n", static_cast<int>(*update.storageError)); hostFailure = true; break; }
            if (update.networkError) {
                std::fprintf(stderr, "Host transport: %s\n", network::NetworkErrorMessage(*update.networkError)); hostFailure = true; break;
            }
        }
        if (!options.join.empty()) {
            client.Pump(static_cast<std::uint64_t>(elapsed));
            const auto token = client.GetReconnectToken();
            if (token && token != savedToken && (client.GetState() == network::ConnectionState::Joining
                || client.GetState() == network::ConnectionState::Active)) {
                savedToken = token;
                const auto* previous = profile.Find(address.c_str(), remotePort);
                if (!previous || previous->token != token) {
                    auto updated = profile;
                    if (auto set = updated.Set({address.c_str(), remotePort, token, options.name.c_str()}); !set) {
                        std::fprintf(stderr, "Profile identity error=%d\n", static_cast<int>(set.error())); return 1;
                    }
                    if (auto saved = updated.Save(options.profile.c_str()); !saved) {
                        std::fprintf(stderr, "Profile write error=%d: %s\n", static_cast<int>(saved.error()), options.profile.c_str()); return 1;
                    }
                    profile = eastl::move(updated);
                }
                std::printf("identity token=%llu endpoint=%s:%u\n", static_cast<unsigned long long>(token), address.c_str(), remotePort);
                std::fflush(stdout);
            }
            if (client.GetState() == network::ConnectionState::Rejected || client.GetState() == network::ConnectionState::Disconnected) {
                std::fprintf(stderr, "Connection ended: %s; saved identity retained\n", network::NetworkErrorMessage(client.GetError())); return 1;
            }
            if (client.GetState() == network::ConnectionState::Active && !accepted) {
                accepted = true;
                std::printf("joined player=%llu token=%llu position=%d,%d,%d tick=%llu\n",
                    static_cast<unsigned long long>(client.View().playerId), static_cast<unsigned long long>(savedToken),
                    client.View().player_position.x, client.View().player_position.z, client.View().player_position.y,
                    static_cast<unsigned long long>(client.View().tick));
            }
            if (accepted && sent == resolved && sent < commands.size()) {
                auto submitted = client.Submit(commands[sent]);
                if (!submitted) { std::fprintf(stderr, "Submit: %s\n", network::NetworkErrorMessage(submitted.error())); return 1; }
                ++sent;
                std::printf("submitted sequence=%llu\n", static_cast<unsigned long long>(*submitted));
            }
            for (const auto& receipt : client.TakeResults()) {
                if (receipt.result.outcome != Outcome::queued) { ++resolved; }
                std::printf("receipt player=%llu sequence=%llu outcome=%s tick=%llu\n",
                    static_cast<unsigned long long>(receipt.player), static_cast<unsigned long long>(receipt.sequence),
                    CommandResultMessage(receipt.result), static_cast<unsigned long long>(receipt.result.tick));
            }
        }
        if (options.discover) {
            discovery.Pump(static_cast<std::uint64_t>(elapsed));
            discoveryElapsed += static_cast<std::uint64_t>(elapsed);
            if (discoveryElapsed >= 2000000) {
                if (auto refreshed = discovery.Refresh(options.discoveryPort,
                    options.discoveryAddress.empty() ? nullptr : options.discoveryAddress.c_str()); !refreshed) {
                    std::fprintf(stderr, "Discovery refresh: %s\n", network::NetworkErrorMessage(refreshed.error())); return 1;
                }
                discoveryElapsed = 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (options.discover) {
        for (const auto& session : discovery.Sessions()) {
            std::printf("session %s:%u name=%s players=%u/%u compatible=%d\n", session.address.c_str(),
                session.gamePort, session.sessionName.c_str(), session.playerCount, session.maxPlayers, session.compatible);
        }
    }
    if (accepted && client.HasView()) {
        const auto& view = client.View();
        std::fputs(ItemDetails(view, content).c_str(), stdout);
        std::printf("final player=%llu token=%llu position=%d,%d,%d tick=%llu resolved=%zu/%zu\n",
            static_cast<unsigned long long>(view.playerId), static_cast<unsigned long long>(savedToken),
            view.player_position.x, view.player_position.z, view.player_position.y, static_cast<unsigned long long>(view.tick), resolved, commands.size());
    }
    if (!options.join.empty()) { client.Disconnect(); }
    if (options.host) {
        auto update = host.Pump(0);
        if (update.storageError) { std::fprintf(stderr, "Storage error=%d\n", static_cast<int>(*update.storageError)); hostFailure = true; }
        if (update.networkError) { std::fprintf(stderr, "Host transport: %s\n", network::NetworkErrorMessage(*update.networkError)); hostFailure = true; }
        for (const auto& receipt : update.commandResults) {
            if (receipt.player == server.GetHostPlayerId() && receipt.result.outcome != Outcome::queued) { ++resolved; }
            std::printf("receipt player=%llu sequence=%llu outcome=%s tick=%llu\n",
                static_cast<unsigned long long>(receipt.player), static_cast<unsigned long long>(receipt.sequence),
                CommandResultMessage(receipt.result), static_cast<unsigned long long>(receipt.result.tick));
        }
        const auto view = server.View();
        std::fputs(ItemDetails(view, content).c_str(), stdout);
        std::printf("final player=%llu position=%d,%d,%d tick=%llu resolved=%zu/%zu\n",
            static_cast<unsigned long long>(view.playerId), view.player_position.x, view.player_position.z, view.player_position.y,
            static_cast<unsigned long long>(view.tick), resolved, commands.size());
    }
    host.Close();
    if (options.host && server.GetWorld().HasSave()) {
        Diagnostics diagnostics;
        if (auto saved = server.Save(&diagnostics); !saved) {
            std::fprintf(stderr, "Final save failed (error=%d): %s\n", static_cast<int>(saved.error()), FormatDiagnostics(diagnostics).c_str()); return 1;
        }
    }
    if (!options.join.empty() && !accepted) { std::fputs("Connection timed out before activation\n", stderr); return 1; }
    if (hostFailure) { return 1; }
    if (resolved != commands.size()) { std::fprintf(stderr, "LAN run ended with %zu unresolved commands\n", commands.size() - resolved); return 1; }
    return 0;
}
}
