#include "sdl/app.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <charconv>

namespace {
bool Port(eastl::string_view value, std::uint16_t& port) {
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
    return !value.empty() && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && port != 0;
}
bool Address(eastl::string_view value) {
    return !value.empty() && value.size() <= 255 && value.find_first_of(" \t\r\n:") == eastl::string_view::npos;
}
void Usage() {
    std::fputs("Usage: cataclysmx_sdl_game [--content path | --content-pack manifest] [--user-dir path] [--world-dir path] [--loc-dir path]\n"
        "  [--tileset directory] [--font path] [--font-size 8-48] [--ui-scale 1-4] [--smoke]\n"
        "  [--scenario demo|first-house|town|zombie-showcase] [--seed uint32] [--save path | --load path]\n"
        "  [--host | --join hostname[:port]] [--discover] [--port n] [--discovery-port n]\n"
        "  [--discovery-address address] [--name name] [--profile path] [--new-character]\n"
        "--save creates a persistent world; --load resumes one. World settings default to the save's directory.\n"
        "LAN peers must use matching content. --discovery-address supports directed discovery, including loopback.\n", stderr);
}
}

int main(int argc, char** argv) {
    using namespace cataclysmx;
    const char* contentPath = nullptr;
    const char* contentPack = nullptr;
    String localizationDirectory = CATACLYSMX_LOC_DIRECTORY;
    String userRoot = DefaultUserRoot();
    String worldDirectory;
    String joinEndpoint;
    sdl::AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const eastl::string_view argument(argv[i]);
        if (argument == "--smoke") { options.smoke = true; }
        else if (argument == "--help") { Usage(); return 0; }
        else if (argument == "--host") { options.session.host = true; }
        else if (argument == "--discover") { options.session.discover = true; }
        else if (argument == "--new-character") { options.session.newCharacter = true; }
        else if (argument == "--join" && i + 1 < argc && argv[i + 1][0]) { options.session.join = true; joinEndpoint = argv[++i]; }
        else if ((argument == "--port" || argument == "--discovery-port") && i + 1 < argc) {
            std::uint16_t port = 0;
            if (!Port(argv[++i], port)) { std::fputs("Invalid port\n", stderr); return 2; }
            if (argument == "--port") { options.session.hostSettings.port = port; }
            else { options.session.hostSettings.discoveryPort = port; }
        }
        else if (argument == "--discovery-address" && i + 1 < argc) {
            const eastl::string_view value(argv[++i]);
            if (!Address(value)) { std::fputs("Invalid discovery address\n", stderr); return 2; }
            options.session.discoveryAddress.assign(value.data(), value.size());
        }
        else if (argument == "--name" && i + 1 < argc) {
            const eastl::string_view value(argv[++i]);
            if (value.empty() || value.size() > 32 || value.find_first_not_of(" \t\r\n") == eastl::string_view::npos) { std::fputs("Invalid name\n", stderr); return 2; }
            options.session.connectionSettings.playerName.assign(value.data(), value.size());
            options.session.hostSettings.sessionName = options.session.connectionSettings.playerName;
        }
        else if (argument == "--profile" && i + 1 < argc && argv[i + 1][0]) { options.session.profilePath = argv[++i]; }
        else if (argument == "--save" && i + 1 < argc && argv[i + 1][0]) { options.session.savePath = argv[++i]; }
        else if (argument == "--load" && i + 1 < argc && argv[i + 1][0]) { options.session.loadPath = argv[++i]; }
        else if (argument == "--world-dir" && i + 1 < argc && argv[i + 1][0]) { worldDirectory = argv[++i]; }
        else if (argument == "--content" && i + 1 < argc && argv[i + 1][0]) { contentPath = argv[++i]; }
        else if (argument == "--content-pack" && i + 1 < argc && argv[i + 1][0]) { contentPack = argv[++i]; }
        else if (argument == "--scenario" && i + 1 < argc) {
            const eastl::string_view value(argv[++i]);
            if (value == "first-house") { options.scenario = NewGameScenario::FirstHouse; }
            else if (value == "demo") { options.scenario = NewGameScenario::Demo; }
            else if (value == "town") { options.scenario = NewGameScenario::Town; }
            else if (value == "zombie-showcase") { options.scenario = NewGameScenario::ZombieShowcase; }
            else { std::fputs("Scenario must be demo, first-house, town, or zombie-showcase\n", stderr); return 2; }
        }
        else if (argument == "--seed" && i + 1 < argc) {
            const eastl::string_view value(argv[++i]);
            std::uint32_t seed = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seed);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) { return 2; }
            options.seedOverride = seed;
        }
        else if (argument == "--loc-dir" && i + 1 < argc && argv[i + 1][0]) { localizationDirectory = argv[++i]; }
        else if (argument == "--user-dir" && i + 1 < argc && argv[i + 1][0]) { userRoot = argv[++i]; }
        else if (argument == "--tileset" && i + 1 < argc && argv[i + 1][0]) { options.tilesetDirectory = argv[++i]; }
        else if (argument == "--font" && i + 1 < argc && argv[i + 1][0]) { options.fontPath = argv[++i]; }
        else if ((argument == "--font-size" || argument == "--ui-scale") && i + 1 < argc) {
            const eastl::string_view value(argv[++i]);
            unsigned int number = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            const bool font = argument == "--font-size";
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
                || number < (font ? 8U : 1U) || number > (font ? 48U : 4U)) {
                std::fputs("Font size must be 8-48; UI scale must be 1-4.\n", stderr); return 2;
            }
            if (font) { options.fontSize = static_cast<int>(number); }
            else { options.uiScale = static_cast<float>(number); }
        }
        else {
            Usage();
            return 2;
        }
    }
    if (contentPath && contentPack) { std::fputs("Choose either --content or --content-pack\n", stderr); return 2; }
    if ((options.session.host && options.session.join) || (options.session.newCharacter && !options.session.join)
        || (!options.session.savePath.empty() && !options.session.loadPath.empty())
        || (options.seedOverride && !options.session.loadPath.empty())
        || (options.session.join && (!options.session.savePath.empty() || !options.session.loadPath.empty()))) {
        std::fputs("Incompatible session or persistence options\n", stderr); return 2;
    }
    options.session.connectionSettings.port = options.session.hostSettings.port;
    if (options.session.join) {
        const eastl::string_view endpoint(joinEndpoint.data(), joinEndpoint.size());
        const auto colon = endpoint.find(':');
        const auto address = endpoint.substr(0, colon);
        if (!Address(address) || (colon != eastl::string_view::npos
            && !Port(endpoint.substr(colon + 1), options.session.connectionSettings.port))) {
            std::fputs("Invalid join address or port\n", stderr); return 2;
        }
        options.session.connectionSettings.address.assign(address.data(), address.size());
    }
    if (worldDirectory.empty() && (!options.session.savePath.empty() || !options.session.loadPath.empty())) {
        worldDirectory = WorldDirectoryForSave((options.session.savePath.empty() ? options.session.loadPath : options.session.savePath).c_str());
    }
    Diagnostics diagnostics;
    SettingsStore settings(userRoot, worldDirectory);
    Localization localization;
    if (!settings.Load(&diagnostics)
        || !localization.Load(localizationDirectory.c_str(), settings.Effective().language, &diagnostics)) {
        std::fprintf(stderr, "Configuration: %s\n", FormatDiagnostics(diagnostics).c_str());
        return 1;
    }
    const auto content = contentPath ? LoadContentFile(contentPath, &diagnostics)
        : LoadContentPack(contentPack ? contentPack : options.scenario == NewGameScenario::FirstHouse
            ? CATACLYSMX_FIRST_HOUSE_PACK : options.scenario == NewGameScenario::ZombieShowcase
                ? CATACLYSMX_ZOMBIE_SHOWCASE_PACK : options.scenario == NewGameScenario::Town
                ? CATACLYSMX_CONTENT_PACK : CATACLYSMX_TERRAIN_FILE, &diagnostics);
    if (!content || !ValidateServerContent(*content, &diagnostics)) {
        std::fprintf(stderr, "Content: %s\n", FormatDiagnostics(diagnostics).c_str());
        return 1;
    }
    return sdl::RunApp(*content, settings, localization, localizationDirectory.c_str(), options);
}
