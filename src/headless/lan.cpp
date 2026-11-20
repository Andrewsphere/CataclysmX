#include "headless/lan.h"
#include "client/lan_profile.h"
#include "client/text.h"
#include "client/combat_menu.h"
#include "client/power_view.h"
#include "client/digital_commands.h"
#include "client/crafting_menu.h"
#include "network/network.h"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <thread>
#include <EASTL/array.h>

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
template<class T> bool Number(eastl::string_view text, T& value) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
bool ParseCombatCommand(const std::string& text, const ContentCatalog& content, Command& command) {
    eastl::array<eastl::string_view, 8> words{};
    eastl::string_view remaining(text.data(), text.size());
    std::size_t count = 0;
    while (!remaining.empty()) {
        const auto start = remaining.find_first_not_of(" \t\r");
        if (start == eastl::string_view::npos) { break; }
        remaining.remove_prefix(start);
        const auto end = remaining.find_first_of(" \t\r");
        if (count == words.size()) { return false; }
        words[count++] = remaining.substr(0, end);
        if (end == eastl::string_view::npos) { break; }
        remaining.remove_prefix(end);
    }
    if (!count) { return false; }
    const auto name = words[0];
    if (ParseDigitalCommand({words.data(), count}, command)) { return true; }
    if (name == "load-forge" && count == 3) {
        command.kind = CommandKind::load_forge;
        return Number(words[1], command.target) && command.target && Number(words[2], command.item) && command.item;
    }
    if ((name == "ignite-forge" || name == "extinguish-forge" || name == "discard-forge-fuel") && count == 2) {
        command.kind = name == "ignite-forge" ? CommandKind::ignite_forge
            : name == "extinguish-forge" ? CommandKind::extinguish_forge : CommandKind::discard_forge_fuel;
        return Number(words[1], command.target) && command.target;
    }
    if (name == "pack-station" && count == 2) {
        command.kind = CommandKind::pack_station;
        return Number(words[1], command.target) && command.target;
    }
    if (name == "place-station" && count == 5) {
        command.kind = CommandKind::place_station;
        return Number(words[1], command.item) && command.item && Number(words[2], command.position.x)
            && Number(words[3], command.position.z) && Number(words[4], command.position.y);
    }
    if (name == "new-character" && count == 1) { command.kind = CommandKind::new_character; return true; }
    if (name == "pause-art" && count == 1) { command.kind = CommandKind::pause_art_study; return true; }
    if (name == "style" && count == 2 && words[1] != "balanced" && words[1] != "aggressive" && words[1] != "defensive") {
        command.kind = CommandKind::select_combat_style;
        if (!Number(words[1], command.combatStyle.value)) { command.combatStyle = content.FindCombatStyle(words[1]); }
        return content.TryGetCombatStyle(command.combatStyle) != nullptr;
    }
    if (name == "study-art" && count == 3) {
        command.kind = CommandKind::study_art;
        if (!Number(words[1], command.book.value)) { command.book = content.FindBook(words[1]); }
        if (!Number(words[2], command.art.value)) { command.art = content.FindCombatArt(words[2]); }
        return content.TryGetBook(command.book) && content.TryGetCombatArt(command.art);
    }
    if ((name == "hold" && (count == 3 || count == 4)) || (name == "releasehand" && count == 2)
        || (name == "regrip" && count == 3) || ((name == "wear" || name == "remove") && count == 2)) {
        command.kind = CommandKind::equipment;
        auto& request = command.equipment;
        request.operation = name == "hold" ? EquipmentOperation::Hold : name == "releasehand" ? EquipmentOperation::Release
            : name == "regrip" ? EquipmentOperation::Regrip : name == "wear" ? EquipmentOperation::Wear : EquipmentOperation::Remove;
        if (name == "hold" || name == "wear" || name == "remove") {
            if (!Number(words[1], request.item) || !request.item) { return false; }
        }
        if (name == "hold" || name == "releasehand" || name == "regrip") {
            const auto hand = words[name == "hold" ? 2 : 1];
            if (hand == "left") { request.hand = Hand::Left; }
            else if (hand == "right") { request.hand = Hand::Right; }
            else { return false; }
        }
        if ((name == "hold" && count == 4) || name == "regrip") {
            const auto grip = words[name == "hold" ? 3 : 2];
            if (grip == "two") { request.grip = GripKind::TwoHanded; }
            else if (grip != "one") { return false; }
        }
        return true;
    }
    if (name != "use" || count < 2) { return false; }
    const std::size_t idIndex = words[1] == "ability" ? 2 : 1;
    if (count <= idIndex) { return false; }
    command.kind = CommandKind::use_ability;
    auto& request = command.ability;
    if (!Number(words[idIndex], request.ability.value)) { request.ability = content.FindAbility(words[idIndex]); }
    if (!content.TryGetAbility(request.ability)) { return false; }
    const std::size_t targetIndex = idIndex + 1;
    if (count == targetIndex || (count == targetIndex + 1 && words[targetIndex] == "self")) {
        request.target.kind = AbilityTargetKind::Self;
    } else if (count == targetIndex + 3 && words[targetIndex] == "tile") {
        request.target.kind = AbilityTargetKind::Tile;
        if (!Number(words[targetIndex + 1], request.target.tile.x) || !Number(words[targetIndex + 2], request.target.tile.z)) { return false; }
    } else if (count == targetIndex + 1) {
        request.target.kind = AbilityTargetKind::CardinalDirection;
        const auto direction = words[targetIndex];
        if (direction == "north") { request.target.direction = CardinalDirection::North; }
        else if (direction == "east") { request.target.direction = CardinalDirection::East; }
        else if (direction == "south") { request.target.direction = CardinalDirection::South; }
        else if (direction == "west") { request.target.direction = CardinalDirection::West; }
        else { return false; }
    } else { return false; }
    return true;
}
bool BindAbility(Command& command, const WorldView& view, const ContentCatalog& content) {
    if (command.kind != CommandKind::use_ability) { return true; }
    const AbilityView* selected = nullptr;
    for (const auto& ability : view.combatInfo.abilities) {
        if (ability.ability != command.ability.ability) { continue; }
        if (!selected || static_cast<unsigned>(ability.source.kind) < static_cast<unsigned>(selected->source.kind)
            || (ability.source.kind == AbilityGrantSourceKind::Art && selected->source.kind == AbilityGrantSourceKind::Art
                && content.GetCombatArt(ability.source.art).id < content.GetCombatArt(selected->source.art).id)) { selected = &ability; }
    }
    if (!selected) { std::fputs("Ability has no grant in the current character snapshot\n", stderr); return false; }
    command.ability.source = selected->source;
    if (command.ability.target.kind == AbilityTargetKind::Tile) { command.ability.target.tile.y = view.player_position.y; }
    return true;
}
void PrintCombat(const WorldView& view, const ContentCatalog& content, eastl::string_view inspection) {
    if (inspection == "machines" || inspection == "information" || inspection == "crafting") {
        Localization localization;
        Vector<CraftingMenuRow> rows;
        CollectCraftingMenu(view, content, localization, rows);
        for (const auto& row : rows) std::puts(row.label.c_str());
        Vector<String> lines; CollectLearningSummary(view, content, lines);
        for (const auto& line : lines) std::puts(line.c_str());
        return;
    }
    if (inspection == "power") {
        PowerWorkshop workshop; workshop.Open(view);
        Vector<String> lines; workshop.Describe(view, content, lines);
        for (const auto& line : lines) std::puts(line.c_str());
        return;
    }
    if (inspection == "stations") {
        for (const auto& station : view.stations) {
            const auto& definition = content.GetObject(station.type);
            std::printf("station=%llu name=%s position=%d,%d,%d enabled=%d broken=%d worker=%llu packing=%s state=%s\n",
                static_cast<unsigned long long>(station.id), definition.name.c_str(),
                station.position.x, station.position.z, station.position.y, station.enabled, station.broken,
                static_cast<unsigned long long>(station.worker), StationActionMessage(station.packing),
                StationAvailabilityText(station, definition));
            if (definition.craftingEnergyPerWork) std::puts(StationPowerText(view, station, definition).c_str());
            if (definition.behavior == ObjectBehaviorKind::Forge) {
                std::printf("forge=%llu state=%s fuel=%llu/%llu ignition=%llu\n", static_cast<unsigned long long>(station.id),
                    station.forge.ready ? "Ready" : "Cold", static_cast<unsigned long long>(station.forge.fuel),
                    static_cast<unsigned long long>(definition.forge.fuelCapacity), static_cast<unsigned long long>(definition.forge.ignitionCost));
            }
        }
        return;
    }
    if (inspection == "combat") { std::puts(CombatDetailsText(content, view).c_str()); }
    Vector<CombatMenuEntry> rows;
    CollectCombatMenu(content, view, rows);
    for (const auto& row : rows) {
        if (inspection == "styles" && row.command.kind != CommandKind::select_combat_style) { continue; }
        if (inspection == "abilities" && row.command.kind != CommandKind::use_ability) { continue; }
        if (row.command.kind == CommandKind::select_combat_style) {
            std::printf("style %s\n", content.GetCombatStyle(row.command.combatStyle).id.c_str());
        } else if (row.command.kind == CommandKind::use_ability) {
            std::printf("ability %s\n", content.GetAbility(row.command.ability.ability).id.c_str());
        }
        std::printf("%s%s\n%s\n", row.enabled ? "" : "[unavailable] ", row.label.c_str(), row.details.c_str());
    }
}
bool ParseCommand(const std::string& text, const ContentCatalog& content, Command& command) {
    if (ParseItemCommand({text.data(), text.size()}, command)) { return true; }
    // Item syntax is tried first to retain `use ITEM_ID ACTION` compatibility.
    command = {};
    if (ParseCombatCommand(text, content, command)) { return true; }
    command = {};
    if (text == "wait") { command.kind = CommandKind::wait; return true; }
    if (text == "pickup") { command.kind = CommandKind::pick_up; return true; }
    if (text == "drop") { command.kind = CommandKind::drop; return true; }
    if (text == "attack") { command.kind = CommandKind::attack; return true; }
    const auto space = text.find(' ');
    if (space == std::string::npos) { return false; }
    if (text.substr(0, space) == "attack") {
        command.kind = CommandKind::attack;
        const char* begin = text.data() + space + 1;
        const char* end = text.data() + text.size();
        const auto parsed = std::from_chars(begin, end, command.target);
        return begin != end && parsed.ec == std::errc{} && parsed.ptr == end;
    }
    if (text.substr(0, space) == "style") {
        command.kind = CommandKind::combat_style;
        const auto style = text.substr(space + 1);
        if (style == "balanced") { command.style = CombatStyle::Balanced; }
        else if (style == "aggressive") { command.style = CombatStyle::Aggressive; }
        else if (style == "defensive") { command.style = CombatStyle::Defensive; }
        else { return false; }
        return true;
    }
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
    struct PlannedCommand { Command command; String inspection; };
    Vector<PlannedCommand> commands;
    for (const auto& text : options.commands) {
        if (text == "combat" || text == "styles" || text == "abilities" || text == "stations" || text == "power"
            || text == "machines" || text == "information" || text == "crafting") {
            commands.push_back({{}, text.c_str()}); continue;
        }
        Command command;
        if (!ParseCommand(text, content, command)) { std::fprintf(stderr, "Invalid LAN command: %s\n", text.c_str()); return 2; }
        commands.push_back({command, {}});
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
            while (sent == resolved && sent < commands.size() && !commands[sent].inspection.empty()) {
                PrintCombat(server.View(0), content, commands[sent].inspection); ++sent; ++resolved;
            }
            if (sent == resolved && sent < commands.size()) {
                auto command = commands[sent].command;
                if (!BindAbility(command, server.View(0), content)) { return 2; }
                ++sent;
                std::printf("submitted sequence=%llu\n", static_cast<unsigned long long>(host.QueueLocal(command)));
            }
            auto update = host.Pump(static_cast<std::uint64_t>(elapsed));
            for (const auto& receipt : update.commandResults) {
                if (receipt.player == server.GetHostPlayerId() && receipt.result.outcome != Outcome::queued) { ++resolved; }
                std::printf("receipt player=%llu sequence=%llu outcome=%s tick=%llu\n",
                    static_cast<unsigned long long>(receipt.player), static_cast<unsigned long long>(receipt.sequence),
                    CommandResultMessage(receipt.result).c_str(), static_cast<unsigned long long>(receipt.result.tick));
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
            while (accepted && sent == resolved && sent < commands.size() && !commands[sent].inspection.empty()) {
                PrintCombat(client.View(), content, commands[sent].inspection); ++sent; ++resolved;
            }
            if (accepted && sent == resolved && sent < commands.size()) {
                auto command = commands[sent].command;
                if (!BindAbility(command, client.View(), content)) { return 2; }
                auto submitted = client.Submit(command);
                if (!submitted) { std::fprintf(stderr, "Submit: %s\n", network::NetworkErrorMessage(submitted.error())); return 1; }
                ++sent;
                std::printf("submitted sequence=%llu\n", static_cast<unsigned long long>(*submitted));
            }
            for (const auto& receipt : client.TakeResults()) {
                if (receipt.result.outcome != Outcome::queued) { ++resolved; }
                std::printf("receipt player=%llu sequence=%llu outcome=%s tick=%llu\n",
                    static_cast<unsigned long long>(receipt.player), static_cast<unsigned long long>(receipt.sequence),
                    CommandResultMessage(receipt.result).c_str(), static_cast<unsigned long long>(receipt.result.tick));
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
                CommandResultMessage(receipt.result).c_str(), static_cast<unsigned long long>(receipt.result.tick));
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
