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

// Cumulative work counters for diagnosing preview stalls; Reset clears them.
struct WorldgenDebugWork {
    std::uint64_t regions = 0;
    std::uint64_t chunks = 0;
    std::uint64_t cells = 0;
    std::uint64_t details = 0;
};

// Frontend-owned, generated-baseline viewport. The selected immutable catalog
// must outlive all Prepare calls. Reset when replacing catalog/localization in
// place; localization is only dereferenced during Select.
// Plans/chunks are retained only around the viewport; overlapping image cells
// survive camera movement. No World access or simulation state is involved.
class WorldgenDebugPreview {
public:
    void Reset();
    void Select(const ContentCatalog& content, std::uint64_t seed, TileCoord selection,
        WorldgenDebugLayer layer, int scale, const Localization* localization = nullptr);
    // Positive display-cell dimensions. Power-of-two scale 1..16 is selected
    // above. The global grid stays aligned across negative/region coordinates.
    // World edges clip the image; renderers keep selection centered themselves.
    // Returns preview-owned mutable storage. Copy for snapshots; do not retain
    // cell references across subsequent Prepare, Select, or Reset calls.
    const WorldgenDebugView& Prepare(int columns, int rows);
    const Vector<String>& Details() const { return details_; }
    WorldgenDebugWork Work() const { return work_; }
private:
    struct PreviewChunk {
        ChunkCoord position{};
        eastl::array<Tile, chunk_size * chunk_size> tiles{};
    };
    const RegionPlan& Plan(RegionCoord region);
    TerminalAppearance Sample(TileRect cell);
    const ContentCatalog* content_ = nullptr;
    const Localization* localization_ = nullptr;
    std::uint64_t seed_ = 0;
    eastl::optional<TileCoord> selection_;
    WorldgenDebugLayer layer_ = WorldgenDebugLayer::Roads;
    WorldgenDebugLayer imageLayer_ = WorldgenDebugLayer::Roads;
    int scale_ = 8;
    Vector<RegionPlan> plans_;
    Vector<PreviewChunk> chunks_;
    WorldgenDebugView image_;
    WorldgenDebugView scratch_;
    Vector<String> details_;
    WorldgenDebugWork work_;
};

}
