#pragma once

#include "core/world_types.h"
#include "core/containers.h"
#include "core/light_types.h"
#include <EASTL/array.h>
#include <EASTL/span.h>
#include <cstddef>

namespace cataclysmx {
class World;

// Observer-facing terrain illumination, distinct from transported/actor exposure
// light. Lower opaque targets use their projected top face; same-level targets
// use horizontal dominant entry faces. Upward illumination retains 3D entry faces.
// Does not establish target visibility or acquire residency. See doc/surface_lighting.md.
std::uint8_t TerrainLightLevel(const World& world, TileCoord observer, TileCoord target);

// Disposable, residency-clipped field. Never acquires chunk demand.
class LightingField {
public:
    void Rebuild(const World& world);
    std::uint8_t Get(TileCoord position) const;
    // Face order: +X, -X, +Z, -Z, +Y, -Y. Direct incident light only.
    std::uint8_t GetIncident(TileCoord position, int face) const;
    // Borrowed until the next Rebuild. Empty for nonresident chunks.
    eastl::span<const std::uint8_t> GetChunkLevels(ChunkCoord position) const;
    // Final recombination generation; zero for nonresident chunks.
    std::uint64_t GetChunkRevision(ChunkCoord position) const;
    // Last Rebuild's sorted unique dirty coordinates, including removed chunks.
    // Borrowed until the next Rebuild. Order: X, then Z, then Y.
    eastl::span<const ChunkCoord> ChangedChunks() const;
private:
    static constexpr int tileCount = chunk_size * chunk_size;
    using Levels = eastl::array<std::uint8_t, tileCount>;
    using Faces = eastl::array<std::uint8_t, 6>;
    struct LightChunk {
        ChunkCoord position;
        Levels levels{};
        Levels sky{};
        Levels skyDirect{};
        Levels flags{};
        Levels firstStep{};
        eastl::array<Faces, tileCount> incident{};
        eastl::array<Faces, tileCount> skyIncident{};
        eastl::array<std::uint32_t, tileCount> terrain{};
        eastl::array<LightEmission, tileCount> emission{};
        eastl::array<int, 6> neighbors{};
        std::uint64_t inputRevision = 0;
        std::uint64_t revision = 0;
        struct Emitter {
            TileCoord position;
            LightEmission emission;
        };
        Vector<Emitter> emitters;
    };
    struct Contribution {
        int chunk = -1;
        int tile = 0;
        std::uint8_t level = 0;
        Faces incident{};
    };
    struct Source {
        TileCoord position;
        LightEmission emission;
        Vector<Contribution> contribution;
    };
    Vector<LightChunk> chunks_;
    Vector<Source> sources_;
    Vector<ChunkCoord> changedChunks_;
    std::uint64_t generation_ = 0;
    std::uint8_t skyLight_ = 0;
    mutable std::size_t lastChunkLookup_ = 0;
    std::size_t FindChunk(ChunkCoord position) const;
    void BuildSource(Source& source);
    void BuildSky(eastl::span<const std::uint8_t> dirty);
};
}
