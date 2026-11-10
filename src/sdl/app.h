#pragma once

#include "client/localization.h"

namespace cataclysmx::sdl {

struct AppOptions {
    bool smoke = false;
    String tilesetDirectory;
    String fontPath;
    float fontSize = 14.0f;
    float uiScale = 1.0f;
};

eastl::optional<Command> GameplayCommand(InputAction action, bool interacting);
// Called on process main; catalog, settings and localization remain immutable.
int RunApp(const ContentCatalog& content, const Settings& settings,
    const Keymap& keys, const Localization& localization, AppOptions options = {});

}
