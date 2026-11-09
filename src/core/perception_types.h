#pragma once

#include "content/content.h"
#include "core/world_types.h"

#include <EASTL/array.h>

namespace cataclysmx {

enum class Visibility : std::uint8_t { Unknown, Remembered, Visible };
// A small closed set for V1; these are semantic identities, never sprite indices.
enum class ObjectKind : std::uint8_t { None, Generic, Chair, Table, Lamp };
enum class ObjectFacing : std::uint8_t { North, East, South, West };

// Owned observation data, deliberately independent of machine/behavior state.
struct ObjectAppearance {
    ObjectKind kind = ObjectKind::None;
    ObjectFacing facing = ObjectFacing::North;
    bool active = false;
    bool broken = false;
    bool operator==(const ObjectAppearance&) const = default;
};

constexpr bool ValidObjectAppearance(const ObjectAppearance& appearance) {
    return appearance.kind <= ObjectKind::Lamp && appearance.facing <= ObjectFacing::West
        && (appearance.kind != ObjectKind::None || appearance == ObjectAppearance{});
}

struct RememberedObject {
    std::uint8_t tileIndex = 0;
    ObjectAppearance appearance;
    bool operator==(const RememberedObject&) const = default;
};

struct MemoryChunk {
    ChunkCoord position;
    // Zero is unobserved. One KiB per allocated single-layer chunk.
    eastl::array<TerrainId, chunk_size * chunk_size> terrain{};
    Vector<RememberedObject> objects; // Increasing tileIndex, no empty appearances.
    bool operator==(const MemoryChunk&) const = default;
};

struct PlayerMemory {
    PersistentId player = 0;
    Vector<MemoryChunk> chunks; // Lexicographic (x,z,y) order.
    bool operator==(const PlayerMemory&) const = default;
};

struct RememberedTile {
    TerrainId terrain{};
    ObjectAppearance object;
    bool operator==(const RememberedTile&) const = default;
};

struct VisionProfile {
    int range = playerSightRange;
    int dimRange = 6;
    int darkRange = 1;
    std::uint8_t glareThreshold = 240;
    int glareDimRange = 3;
    bool operator==(const VisionProfile&) const = default;
};

// Work counters, not simulation inputs. Resetting them does not invalidate caches.
struct PerceptionStats {
    std::uint64_t opticalRebuilds = 0;
    std::uint64_t volumeRebuilds = 0;
    std::uint64_t filterRebuilds = 0;
    std::uint64_t filterTilesSampled = 0;
    std::uint64_t memoryTilesExamined = 0;
    std::uint64_t geometryRebuilds = 0;
    std::uint64_t lightingRebuilds = 0;
    std::uint64_t pointLosCalls = 0;
    std::uint64_t traversedVoxels = 0;
    std::uint64_t projectedCells = 0;
    std::uint64_t observationScans = 0;
    std::uint64_t memoryTilesChanged = 0;
    std::uint64_t demandReconciliations = 0;
};

constexpr bool ValidVisionProfile(const VisionProfile& vision) {
    return vision.range >= 0 && vision.range <= playerSightRange
        && vision.darkRange >= 0 && vision.darkRange <= vision.dimRange
        && vision.dimRange <= vision.range && vision.glareDimRange >= 0
        && vision.glareDimRange <= vision.dimRange && vision.glareThreshold >= 192;
}

}
