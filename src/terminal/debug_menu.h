#pragma once

#include "core/world.h"

#include <ftxui/component/component.hpp>

#include <functional>

namespace cataclysmx {

// World and its immutable content must outlive the component. Inspection uses
// generated baseline data only; onClose dismisses the owning application's modal.
ftxui::Component CreateWorldgenDebugMenu(const World& world, TileCoord playerPosition,
    std::function<void()> onClose);

}
