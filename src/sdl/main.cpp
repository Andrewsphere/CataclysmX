#include "sdl/app.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <charconv>

int main(int argc, char** argv) {
    using namespace cataclysmx;
    String contentPath = CATACLYSMX_TERRAIN_FILE;
    String localizationDirectory = CATACLYSMX_LOC_DIRECTORY;
    String userRoot = DefaultUserRoot();
    sdl::AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const eastl::string_view argument(argv[i]);
        if (argument == "--smoke") { options.smoke = true; }
        else if (argument == "--content" && i + 1 < argc) { contentPath = argv[++i]; }
        else if (argument == "--loc-dir" && i + 1 < argc) { localizationDirectory = argv[++i]; }
        else if (argument == "--user-dir" && i + 1 < argc) { userRoot = argv[++i]; }
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
            if (font) { options.fontSize = static_cast<float>(number); }
            else { options.uiScale = static_cast<float>(number); }
        }
        else {
            std::fputs("Usage: cataclysmx_sdl_game [--content path] [--user-dir path] [--loc-dir path]\n"
                "  [--tileset directory] [--font path] [--font-size 8-48] [--ui-scale 1-4] [--smoke]\n", stderr);
            return 2;
        }
    }
    Diagnostics diagnostics;
    SettingsStore settings(userRoot);
    Localization localization;
    if (!settings.Load(&diagnostics)
        || !localization.Load(localizationDirectory.c_str(), settings.Effective().language, &diagnostics)) {
        std::fprintf(stderr, "Configuration: %s\n", FormatDiagnostics(diagnostics).c_str());
        return 1;
    }
    const auto content = LoadContentFile(contentPath.c_str(), &diagnostics);
    if (!content || !ValidateServerContent(*content, &diagnostics)) {
        std::fprintf(stderr, "Content: %s\n", FormatDiagnostics(diagnostics).c_str());
        return 1;
    }
    return sdl::RunApp(*content, settings.Effective(), settings.Keys(), localization, options);
}
