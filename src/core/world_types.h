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

struct TileCoord {
    int x = 0;
    int z = 0;
    bool operator==(const TileCoord&) const = default;
};
struct ChunkCoord {
    int x = 0;
    int z = 0;
    bool operator==(const ChunkCoord&) const = default;
};

}
