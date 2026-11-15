#include "objects/storage_access.h"
#include "core/world.h"
#include <EASTL/array.h>
#include <cstdlib>

namespace cataclysmx {
bool CanReachStorage(const World& world, TileCoord from, ObjectHandle handle, unsigned radius) {
    const auto* object = world.GetObject(handle);
    const auto* definition = object ? world.GetContent().TryGetObject(object->type) : nullptr;
    if (radius > 8 || !definition || !definition->capacityVolumeMl || !world.IsActive(from)
        || !world.IsActive(object->position)) { return false; }
    const TileCoord target = object->position;
    const auto* tile = world.GetTile(target);
    if (!tile || !Passable(world.GetContent(), tile->terrain)
        || world.GetContent().GetTerrain(tile->terrain).openSpace || from.y != target.y
        || std::abs(from.x - target.x) > static_cast<int>(radius)
        || std::abs(from.z - target.z) > static_cast<int>(radius)) { return false; }
    const auto passable = [&world](TileCoord p) {
        const auto* value = world.GetTile(p);
        return value && world.IsActive(p) && !world.BlocksMovement(p)
            && !world.GetContent().GetTerrain(value->terrain).openSpace;
    };
    if (!passable(from) && from != target) { return false; }
    eastl::array<TileCoord, 289> queue{};
    eastl::array<bool, 289> seen{};
    const int r = static_cast<int>(radius), width = r * 2 + 1;
    std::size_t count = 1;
    queue[0] = from;
    seen[r * width + r] = true;
    for (std::size_t i = 0; i < count; ++i) {
        const TileCoord current = queue[i];
        if (current == target) { return true; }
        for (int dz = -1; dz <= 1; ++dz) { for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz) { continue; }
            const TileCoord next{current.x + dx, current.z + dz, current.y};
            const int x = next.x - from.x + r, z = next.z - from.z + r;
            if (x < 0 || z < 0 || x >= width || z >= width || seen[z * width + x]) { continue; }
            if ((next != target && !passable(next)) || (dx && dz
                && (!passable({current.x + dx, current.z, current.y})
                    || !passable({current.x, current.z + dz, current.y})))) { continue; }
            if (next == target) { return true; }
            seen[z * width + x] = true;
            queue[count++] = next;
        } }
    }
    return false;
}
}
