#pragma once

#include "worldgen/worldgen.h"

namespace cataclysmx {

enum class WorldgenDebugLayer { Roads, Parcels, Ownership, Biomes, Terrain };

struct WorldgenDebugView {
    TileRect bounds{};
    int scale = 1; // World tiles per displayed cell, in [1,16].
    int width = 0;
    int height = 0;
    Vector<TerminalAppearance> cells; // Row-major; owning, frontend-independent values.
};

const char* WorldgenDebugLayerName(WorldgenDebugLayer layer);
const char* WorldgenDebugLegend(WorldgenDebugLayer layer);
// Pure generated previews, including unexplored regions; never load/mutate World.
// Preconditions are the same as GenerateChunk, plus a valid layer and scale 1..16.
WorldgenDebugView BuildWorldgenDebugView(const ContentCatalog& content,
    const RegionPlan& plan, WorldgenDebugLayer layer, int scale);
// Selected tile, environment, owner/building information, and boundary contracts.
// Selection must be a valid tile inside plan.bounds. Results own their strings.
Vector<String> WorldgenDebugDetails(const ContentCatalog& content,
    const RegionPlan& plan, TileCoord selection);

}
