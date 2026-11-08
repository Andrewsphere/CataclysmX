#include "core/integration.h"
#include "core/server.h"
#include "client/text.h"

#include <EASTL/array.h>
#include <EASTL/string_view.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

namespace {

using namespace cataclysmx;

template<class T>
bool number(eastl::string_view text, T& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool direction(eastl::string_view text, Command& command) {
    if (text == "n") { command.dz = -1;
    } else if (text == "s") { command.dz = 1;
    } else if (text == "e") { command.dx = 1;
    } else if (text == "w") { command.dx = -1;
    } else { return false; }
    return true;
}

void inspect(const Server& server) {
    const World& world = server.GetWorld();
    std::size_t active = 0;
    for (ChunkCoord coord : world.LoadedChunks()) {
        active += world.GetChunk(coord)->active ? 1 : 0;
    }
    std::printf("inspect tick=%llu loaded=%zu active=%zu archived=%zu invariants=%s\n",
        static_cast<unsigned long long>(server.GetTick()), world.LoadedChunks().size(), active,
        world.ArchivedChunkCount(), world.CheckInvariants() ? "ok" : "BROKEN");
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
bool execute(Server& server, eastl::string_view line, bool& quit) {
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
        std::fputs(render_text(server.View()).c_str(), stdout);
        return true;
    }
    if (name == "inspect" && count == 1) { inspect(server); return true; }
    if (name == "bubble" && count == 3) {
        int active = 0;
        int resident = 0;
        if (!number(words[1], active) || !number(words[2], resident)
            || !server.SetBubbleRadii(active, resident)) { return false; }
        std::printf("bubble active=%d resident=%d\n", active, resident);
        return true;
    }
    if (name == "force" && count == 5) {
        ChunkRegion region;
        if (!number(words[1], region.center.x) || !number(words[2], region.center.z)
            || !number(words[3], region.radius)) { return false; }
        LoadMode mode;
        if (words[4] == "resident") { mode = LoadMode::resident;
        } else if (words[4] == "active") { mode = LoadMode::active;
        } else { return false; }
        auto request = server.GetWorld().RequestChunks(region, mode);
        if (!request) { return false; }
        server.GetWorld().ReconcileChunks();
        std::printf("request=%llu\n", static_cast<unsigned long long>(request->value));
        return true;
    }
    if (name == "release" && count == 2) {
        LoadRequest request;
        if (!number(words[1], request.value) || !server.GetWorld().ReleaseRequest(request)) { return false; }
        server.GetWorld().ReconcileChunks();
        std::puts("released");
        return true;
    }
    Command command;
    if (name == "wait" && count == 1) {
        command.kind = CommandKind::wait;
    } else if ((name == "move" || name == "interact") && count == 2) {
        command.kind = name == "move" ? CommandKind::move : CommandKind::interact;
        if (!direction(words[1], command)) { return false; }
    } else if ((name == "pickup" || name == "drop") && (count == 1 || count == 2)) {
        command.kind = name == "pickup" ? CommandKind::pick_up : CommandKind::drop;
        if (count == 2 && !number(words[1], command.item)) { return false; }
    } else { return false; }
    const CommandResult result = server.Send(command);
    std::printf("tick=%llu advanced=%d %s\n", static_cast<unsigned long long>(result.tick),
        result.advanced ? 1 : 0, outcome_message(result.outcome));
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
    Server server;
    if (argc == 1) {
        bool quit = false;
        for (const char* command : {"pickup", "move e", "interact e", "wait", "view", "inspect"}) {
            if (!execute(server, command, quit)) { return 1; }
        }
        return server.GetWorld().CheckInvariants() ? 0 : 1;
    }
    if (argc != 2 || std::strcmp(argv[1], "--script") != 0) {
        std::fputs("Usage: cataclysmx_headless [--script | --probe | --fail-log]\n", stderr);
        return 2;
    }
    bool quit = false;
    std::string line;
    std::size_t line_number = 0;
    while (!quit && std::getline(std::cin, line)) {
        ++line_number;
        if (!execute(server, eastl::string_view(line.data(), line.size()), quit)) {
            std::fprintf(stderr, "Invalid command/request on line %zu: %s\n", line_number, line.c_str());
            return 2;
        }
    }
    return !std::cin.bad() && server.GetWorld().CheckInvariants() ? 0 : 1;
}
