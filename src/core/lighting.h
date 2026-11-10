#pragma once

#include "core/world_types.h"
#include "core/containers.h"
#include "core/light_types.h"
#include <EASTL/array.h>
#include <EASTL/span.h>
#include <EASTL/unique_ptr.h>
#include <cstddef>

namespace cataclysmx {
class World;

// Observer-facing terrain illumination, distinct from transported/actor exposure
// light. Lower opaque targets use their projected top face; same-level targets
// use exposed horizontal observer-facing sides. Upward illumination retains 3D entry faces.
// Does not establish target visibility or acquire residency. See doc/surface_lighting.md.
std::uint8_t TerrainLightLevel(const World& world, TileCoord observer, TileCoord target);

// Disposable, residency-clipped field. Never acquires chunk demand.
class LightingField {
public:
    LightingField();
    ~LightingField();
    LightingField(LightingField&&) noexcept;
    LightingField& operator=(LightingField&&) noexcept;
    LightingField(const LightingField&) = delete;
    LightingField& operator=(const LightingField&) = delete;
    // Synchronize inputs and publish invalidation revisions without computing output.
    void Prepare(const World& world);
    // Prepare must precede this call after any world input/residency change.
    void EnsureChunk(const World& world, ChunkCoord position);
    void Rebuild(const World& world);
    // Value reads require ensured resident output; misuse fails fast.
    std::uint8_t Get(TileCoord position) const;
    // Face order: +X, -X, +Z, -Z, +Y, -Y. Direct incident light only.
    std::uint8_t GetIncident(TileCoord position, int face) const;
    // Reads require EnsureChunk (or Rebuild). Borrowed until the next Prepare.
    eastl::span<const std::uint8_t> GetChunkLevels(ChunkCoord position) const;
    // Invalidation generation, available before EnsureChunk; zero if absent.
    std::uint64_t GetChunkRevision(ChunkCoord position) const;
    // Last Prepare's unique dirty coordinates in unspecified order, including
    // removed chunks and outputs never computed. Borrowed until the next Prepare.
    eastl::span<const ChunkCoord> ChangedChunks() const;
    struct WorkStats {
        std::size_t outputChunks = 0;
        std::size_t skyDirectChunks = 0;
        std::size_t skyFirstChunks = 0; // Bounded sky spill patches (legacy counter name).
        std::size_t sourcePatches = 0;
    };
    // Work since the last Prepare, including explicit dependency computation.
    const WorkStats& GetWorkStats() const;
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
        eastl::array<Faces, tileCount> incident{};
        eastl::array<Faces, tileCount> skyIncident{};
        eastl::array<std::uint32_t, tileCount> terrain{};
        eastl::array<LightEmission, tileCount> emission{};
        std::uint64_t inputRevision = 0;
        std::uint64_t revision = 0;
        bool outputReady = false;
        bool directReady = false;
        bool skyReady = false;
        bool uniformFlags = false;
        bool dirty = false; // Prepare-only invalidation scratch.
        struct Emitter {
            TileCoord position;
            LightEmission emission;
        };
        Vector<Emitter> emitters;
    };
    struct Contribution {
        ChunkCoord chunk{};
        int tile = 0;
        std::uint8_t level = 0;
        Faces incident{};
    };
    struct Source {
        TileCoord position;
        LightEmission emission;
        Vector<Contribution> contribution;
        bool ready = false;
    };
    struct LightColumn {
        eastl::array<LightChunk, world_layer_count> layers;
    };
    struct Storage;
    eastl::unique_ptr<Storage> storage_;
    Vector<Source> sources_;
    Vector<ChunkCoord> changedChunks_;
    struct SkyCell {
        std::uint8_t flags = 0;
        std::uint8_t distance = skySpillDistance + 1;
    };
    // Reused per requested output, never per tile or direct-sky source.
    Vector<SkyCell> skyCells_;
    Vector<int> skyQueue_;
    std::uint64_t generation_ = 0;
    std::uint8_t skyLight_ = 0;
    WorkStats workStats_;
    LightColumn* FindColumn(ColumnCoord position) const;
    LightChunk* FindChunk(ChunkCoord position) const;
    void BuildSource(Source& source);
    void EnsureSkyDirect(LightColumn& column, int layer);
    void BuildSky(LightChunk& chunk);
};
}
