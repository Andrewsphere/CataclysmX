#include "core/integration.h"
#include "core/server.h"
#include "core/first_house_scenario.h"
#include "client/text.h"
#include "headless/lan.h"
#include "client/settings.h"
#include "client/crafting_menu.h"
#include "client/combat_menu.h"
#include "client/power_view.h"

#include <EASTL/array.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/string_view.h>

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

using namespace cataclysmx;

struct HeadlessConfiguration {
    bool enabled = false;
    String userRoot;
    String worldDirectory;
    eastl::optional<SettingsStore> store;

    SettingsResult<SettingsStore> LoadFor(const char* savePath, Diagnostics* diagnostics) const {
        String directory = worldDirectory;
        if (directory.empty() && savePath) { directory = WorldDirectoryForSave(savePath); }
        SettingsStore candidate(userRoot, directory);
        const auto loaded = candidate.Load(diagnostics);
        if (!loaded) { return eastl::unexpected(loaded.error()); }
        return candidate;
    }
};

template<class T>
bool Number(eastl::string_view text, T& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool Direction(eastl::string_view text, Command& command) {
    if (text == "n") { command.dz = -1;
    } else if (text == "s") { command.dz = 1;
    } else if (text == "e") { command.dx = 1;
    } else if (text == "w") { command.dx = -1;
    } else if (text == "nw") { command.dx = -1; command.dz = -1;
    } else if (text == "ne") { command.dx = 1; command.dz = -1;
    } else if (text == "sw") { command.dx = -1; command.dz = 1;
    } else if (text == "se") { command.dx = 1; command.dz = 1;
    } else if (text == "down" || text == "<") { command.dy = -1;
    } else if (text == "up" || text == ">") { command.dy = 1;
    } else { return false; }
    return true;
}

void Inspect(const Server& server) {
    const World& world = server.GetWorld();
    std::size_t active = 0;
    for (ChunkCoord coord : world.LoadedChunks()) {
        active += world.GetChunk(coord)->active ? 1 : 0;
    }
    std::printf("inspect tick=%llu loaded=%zu active=%zu archived=%zu invariants=%s\n",
        static_cast<unsigned long long>(server.GetTick()), world.LoadedChunks().size(), active,
        world.ArchivedChunkCount(), world.CheckInvariants() ? "ok" : "BROKEN");
    const WorldView view = server.View(0);
    const PacingSettings pacing = server.GetPacing();
    std::printf("pacing rate=%u limit=%s idle-pause=%s speed=%u delay=%llu queued=%s\n",
        pacing.ticksPerSecond, pacing.rateLimited ? "on" : "off",
        pacing.pauseWhenIdle ? "on" : "off", view.speed,
        static_cast<unsigned long long>(view.remainingTicks), view.commandQueued ? "yes" : "no");
    for (ChunkCoord coord : world.LoadedChunks()) {
        const Chunk* chunk = world.GetChunk(coord);
        if (chunk->actors.empty() && chunk->ground_items.empty() && chunk->objects.empty()) { continue; }
        std::printf("chunk %d %d y=%d %s actors=%zu ground=%zu objects=%zu\n", coord.x, coord.z, coord.y,
            chunk->active ? "active" : "resident", chunk->actors.size(), chunk->ground_items.size(),
            chunk->objects.size());
        for (ActorHandle handle : chunk->actors) {
            const Actor* actor = world.GetActor(handle);
            std::printf("actor id=%llu position=%d,%d,%d turns=%llu inventory=%zu\n",
                static_cast<unsigned long long>(actor->id), actor->position.x, actor->position.z, actor->position.y,
                static_cast<unsigned long long>(actor->turns), actor->inventory.size());
        }
    }
}

// Returns false for malformed syntax/failed administrative requests. Gameplay
// rejections are valid responses, so scripts can exercise blocked actions.
bool Execute(eastl::unique_ptr<Server>& ownedServer, eastl::string_view line, bool& quit,
    HeadlessConfiguration& configuration) {
    Server& server = *ownedServer;
    eastl::array<eastl::string_view, 8> words{};
    std::size_t count = 0;
    while (!line.empty()) {
        const auto start = line.find_first_not_of(" \t\r");
        if (start == eastl::string_view::npos) { break; }
        line.remove_prefix(start);
        if (line.front() == '#') { break; }
        const auto end = line.find_first_of(" \t\r");
        if (count == words.size()) { return false; }
        words[count++] = line.substr(0, end);
        if (end == eastl::string_view::npos) { break; }
        line.remove_prefix(end);
    }
    if (count == 0) { return true; }
    const auto name = words[0];
    if (name == "quit" && count == 1) { quit = true; return true; }
    if (name == "view" && count == 1) {
        const int radius = configuration.store ? configuration.store->Effective().viewRadius : 8;
        std::fputs(render_text(server.View(radius), server.GetWorld().GetContent()).c_str(), stdout);
        return true;
    }
    if (name == "inspect" && count == 1) { Inspect(server); return true; }
    if (name == "power" && count == 1) {
        PowerWorkshop workshop; const auto view = server.View(8); workshop.Open(view);
        Vector<String> lines; workshop.Describe(view, server.GetWorld().GetContent(), lines);
        for (const auto& text : lines) std::puts(text.c_str());
        return true;
    }
    if (name == "power-debug" && count == 1) {
        PowerDebugSnapshot snapshot; CapturePowerDebug(server.GetWorld(), snapshot);
        Vector<String> lines; DescribePowerDebug(snapshot, server.View(0).player_position, lines);
        for (const auto& text : lines) std::puts(text.c_str());
        return true;
    }
    if ((name == "combat" || name == "styles" || name == "abilities") && count == 1) {
        const auto view = server.View(0);
        const auto& content = server.GetWorld().GetContent();
        if (name == "combat") {
            std::puts(CombatDetailsText(content, view).c_str());
            Vector<CombatMenuEntry> rows;
            CollectCombatMenu(content, view, rows);
            for (const auto& row : rows) {
                std::printf("%s%s\n%s\n", row.enabled ? "" : "[unavailable] ", row.label.c_str(), row.details.c_str());
            }
        }
        if (name == "styles" || name == "combat") {
            for (std::uint32_t i = 1; i <= content.CombatStyleCount(); ++i) {
                const auto& style = content.GetCombatStyle({i});
                bool known = style.art.value == 0;
                for (const auto& art : view.combatInfo.arts) { known |= art.art == style.art; }
                std::printf("style %s known=%d selected=%d\n", style.id.c_str(), known, view.combat.styleId.value == i);
            }
        }
        if (name == "abilities" || name == "combat") {
            for (const auto& ability : view.combatInfo.abilities) {
                std::printf("ability %s source=%s status=%s cooldown=%llu targeting=%u\n", content.GetAbility(ability.ability).id.c_str(),
                    ability.source.kind == AbilityGrantSourceKind::Art ? content.GetCombatArt(ability.source.art).id.c_str() : "innate",
                    CombatRefusalText(ability.availability.reason).c_str(), static_cast<unsigned long long>(ability.cooldownRemaining), static_cast<unsigned>(ability.targeting));
            }
        }
        return true;
    }
    if (name == "skills" && count == 1) {
        Vector<String> lines;
        CollectLearningSummary(server.View(0), server.GetWorld().GetContent(), lines);
        for (const auto& text : lines) { std::puts(text.c_str()); }
        return true;
    }
    if ((name == "recipes" || name == "crafting") && count == 1) {
        Vector<CraftingMenuRow> rows;
        CollectCraftingMenu(server.View(0), server.GetWorld().GetContent(), Localization{}, rows);
        for (const auto& row : rows) {
            if (name == "crafting" || row.command.kind == CommandKind::craft) { std::puts(row.label.c_str()); }
        }
        return true;
    }
    if (name == "items" && count == 1) {
        std::fputs(ItemDetails(server.View(0), server.GetWorld().GetContent()).c_str(), stdout); return true;
    }
    if (name == "save" && (count == 1 || count == 2)) {
        Diagnostics diagnostics;
        SaveResult<void> saved;
        if (count == 2) {
            const String path(words[1].data(), words[1].size());
            if (configuration.enabled) {
                auto candidate = configuration.LoadFor(path.c_str(), &diagnostics);
                if (!candidate || !ApplySettings(server, *candidate, &diagnostics)) {
                    std::fprintf(stderr, "Configuration failed: %s\n", FormatDiagnostics(diagnostics).c_str()); return false;
                }
                configuration.store = eastl::move(*candidate);
            }
            saved = server.CreateSave(path.c_str(), &diagnostics);
        } else { saved = server.Save(&diagnostics); }
        if (!saved) {
            std::fprintf(stderr, "Save failed (error=%d): %s\n", static_cast<int>(saved.error()),
                FormatDiagnostics(diagnostics).c_str());
            return false;
        }
        if (count == 2 && configuration.store && !configuration.store->Save(SettingsScope::World, &diagnostics)) {
            std::fprintf(stderr, "World configuration save failed: %s\n", FormatDiagnostics(diagnostics).c_str()); return false;
        }
        return true;
    }
    if (name == "load" && count == 2) {
        Diagnostics diagnostics;
        const String path(words[1].data(), words[1].size());
        auto loaded = Server::LoadSave(server.GetWorld().GetContent(), path.c_str(), &diagnostics);
        if (!loaded) {
            std::fprintf(stderr, "Load failed (error=%d): %s\n", static_cast<int>(loaded.error()),
                FormatDiagnostics(diagnostics).c_str());
            return false;
        }
        if (configuration.enabled) {
            auto candidate = configuration.LoadFor(path.c_str(), &diagnostics);
            if (!candidate || !ApplySettings(**loaded, *candidate, &diagnostics)) {
                std::fprintf(stderr, "Configuration failed: %s\n", FormatDiagnostics(diagnostics).c_str()); return false;
            }
            configuration.store = eastl::move(*candidate);
        }
        ownedServer = eastl::move(*loaded);
        return true;
    }
    if (name == "autosave" && count == 2) {
        std::uint64_t turns = 0;
        if (!Number(words[1], turns)) { return false; }
        server.SetAutosaveInterval(turns);
        return true;
    }
    if (name == "rate" && count == 2) {
        PacingSettings pacing = server.GetPacing();
        if (!Number(words[1], pacing.ticksPerSecond) || pacing.ticksPerSecond == 0
            || !server.SetPacing(pacing)) { return false; }
        std::printf("rate=%u\n", pacing.ticksPerSecond);
        return true;
    }
    if ((name == "limit" || name == "idle-pause") && count == 2) {
        PacingSettings pacing = server.GetPacing();
        const bool enabled = words[1] == "on";
        if (!enabled && words[1] != "off") { return false; }
        if (name == "limit") { pacing.rateLimited = enabled; }
        else { pacing.pauseWhenIdle = enabled; }
        if (!server.SetPacing(pacing)) { return false; }
        std::printf("%.*s=%s\n", static_cast<int>(name.size()), name.data(), enabled ? "on" : "off");
        return true;
    }
    if (name == "elapse" && count == 2) {
        std::uint64_t microseconds = 0;
        if (!Number(words[1], microseconds)) { return false; }
        const SessionUpdate update = server.Update(microseconds);
        std::printf("tick=%llu skipped=%llu needs-input=%d\n",
            static_cast<unsigned long long>(update.tick),
            static_cast<unsigned long long>(update.skippedActions), update.needsInput ? 1 : 0);
        if (update.commandResult) {
            const CommandResult& resolved = *update.commandResult;
            std::printf("resolved tick=%llu advanced=%d %s\n",
                static_cast<unsigned long long>(resolved.tick), resolved.advanced ? 1 : 0,
                CommandResultMessage(resolved).c_str());
        }
        if (update.storageError) {
            std::fprintf(stderr, "%s (error=%d)\n",
                update.commandResult && update.commandResult->storageError
                    ? "World load failed for queued action" : "Autosave failed during elapsed update",
                static_cast<int>(*update.storageError));
            return false;
        }
        return true;
    }
    if (name == "bubble" && count == 3) {
        int active = 0;
        int resident = 0;
        if (!Number(words[1], active) || !Number(words[2], resident)
            || !server.SetBubbleRadii(active, resident)) { return false; }
        std::printf("bubble active=%d resident=%d\n", active, resident);
        return true;
    }
    if (name == "force" && count == 5) {
        ChunkRegion region;
        if (!Number(words[1], region.center.x) || !Number(words[2], region.center.z)
            || !Number(words[3], region.radius)) { return false; }
        LoadMode mode;
        if (words[4] == "resident") { mode = LoadMode::resident;
        } else if (words[4] == "active") { mode = LoadMode::active;
        } else { return false; }
        auto request = server.GetWorld().RequestChunks(region, mode);
        if (!request) { return false; }
        if (!server.GetWorld().ReconcileChunks()) {
            if (!server.GetWorld().ReleaseRequest(*request)) { std::abort(); }
            return false;
        }
        std::printf("request=%llu\n", static_cast<unsigned long long>(request->value));
        return true;
    }
    if (name == "release" && count == 2) {
        LoadRequest request;
        if (!Number(words[1], request.value) || !server.GetWorld().ReleaseRequest(request)) { return false; }
        if (!server.GetWorld().ReconcileChunks()) { return false; }
        std::puts("released");
        return true;
    }
    Command command;
    if (name == "wire-grid" && count == 7) {
        command.kind = CommandKind::wire_grid;
        if (!Number(words[1], command.item) || !Number(words[2], command.powerBounds.min.x)
            || !Number(words[3], command.powerBounds.min.z) || !Number(words[4], command.powerBounds.max.x)
            || !Number(words[5], command.powerBounds.max.z) || !Number(words[6], command.powerBounds.min.y)) return false;
        command.powerBounds.max.y = command.powerBounds.min.y;
    } else if (name == "place-power" && count == 6) {
        command.kind = CommandKind::place_power_object;
        if (!Number(words[1], command.item) || !Number(words[2], command.otherItem)
            || !Number(words[3], command.position.x) || !Number(words[4], command.position.z) || !Number(words[5], command.position.y)) return false;
    } else if (name == "new-character" && count == 1) {
        command.kind = CommandKind::new_character;
    } else if (name == "study-art" && count == 3) {
        command.kind = CommandKind::study_art;
        const auto& content = server.GetWorld().GetContent();
        if (!Number(words[1], command.book.value)) { command.book = content.FindBook(words[1]); }
        if (!Number(words[2], command.art.value)) { command.art = content.FindCombatArt(words[2]); }
        if (!content.TryGetBook(command.book) || !content.TryGetCombatArt(command.art)) { return false; }
    } else if (name == "pause-art" && count == 1) {
        command.kind = CommandKind::pause_art_study;
    } else if ((name == "hold" && (count == 3 || count == 4)) || (name == "releasehand" && count == 2)
        || (name == "regrip" && count == 3) || ((name == "wear" || name == "remove") && count == 2)) {
        command.kind = CommandKind::equipment;
        auto& request = command.equipment;
        request.operation = name == "hold" ? EquipmentOperation::Hold : name == "releasehand" ? EquipmentOperation::Release
            : name == "regrip" ? EquipmentOperation::Regrip : name == "wear" ? EquipmentOperation::Wear : EquipmentOperation::Remove;
        if (name == "hold" || name == "wear" || name == "remove") {
            if (!Number(words[1], request.item) || request.item == 0) { return false; }
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
    } else if (name == "use" && count >= 2 && (words[1] == "ability"
        || server.GetWorld().GetContent().FindAbility(words[1]).value != 0)) {
        const std::size_t idIndex = words[1] == "ability" ? 2 : 1;
        if (count <= idIndex) { return false; }
        command.kind = CommandKind::use_ability;
        auto& request = command.ability;
        const auto& content = server.GetWorld().GetContent();
        if (!Number(words[idIndex], request.ability.value)) { request.ability = content.FindAbility(words[idIndex]); }
        if (!content.TryGetAbility(request.ability)) { return false; }
        // Select a real grant from the snapshot, deterministically by stable art ID.
        const auto view = server.View(0);
        const AbilityView* selected = nullptr;
        for (const auto& ability : view.combatInfo.abilities) {
            if (ability.ability != request.ability) { continue; }
            if (!selected || static_cast<unsigned>(ability.source.kind) < static_cast<unsigned>(selected->source.kind)
                || (ability.source.kind == AbilityGrantSourceKind::Art && selected->source.kind == AbilityGrantSourceKind::Art
                    && content.GetCombatArt(ability.source.art).id < content.GetCombatArt(selected->source.art).id)) { selected = &ability; }
        }
        if (!selected) { return false; }
        request.source = selected->source;
        const std::size_t targetIndex = idIndex + 1;
        if (count == targetIndex || (count == targetIndex + 1 && words[targetIndex] == "self")) {
            request.target.kind = AbilityTargetKind::Self;
        } else if (count == targetIndex + 3 && words[targetIndex] == "tile") {
            request.target.kind = AbilityTargetKind::Tile;
            request.target.tile.y = view.player_position.y;
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
    } else if (name == "study" && count == 2) {
        command.kind = CommandKind::study_book;
        if (!Number(words[1], command.book.value) || command.book.value == 0) { return false; }
    } else if (name == "pause-study" && count == 1) {
        command.kind = CommandKind::pause_study;
    } else if (name == "craft" && count == 2) {
        command.kind = CommandKind::craft;
        if (!Number(words[1], command.recipe.value) || command.recipe.value == 0) { return false; }
    } else if ((name == "recycle" || name == "resume-craft" || name == "pause-craft" || name == "cancel-craft") && count == 2) {
        command.kind = name == "recycle" ? CommandKind::recycle : name == "resume-craft" ? CommandKind::resume_craft
            : name == "pause-craft" ? CommandKind::pause_craft : CommandKind::cancel_craft;
        if (!Number(words[1], command.item) || command.item == 0) { return false; }
    } else if (name == "wait" && (count == 1 || count == 2)) {
        command.kind = CommandKind::wait;
        if (count == 2 && (!Number(words[1], command.waitCost) || command.waitCost == 0)) { return false; }
    } else if (name == "attack" && (count == 1 || count == 2)) {
        command.kind = CommandKind::attack;
        if (count == 2 && !Number(words[1], command.target)) { return false; }
    } else if (name == "style" && count == 2) {
        command.kind = CommandKind::combat_style;
        if (words[1] == "balanced") { command.style = CombatStyle::Balanced; }
        else if (words[1] == "aggressive") { command.style = CombatStyle::Aggressive; }
        else if (words[1] == "defensive") { command.style = CombatStyle::Defensive; }
        else {
            command.kind = CommandKind::select_combat_style;
            if (!Number(words[1], command.combatStyle.value)) { command.combatStyle = server.GetWorld().GetContent().FindCombatStyle(words[1]); }
            if (!server.GetWorld().GetContent().TryGetCombatStyle(command.combatStyle)) { return false; }
        }
    } else if (name == "ungrab" && count == 1) {
        command.kind = CommandKind::release_grab;
    } else if ((name == "move" || name == "interact" || name == "grab") && count == 2) {
        command.kind = name == "move" ? CommandKind::move : name == "grab" ? CommandKind::grab : CommandKind::interact;
        if (!Direction(words[1], command)) { return false; }
        if (name == "grab" && command.dy != 0) { return false; }
    } else if ((name == "pickup" || name == "drop") && (count == 1 || count == 2)) {
        command.kind = name == "pickup" ? CommandKind::pick_up : CommandKind::drop;
        if (count == 2 && !Number(words[1], command.item)) { return false; }
    } else if (name == "use" || name == "store") {
        String text;
        for (std::size_t i = 0; i < count; ++i) {
            if (i != 0) { text += ' '; }
            text.append(words[i].data(), words[i].size());
        }
        if (!ParseItemCommand({text.data(), text.size()}, command)) { return false; }
    } else { return false; }
    const CommandResult result = server.Send(command);
    std::printf("tick=%llu advanced=%d %s\n", static_cast<unsigned long long>(result.tick),
        result.advanced ? 1 : 0, CommandResultMessage(result).c_str());
    if (result.storageError) {
        std::fprintf(stderr, "%s (error=%d)\n",
            result.advanced ? "Autosave failed after completed turn" : "World load failed",
            static_cast<int>(*result.storageError));
        return false;
    }
    return true;
}

}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--fail-log") == 0) {
        cataclysmx::logging_failure_probe();
    }
    if (argc == 2 && std::strcmp(argv[1], "--probe") == 0) {
        if (!cataclysmx::core_smoke() || !cataclysmx::json_smoke("{\"seed\":42}")) { return 1; }
        std::printf("seed=42 checksum=%u\n", cataclysmx::headless_checksum(42));
        return 0;
    }
    const char* contentPath = nullptr;
    const char* contentPack = nullptr;
    NewGameScenario scenario = NewGameScenario::Town;
    eastl::optional<std::uint32_t> seedOverride;
    const char* savePath = nullptr;
    const char* loadPath = nullptr;
    bool script = false;
    cataclysmx::headless::LanOptions lan;
    bool lanMode = false;
    HeadlessConfiguration configuration;
    for (int i = 1; i < argc; ++i) {
        const eastl::string_view argument(argv[i]);
        if (argument == "--script") { script = true;
        } else if (argument == "--host") { lan.host = lanMode = true;
        } else if (argument == "--discover") { lan.discover = lanMode = true;
        } else if (argument == "--new-character") { lan.newCharacter = lanMode = true;
        } else if (argument == "--join" && i + 1 < argc) { lan.join = argv[++i]; lanMode = true;
        } else if (argument == "--name" && i + 1 < argc) { lan.name = argv[++i]; lanMode = true;
        } else if (argument == "--profile" && i + 1 < argc) { lan.profile = argv[++i]; lanMode = true;
        } else if (argument == "--discovery-address" && i + 1 < argc) { lan.discoveryAddress = argv[++i]; lanMode = true;
        } else if (argument == "--command" && i + 1 < argc) { lan.commands.emplace_back(argv[++i]); lanMode = true;
        } else if (argument == "--port" && i + 1 < argc) {
            const eastl::string_view text(argv[++i]);
            if (!Number(text, lan.port) || !lan.port) { return 2; }
            lanMode = true;
        } else if (argument == "--discovery-port" && i + 1 < argc) {
            const eastl::string_view text(argv[++i]);
            if (!Number(text, lan.discoveryPort) || !lan.discoveryPort) { return 2; }
            lanMode = true;
        } else if (argument == "--run-for" && i + 1 < argc) {
            const eastl::string_view text(argv[++i]);
            if (!Number(text, lan.runForMs)) { return 2; }
            lanMode = true;
        } else if (argument == "--content" && i + 1 < argc) { contentPath = argv[++i];
        } else if (argument == "--content-pack" && i + 1 < argc) { contentPack = argv[++i];
        } else if (argument == "--scenario" && i + 1 < argc) {
            const eastl::string_view value(argv[++i]);
            if (value == "first-house") { scenario = NewGameScenario::FirstHouse; }
            else if (value == "demo") { scenario = NewGameScenario::Demo; }
            else if (value == "town") { scenario = NewGameScenario::Town; }
            else if (value == "zombie-showcase") { scenario = NewGameScenario::ZombieShowcase; }
            else { std::fputs("Scenario must be demo, first-house, town, or zombie-showcase\n", stderr); return 2; }
        } else if (argument == "--seed" && i + 1 < argc) {
            std::uint32_t seed = 0;
            if (!Number(eastl::string_view(argv[++i]), seed)) { return 2; }
            seedOverride = seed;
        } else if (argument == "--save" && i + 1 < argc) { savePath = argv[++i];
        } else if (argument == "--load" && i + 1 < argc) { loadPath = argv[++i];
        } else if (argument == "--user-dir" && i + 1 < argc) { configuration.userRoot = argv[++i]; configuration.enabled = true;
        } else if (argument == "--world-dir" && i + 1 < argc) { configuration.worldDirectory = argv[++i]; configuration.enabled = true;
        } else {
            std::fputs("Usage: cataclysmx_headless [--content path] [--user-dir path] [--world-dir path] [--save path | --load path] [--script | --host | --join address[:port] | --discover] [--port n] [--discovery-port n] [--discovery-address address] [--name name] [--profile path] [--new-character] [--run-for ms] [--command 'move e'] | --probe | --fail-log\nMovement: move n/s/e/w, move down or move < (downstairs), move up or move > (upstairs).\nCombat: attack [persistentId], style balanced|aggressive|defensive.\n", stderr);
            std::fputs("Content: --content <single definition file> | --content-pack <manifest>. New game: --scenario demo|first-house|town|zombie-showcase (default town) --seed <uint32>.\nGrab (local --script): grab n/ne/e/se/s/sw/w/nw, move in any of those directions, ungrab.\nCrafting (local --script): recipes, crafting, craft ID, recycle ITEM_ID, resume-craft JOB_ID, pause-craft JOB_ID, cancel-craft JOB_ID.\nLearning (local --script): skills, study BOOK_ID, pause-study.\nCombat (local --script): combat, styles, style ID, hold ITEM_ID left|right [two], releasehand left|right, regrip left|right one|two, wear ITEM_ID, remove ITEM_ID, abilities, use ability ID [self|tile X Z|north|east|south|west], study-art BOOK ART, pause-art, new-character.\n", stderr);
            return 2;
        }
    }
    if (contentPath && contentPack) { std::fputs("Choose either --content or --content-pack\n", stderr); return 2; }
    if (loadPath && seedOverride) { std::fputs("--seed applies only to a new game, not --load\n", stderr); return 2; }
    if (savePath != nullptr && loadPath != nullptr) {
        std::fputs("Choose either --save or --load\n", stderr);
        return 2;
    }
    if (script && lanMode) { std::fputs("--script and live LAN modes are separate\n", stderr); return 2; }
    if ((!lan.join.empty() || lan.discover) && (savePath || loadPath)) {
        std::fputs("Guest/discovery mode does not own a host game save\n", stderr); return 2;
    }
    Diagnostics diagnostics;
    if (configuration.enabled) {
        auto loaded = configuration.LoadFor(loadPath ? loadPath : savePath, &diagnostics);
        if (!loaded) { std::fprintf(stderr, "Configuration failed: %s\n", FormatDiagnostics(diagnostics).c_str()); return 1; }
        configuration.store = eastl::move(*loaded);
    }
    // Scripted save loads also retain a reference to this main-stack catalog.
    const auto content = contentPath ? LoadContentFile(contentPath, &diagnostics)
        : LoadContentPack(contentPack ? contentPack : scenario == NewGameScenario::FirstHouse
            ? CATACLYSMX_FIRST_HOUSE_PACK : scenario == NewGameScenario::ZombieShowcase
                ? CATACLYSMX_ZOMBIE_SHOWCASE_PACK : scenario == NewGameScenario::Town
                ? CATACLYSMX_CONTENT_PACK : CATACLYSMX_TERRAIN_FILE, &diagnostics);
    if (!content) {
        std::fprintf(stderr, "Content load failed (error=%d): %s\n", static_cast<int>(content.error()),
            FormatDiagnostics(diagnostics).c_str());
        return 1;
    }
    const auto validated = ValidateServerContent(*content, &diagnostics);
    if (!validated) {
        std::fprintf(stderr, "Content validation failed: %s\n", FormatDiagnostics(diagnostics).c_str());
        return 1;
    }
    eastl::unique_ptr<Server> server;
    diagnostics.Clear();
    if (loadPath != nullptr) {
        auto loaded = Server::LoadSave(*content, loadPath, &diagnostics);
        if (!loaded) {
            std::fprintf(stderr, "Load failed (error=%d): %s\n", static_cast<int>(loaded.error()),
                FormatDiagnostics(diagnostics).c_str());
            return 1;
        }
        server = eastl::move(*loaded);
    } else {
        const auto seed = seedOverride.value_or(configuration.store ? configuration.store->Effective().seed : 42);
        if ((scenario == NewGameScenario::FirstHouse && !FindFirstHouseStart(*content, seed, &diagnostics))
            || (scenario == NewGameScenario::Town && !FindTownStart(*content, seed, &diagnostics))) {
            std::fprintf(stderr, "Scenario validation failed: %s\n", FormatDiagnostics(diagnostics).c_str()); return 1;
        }
        server = eastl::make_unique<Server>(*content, seed, scenario);
    }
    if (configuration.store && !ApplySettings(*server, *configuration.store, &diagnostics)) {
        std::fprintf(stderr, "Configuration application failed: %s\n", FormatDiagnostics(diagnostics).c_str()); return 1;
    }
    if (loadPath == nullptr) {
        if (savePath != nullptr) {
            if (!configuration.enabled) { server->SetAutosaveInterval(1000); }
            diagnostics.Clear();
            const auto saved = server->CreateSave(savePath, &diagnostics);
            if (!saved) {
                std::fprintf(stderr, "Save creation failed (error=%d): %s\n", static_cast<int>(saved.error()),
                    FormatDiagnostics(diagnostics).c_str());
                return 1;
            }
            if (configuration.store && !configuration.store->Save(SettingsScope::World, &diagnostics)) {
                std::fprintf(stderr, "World configuration save failed: %s\n", FormatDiagnostics(diagnostics).c_str()); return 1;
            }
        }
    }
    if (lanMode) { return cataclysmx::headless::RunLan(*server, *content, lan); }
    if (!script) {
        bool quit = false;
        for (const char* command : {"pickup", "move e", "interact e", "wait", "view", "inspect"}) {
            if (!Execute(server, command, quit, configuration)) { return 1; }
        }
        return server->GetWorld().CheckInvariants() ? 0 : 1;
    }
    bool quit = false;
    std::string line;
    std::size_t line_number = 0;
    while (!quit && std::getline(std::cin, line)) {
        ++line_number;
        if (!Execute(server, eastl::string_view(line.data(), line.size()), quit, configuration)) {
            std::fprintf(stderr, "Invalid command/request on line %zu: %s\n", line_number, line.c_str());
            return 2;
        }
    }
    return !std::cin.bad() && server->GetWorld().CheckInvariants() ? 0 : 1;
}
