#pragma once

#include "client/debug_map.h"

namespace cataclysmx {

class Localization;

enum class WorldgenDebugLayer { Roads, Parcels, Ownership, Biomes, Terrain };

using WorldgenDebugView = DebugMapView;

const char* WorldgenDebugLayerName(WorldgenDebugLayer layer);
const char* WorldgenDebugLegend(WorldgenDebugLayer layer);
// Pure generated previews, including unexplored regions; never load/mutate World.
// Preconditions are the same as GenerateChunk, plus a valid layer and scale 1..16.
WorldgenDebugView BuildWorldgenDebugView(const ContentCatalog& content,
    const RegionPlan& plan, WorldgenDebugLayer layer, int scale);
// Selected tile, environment, owner/building information, and boundary contracts.
// Selection must be a valid tile inside plan.bounds. Results own their strings.
Vector<String> WorldgenDebugDetails(const ContentCatalog& content,
    const RegionPlan& plan, TileCoord selection, const Localization* localization = nullptr);

}
