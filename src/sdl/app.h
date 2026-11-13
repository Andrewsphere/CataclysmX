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
// Called on process main. Only the session owner accesses the live world.
int RunApp(const ContentCatalog& content, SettingsStore& store, Localization& localization,
    const char* localizationDirectory, AppOptions options = {});

}
