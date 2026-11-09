#pragma once

#include "core/perception_types.h"
#include <EASTL/utility.h>

namespace cataclysmx {

class World;

struct OpticalChunk {
    ChunkCoord position;
    eastl::array<std::uint8_t, chunk_size * chunk_size> cells{};
    eastl::array<std::uint16_t, chunk_size> opaqueRows{};
    eastl::array<std::uint16_t, chunk_size> closedRows{};
    bool transparent = false;
    bool openBelow = false;
    bool closedBelow = false;
};
// Mutation-generation snapshot. Values remain valid when World storage moves.
struct OpticalField {
    Vector<OpticalChunk> chunks;
    std::uint64_t revision = 0;
    void Rebuild(const World& world);
    const OpticalChunk* Find(ChunkCoord position) const;
};

struct OpticalCounts { int obstacles = 0; int floors = 0; };
// Chunk-aligned observer working volume. Reused within a chunk until optical
// mutation; never borrows World/chunk storage.
struct VisibilityWorkspace {
    ChunkCoord firstChunk{};
    ChunkCoord lastChunk{};
    int width = 0;
    int depth = 0;
    const OpticalField* source = nullptr;
    std::uint64_t revision = 0;
    Vector<std::uint8_t> optics;
    Vector<OpticalCounts> sums;
    bool Prepare(const OpticalField& field, ChunkCoord first, ChunkCoord last);
    std::uint8_t Sample(TileCoord position) const;
    OpticalCounts Box(int x0, int z0, int y0, int x1, int z1, int y1) const;
};

// Bounded 3D mask with implicit clear slabs and dense fallback cells.
// Absent residency is opaque.
struct VisibilityMask {
    TileCoord origin{};
    int radius = 0;
    int minimumY = 0;
    int maximumY = -1;
    Vector<std::uint8_t> cells; // bit 0 visible, bit 1 opaque endpoint
    eastl::array<bool, world_max_y - world_min_y + 1> visibleLayers{};
    eastl::array<bool, world_max_y - world_min_y + 1> clearLayers{};
    std::size_t Index(TileCoord position) const;
    std::uint8_t Get(TileCoord position) const;
    void MaterializeLayer(int y);
    bool Contains(TileCoord position) const;
    // Inclusive integer row interval, empty when first > second. Only valid for
    // an implicit clear layer; dense layers must be inspected individually.
    eastl::pair<int, int> ClearRow(int z, int y) const;
};

void BuildVisibilityMask(const World& world, TileCoord origin, int radius,
    VisibilityMask& mask, PerceptionStats& stats, const OpticalField* optics = nullptr,
    VisibilityWorkspace* workspace = nullptr);
bool TerrainWithinVision(const World& world, TileCoord observer, TileCoord target,
    const VisionProfile& vision);
int VisionRange(const VisionProfile& vision, LightBand targetBand, std::uint8_t observerLight);

// Geometry queries are independent of illumination and frontend projection.
bool OpenVerticalFace(const World& world, TileCoord first, TileCoord second);
bool LineVisible(const World& world, TileCoord origin, TileCoord target);
bool CanSeeTile(const World& world, TileCoord observer, TileCoord target,
    const VisionProfile& vision = {});

}
