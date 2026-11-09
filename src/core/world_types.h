#pragma once

#include <cstdint>

namespace cataclysmx {

template<class Tag>
struct Handle {
    std::uint64_t value = 0;
    bool operator==(const Handle&) const = default;
};
using ActorHandle = Handle<struct ActorTag>;
using ItemHandle = Handle<struct ItemTag>;
using ObjectHandle = Handle<struct ObjectTag>;
using LoadRequest = Handle<struct LoadRequestTag>;
using PersistentId = std::uint64_t;

inline constexpr int chunk_size = 16;
inline constexpr int world_coordinate_limit = 1'000'000;
inline constexpr int world_min_y = -10;
inline constexpr int world_max_y = 10;
inline constexpr int world_layer_count = world_max_y - world_min_y + 1;

struct TileCoord {
    int x = 0;
    int z = 0;
    // X/Z remain the horizontal aggregate arguments; omitted Y means surface.
    int y = 0;
    bool operator==(const TileCoord&) const = default;
};
struct ChunkCoord {
    int x = 0;
    int z = 0;
    int y = 0;
    bool operator==(const ChunkCoord&) const = default;
};

}
