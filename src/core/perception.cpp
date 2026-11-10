#include "core/perception.h"
#include "core/planar_visibility.h"
#include "core/world.h"
#include "core/lighting.h"

#include <EASTL/algorithm.h>
#include <ankerl/unordered_dense.h>
#include <cstdlib>

namespace cataclysmx {

bool OpenVerticalFace(const World& world, TileCoord first, TileCoord second) {
    const Tile* a = world.GetTile(first);
    const Tile* b = world.GetTile(second);
    if (!a || !b || first.x != second.x || first.z != second.z
        || std::abs(first.y - second.y) != 1) { return false; }
    const auto& upper = world.GetContent().GetTerrain((first.y > second.y ? a : b)->terrain);
    const auto& lower = world.GetContent().GetTerrain((first.y > second.y ? b : a)->terrain);
    return upper.openSpace || (upper.stairs && lower.stairs);
}

bool LineVisible(const World& world, TileCoord origin, TileCoord target) {
    world.RecordPointLos(false);
    if (!ValidPosition(origin) || !ValidPosition(target) || target.y > origin.y) { return false; }
    const std::int64_t dx = std::int64_t{target.x} - origin.x;
    const std::int64_t dz = std::int64_t{target.z} - origin.z;
    if (dx * dx + dz * dz > playerSightRange * playerSightRange) { return false; }
    int radius = 0;
    while (radius * radius < dx * dx + dz * dz) { ++radius; }
    const int width = radius * 2 + 1;
    const int startX = origin.x - radius;
    const int startZ = origin.z - radius;
    Vector<std::uint8_t> optics(static_cast<std::size_t>(width) * width, 0);
    Vector<std::uint8_t> visible(optics.size(), 0);
    const auto first = ChunkOf({startX, startZ, origin.y});
    const auto last = ChunkOf({origin.x + radius, origin.z + radius, origin.y});
    // Copy clipped chunk rows on this plane only. In particular, do not enumerate
    // resident chunks or snapshot/project other elevations for a point query.
    for (int cz = first.z; cz <= last.z; ++cz) {
        for (int cx = first.x; cx <= last.x; ++cx) {
            const Chunk* chunk = world.GetChunk({cx, cz, origin.y});
            if (!chunk) { continue; }
            const int x0 = eastl::max(eastl::max(startX, cx * chunk_size), -world_coordinate_limit);
            const int x1 = eastl::min(eastl::min(origin.x + radius, (cx + 1) * chunk_size - 1), world_coordinate_limit);
            const int z0 = eastl::max(eastl::max(startZ, cz * chunk_size), -world_coordinate_limit);
            const int z1 = eastl::min(eastl::min(origin.z + radius, (cz + 1) * chunk_size - 1), world_coordinate_limit);
            TerrainId lastTerrain{};
            bool haveTerrain = false;
            std::uint8_t flags = 0;
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    world.RecordPointLos(true);
                    const auto terrain = chunk->tiles[(z - cz * chunk_size) * chunk_size + x - cx * chunk_size].terrain;
                    if (!haveTerrain || terrain != lastTerrain) {
                        lastTerrain = terrain;
                        haveTerrain = true;
                        flags = static_cast<std::uint8_t>(1
                            | (world.GetContent().GetTerrain(terrain).blocksSight ? 2 : 0));
                    }
                    optics[(z - startZ) * width + x - startX] = flags;
                }
            }
        }
    }
    std::uint64_t visits = 0;
    BuildPlanarVisibility({optics.data(), optics.size()}, width, width, radius, radius, radius,
        {visible.data(), visible.size()}, &visits);
    world.RecordPointLos(true, visits);
    if (!visible[(target.z - startZ) * width + target.x - startX]) { return false; }
    if (target.y == origin.y) { return true; }
    TileCoord cursor{target.x, target.z, origin.y};
    for (;;) {
        world.RecordPointLos(true);
        const Tile* tile = world.GetTile(cursor);
        if (!tile) { return false; }
        if (cursor.y == target.y) { return true; }
        if (!world.GetContent().GetTerrain(tile->terrain).openSpace) { return false; }
        --cursor.y;
    }
}

int VisionRange(const VisionProfile& vision, LightBand targetBand, std::uint8_t observerLight) {
    int range = vision.range;
    if (targetBand == LightBand::Dark) { range = vision.darkRange; }
    if (targetBand == LightBand::Dim) { range = vision.dimRange; }
    if ((targetBand == LightBand::Dark || targetBand == LightBand::Dim)
        && observerLight >= vision.glareThreshold && range > vision.glareDimRange) {
        range = vision.glareDimRange;
    }
    return range;
}

bool TerrainWithinVision(const World& world, TileCoord observer, TileCoord target,
    const VisionProfile& vision) {
    if (!ValidVisionProfile(vision)) { std::abort(); }
    if (!ValidPosition(observer) || !ValidPosition(target) || target.y > observer.y) { return false; }
    const std::int64_t dx = std::int64_t{target.x} - observer.x;
    const std::int64_t dz = std::int64_t{target.z} - observer.z;
    const auto squared = dx * dx + dz * dz;
    if (squared > vision.range * vision.range) { return false; }
    const auto band = ClassifyLight(TerrainLightLevel(world, observer, target));
    const int range = VisionRange(vision, band, world.GetLightLevel(observer));
    return squared <= range * range;
}

bool CanSeeTile(const World& world, TileCoord observer, TileCoord target,
    const VisionProfile& vision) {
    return TerrainWithinVision(world, observer, target, vision)
        && LineVisible(world, observer, target);
}

namespace {
OpticalChunk SnapshotChunk(const World& world, ChunkCoord coord, const Chunk& chunk) {
    OpticalChunk snapshot;
    snapshot.position = coord;
    snapshot.transparent = true;
    TerrainId previous{};
    std::uint8_t flags = 0;
    for (int i = 0; i < chunk_size * chunk_size; ++i) {
        if (chunk.tiles[i].terrain != previous) {
            previous = chunk.tiles[i].terrain;
            const auto& terrain = world.GetContent().GetTerrain(previous);
            flags = static_cast<std::uint8_t>(1 | (terrain.blocksSight ? 2 : 0)
                | (terrain.openSpace ? 4 : 0) | (terrain.stairs ? 8 : 0));
        }
        snapshot.cells[i] = flags;
        if (flags & 2) { snapshot.opaqueRows[i / chunk_size] |= std::uint16_t{1} << (i % chunk_size); }
        snapshot.transparent = snapshot.transparent && !(flags & 2);
    }
    return snapshot;
}

std::uint8_t SampleField(const OpticalField& field, TileCoord position) {
    if (!ValidPosition(position)) { return 0; }
    const auto* chunk = field.Find(ChunkOf(position));
    if (!chunk) { return 0; }
    const int x = position.x - chunk->position.x * chunk_size;
    const int z = position.z - chunk->position.z * chunk_size;
    return chunk->cells[z * chunk_size + x];
}
}

struct OpticalField::Storage {
    struct ChunkHash {
        std::uint64_t operator()(ChunkCoord position) const {
            const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.x)) << 32)
                | static_cast<std::uint32_t>(position.z);
            const auto horizontal = ankerl::unordered_dense::hash<std::uint64_t>{}(key);
            return ankerl::unordered_dense::hash<std::uint64_t>{}(horizontal
                ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.y)) * 0x9e3779b97f4a7c15ULL));
        }
    };
    ankerl::unordered_dense::map<ChunkCoord, OpticalChunk, ChunkHash> chunks;
};

OpticalField::OpticalField() = default;
OpticalField::~OpticalField() = default;
OpticalField::OpticalField(OpticalField&&) noexcept = default;
OpticalField& OpticalField::operator=(OpticalField&&) noexcept = default;

const OpticalChunk* OpticalField::Find(ChunkCoord position) const {
    if (!storage_) { return nullptr; }
    const auto found = storage_->chunks.find(position);
    return found != storage_->chunks.end() ? &found->second : nullptr;
}

void OpticalField::Rebuild(const World& world) {
    ++revision;
    if (!storage_) { storage_.reset(new Storage{}); }
    storage_->chunks.clear();
    for (ChunkCoord coord : world.LoadedChunks()) {
        storage_->chunks.emplace(coord, SnapshotChunk(world, coord, *world.GetChunk(coord)));
    }
}

void OpticalField::Refresh(const World& world, eastl::span<const ChunkCoord> positions) {
    bool changed = false;
    if (!storage_) { storage_.reset(new Storage{}); }
    for (ChunkCoord coord : positions) {
        auto found = storage_->chunks.find(coord);
        const bool present = found != storage_->chunks.end();
        const Chunk* chunk = world.GetChunk(coord);
        if (!chunk) {
            if (present) { storage_->chunks.erase(found); changed = true; }
            continue;
        }
        auto snapshot = SnapshotChunk(world, coord, *chunk);
        if (!present) { storage_->chunks.emplace(coord, eastl::move(snapshot)); changed = true; }
        else if (found->second.cells != snapshot.cells) { found->second = eastl::move(snapshot); changed = true; }
    }
    if (changed) { ++revision; }
}

bool VisibilityWorkspace::Prepare(const OpticalField& field, ChunkCoord first, ChunkCoord last) {
    if (first.y != last.y) { std::abort(); }
    if (source == &field && revision == field.revision && firstChunk == first && lastChunk == last) { return false; }
    const bool sameWindow = source == &field && firstChunk == first && lastChunk == last;
    source = &field;
    revision = field.revision;
    firstChunk = first;
    lastChunk = last;
    width = (last.x - first.x + 1) * chunk_size;
    depth = (last.z - first.z + 1) * chunk_size;
    if (!sameWindow) { optics.assign(static_cast<std::size_t>(width) * depth, 0); }
    bool changed = !sameWindow;
    for (int cz = first.z; cz <= last.z; ++cz) {
        for (int cx = first.x; cx <= last.x; ++cx) {
            const auto* chunk = field.Find({cx, cz, first.y});
            for (int z = 0; z < chunk_size; ++z) {
                for (int x = 0; x < chunk_size; ++x) {
                    const TileCoord p{cx * chunk_size + x, cz * chunk_size + z, first.y};
                    const auto flags = chunk && ValidPosition(p) ? chunk->cells[z * chunk_size + x] : std::uint8_t{0};
                    auto& destination = optics[((cz - first.z) * chunk_size + z) * width
                        + (cx - first.x) * chunk_size + x];
                    if (destination != flags) { destination = flags; changed = true; }
                }
            }
        }
    }
    return changed;
}

std::uint8_t VisibilityWorkspace::Sample(TileCoord p) const {
    const int x = p.x - firstChunk.x * chunk_size;
    const int z = p.z - firstChunk.z * chunk_size;
    if (x < 0 || x >= width || z < 0 || z >= depth || p.y != firstChunk.y) { return 0; }
    return optics[z * width + x];
}

std::size_t VisibilityMask::Index(TileCoord p) const {
    const int width = radius * 2 + 1;
    const int offset = layerOffsets[p.y - minimumY];
    if (offset < 0) { std::abort(); }
    return static_cast<std::size_t>(offset) + (p.z - origin.z + radius) * width + p.x - origin.x + radius;
}

bool VisibilityMask::Contains(TileCoord p) const { return (Get(p) & 1) != 0; }

std::uint8_t VisibilityMask::Get(TileCoord p) const {
    if (p.y < minimumY || p.y > maximumY || std::abs(p.x - origin.x) > radius
        || std::abs(p.z - origin.z) > radius) { return 0; }
    const int layer = p.y - minimumY;
    if (clearLayers[layer]) {
        const int dx = p.x - origin.x;
        const int dz = p.z - origin.z;
        return dx * dx + dz * dz <= radius * radius ? 1 : 0;
    }
    return layerOffsets[layer] < 0 ? 0 : cells[Index(p)];
}

eastl::pair<int, int> VisibilityMask::ClearRow(int z, int y) const {
    if (y < minimumY || y > maximumY || !clearLayers[y - minimumY]) { return {1, 0}; }
    const int dz = z - origin.z;
    if (std::abs(dz) > radius) { return {1, 0}; }
    const int squared = radius * radius - dz * dz;
    int low = 0;
    int high = radius + 1;
    while (low + 1 < high) {
        const int middle = (low + high) / 2;
        if (middle * middle <= squared) { low = middle; }
        else { high = middle; }
    }
    return {origin.x - low, origin.x + low};
}

void VisibilityMask::MaterializeLayer(int y) {
    if (y < minimumY || y > maximumY) { std::abort(); }
    const int layer = y - minimumY;
    if (layerOffsets[layer] >= 0) { return; }
    const int width = radius * 2 + 1;
    layerOffsets[layer] = static_cast<int>(cells.size());
    cells.resize(cells.size() + static_cast<std::size_t>(width) * width, 0);
    if (clearLayers[layer]) {
        for (int z = origin.z - radius; z <= origin.z + radius; ++z) {
            const auto row = ClearRow(z, y);
            if (row.first > row.second) { continue; }
            auto* begin = cells.data() + Index({row.first, z, y});
            eastl::fill(begin, begin + row.second - row.first + 1, std::uint8_t{1});
        }
    }
    clearLayers[layer] = false;
}

void BuildVisibilityMask(const World& world, TileCoord origin, int radius,
    VisibilityMask& output, PerceptionStats& stats, const OpticalField* suppliedOptics,
    VisibilityWorkspace* suppliedWorkspace) {
    if (radius < 0 || radius > playerSightRange || !ValidPosition(origin)) { std::abort(); }
    ++stats.geometryRebuilds;
    output.origin = origin;
    output.radius = radius;
    output.minimumY = world_min_y;
    output.maximumY = origin.y;
    output.cells.clear();
    output.visibleLayers.fill(false);
    output.clearLayers.fill(false);
    output.layerOffsets.fill(-1);
    OpticalField localOptics;
    if (!suppliedOptics) {
        localOptics.Rebuild(world);
        ++stats.opticalRebuilds;
        suppliedOptics = &localOptics;
        suppliedWorkspace = nullptr;
    }
    VisibilityWorkspace localWorkspace;
    auto& workspace = suppliedWorkspace ? *suppliedWorkspace : localWorkspace;
    workspace.projectionSamples = 0;
    const auto first = ChunkOf({origin.x - radius, origin.z - radius, origin.y});
    const auto last = ChunkOf({origin.x + radius, origin.z + radius, origin.y});
    if (workspace.Prepare(*suppliedOptics, first, last)) { ++stats.volumeRebuilds; }
    Vector<std::uint8_t> planar(workspace.optics.size(), 0);
    BuildPlanarVisibility({workspace.optics.data(), workspace.optics.size()}, workspace.width, workspace.depth,
        origin.x - first.x * chunk_size, origin.z - first.z * chunk_size, radius,
        {planar.data(), planar.size()}, &stats.traversedVoxels);
    const auto mark = [&](TileCoord p, std::uint8_t flags) {
        output.MaterializeLayer(p.y);
        output.cells[output.Index(p)] = static_cast<std::uint8_t>(1 | (flags & 2));
        output.visibleLayers[p.y - output.minimumY] = true;
    };
    for (int z = -radius; z <= radius; ++z) {
        for (int x = -radius; x <= radius; ++x) {
            TileCoord p{origin.x + x, origin.z + z, origin.y};
            const int index = (p.z - first.z * chunk_size) * workspace.width + p.x - first.x * chunk_size;
            if (!planar[index]) { continue; }
            auto flags = workspace.Sample(p);
            mark(p, flags);
            while ((flags & 4) && p.y > world_min_y) {
                --p.y;
                ++workspace.projectionSamples;
                flags = SampleField(*suppliedOptics, p);
                if (!(flags & 1)) { break; }
                mark(p, flags);
            }
        }
    }
    stats.projectedCells += workspace.projectionSamples;
    // Retain the implicit-circle fast path for filtering and observation footprints.
    // A layer qualifies only when every circle cell is visible and nonopaque.
    const int width = radius * 2 + 1;
    for (int y = output.minimumY; y <= output.maximumY; ++y) {
        if (!output.visibleLayers[y - output.minimumY]) { continue; }
        bool clear = true;
        for (int z = -radius; z <= radius && clear; ++z) {
            for (int x = -radius; x <= radius; ++x) {
                if (x * x + z * z <= radius * radius && output.Get({origin.x + x, origin.z + z, y}) != 1) {
                    clear = false;
                    break;
                }
            }
        }
        output.clearLayers[y - output.minimumY] = clear;
    }
    Vector<std::uint8_t> packed;
    for (int y = output.minimumY; y <= output.maximumY; ++y) {
        const int layer = y - output.minimumY;
        const int offset = output.layerOffsets[layer];
        if (offset < 0 || output.clearLayers[layer]) { output.layerOffsets[layer] = -1; continue; }
        output.layerOffsets[layer] = static_cast<int>(packed.size());
        packed.insert(packed.end(), output.cells.begin() + offset, output.cells.begin() + offset + width * width);
    }
    output.cells.swap(packed);
}
}
