#pragma once

#include "worldgen/worldgen.h"

namespace cataclysmx {

// Owned, frontend-independent tile image shared by live and generated debug views.
struct DebugMapView {
    TileRect bounds{};
    int scale = 1;
    int width = 0;
    int height = 0;
    Vector<TerminalAppearance> cells;
};

}
