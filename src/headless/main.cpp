#include "core/integration.h"
#include "core/server.h"
#include "client/text.h"
#include "headless/lan.h"
#include "client/settings.h"

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
        std::printf("chunk %d %d %s actors=%zu ground=%zu objects=%zu\n", coord.x, coord.z,
            chunk->active ? "active" : "resident", chunk->actors.size(), chunk->ground_items.size(),
            chunk->objects.size());
        for (ActorHandle handle : chunk->actors) {
            const Actor* actor = world.GetActor(handle);
            std::printf("actor id=%llu position=%d,%d turns=%llu inventory=%zu\n",
                static_cast<unsigned long long>(actor->id), actor->position.x, actor->position.z,
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
    if (name == "save" && (count == 1 || count == 2)) {
        Diagnostics diagnostics;
        SaveResult<void> saved;
        if (count == 2) {
            const String path(words[1].data(), words[1].size());
            if (configuration.enabled) {
                auto candidate = configuration.LoadFor(path.c_str(), &diagnostics);
                if (!candidate || !ApplySettings(server, candidate->Effective(), &diagnostics)) {
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
            if (!candidate || !ApplySettings(**loaded, candidate->Effective(), &diagnostics)) {
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
                OutcomeMessage(resolved.outcome));
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
    if (name == "wait" && (count == 1 || count == 2)) {
        command.kind = CommandKind::wait;
        if (count == 2 && (!Number(words[1], command.waitCost) || command.waitCost == 0)) { return false; }
    } else if ((name == "move" || name == "interact") && count == 2) {
        command.kind = name == "move" ? CommandKind::move : CommandKind::interact;
        if (!Direction(words[1], command)) { return false; }
    } else if ((name == "pickup" || name == "drop") && (count == 1 || count == 2)) {
        command.kind = name == "pickup" ? CommandKind::pick_up : CommandKind::drop;
        if (count == 2 && !Number(words[1], command.item)) { return false; }
    } else { return false; }
    const CommandResult result = server.Send(command);
    std::printf("tick=%llu advanced=%d %s\n", static_cast<unsigned long long>(result.tick),
        result.advanced ? 1 : 0, OutcomeMessage(result.outcome));
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
    const char* contentPath = CATACLYSMX_TERRAIN_FILE;
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
        } else if (argument == "--save" && i + 1 < argc) { savePath = argv[++i];
        } else if (argument == "--load" && i + 1 < argc) { loadPath = argv[++i];
        } else if (argument == "--user-dir" && i + 1 < argc) { configuration.userRoot = argv[++i]; configuration.enabled = true;
        } else if (argument == "--world-dir" && i + 1 < argc) { configuration.worldDirectory = argv[++i]; configuration.enabled = true;
        } else {
            std::fputs("Usage: cataclysmx_headless [--content path] [--user-dir path] [--world-dir path] [--save path | --load path] [--script | --host | --join address[:port] | --discover] [--port n] [--discovery-port n] [--discovery-address address] [--name name] [--profile path] [--new-character] [--run-for ms] [--command 'move e'] | --probe | --fail-log\n", stderr);
            return 2;
        }
    }
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
    const auto content = LoadContentFile(contentPath, &diagnostics);
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
        server = eastl::make_unique<Server>(*content, configuration.store ? configuration.store->Effective().seed : 42);
    }
    if (configuration.store && !ApplySettings(*server, configuration.store->Effective(), &diagnostics)) {
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
