#pragma once

#include "core/perception_types.h"
#include <EASTL/utility.h>
#include <EASTL/span.h>

namespace cataclysmx {

class World;

struct OpticalChunk {
    ChunkCoord position;
    eastl::array<std::uint8_t, chunk_size * chunk_size> cells{};
    eastl::array<std::uint16_t, chunk_size> opaqueRows{};
    bool transparent = false;
};
// Mutation-generation snapshot. Values remain valid when World storage moves.
struct OpticalField {
    Vector<OpticalChunk> chunks;
    std::uint64_t revision = 0;
    void Rebuild(const World& world);
    // Exact changed/residency coordinates; absent World chunks are removed.
    // Advances revision only when copied optical flags or residency differ.
    void Refresh(const World& world, eastl::span<const ChunkCoord> positions);
    const OpticalChunk* Find(ChunkCoord position) const;
};

// Chunk-aligned observer plane; never borrows World/chunk storage.
struct VisibilityWorkspace {
    ChunkCoord firstChunk{};
    ChunkCoord lastChunk{};
    int width = 0;
    int depth = 0;
    const OpticalField* source = nullptr;
    std::uint64_t revision = 0;
    Vector<std::uint8_t> optics;
    std::uint64_t projectionSamples = 0; // Downward residency probes in last build.
    bool Prepare(const OpticalField& field, ChunkCoord first, ChunkCoord last);
    std::uint8_t Sample(TileCoord position) const;
};

// Downward-projected planar mask, with packed dense layers and implicit circles.
// Absent residency is opaque.
struct VisibilityMask {
    TileCoord origin{};
    int radius = 0;
    int minimumY = 0;
    int maximumY = -1;
    Vector<std::uint8_t> cells; // bit 0 visible, bit 1 opaque endpoint
    eastl::array<bool, world_max_y - world_min_y + 1> visibleLayers{};
    eastl::array<bool, world_max_y - world_min_y + 1> clearLayers{};
    // Offsets are indexed relative to minimumY; -1 means no dense allocation.
    eastl::array<int, world_max_y - world_min_y + 1> layerOffsets{};
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
// Local observer-plane shadowcast plus only the target's downward column.
// Work is bounded by horizontal target radius and column depth, not residency.
bool LineVisible(const World& world, TileCoord origin, TileCoord target);
bool CanSeeTile(const World& world, TileCoord observer, TileCoord target,
    const VisionProfile& vision = {});

}
