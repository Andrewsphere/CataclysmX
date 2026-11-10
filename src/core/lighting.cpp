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
static_assert(skySpillDistance >= 1 && skySpillDistance < chunk_size); // One horizontal chunk halo bounds invalidation.

std::uint8_t SkySpillLevel(std::uint8_t skyLight, int distance) {
    // Called for distances 1..skySpillDistance. Keep the first step at two-thirds
    // intensity and fade across the configured reach, with only one integer division.
    return static_cast<std::uint8_t>(2*skyLight*(skySpillDistance+1-distance)/(3*skySpillDistance));
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
    auto mask = observer.y > target.y ? (1U << 4) : EntryFaces(observer, target);
    if (observer.y == target.y) {
        // A shallow view can see an exposed facade even when the center ray's
        // dominant entry face points into the next solid tile along the wall.
        mask = 0;
        if (observer.x != target.x) { mask |= 1U << (observer.x > target.x ? 0 : 1); }
        if (observer.z != target.z) { mask |= 1U << (observer.z > target.z ? 2 : 3); }
    }
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
            || (face >= 4 && !OpenVerticalFace(world, sample, target))) {
            if (observer.y == target.y) { continue; }
            return level;
        }
        incident = eastl::max(incident, world.GetLightLevel(sample));
        incident = eastl::max(incident, world.GetIncidentLightLevel(target, face));
    }
    return eastl::max(level, incident);
}

std::size_t LightingField::FindChunk(ChunkCoord coord) const {
    if (lastChunkLookup_ < chunks_.size() && chunks_[lastChunkLookup_]->position == coord) { return lastChunkLookup_; }
    auto found = eastl::lower_bound(chunks_.begin(), chunks_.end(), coord,
        [](const eastl::unique_ptr<LightChunk>& chunk, ChunkCoord key) { return Less(chunk->position, key); });
    if (found == chunks_.end() || (*found)->position != coord) { return chunks_.size(); }
    lastChunkLookup_ = static_cast<std::size_t>(found-chunks_.begin());
    return lastChunkLookup_;
}
std::uint8_t LightingField::Get(TileCoord p) const {
    if (!ValidPosition(p)) { return 0; }
    const auto index = FindChunk(ChunkOf(p));
    if (index == chunks_.size()) { return 0; }
    if (!chunks_[index]->outputReady) { std::abort(); }
    return chunks_[index]->levels[TileIndex(p)];
}
std::uint8_t LightingField::GetIncident(TileCoord p, int face) const {
    if (!ValidPosition(p) || face < 0 || face >= 6) { return 0; }
    const auto index = FindChunk(ChunkOf(p));
    if (index == chunks_.size()) { return 0; }
    if (!chunks_[index]->outputReady) { std::abort(); }
    return chunks_[index]->incident[TileIndex(p)][face];
}
eastl::span<const std::uint8_t> LightingField::GetChunkLevels(ChunkCoord coord) const {
    const auto index = FindChunk(coord);
    if (index == chunks_.size()) { return {}; }
    if (!chunks_[index]->outputReady) { std::abort(); }
    return {chunks_[index]->levels.data(), chunks_[index]->levels.size()};
}
std::uint64_t LightingField::GetChunkRevision(ChunkCoord coord) const {
    const auto index = FindChunk(coord);
    return index < chunks_.size() ? chunks_[index]->revision : 0;
}
eastl::span<const ChunkCoord> LightingField::ChangedChunks() const {
    return {changedChunks_.data(), changedChunks_.size()};
}

void LightingField::BuildSource(Source& source) {
    ++workStats_.sourcePatches;
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
                cell.flags = chunks_[c]->flags[cell.tile];
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
        if (cell.level) { source.contribution.push_back({chunks_[cell.chunk]->position, cell.tile, cell.level, cell.incident}); }
    }
    source.ready = true;
}

void LightingField::EnsureSkyDirect(std::size_t c) {
    auto& chunk = *chunks_[c];
    if (chunk.directReady) { return; }
    ++workStats_.skyDirectChunks;
    chunk.skyDirect.fill(0);
    for (auto& faces : chunk.skyIncident) { faces.fill(0); }
    chunk.directReady = true;
    if (!skyLight_) { return; }
    const int above = chunk.neighbors[4];
    if (chunk.position.y != world_max_y && above < 0) { return; }
    if (above >= 0 && chunks_[above]->uniformFlags) {
        const auto upperFlags = chunks_[above]->flags[0];
        if (!(upperFlags & transparent)
            || (chunk.uniformFlags && !OpenFace(chunk.flags[0], upperFlags))) { return; }
    }
    if (above >= 0) { EnsureSkyDirect(static_cast<std::size_t>(above)); }
    // Uniform cells permit whole-array fills (including solid sky endpoints).
    if (chunk.uniformFlags && (chunk.flags[0] & resident)) {
        bool all = chunk.position.y == world_max_y;
        if (!all && above >= 0) {
            const auto& upper = *chunks_[above];
            all = upper.uniformFlags && (upper.flags[0] & transparent)
                && OpenFace(chunk.flags[0], upper.flags[0])
                && eastl::all_of(upper.skyDirect.begin(), upper.skyDirect.end(),
                    [this](std::uint8_t level) { return level == skyLight_; });
        }
        if (all) {
            chunk.skyDirect.fill(skyLight_);
            if (!(chunk.flags[0] & transparent)) {
                for (auto& faces : chunk.skyIncident) { faces[4] = skyLight_; }
            }
            return;
        }
    }
    for (int i = 0; i < tileCount; ++i) {
        if (!(chunk.flags[i] & resident)) { continue; }
        if (chunk.position.y != world_max_y) {
            const auto& upper = *chunks_[above];
            if (!upper.skyDirect[i] || !(upper.flags[i] & transparent)
                || !OpenFace(chunk.flags[i], upper.flags[i])) { continue; }
        }
        chunk.skyDirect[i] = skyLight_;
        if (!(chunk.flags[i] & transparent)) { chunk.skyIncident[i][4] = skyLight_; }
    }
}

void LightingField::BuildSky(std::size_t c) {
    auto& chunk = *chunks_[c];
    if (chunk.skyReady) { return; }
    EnsureSkyDirect(c);
    chunk.sky = chunk.skyDirect;
    chunk.skyReady = true;
    if (SkySpillLevel(skyLight_, 1) == 0 || (chunk.uniformFlags && !(chunk.flags[0] & transparent))) { return; }
    bool needsSpill = false;
    for (int i = 0; i < tileCount; ++i) {
        if (!chunk.skyDirect[i] && (chunk.flags[i] & transparent)) { needsSpill = true; break; }
    }
    if (!needsSpill) { return; }

    ++workStats_.skyFirstChunks;
    int reach = skySpillDistance;
    while (SkySpillLevel(skyLight_, reach) == 0) { --reach; }
    const int side = chunk_size + 2*reach;
    const int height = 2*reach+1;
    const TileCoord minimum{chunk.position.x*chunk_size-reach,
        chunk.position.z*chunk_size-reach,chunk.position.y-reach};
    const auto index = [side, minimum](int x, int z, int y) {
        return ((y-minimum.y)*side+z-minimum.z)*side+x-minimum.x;
    };
    skyCells_.clear();
    skyCells_.resize(side*side*height);
    skyQueue_.clear();
    skyQueue_.reserve(skyCells_.size());
    const auto first = ChunkOf(minimum);
    const auto last = ChunkOf({minimum.x+side-1,minimum.z+side-1,minimum.y+height-1});
    // Copy by chunk, not by tile lookup. Only direct caches are dependencies;
    // no neighboring output is finalized and no transported value is a seed.
    for (int x = first.x; x <= last.x; ++x) {
        for (int z = first.z; z <= last.z; ++z) {
            for (int y = eastl::max(first.y, world_min_y); y <= eastl::min(last.y, world_max_y); ++y) {
                const auto neighbor = FindChunk({x,z,y});
                if (neighbor == chunks_.size()) { continue; }
                EnsureSkyDirect(neighbor);
                const auto& input = *chunks_[neighbor];
                for (int tz = eastl::max(z*chunk_size,minimum.z);
                     tz < eastl::min((z+1)*chunk_size,minimum.z+side); ++tz) {
                    for (int tx = eastl::max(x*chunk_size,minimum.x);
                         tx < eastl::min((x+1)*chunk_size,minimum.x+side); ++tx) {
                        const int tile = (tz-z*chunk_size)*chunk_size+tx-x*chunk_size;
                        const int i = index(tx,tz,y);
                        auto& cell = skyCells_[i];
                        cell.flags = input.flags[tile];
                        if (input.skyDirect[tile] && (cell.flags & transparent)) {
                            cell.distance = 0;
                            skyQueue_.push_back(i);
                        }
                    }
                }
            }
        }
    }
    // Multi-source FIFO visits each cell at most once, at its shortest connected
    // path distance. Any <= reach path to the output fits inside this halo.
    const int strides[] = {1,-1,side,-side,side*side,-side*side};
    for (std::size_t head = 0; head < skyQueue_.size(); ++head) {
        const int i = skyQueue_[head];
        const auto cell = skyCells_[i];
        if (cell.distance >= reach) { continue; }
        const int x = i%side;
        const int z = i/side%side;
        const int y = i/(side*side);
        const bool boundary[] = {x == side-1,x == 0,z == side-1,z == 0,y == height-1,y == 0};
        for (int d = 0; d < 6; ++d) {
            if (boundary[d]) { continue; }
            const int nextIndex = i+strides[d];
            auto& next = skyCells_[nextIndex];
            if (next.distance <= reach || !Connected(cell.flags,next.flags,d)) { continue; }
            next.distance = static_cast<std::uint8_t>(cell.distance+1);
            skyQueue_.push_back(nextIndex);
        }
    }
    for (int i = 0; i < tileCount; ++i) {
        if (chunk.sky[i]) { continue; }
        const int distance = skyCells_[index(chunk.position.x*chunk_size+i%chunk_size,
            chunk.position.z*chunk_size+i/chunk_size,chunk.position.y)].distance;
        if (distance >= 1 && distance <= reach) {
            chunk.sky[i] = SkySpillLevel(skyLight_, distance);
        }
    }
}

namespace {
bool SourceTouches(TileCoord position, LightEmission emission, ChunkCoord chunk) {
    const int margin = emission.range + lightSpillDistance;
    const std::int64_t x = std::int64_t{chunk.x}*chunk_size;
    const std::int64_t z = std::int64_t{chunk.z}*chunk_size;
    return std::abs(chunk.y-position.y) <= lightSpillDistance
        && x <= std::int64_t{position.x}+margin && x+chunk_size-1 >= std::int64_t{position.x}-margin
        && z <= std::int64_t{position.z}+margin && z+chunk_size-1 >= std::int64_t{position.z}-margin;
}
}

void LightingField::Prepare(const World& world) {
    ++generation_;
    if (!generation_) { std::abort(); } // Zero is reserved for absence; never reuse generations.
    changedChunks_.clear();
    workStats_ = {};
    const auto loaded = world.LoadedChunks();
    bool geometryChanged = loaded.size() != chunks_.size();
    if (!geometryChanged) {
        for (std::size_t c = 0; c < loaded.size(); ++c) {
            if (loaded[c] != chunks_[c]->position) { geometryChanged = true; break; }
        }
    }
    Vector<ChunkCoord> opticalChanges;
    if (geometryChanged) {
        Vector<eastl::unique_ptr<LightChunk>> next;
        next.reserve(loaded.size());
        std::size_t old = 0;
        for (const auto coord : loaded) {
            while (old < chunks_.size() && Less(chunks_[old]->position, coord)) {
                opticalChanges.push_back(chunks_[old]->position);
                changedChunks_.push_back(chunks_[old++]->position);
            }
            if (old < chunks_.size() && chunks_[old]->position == coord) {
                next.push_back(eastl::move(chunks_[old++]));
            } else {
                eastl::unique_ptr<LightChunk> chunk(new LightChunk{});
                chunk->position = coord;
                next.push_back(eastl::move(chunk));
                opticalChanges.push_back(coord);
            }
        }
        while (old < chunks_.size()) {
            opticalChanges.push_back(chunks_[old]->position);
            changedChunks_.push_back(chunks_[old++]->position);
        }
        chunks_ = eastl::move(next);
        lastChunkLookup_ = chunks_.size();
        for (std::size_t c = 0; c < loaded.size(); ++c) {
            for (int d = 0; d < 6; ++d) {
                const auto offset = directions[d];
                const auto coord = loaded[c];
                const auto neighbor = FindChunk({coord.x+offset.x,coord.z+offset.z,coord.y+offset.y});
                chunks_[c]->neighbors[d] = neighbor == chunks_.size() ? -1 : static_cast<int>(neighbor);
            }
        }
    }
    Vector<Source> nextSources;
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        auto& optical = *chunks_[c];
        const auto coord = loaded[c];
        const auto revision = world.GetLightingInputRevision(coord);
        if (revision == optical.inputRevision) {
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
        optical.uniformFlags = eastl::all_of(optical.flags.begin(), optical.flags.end(),
            [&optical](std::uint8_t flags) { return flags == optical.flags[0]; });
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
    const bool skyChanged = skyLight_ != world.GetSkyLight();
    skyLight_ = world.GetSkyLight();
    Vector<std::uint8_t> dirty(chunks_.size(), skyChanged ? 1 : 0);
    if (!skyChanged) {
        Vector<ChunkCoord> columns;
        for (const auto changed : opticalChanges) { columns.push_back({changed.x,changed.z,0}); }
        eastl::sort(columns.begin(), columns.end(), Less);
        columns.erase(eastl::unique(columns.begin(), columns.end()), columns.end());
        for (const auto column : columns) {
            // Whole columns plus one horizontal chunk contain the configured sky steps
            // (skySpillDistance < chunk_size), including diagonal paths.
            // Deduplicate Y first: initial residency must not be a quadratic scan.
            for (int x = column.x-1; x <= column.x+1; ++x) {
                for (int z = column.z-1; z <= column.z+1; ++z) {
                    auto found = eastl::lower_bound(chunks_.begin(), chunks_.end(), ChunkCoord{x,z,world_min_y},
                        [](const eastl::unique_ptr<LightChunk>& chunk, ChunkCoord key) { return Less(chunk->position,key); });
                    while (found != chunks_.end() && (*found)->position.x == x && (*found)->position.z == z) {
                        dirty[static_cast<std::size_t>(found-chunks_.begin())] = 1;
                        ++found;
                    }
                }
            }
        }
    }
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        if (dirty[c]) { chunks_[c]->directReady = false; chunks_[c]->skyReady = false; }
        if (!chunks_[c]->revision) { dirty[c] = 1; }
    }
    const auto affected = [&opticalChanges](const Source& source) {
        for (const auto changed : opticalChanges) {
            if (SourceTouches(source.position, source.emission, changed)) { return true; }
        }
        return false;
    };
    const auto mark = [this, &dirty](const Source& source) {
        // Bounds, not cached contributions: an unrequested output must publish
        // invalidation before an observer compares its own cached revision.
        const int margin = source.emission.range + lightSpillDistance;
        const auto minimum = ChunkOf({source.position.x-margin,source.position.z-margin,source.position.y-lightSpillDistance});
        const auto maximum = ChunkOf({source.position.x+margin,source.position.z+margin,source.position.y+lightSpillDistance});
        for (int x = minimum.x; x <= maximum.x; ++x) {
            for (int z = minimum.z; z <= maximum.z; ++z) {
                for (int y = minimum.y; y <= maximum.y; ++y) {
                    const auto c = FindChunk({x,z,y});
                    if (c < chunks_.size()) { dirty[c] = 1; }
                }
            }
        }
    };
    std::size_t old = 0;
    for (auto& source : nextSources) {
        while (old < sources_.size() && lessSource(sources_[old], source)) { mark(sources_[old++]); }
        if (old < sources_.size() && !lessSource(source, sources_[old])) {
            if (!affected(source)) {
                source.contribution = eastl::move(sources_[old].contribution);
                source.ready = sources_[old++].ready;
                continue;
            }
            mark(sources_[old++]);
        }
        mark(source);
    }
    while (old < sources_.size()) { mark(sources_[old++]); }
    sources_ = eastl::move(nextSources);
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
        if (!dirty[c]) { continue; }
        chunks_[c]->outputReady = false;
        chunks_[c]->revision = generation_;
        changedChunks_.push_back(chunks_[c]->position);
    }
    eastl::sort(changedChunks_.begin(), changedChunks_.end(), Less);
    changedChunks_.erase(eastl::unique(changedChunks_.begin(), changedChunks_.end()), changedChunks_.end());
}

void LightingField::EnsureChunk(const World& world, ChunkCoord position) {
    // World is explicit so callers follow the same Prepare/Ensure protocol as
    // Rebuild; no pointer to a World or resident object survives either call.
    (void)world;
    const auto c = FindChunk(position);
    if (c == chunks_.size() || chunks_[c]->outputReady) { return; }
    auto& chunk = *chunks_[c];
    BuildSky(c);
    chunk.levels = chunk.sky;
    chunk.incident = chunk.skyIncident;
    // Max distributes over both bounded integer spill steps. Independently
    // cached complete source patches therefore combine exactly like one field.
    for (auto& source : sources_) {
        if (!SourceTouches(source.position, source.emission, position)) { continue; }
        if (!source.ready) { BuildSource(source); }
        for (const auto& contribution : source.contribution) {
            if (contribution.chunk != position) { continue; }
            auto& level = chunk.levels[contribution.tile];
            level = eastl::max(level, contribution.level);
            for (int d = 0; d < 6; ++d) {
                auto& face = chunk.incident[contribution.tile][d];
                face = eastl::max(face, contribution.incident[d]);
            }
        }
    }
    chunk.outputReady = true;
    ++workStats_.outputChunks;
}

const LightingField::WorkStats& LightingField::GetWorkStats() const { return workStats_; }

void LightingField::Rebuild(const World& world) {
    Prepare(world);
    for (const auto coord : world.LoadedChunks()) { EnsureChunk(world, coord); }
}
}
