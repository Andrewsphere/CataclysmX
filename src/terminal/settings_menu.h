#pragma once

#include "client/localization.h"
#include "terminal/keyboard.h"

#include <ftxui/component/component.hpp>

#include <functional>

namespace cataclysmx {

// References outlive the modal. All edits, including capture/text entry, are
// consumed here; Apply does not send commands or elapsed time to the server.
// Residency previews use the server policy when no scoped selector overrides it.
ftxui::Component CreateSettingsMenu(SettingsStore& store, Localization& localization,
    Server& server, TerminalKeyboard& keyboard, const char* localizationDirectory,
    std::function<void()> onClose);

}
