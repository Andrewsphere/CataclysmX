#include "core/planar_visibility.h"
#include "core/light_types.h"

#include <EASTL/algorithm.h>
#include <cstdlib>

namespace cataclysmx {
namespace {
struct Slope { int numerator; int denominator; };

int FloorDivide(int numerator, int denominator) {
    const int quotient = numerator / denominator;
    return quotient - (numerator % denominator < 0 ? 1 : 0);
}

struct Scan {
    eastl::span<const std::uint8_t> optics;
    eastl::span<std::uint8_t> visible;
    int width;
    int depth;
    int originX;
    int originZ;
    int radius;
    std::uint64_t* visited;

    void Row(int quadrant, int distance, Slope start, Slope end) const {
        if (distance > radius) { return; }
        // Round start ties up, end ties down. All denominators are positive.
        const int first = FloorDivide(2 * distance * start.numerator + start.denominator,
            2 * start.denominator);
        const int last = -FloorDivide(-2 * distance * end.numerator + end.denominator,
            2 * end.denominator);
        bool previousWall = false;
        bool havePrevious = false;
        for (int column = first; column <= last; ++column) {
            int x = originX;
            int z = originZ;
            switch (quadrant) {
            case 0: x += column; z -= distance; break;
            case 1: x += distance; z += column; break;
            case 2: x += column; z += distance; break;
            case 3: x -= distance; z += column; break;
            default: std::abort();
            }
            if (visited) { ++*visited; }
            const bool inBounds = x >= 0 && x < width && z >= 0 && z < depth;
            const auto flags = inBounds ? optics[z * width + x] : std::uint8_t{0};
            const bool wall = !(flags & 1) || (flags & 2);
            const bool symmetric = column * start.denominator >= distance * start.numerator
                && column * end.denominator <= distance * end.numerator;
            if ((flags & 1) && (wall || symmetric)
                && distance * distance + column * column <= radius * radius) {
                visible[z * width + x] = 1;
            }
            if (havePrevious && previousWall && !wall) {
                start = {2 * column - 1, 2 * distance};
            }
            if (havePrevious && !previousWall && wall) {
                Row(quadrant, distance + 1, start, {2 * column - 1, 2 * distance});
            }
            havePrevious = true;
            previousWall = wall;
        }
        if (havePrevious && !previousWall) { Row(quadrant, distance + 1, start, end); }
    }
};
}

void BuildPlanarVisibility(eastl::span<const std::uint8_t> optics, int width, int depth,
    int originX, int originZ, int radius, eastl::span<std::uint8_t> visible,
    std::uint64_t* visitedCells) {
    if (width <= 0 || depth <= 0 || radius < 0 || radius > playerSightRange
        || originX < 0 || originX >= width || originZ < 0 || originZ >= depth
        || optics.size() != static_cast<std::size_t>(width) * depth
        || visible.size() != optics.size()) { std::abort(); }
    eastl::fill(visible.begin(), visible.end(), std::uint8_t{0});
    if (visitedCells) { ++*visitedCells; }
    const int index = originZ * width + originX;
    if (!(optics[index] & 1)) { return; }
    visible[index] = 1;
    const Scan scan{optics, visible, width, depth, originX, originZ, radius, visitedCells};
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        scan.Row(quadrant, 1, {-1, 1}, {1, 1});
    }
}
}
