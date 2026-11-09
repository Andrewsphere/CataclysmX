#include "core/lighting.h"
#include "core/world.h"
#include "core/perception.h"
#include "core/planar_visibility.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/utility.h>
#include <cstdlib>

namespace cataclysmx {
namespace {
bool Less(ChunkCoord a, ChunkCoord b) {
    return a.x < b.x || (a.x == b.x && (a.z < b.z || (a.z == b.z && a.y < b.y)));
}
constexpr TileCoord directions[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
constexpr std::uint8_t resident = 1;
constexpr std::uint8_t transparent = 2;
constexpr std::uint8_t openSpace = 4;
constexpr std::uint8_t stairs = 8;
constexpr int tileCount = chunk_size * chunk_size;

template<class OpticalChunk>
int Neighbor(const OpticalChunk& chunk, int chunkIndex, int index, int direction, int& nextIndex) {
    switch (direction) {
    case 0:
        if (index % chunk_size == chunk_size-1) {
            nextIndex = index-(chunk_size-1); return chunk.neighbors[direction];
        }
        nextIndex = index+1; return chunkIndex;
    case 1:
        if (index % chunk_size == 0) {
            nextIndex = index+(chunk_size-1); return chunk.neighbors[direction];
        }
        nextIndex = index-1; return chunkIndex;
    case 2:
        if (index >= tileCount-chunk_size) {
            nextIndex = index-(tileCount-chunk_size); return chunk.neighbors[direction];
        }
        nextIndex = index+chunk_size; return chunkIndex;
    case 3:
        if (index < chunk_size) {
            nextIndex = index+(tileCount-chunk_size); return chunk.neighbors[direction];
        }
        nextIndex = index-chunk_size; return chunkIndex;
    default: nextIndex = index; return chunk.neighbors[direction];
    }
}

bool OpenFace(std::uint8_t lower, std::uint8_t upper) {
    return (upper & openSpace) || ((lower & stairs) && (upper & stairs));
}
bool Connected(std::uint8_t from, std::uint8_t to, int direction) {
    if (!(from & transparent) || !(to & transparent)) { return false; }
    return direction < 4 || (direction == 4 ? OpenFace(from, to) : OpenFace(to, from));
}

// Faces crossed when a center ray enters its target. Bit order matches directions.
unsigned int EntryFaces(TileCoord origin, TileCoord target) {
    const std::int64_t delta[] = {std::int64_t{origin.x}-target.x,
        std::int64_t{origin.z}-target.z, std::int64_t{origin.y}-target.y};
    const auto largest = eastl::max(eastl::max(std::abs(delta[0]), std::abs(delta[1])), std::abs(delta[2]));
    unsigned int mask = 0;
    if (!largest) { return mask; }
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(delta[axis]) == largest) { mask |= 1U << (axis*2 + (delta[axis] < 0 ? 1 : 0)); }
    }
    return mask;
}

struct LocalCell {
    int chunk = -1;
    int tile = 0;
    std::uint8_t flags = 0;
    std::uint8_t direct = 0;
    std::uint8_t first = 0;
    std::uint8_t level = 0;
    eastl::array<std::uint8_t, 6> incident{};
};
}

const char* LightBandName(LightBand band) {
    switch (band) {
    case LightBand::Dark: return "Dark";
    case LightBand::Dim: return "Dim";
    case LightBand::Ordinary: return "Ordinary";
    case LightBand::Bright: return "Bright";
    }
    return "Dark";
}

std::uint8_t TerrainLightLevel(const World& world, TileCoord observer, TileCoord target) {
    if (!ValidPosition(observer) || !ValidPosition(target)) { return 0; }
    const Tile* tile = world.GetTile(target);
    if (!tile || !world.GetTile(observer)) { return 0; }
    const auto& terrain = world.GetContent().GetTerrain(tile->terrain);
    if (!terrain.blocksSight) { return world.GetLightLevel(target); }
    auto level = terrain.light.intensity;
    // Lower terrain is projected through its vertical column, so its receiving
    // surface is the top even when horizontal displacement dominates.
    const auto mask = observer.y > target.y ? (1U << 4) : EntryFaces(observer, target);
    // Incident endpoints belong to faces, never to the observer-independent solid
    // scalar. Do not light-filter the observer-plane air above a projected
    // target. The caller establishes planar/column target visibility separately.
    std::uint8_t incident = 0;
    for (int face = 0; face < 6; ++face) {
        if (!(mask & (1U << face))) { continue; }
        const auto offset = directions[face];
        const TileCoord sample{target.x+offset.x, target.z+offset.z, target.y+offset.y};
        const Tile* sampleTile = world.GetTile(sample);
        if (!sampleTile || world.GetContent().GetTerrain(sampleTile->terrain).blocksSight
            || (face >= 4 && !OpenVerticalFace(world, sample, target))) { return level; }
        incident = eastl::max(incident, world.GetLightLevel(sample));
        incident = eastl::max(incident, world.GetIncidentLightLevel(target, face));
    }
    return eastl::max(level, incident);
}

std::size_t LightingField::FindChunk(ChunkCoord coord) const {
    if (lastChunkLookup_ < chunks_.size() && chunks_[lastChunkLookup_].position == coord) { return lastChunkLookup_; }
    auto found = eastl::lower_bound(chunks_.begin(), chunks_.end(), coord,
        [](const LightChunk& chunk, ChunkCoord key) { return Less(chunk.position, key); });
    if (found == chunks_.end() || found->position != coord) { return chunks_.size(); }
    lastChunkLookup_ = static_cast<std::size_t>(found-chunks_.begin());
    return lastChunkLookup_;
}
std::uint8_t LightingField::Get(TileCoord p) const {
    if (!ValidPosition(p)) { return 0; }
    const auto index = FindChunk(ChunkOf(p));
    return index < chunks_.size() ? chunks_[index].levels[TileIndex(p)] : 0;
}
std::uint8_t LightingField::GetIncident(TileCoord p, int face) const {
    if (!ValidPosition(p) || face < 0 || face >= 6) { return 0; }
    const auto index = FindChunk(ChunkOf(p));
    return index < chunks_.size() ? chunks_[index].incident[TileIndex(p)][face] : 0;
}
eastl::span<const std::uint8_t> LightingField::GetChunkLevels(ChunkCoord coord) const {
    const auto index = FindChunk(coord);
    if (index == chunks_.size()) { return {}; }
    return {chunks_[index].levels.data(), chunks_[index].levels.size()};
}
std::uint64_t LightingField::GetChunkRevision(ChunkCoord coord) const {
    const auto index = FindChunk(coord);
    return index < chunks_.size() ? chunks_[index].revision : 0;
}
eastl::span<const ChunkCoord> LightingField::ChangedChunks() const {
    return {changedChunks_.data(), changedChunks_.size()};
}

void LightingField::BuildSource(Source& source) {
    source.contribution.clear();
    const int r = source.emission.range;
    const int margin = r + lightSpillDistance;
    const int side = margin*2+1;
    constexpr int height = lightSpillDistance*2+1;
    const TileCoord minimum{source.position.x-margin, source.position.z-margin, source.position.y-lightSpillDistance};
    Vector<LocalCell> cells(side*side*height);
    const auto index = [side, minimum](TileCoord p) {
        return ((p.y-minimum.y)*side + p.z-minimum.z)*side + p.x-minimum.x;
    };
    for (int y = 0; y < height; ++y) {
        for (int z = 0; z < side; ++z) {
            for (int x = 0; x < side; ++x) {
                const TileCoord p{minimum.x+x, minimum.z+z, minimum.y+y};
                if (!ValidPosition(p)) { continue; }
                const auto c = FindChunk(ChunkOf(p));
                if (c == chunks_.size()) { continue; }
                auto& cell = cells[index(p)];
                cell.chunk = static_cast<int>(c);
                cell.tile = TileIndex(p);
                cell.flags = chunks_[c].flags[cell.tile];
            }
        }
    }
    Vector<std::uint8_t> optics(side*side, 0);
    Vector<std::uint8_t> visible(side*side, 0);
    const int plane = lightSpillDistance*side*side;
    for (int i = 0; i < side*side; ++i) {
        const auto flags = cells[plane+i].flags;
        optics[i] = (flags & resident) ? static_cast<std::uint8_t>(1 | ((flags & transparent) ? 0 : 2)) : 0;
    }
    BuildPlanarVisibility({optics.data(), optics.size()}, side, side, margin, margin, r,
        {visible.data(), visible.size()});
    for (int z = -r; z <= r; ++z) {
        for (int x = -r; x <= r; ++x) {
            const int squared = x*x+z*z;
            if (squared > r*r) { continue; }
            const TileCoord p{source.position.x+x,source.position.z+z,source.position.y};
            auto& cell = cells[index(p)];
            if (cell.chunk < 0 || !visible[(z+margin)*side+x+margin]) { continue; }
            int distance = 0;
            while (distance*distance < squared) { ++distance; }
            cell.direct = cell.level = static_cast<std::uint8_t>(source.emission.intensity*(r+1-distance)/(r+1));
            if (!(cell.flags & transparent)) {
                const auto mask = EntryFaces(source.position, p);
                for (int face = 0; face < 6; ++face) {
                    if (mask & (1U << face)) { cell.incident[face] = cell.direct; }
                }
            }
        }
    }
    const int strides[] = {1,-1,side,-side,side*side,-side*side};
    // Nonzero seeds are at least two cells inside this box before first step,
    // and at least one inside before second step. No row wrapping is possible.
    for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
        const auto& cell = cells[i];
        if (cell.direct < 4 || !(cell.flags & transparent)) { continue; }
        for (int d = 0; d < 6; ++d) {
            auto& next = cells[i+strides[d]];
            if (Connected(cell.flags, next.flags, d)) { next.first = eastl::max(next.first, static_cast<std::uint8_t>(cell.direct/4)); }
        }
    }
    for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
        auto& cell = cells[i];
        cell.level = eastl::max(cell.level, cell.first);
        if (cell.first < 2) { continue; }
        for (int d = 0; d < 6; ++d) {
            auto& next = cells[i+strides[d]];
            if (Connected(cell.flags, next.flags, d)) { next.level = eastl::max(next.level, static_cast<std::uint8_t>(cell.first/2)); }
        }
    }
    for (const auto& cell : cells) {
        if (cell.level) { source.contribution.push_back({cell.chunk, cell.tile, cell.level, cell.incident}); }
    }
}

void LightingField::BuildSky(eastl::span<const std::uint8_t> dirty) {
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        if (!dirty[c]) { continue; }
        auto& chunk = chunks_[c];
        chunk.skyDirect.fill(0); chunk.firstStep.fill(0);
        if (!skyLight_) { chunk.sky.fill(0); }
        for (auto& face : chunk.skyIncident) { face.fill(0); }
    }
    if (!skyLight_) { return; }
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        if (!dirty[c] || chunks_[c].position.y != world_max_y) { continue; }
        for (int i = 0; i < tileCount; ++i) {
            int cursor = static_cast<int>(c);
            for (;;) {
                auto& current = chunks_[cursor];
                const auto flags = current.flags[i];
                if (!(flags & resident)) { break; }
                current.skyDirect[i] = skyLight_;
                if (!(flags & transparent)) { current.skyIncident[i][4] = skyLight_; break; }
                const int below = current.neighbors[5];
                if (below < 0 || !(chunks_[below].flags[i] & resident)
                    || !OpenFace(chunks_[below].flags[i], flags)) { break; }
                cursor = below;
            }
        }
    }
    const auto firstLevel = static_cast<std::uint8_t>(skyLight_/4);
    const auto secondLevel = static_cast<std::uint8_t>(firstLevel/2);
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        if (!dirty[c]) { continue; }
        auto& chunk = chunks_[c];
        chunk.sky = chunk.skyDirect;
        if (!firstLevel) { continue; }
        for (int i = 0; i < tileCount; ++i) {
            if (chunk.skyDirect[i] || !(chunk.flags[i] & transparent)) { continue; }
            for (int d = 0; d < 6; ++d) {
                int tile;
                const int next = Neighbor(chunk, static_cast<int>(c), i, d, tile);
                if (next < 0 || !chunks_[next].skyDirect[tile]
                    || !Connected(chunk.flags[i], chunks_[next].flags[tile], d)) { continue; }
                chunk.firstStep[i] = firstLevel;
                chunk.sky[i] = firstLevel;
                break; // Every direct sky seed has the same intensity.
            }
        }
    }
    if (!secondLevel) { return; }
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        if (!dirty[c]) { continue; }
        auto& chunk = chunks_[c];
        for (int i = 0; i < tileCount; ++i) {
            // A direct-lit cell needs no firstStep cache: its direct/4 at a
            // connected neighbor dominates any second hop through this cell.
            // Likewise any first-step value already dominates all second steps.
            if (chunk.sky[i] || !(chunk.flags[i] & transparent)) { continue; }
            // All refreshed first steps are complete before this pass. Outside
            // the whole-column, one-chunk halo, cached first steps are unchanged:
            // edits cannot reach that boundary in either of the two tile steps.
            for (int d = 0; d < 6; ++d) {
                int tile;
                const int next = Neighbor(chunk, static_cast<int>(c), i, d, tile);
                if (next < 0 || !chunks_[next].firstStep[tile]
                    || !Connected(chunk.flags[i], chunks_[next].flags[tile], d)) { continue; }
                chunk.sky[i] = secondLevel;
                break;
            }
        }
    }
}

void LightingField::Rebuild(const World& world) {
    ++generation_;
    if (!generation_) { std::abort(); } // Zero is reserved for absence; never reuse generations.
    changedChunks_.clear();
    const auto loaded = world.LoadedChunks();
    bool geometryChanged = loaded.size() != chunks_.size();
    if (!geometryChanged) {
        for (std::size_t c = 0; c < loaded.size(); ++c) {
            if (loaded[c] != chunks_[c].position) { geometryChanged = true; break; }
        }
    }
    if (geometryChanged) {
        for (const auto& chunk : chunks_) { changedChunks_.push_back(chunk.position); }
        chunks_.clear();
        chunks_.reserve(loaded.size());
        for (const auto coord : loaded) { chunks_.push_back({coord}); }
        for (std::size_t c = 0; c < loaded.size(); ++c) {
            for (int d = 0; d < 6; ++d) {
                const auto offset = directions[d];
                const auto coord = loaded[c];
                const auto next = FindChunk({coord.x+offset.x,coord.z+offset.z,coord.y+offset.y});
                chunks_[c].neighbors[d] = next == chunks_.size() ? -1 : static_cast<int>(next);
            }
        }
    }
    Vector<Source> nextSources;
    Vector<ChunkCoord> opticalChanges;
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        auto& optical = chunks_[c];
        const auto coord = loaded[c];
        const auto revision = world.GetLightingInputRevision(coord);
        if (!geometryChanged && revision == optical.inputRevision) {
            for (const auto& emitter : optical.emitters) { nextSources.push_back({emitter.position, emitter.emission, {}}); }
            continue;
        }
        optical.inputRevision = revision;
        optical.emitters.clear();
        const Chunk& chunk = *world.GetChunk(coord);
        bool opticalChanged = false;
        for (int i = 0; i < tileCount; ++i) {
            const TileCoord p{coord.x*chunk_size+i%chunk_size,coord.z*chunk_size+i/chunk_size,coord.y};
            if (optical.terrain[i] != chunk.tiles[i].terrain.value || !optical.flags[i]) {
                const auto& terrain = world.GetContent().GetTerrain(chunk.tiles[i].terrain);
                const std::uint8_t flags = ValidPosition(p) ? resident | (terrain.blocksSight ? 0 : transparent)
                    | (terrain.openSpace ? openSpace : 0) | (terrain.stairs ? stairs : 0) : 0;
                if (flags != optical.flags[i]) { opticalChanged = true; }
                optical.flags[i] = flags;
                optical.terrain[i] = chunk.tiles[i].terrain.value;
                optical.emission[i] = terrain.light;
            }
            if (optical.emission[i].intensity) { optical.emitters.push_back({p,optical.emission[i]}); }
        }
        for (const auto handle : chunk.objects) {
            const auto& object = *world.GetObject(handle);
            if (object.enabled && object.light.intensity) { optical.emitters.push_back({object.position,object.light}); }
        }
        for (const auto handle : chunk.actors) {
            const auto& actor = *world.GetActor(handle);
            if (!actor.parked && actor.light.intensity) { optical.emitters.push_back({actor.position,actor.light}); }
        }
        if (opticalChanged) { opticalChanges.push_back(coord); }
        for (const auto& emitter : optical.emitters) { nextSources.push_back({emitter.position, emitter.emission, {}}); }
    }
    const auto lessSource = [](const Source& a, const Source& b) {
        if (a.position.x != b.position.x) { return a.position.x < b.position.x; }
        if (a.position.z != b.position.z) { return a.position.z < b.position.z; }
        if (a.position.y != b.position.y) { return a.position.y < b.position.y; }
        if (a.emission.range != b.emission.range) { return a.emission.range < b.emission.range; }
        return a.emission.intensity < b.emission.intensity;
    };
    eastl::sort(nextSources.begin(), nextSources.end(), lessSource);
    const bool skyChanged = geometryChanged || skyLight_ != world.GetSkyLight();
    skyLight_ = world.GetSkyLight();
    Vector<std::uint8_t> dirty(chunks_.size(), skyChanged ? 1 : 0);
    if (!skyChanged) {
        for (std::size_t c = 0; c < chunks_.size(); ++c) {
            for (const auto changed : opticalChanges) {
                // Whole vertical columns plus one chunk in X/Z contain both spill steps.
                if (std::abs(chunks_[c].position.x-changed.x) <= 1
                    && std::abs(chunks_[c].position.z-changed.z) <= 1) { dirty[c] = 1; break; }
            }
        }
    }
    if (skyChanged || !opticalChanges.empty()) { BuildSky({dirty.data(), dirty.size()}); }
    const auto affected = [&opticalChanges](const Source& source) {
        const int margin = source.emission.range + lightSpillDistance;
        for (const auto changed : opticalChanges) {
            if (std::abs(changed.y-source.position.y) > lightSpillDistance) { continue; }
            const std::int64_t x = std::int64_t{changed.x}*chunk_size;
            const std::int64_t z = std::int64_t{changed.z}*chunk_size;
            if (x <= std::int64_t{source.position.x}+margin && x+chunk_size-1 >= std::int64_t{source.position.x}-margin
                && z <= std::int64_t{source.position.z}+margin && z+chunk_size-1 >= std::int64_t{source.position.z}-margin) { return true; }
        }
        return false;
    };
    const auto mark = [&dirty](const Source& source) {
        for (const auto& contribution : source.contribution) { dirty[contribution.chunk] = 1; }
    };
    std::size_t old = 0;
    for (auto& source : nextSources) {
        if (!geometryChanged) {
            while (old < sources_.size() && lessSource(sources_[old], source)) { mark(sources_[old++]); }
            if (old < sources_.size() && !lessSource(source, sources_[old])) {
                if (!affected(source)) {
                    source.contribution = eastl::move(sources_[old++].contribution);
                    continue;
                }
                mark(sources_[old++]);
            }
        }
        BuildSource(source);
        mark(source);
    }
    if (!geometryChanged) { while (old < sources_.size()) { mark(sources_[old++]); } }
    sources_ = eastl::move(nextSources);
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        if (!dirty[c]) { continue; }
        chunks_[c].levels = chunks_[c].sky;
        chunks_[c].incident = chunks_[c].skyIncident;
    }
    // Max distributes over both bounded integer spill steps. Independently
    // cached complete source patches therefore combine exactly like one field.
    for (const auto& source : sources_) {
        for (const auto& contribution : source.contribution) {
            if (!dirty[contribution.chunk]) { continue; }
            auto& chunk = chunks_[contribution.chunk];
            auto& level = chunk.levels[contribution.tile];
            level = eastl::max(level, contribution.level);
            for (int d = 0; d < 6; ++d) {
                auto& face = chunk.incident[contribution.tile][d];
                face = eastl::max(face, contribution.incident[d]);
            }
        }
    }
    // Publish only after scalar and directional values have their final maxima.
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        if (!dirty[c]) { continue; }
        chunks_[c].revision = generation_;
        changedChunks_.push_back(chunks_[c].position);
    }
    if (geometryChanged) {
        eastl::sort(changedChunks_.begin(), changedChunks_.end(), Less);
        changedChunks_.erase(eastl::unique(changedChunks_.begin(), changedChunks_.end()), changedChunks_.end());
    }
}
}
