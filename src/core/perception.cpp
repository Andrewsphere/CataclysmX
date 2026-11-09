#include "core/perception.h"
#include "core/world.h"
#include "core/lighting.h"

#include <EASTL/algorithm.h>

#include <cstdlib>
#include <cstring>

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
    if (!ValidPosition(origin) || !ValidPosition(target)) { return false; }
    const int delta[] = {std::abs(target.x - origin.x), std::abs(target.z - origin.z),
        std::abs(target.y - origin.y)};
    const int step[] = {target.x > origin.x ? 1 : -1, target.z > origin.z ? 1 : -1,
        target.y > origin.y ? 1 : -1};
    int numerator[] = {1, 1, 1};
    TileCoord cursor = origin;
    for (;;) {
        world.RecordPointLos(true);
        const Tile* tile = world.GetTile(cursor);
        if (!tile) { return false; }
        if (cursor == target) { return true; }
        if (cursor != origin && world.GetContent().GetTerrain(tile->terrain).blocksSight) {
            return false;
        }
        int axis = -1;
        for (int i = 0; i < 3; ++i) {
            if (delta[i] != 0 && (axis < 0
                || std::int64_t{numerator[i]} * delta[axis]
                    < std::int64_t{numerator[axis]} * delta[i])) { axis = i; }
        }
        if (axis < 0) { std::abort(); }
        unsigned int mask = 0;
        for (int i = 0; i < 3; ++i) {
            if (delta[i] != 0 && std::int64_t{numerator[i]} * delta[axis]
                == std::int64_t{numerator[axis]} * delta[i]) { mask |= 1U << i; }
        }
        for (unsigned int subset = 1; subset <= 7; ++subset) {
            if ((subset & mask) != subset) { continue; }
            const TileCoord touched{cursor.x + ((subset & 1) ? step[0] : 0),
                cursor.z + ((subset & 2) ? step[1] : 0),
                cursor.y + ((subset & 4) ? step[2] : 0)};
            const Tile* touchedTile = world.GetTile(touched);
            if (!touchedTile) { return false; }
            if (mask & 4) {
                TileCoord other = touched;
                other.y += (subset & 4) ? -step[2] : step[2];
                if (!OpenVerticalFace(world, touched, other)) { return false; }
            }
            if (touched != target && world.GetContent().GetTerrain(touchedTile->terrain).blocksSight) {
                return false;
            }
        }
        if ((mask & 4) && !OpenVerticalFace(world, cursor,
            {cursor.x, cursor.z, cursor.y + step[2]})) { return false; }
        if (mask & 1) { cursor.x += step[0]; numerator[0] += 2; }
        if (mask & 2) { cursor.z += step[1]; numerator[1] += 2; }
        if (mask & 4) { cursor.y += step[2]; numerator[2] += 2; }
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
    if (!ValidPosition(observer) || !ValidPosition(target)) { return false; }
    const std::int64_t dx = target.x - observer.x;
    const std::int64_t dz = target.z - observer.z;
    const std::int64_t dy = target.y - observer.y;
    const auto distanceSquared = dx * dx + dz * dz + dy * dy;
    const auto band = ClassifyLight(TerrainLightLevel(world, observer, target));
    const int range = VisionRange(vision, band, world.GetLightLevel(observer));
    return distanceSquared <= range * range;
}

bool CanSeeTile(const World& world, TileCoord observer, TileCoord target,
    const VisionProfile& vision) {
    return TerrainWithinVision(world, observer, target, vision) && LineVisible(world, observer, target);
}

namespace {
bool OpticalLess(ChunkCoord a, ChunkCoord b) {
    return a.x < b.x || (a.x == b.x && (a.z < b.z || (a.z == b.z && a.y < b.y)));
}
}

const OpticalChunk* OpticalField::Find(ChunkCoord position) const {
    const auto found = eastl::lower_bound(chunks.begin(), chunks.end(), position,
        [](const OpticalChunk& chunk, ChunkCoord key) { return OpticalLess(chunk.position, key); });
    return found != chunks.end() && found->position == position ? &*found : nullptr;
}

void OpticalField::Rebuild(const World& world) {
    ++revision;
    chunks.clear();
    TerrainId lastTerrain{};
    std::uint8_t flags = 0;
    for (ChunkCoord coord : world.LoadedChunks()) {
        OpticalChunk snapshot;
        snapshot.position = coord;
        snapshot.transparent = true;
        const Chunk& chunk = *world.GetChunk(coord);
        for (int i = 0; i < chunk_size * chunk_size; ++i) {
            if (chunk.tiles[i].terrain != lastTerrain) {
                lastTerrain = chunk.tiles[i].terrain;
                const auto& terrain = world.GetContent().GetTerrain(lastTerrain);
                flags = static_cast<std::uint8_t>(1 | (terrain.blocksSight ? 2 : 0)
                    | (terrain.openSpace ? 4 : 0) | (terrain.stairs ? 8 : 0));
            }
            snapshot.cells[i] = flags;
            if (flags & 2) { snapshot.opaqueRows[i / chunk_size] |= std::uint16_t{1} << (i % chunk_size); }
            snapshot.transparent = snapshot.transparent && !(flags & 2);
        }
        chunks.push_back(snapshot);
    }
    for (auto& chunk : chunks) {
        const auto* lower = Find({chunk.position.x, chunk.position.z, chunk.position.y - 1});
        chunk.openBelow = lower != nullptr;
        chunk.closedBelow = true;
        if (!lower) { chunk.closedRows.fill(0xFFFF); continue; }
        for (int i = 0; i < chunk_size * chunk_size; ++i) {
            const auto a = chunk.cells[i];
            const auto b = lower->cells[i];
            const bool open = (a & 4) || ((a & 8) && (b & 8));
            if (!open) { chunk.closedRows[i / chunk_size] |= std::uint16_t{1} << (i % chunk_size); }
            chunk.openBelow = chunk.openBelow && open;
            chunk.closedBelow = chunk.closedBelow && !open;
        }
    }
}

bool VisibilityWorkspace::Prepare(const OpticalField& field, ChunkCoord first, ChunkCoord last) {
    if (source == &field && revision == field.revision && firstChunk == first && lastChunk == last) { return false; }
    source = &field;
    revision = field.revision;
    firstChunk = first;
    lastChunk = last;
    width = (last.x - first.x + 1) * chunk_size;
    depth = (last.z - first.z + 1) * chunk_size;
    constexpr int height = world_max_y - world_min_y + 1;
    const int plane = width * depth;
    optics.assign(static_cast<std::size_t>(plane) * height, 0);
    for (const auto& chunk : field.chunks) {
        const auto p = chunk.position;
        if (p.x < first.x || p.x > last.x || p.z < first.z || p.z > last.z) { continue; }
        const int x = (p.x - first.x) * chunk_size;
        const int z = (p.z - first.z) * chunk_size;
        const auto offset = static_cast<std::size_t>(p.y - world_min_y) * plane + z * width + x;
        for (int row = 0; row < chunk_size; ++row) {
            std::memcpy(optics.data() + offset + row * width, chunk.cells.data() + row * chunk_size, chunk_size);
        }
    }
    // Only edge-of-world windows can contain materialized chunk cells outside
    // the coordinate contract. They remain missing to the reference traversal.
    const int startX = first.x * chunk_size;
    const int startZ = first.z * chunk_size;
    if (!ValidPosition({startX, startZ, 0}) || !ValidPosition({startX + width - 1, startZ + depth - 1, 0})) {
        for (int y = world_min_y; y <= world_max_y; ++y) {
            for (int z = 0; z < depth; ++z) {
                for (int x = 0; x < width; ++x) {
                    if (!ValidPosition({startX + x, startZ + z, y})) {
                        optics[(y - world_min_y) * plane + z * width + x] = 0;
                    }
                }
            }
        }
    }
    const int stride = width + 1;
    const int sumPlane = stride * (depth + 1);
    sums.assign(static_cast<std::size_t>(sumPlane) * (height + 1), {});
    for (int y = 1; y <= height; ++y) {
        for (int z = 1; z <= depth; ++z) {
            OpticalCounts row;
            const auto* cells = optics.data() + (y - 1) * plane + (z - 1) * width;
            auto* values = sums.data() + y * sumPlane + z * stride;
            for (int x = 1; x <= width; ++x) {
                const auto upper = cells[x - 1];
                const auto lower = y == 1 ? 0 : cells[x - 1 - plane];
                row.obstacles += !(upper & 1) || (upper & 2);
                row.floors += !((upper & 1) && (lower & 1)
                    && ((upper & 4) || ((upper & 8) && (lower & 8))));
                const auto a = values[x - stride];
                const auto b = values[x - sumPlane];
                const auto c = values[x - stride - sumPlane];
                values[x] = {row.obstacles + a.obstacles + b.obstacles - c.obstacles,
                    row.floors + a.floors + b.floors - c.floors};
            }
        }
    }
    return true;
}

std::uint8_t VisibilityWorkspace::Sample(TileCoord p) const {
    const int x = p.x - firstChunk.x * chunk_size;
    const int z = p.z - firstChunk.z * chunk_size;
    if (x < 0 || x >= width || z < 0 || z >= depth || p.y < world_min_y || p.y > world_max_y) { return 0; }
    return optics[(static_cast<std::size_t>(p.y - world_min_y) * depth + z) * width + x];
}

OpticalCounts VisibilityWorkspace::Box(int x0, int z0, int y0, int x1, int z1, int y1) const {
    x0 -= firstChunk.x * chunk_size;
    x1 -= firstChunk.x * chunk_size;
    z0 -= firstChunk.z * chunk_size;
    z1 -= firstChunk.z * chunk_size;
    y0 -= world_min_y;
    y1 -= world_min_y;
    const int stride = width + 1;
    const int plane = stride * (depth + 1);
    const auto* low = sums.data() + y0 * plane;
    const auto* high = sums.data() + y1 * plane;
    const auto a = high[z1 * stride + x1];
    const auto b = high[z1 * stride + x0];
    const auto c = high[z0 * stride + x1];
    const auto d = low[z1 * stride + x1];
    const auto e = high[z0 * stride + x0];
    const auto f = low[z1 * stride + x0];
    const auto g = low[z0 * stride + x1];
    const auto h = low[z0 * stride + x0];
    return {a.obstacles - b.obstacles - c.obstacles - d.obstacles
        + e.obstacles + f.obstacles + g.obstacles - h.obstacles,
        a.floors - b.floors - c.floors - d.floors + e.floors + f.floors + g.floors - h.floors};
}

std::size_t VisibilityMask::Index(TileCoord p) const {
    const int width = radius * 2 + 1;
    return (static_cast<std::size_t>(p.y - minimumY) * width + p.z - origin.z + radius)
        * width + p.x - origin.x + radius;
}

bool VisibilityMask::Contains(TileCoord p) const {
    return (Get(p) & 1) != 0;
}

std::uint8_t VisibilityMask::Get(TileCoord p) const {
    if (p.y < minimumY || p.y > maximumY || std::abs(p.x - origin.x) > radius
        || std::abs(p.z - origin.z) > radius) { return 0; }
    if (clearLayers[p.y - minimumY]) {
        const int dx = p.x - origin.x;
        const int dz = p.z - origin.z;
        const int dy = p.y - origin.y;
        return dx*dx + dz*dz + dy*dy <= radius*radius ? 1 : 0;
    }
    return cells.empty() ? 0 : cells[Index(p)];
}

eastl::pair<int, int> VisibilityMask::ClearRow(int z, int y) const {
    if (y < minimumY || y > maximumY || !clearLayers[y - minimumY]) { return {1, 0}; }
    const int dy = y - origin.y;
    const int dz = z - origin.z;
    const int squared = radius*radius - dy*dy - dz*dz;
    if (squared < 0) { return {1, 0}; }
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
    const int layer = y - minimumY;
    if (!clearLayers[layer]) { return; }
    const int width = radius * 2 + 1;
    if (cells.empty()) {
        cells.resize(static_cast<std::size_t>(width) * width * (maximumY - minimumY + 1), 0);
    }
    for (int z = origin.z - radius; z <= origin.z + radius; ++z) {
        const auto row = ClearRow(z, y);
        if (row.first > row.second) { continue; }
        auto* begin = cells.data() + Index({row.first, z, y});
        eastl::fill(begin, begin + row.second - row.first + 1, std::uint8_t{1});
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
    output.minimumY = eastl::max(world_min_y, origin.y - radius);
    output.maximumY = eastl::min(world_max_y, origin.y + radius);
    const int width = radius * 2 + 1;
    const auto size = static_cast<std::size_t>(width) * width
        * (output.maximumY - output.minimumY + 1);
    output.cells.clear();
    output.visibleLayers.fill(false);
    output.clearLayers.fill(false);
    OpticalField localOptics;
    if (!suppliedOptics) {
        localOptics.Rebuild(world);
        ++stats.opticalRebuilds;
        suppliedOptics = &localOptics;
        // A temporary field cannot be the identity of a cross-call workspace.
        suppliedWorkspace = nullptr;
    }
    const auto& field = *suppliedOptics;
    const auto firstChunk = ChunkOf({origin.x - radius, origin.z - radius, 0});
    const auto lastChunk = ChunkOf({origin.x + radius, origin.z + radius, 0});
    const bool validBox = ValidPosition({origin.x - radius, origin.z - radius, origin.y})
        && ValidPosition({origin.x + radius, origin.z + radius, origin.y});
    const int layers = output.maximumY - output.minimumY + 1;
    const int observerLayer = origin.y - output.minimumY;
    eastl::array<bool, world_max_y - world_min_y + 1> transparent{};
    eastl::array<bool, world_max_y - world_min_y + 1> openBelow{};
    eastl::array<bool, world_max_y - world_min_y + 1> closedBelow{};
    for (int layer = 0; layer < layers; ++layer) {
        transparent[layer] = validBox;
        openBelow[layer] = true;
        closedBelow[layer] = true;
        for (int x = firstChunk.x; x <= lastChunk.x; ++x) {
            for (int z = firstChunk.z; z <= lastChunk.z; ++z) {
                const auto* chunk = field.Find({x, z, layer + output.minimumY});
                bool clear = chunk && chunk->transparent;
                bool open = chunk && chunk->openBelow;
                bool closed = !chunk || chunk->closedBelow;
                const int x0 = eastl::max(0, origin.x - radius - x * chunk_size);
                const int x1 = eastl::min(chunk_size, origin.x + radius + 1 - x * chunk_size);
                const int z0 = eastl::max(0, origin.z - radius - z * chunk_size);
                const int z1 = eastl::min(chunk_size, origin.z + radius + 1 - z * chunk_size);
                if (chunk && (x0 != 0 || x1 != chunk_size || z0 != 0 || z1 != chunk_size)) {
                    const auto bits = static_cast<std::uint16_t>(((1U << x1) - 1) & ~((1U << x0) - 1));
                    clear = open = closed = true;
                    for (int row = z0; row < z1; ++row) {
                        clear = clear && !(chunk->opaqueRows[row] & bits);
                        open = open && !(chunk->closedRows[row] & bits);
                        closed = closed && (chunk->closedRows[row] & bits) == bits;
                    }
                }
                transparent[layer] = transparent[layer] && clear;
                openBelow[layer] = openBelow[layer] && open;
                closedBelow[layer] = closedBelow[layer] && closed;
            }
        }
    }
    bool allCertified = true;
    eastl::array<std::uint8_t, world_max_y - world_min_y + 1> certified{};
    for (int layer = 0; layer < layers; ++layer) {
        bool clear = true;
        bool closed = false;
        for (int i = eastl::min(layer, observerLayer); i <= eastl::max(layer, observerLayer); ++i) {
            clear = clear && transparent[i];
            if (i > eastl::min(layer, observerLayer)) {
                clear = clear && openBelow[i];
                closed = closed || closedBelow[i];
            }
        }
        certified[layer] = closed ? 2 : (clear ? 1 : 0);
        allCertified = allCertified && certified[layer] != 0;
    }
    if (allCertified) {
        for (int layer = 0; layer < layers; ++layer) {
            output.visibleLayers[layer] = output.clearLayers[layer] = certified[layer] == 1;
        }
        return;
    }
    output.cells.assign(size, 0);
    VisibilityWorkspace localWorkspace;
    if (!suppliedWorkspace) { suppliedWorkspace = &localWorkspace; }
    auto& workspace = *suppliedWorkspace;
    if (workspace.Prepare(field, firstChunk, lastChunk)) { ++stats.volumeRebuilds; }
    const auto sample = [&](TileCoord p) -> std::uint8_t {
        if (p.y < output.minimumY || p.y > output.maximumY
            || std::abs(p.x - origin.x) > radius || std::abs(p.z - origin.z) > radius) { return 0; }
        return workspace.Sample(p);
    };
    const auto vertical = [&](TileCoord a, TileCoord b) {
        const auto upper = sample(a.y > b.y ? a : b);
        const auto lower = sample(a.y > b.y ? b : a);
        return (upper & 1) && (lower & 1) && ((upper & 4) || ((upper & 8) && (lower & 8)));
    };
    const auto fillClearSlab = [&](int layer) {
        output.visibleLayers[layer] = true;
        output.clearLayers[layer] = true;
    };
    const auto box = [&](int x0, int z0, int y0, int x1, int z1, int y1) {
        return workspace.Box(x0 + origin.x - radius, z0 + origin.z - radius, y0 + output.minimumY,
            x1 + origin.x - radius, z1 + origin.z - radius, y1 + output.minimumY);
    };
    // Prefixes share *ordered 3D face events*, not projected 2D shadows. A node
    // is clear, an opaque endpoint, or closed (side blocker/missing/closed floor).
    struct Node {
        eastl::array<std::uint32_t, 7> children{};
        std::uint8_t state = 0;
    };
    Vector<Node> nodes(8); // Separate roots for the eight step-sign octants.
    if (!(sample(origin) & 1)) { return; }
    const bool closedUp = !vertical(origin, {origin.x, origin.z, origin.y + 1});
    const bool closedDown = !vertical(origin, {origin.x, origin.z, origin.y - 1});
    for (int y = output.minimumY; y <= output.maximumY; ++y) {
        const auto state = certified[y - output.minimumY];
        if (state == 2) { continue; }
        if (state == 1) { fillClearSlab(y - output.minimumY); continue; }
        for (int z = -radius; z <= radius; ++z) {
            for (int x = -radius; x <= radius; ++x) {
                const int dy = y - origin.y;
                if (x*x + z*z + dy*dy > radius*radius) { continue; }
                const TileCoord target{origin.x + x, origin.z + z, y};
                if (!(sample(target) & 1)) { continue; }
                const int x0 = radius + eastl::min(0, x);
                const int x1 = radius + eastl::max(0, x) + 1;
                const int z0 = radius + eastl::min(0, z);
                const int z1 = radius + eastl::max(0, z) + 1;
                const int y0 = eastl::min(origin.y, y) - output.minimumY;
                const int y1 = eastl::max(origin.y, y) - output.minimumY + 1;
                if ((dy > 0 && closedUp) || (dy < 0 && closedDown)) {
                    const int face = origin.y - output.minimumY + (dy > 0 ? 1 : 0);
                    if (box(x0, z0, face, x1, z1, face + 1).floors == (x1 - x0) * (z1 - z0)) {
                        continue;
                    }
                }
                const auto bounds = box(x0, z0, y0, x1, z1, y1);
                const int endpointObstacles = ((sample(origin) & 2) ? 1 : 0)
                    + ((target != origin && (sample(target) & 2)) ? 1 : 0);
                if (bounds.obstacles == endpointObstacles
                    && (dy == 0 || box(x0, z0, y0 + 1, x1, z1, y1).floors == 0)) {
                    output.cells[output.Index(target)] = static_cast<std::uint8_t>(1 | (sample(target) & 2));
                    output.visibleLayers[y - output.minimumY] = true;
                    continue;
                }
                const int delta[] = {std::abs(x), std::abs(z), std::abs(dy)};
                const int step[] = {x > 0 ? 1 : -1, z > 0 ? 1 : -1, dy > 0 ? 1 : -1};
                int numerator[] = {1, 1, 1};
                TileCoord cursor = origin;
                std::uint32_t node = (x > 0 ? 1U : 0U) | (z > 0 ? 2U : 0U) | (dy > 0 ? 4U : 0U);
                bool visible = true;
                int events = 0;
                while (cursor != target) {
                    // Once a shared prefix clears nearby geometry, certify the
                    // entire remaining corridor instead of expanding long sky
                    // tails into the trie. The ray's phase need not be reset:
                    // a clear enclosing volume proves every possible tail clear.
                    if (++events % 4 == 0) {
                        const int cx = cursor.x - origin.x + radius;
                        const int cz = cursor.z - origin.z + radius;
                        const int cy = cursor.y - output.minimumY;
                        const int tx = target.x - origin.x + radius;
                        const int tz = target.z - origin.z + radius;
                        const int ty = target.y - output.minimumY;
                        const int lowY = eastl::min(cy, ty);
                        const int highY = eastl::max(cy, ty) + 1;
                        const int lowX = eastl::min(cx, tx);
                        const int highX = eastl::max(cx, tx) + 1;
                        const int lowZ = eastl::min(cz, tz);
                        const int highZ = eastl::max(cz, tz) + 1;
                        const auto remaining = box(lowX, lowZ, lowY, highX, highZ, highY);
                        const int exempt = ((sample(target) & 2) ? 1 : 0);
                        if (remaining.obstacles == exempt && (cy == ty
                            || box(lowX, lowZ, lowY + 1, highX, highZ, highY).floors == 0)) { break; }
                    }
                    int axis = -1;
                    for (int i = 0; i < 3; ++i) {
                        if (delta[i] && (axis < 0 || numerator[i] * delta[axis]
                            < numerator[axis] * delta[i])) { axis = i; }
                    }
                    unsigned mask = 0;
                    for (int i = 0; i < 3; ++i) {
                        if (delta[i] && numerator[i] * delta[axis] == numerator[axis] * delta[i]) {
                            mask |= 1U << i;
                        }
                    }
                    const TileCoord next{cursor.x + ((mask & 1) ? step[0] : 0),
                        cursor.z + ((mask & 2) ? step[1] : 0), cursor.y + ((mask & 4) ? step[2] : 0)};
                    auto child = nodes[node].children[mask - 1];
                    if (!child) {
                        Node fresh;
                        for (unsigned subset = 1; subset <= 7; ++subset) {
                            if ((subset & mask) != subset) { continue; }
                            const TileCoord touched{cursor.x + ((subset & 1) ? step[0] : 0),
                                cursor.z + ((subset & 2) ? step[1] : 0),
                                cursor.y + ((subset & 4) ? step[2] : 0)};
                            ++stats.traversedVoxels;
                            const auto cell = sample(touched);
                            if (!(cell & 1) || ((cell & 2) && subset != mask)) { fresh.state = 2; break; }
                            if ((cell & 2) && subset == mask) { fresh.state = 1; }
                            if (mask & 4) {
                                TileCoord other = touched;
                                other.y += (subset & 4) ? -step[2] : step[2];
                                if (!vertical(touched, other)) { fresh.state = 2; break; }
                            }
                        }
                        if ((mask & 4) && !vertical(cursor, {cursor.x, cursor.z, cursor.y + step[2]})) {
                            fresh.state = 2;
                        }
                        child = static_cast<std::uint32_t>(nodes.size());
                        nodes[node].children[mask - 1] = child;
                        nodes.push_back(fresh);
                    }
                    node = child;
                    if (nodes[node].state != 0) {
                        visible = nodes[node].state == 1 && next == target;
                        break;
                    }
                    cursor = next;
                    for (int i = 0; i < 3; ++i) { if (mask & (1U << i)) { numerator[i] += 2; } }
                }
                output.cells[output.Index(target)] = visible ? static_cast<std::uint8_t>(1 | (sample(target) & 2)) : 0;
                if (visible) { output.visibleLayers[y - output.minimumY] = true; }
            }
        }
    }
}

}
