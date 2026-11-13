#pragma once

#include "core/world.h"
#include "client/localization.h"

#include <ftxui/component/component.hpp>

#include <functional>

namespace cataclysmx {

// World and its immutable content must outlive the component. The initial view
// is generated terrain; Inspect toggles read-only resident AI inspection.
// onClose dismisses the owning application's modal.
ftxui::Component CreateWorldgenDebugMenu(const World& world, TileCoord playerPosition,
    std::function<void()> onClose, const Localization* localization = nullptr,
    const Keymap* keymap = nullptr);

}
