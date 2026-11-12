#include "actors/ai_vision.h"

#include "core/world.h"

namespace cataclysmx {
namespace {

struct Optics {
    bool transparent = false;
    bool openSpace = false;
    bool stairs = false;
};

Optics ReadOptics(const World& world, TileCoord position, AiVisionStats* stats) {
    if (stats) { ++stats->cells; }
    const Tile* tile = world.GetTile(position);
    if (!tile) { return {}; }
    const auto& terrain = world.GetContent().GetTerrain(tile->terrain);
    return {!terrain.blocksSight, terrain.openSpace, terrain.stairs};
}

TileCoord Advance(TileCoord position, unsigned mask, const int* step) {
    if (mask & 1U) { position.x += step[0]; }
    if (mask & 2U) { position.z += step[1]; }
    if (mask & 4U) { position.y += step[2]; }
    return position;
}

}

bool AiLineVisible(const World& world, TileCoord origin, TileCoord target,
    AiVisionStats* stats) {
    if (stats) { ++stats->rays; }
    if (!ValidPosition(origin) || !ValidPosition(target)) { return false; }
    // Widen before subtraction, even for rejected inputs at opposite map edges.
    const std::int64_t delta[] = {std::int64_t{target.x} - origin.x,
        std::int64_t{target.z} - origin.z, std::int64_t{target.y} - origin.y};
    int distance[3]{};
    int step[3]{};
    int next[3] = {1, 1, 1};
    for (int axis = 0; axis < 3; ++axis) {
        if (delta[axis] < -maxAiSightRange || delta[axis] > maxAiSightRange) { return false; }
        step[axis] = delta[axis] < 0 ? -1 : 1;
        distance[axis] = static_cast<int>(delta[axis] < 0 ? -delta[axis] : delta[axis]);
    }

    TileCoord cursor = origin;
    Optics current = ReadOptics(world, cursor, stats);
    if (!current.transparent) { return false; }
    while (cursor != target) {
        // Crossing times are odd numerators / (2 * absolute delta). Compare
        // rational times by cross multiplication; the common 2 cancels.
        int first = -1;
        for (int axis = 0; axis < 3; ++axis) {
            if (distance[axis] == 0) { continue; }
            if (first < 0 || next[axis] * distance[first] < next[first] * distance[axis]) {
                first = axis;
            }
        }
        unsigned tied = 0;
        for (int axis = 0; axis < 3; ++axis) {
            if (distance[axis] != 0
                && next[axis] * distance[first] == next[first] * distance[axis]) {
                tied |= 1U << axis;
            }
        }

        // At a k-axis tie all 2^k incident cells belong to the closed-cell
        // supercover, even cells touched only at a point. Keep their optics on
        // the stack so each is read once and floor tests need no extra reads.
        Optics incident[8]{};
        incident[0] = current;
        for (unsigned subset = 1; subset < 8; ++subset) {
            if ((subset & tied) != subset) { continue; }
            incident[subset] = ReadOptics(world, Advance(cursor, subset, step), stats);
            if (!incident[subset].transparent) { return false; }
        }
        if (tied & 4U) {
            // Check the vertical face in EVERY horizontal column incident at
            // this crossing, not just the column selected by an axis order.
            // The upper tile owns the slab at y - 1/2 (centers are integer
            // coordinates). Horizontal rays within a floor tile never hit it.
            for (unsigned subset = 0; subset < 4; ++subset) {
                if ((subset & tied) != subset) { continue; }
                const Optics& upper = incident[subset | (step[2] > 0 ? 4U : 0U)];
                const Optics& lower = incident[subset | (step[2] > 0 ? 0U : 4U)];
                if (!upper.openSpace && !(upper.stairs && lower.stairs)) { return false; }
            }
        }
        cursor = Advance(cursor, tied, step);
        current = incident[tied];
        for (int axis = 0; axis < 3; ++axis) {
            if (tied & (1U << axis)) { next[axis] += 2; }
        }
    }
    return true;
}

}
