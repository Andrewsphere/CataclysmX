#include "core/lighting.h"
#include "core/world.h"
#include "core/perception.h"
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
    const auto mask = EntryFaces(observer, target);
    // Incident endpoints belong to faces, never to the observer-independent solid
    // scalar. Do not cast rays to sample centers: their floor ties can differ
    // from the genuine target ray. The caller establishes target visibility.
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

void LightingField::BuildSource(Source& source) {
    source.contribution.clear();
    const int r = source.emission.range;
    const int margin = r + lightSpillDistance;
    const int side = margin*2+1;
    const TileCoord minimum{source.position.x-margin, source.position.z-margin, source.position.y-margin};
    Vector<LocalCell> cells(side*side*side);
    const auto index = [side, minimum](TileCoord p) {
        return ((p.y-minimum.y)*side + p.z-minimum.z)*side + p.x-minimum.x;
    };
    const auto flags = [&cells, &index, minimum, side](TileCoord p) -> std::uint8_t {
        if (p.x < minimum.x || p.x >= minimum.x+side || p.z < minimum.z || p.z >= minimum.z+side
            || p.y < minimum.y || p.y >= minimum.y+side) { return 0; }
        return cells[index(p)].flags;
    };
    for (int y = 0; y < side; ++y) {
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
    // The same conservative integer supercover as LineVisible, over the bounded
    // packed source neighborhood. No World/chunk/content lookups per ray voxel.
    const auto visible = [&flags](TileCoord origin, TileCoord target) {
        const int delta[] = {std::abs(target.x-origin.x), std::abs(target.z-origin.z), std::abs(target.y-origin.y)};
        const int step[] = {target.x > origin.x ? 1 : -1, target.z > origin.z ? 1 : -1, target.y > origin.y ? 1 : -1};
        int numerator[] = {1,1,1};
        TileCoord cursor = origin;
        for (;;) {
            const auto current = flags(cursor);
            if (!(current & resident)) { return false; }
            if (cursor == target) { return true; }
            if (cursor != origin && !(current & transparent)) { return false; }
            int axis = -1;
            for (int i = 0; i < 3; ++i) {
                if (delta[i] && (axis < 0 || numerator[i]*delta[axis] < numerator[axis]*delta[i])) { axis = i; }
            }
            if (axis < 0) { std::abort(); }
            unsigned int mask = 0;
            for (int i = 0; i < 3; ++i) {
                if (delta[i] && numerator[i]*delta[axis] == numerator[axis]*delta[i]) { mask |= 1U << i; }
            }
            for (unsigned int subset = 1; subset <= 7; ++subset) {
                if ((subset & mask) != subset) { continue; }
                const TileCoord touched{cursor.x+((subset & 1) ? step[0] : 0),
                    cursor.z+((subset & 2) ? step[1] : 0), cursor.y+((subset & 4) ? step[2] : 0)};
                const auto touchedFlags = flags(touched);
                if (!(touchedFlags & resident)) { return false; }
                if (mask & 4) {
                    TileCoord other = touched;
                    other.y += (subset & 4) ? -step[2] : step[2];
                    const auto otherFlags = flags(other);
                    if (!(otherFlags & resident) || !(touched.y > other.y
                        ? OpenFace(otherFlags, touchedFlags) : OpenFace(touchedFlags, otherFlags))) { return false; }
                }
                if (touched != target && !(touchedFlags & transparent)) { return false; }
            }
            if (mask & 4) {
                const auto next = flags({cursor.x,cursor.z,cursor.y+step[2]});
                if (!(next & resident) || !(step[2] > 0 ? OpenFace(current, next) : OpenFace(next, current))) { return false; }
            }
            if (mask & 1) { cursor.x += step[0]; numerator[0] += 2; }
            if (mask & 2) { cursor.z += step[1]; numerator[1] += 2; }
            if (mask & 4) { cursor.y += step[2]; numerator[2] += 2; }
        }
    };
    for (int y = -r; y <= r; ++y) {
        for (int z = -r; z <= r; ++z) {
            for (int x = -r; x <= r; ++x) {
                const int squared = x*x+z*z+y*y;
                if (squared > r*r) { continue; }
                const TileCoord p{source.position.x+x,source.position.z+z,source.position.y+y};
                auto& cell = cells[index(p)];
                if (cell.chunk < 0 || !visible(source.position, p)) { continue; }
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

void LightingField::BuildSky() {
    for (auto& chunk : chunks_) {
        chunk.sky.fill(0); chunk.firstStep.fill(0);
        for (auto& face : chunk.skyIncident) { face.fill(0); }
    }
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        if (chunks_[c].position.y != world_max_y) { continue; }
        for (int i = 0; i < tileCount; ++i) {
            int cursor = static_cast<int>(c);
            for (;;) {
                auto& current = chunks_[cursor];
                const auto flags = current.flags[i];
                if (!(flags & resident)) { break; }
                current.sky[i] = skyLight_;
                if (!(flags & transparent)) { current.skyIncident[i][4] = skyLight_; break; }
                const int below = current.neighbors[5];
                if (below < 0 || !(chunks_[below].flags[i] & resident)
                    || !OpenFace(chunks_[below].flags[i], flags)) { break; }
                cursor = below;
            }
        }
    }
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        const auto& chunk = chunks_[c];
        for (int i = 0; i < tileCount; ++i) {
            if (chunk.sky[i] < 4 || !(chunk.flags[i] & transparent)) { continue; }
            for (int d = 0; d < 6; ++d) {
                int tile;
                const int next = Neighbor(chunk, static_cast<int>(c), i, d, tile);
                if (next < 0 || !Connected(chunk.flags[i], chunks_[next].flags[tile], d)) { continue; }
                auto& first = chunks_[next].firstStep[tile];
                first = eastl::max(first, static_cast<std::uint8_t>(chunk.sky[i]/4));
            }
        }
    }
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        auto& chunk = chunks_[c];
        for (int i = 0; i < tileCount; ++i) {
            const auto seed = chunk.firstStep[i];
            chunk.sky[i] = eastl::max(chunk.sky[i], seed);
            if (seed < 2) { continue; }
            for (int d = 0; d < 6; ++d) {
                int tile;
                const int next = Neighbor(chunk, static_cast<int>(c), i, d, tile);
                if (next < 0 || !Connected(chunk.flags[i], chunks_[next].flags[tile], d)) { continue; }
                auto& level = chunks_[next].sky[tile];
                level = eastl::max(level, static_cast<std::uint8_t>(seed/2));
            }
        }
    }
}

void LightingField::Rebuild(const World& world) {
    const auto loaded = world.LoadedChunks();
    bool geometryChanged = loaded.size() != chunks_.size();
    if (!geometryChanged) {
        for (std::size_t c = 0; c < loaded.size(); ++c) {
            if (loaded[c] != chunks_[c].position) { geometryChanged = true; break; }
        }
    }
    if (geometryChanged) {
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
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        auto& optical = chunks_[c];
        const Chunk& chunk = *world.GetChunk(loaded[c]);
        const auto coord = loaded[c];
        for (int i = 0; i < tileCount; ++i) {
            const TileCoord p{coord.x*chunk_size+i%chunk_size,coord.z*chunk_size+i/chunk_size,coord.y};
            if (optical.terrain[i] != chunk.tiles[i].terrain.value || !optical.flags[i]) {
                const auto& terrain = world.GetContent().GetTerrain(chunk.tiles[i].terrain);
                const std::uint8_t flags = ValidPosition(p) ? resident | (terrain.blocksSight ? 0 : transparent)
                    | (terrain.openSpace ? openSpace : 0) | (terrain.stairs ? stairs : 0) : 0;
                if (flags != optical.flags[i]) { geometryChanged = true; }
                optical.flags[i] = flags;
                optical.terrain[i] = chunk.tiles[i].terrain.value;
                optical.emission[i] = terrain.light;
            }
            if (optical.emission[i].intensity) { nextSources.push_back({p,optical.emission[i],{}}); }
        }
        for (const auto handle : chunk.objects) {
            const auto& object = *world.GetObject(handle);
            if (object.enabled && object.light.intensity) { nextSources.push_back({object.position,object.light,{}}); }
        }
        for (const auto handle : chunk.actors) {
            const auto& actor = *world.GetActor(handle);
            if (!actor.parked && actor.light.intensity) { nextSources.push_back({actor.position,actor.light,{}}); }
        }
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
    if (skyChanged) { BuildSky(); }
    Vector<std::uint8_t> dirty(chunks_.size(), skyChanged ? 1 : 0);
    const auto mark = [&dirty](const Source& source) {
        for (const auto& contribution : source.contribution) { dirty[contribution.chunk] = 1; }
    };
    std::size_t old = 0;
    for (auto& source : nextSources) {
        if (!geometryChanged) {
            while (old < sources_.size() && lessSource(sources_[old], source)) { mark(sources_[old++]); }
            if (old < sources_.size() && !lessSource(source, sources_[old])) {
                source.contribution = eastl::move(sources_[old++].contribution);
                continue;
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
}
}
