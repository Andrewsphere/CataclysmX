#pragma once

#include "core/server.h"

namespace cataclysmx::sdl {

struct MapItemVisual {
    PersistentId item = 0;
    ItemTypeId type{};
    bool operator==(const MapItemVisual&) const = default;
};

// Call only on the session owner, against the world used to capture the view.
// Row-major output contains only the chosen visible ground-item overlays.
Vector<MapItemVisual> CaptureMapItems(const World& world, const WorldView& view);

}
