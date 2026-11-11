#pragma once

// Persistence boundary values, never runtime handles. Kept separate from the
// simulation-facing World interface; item parents precede their descendants.
#include "core/world.h"
#include "content/object_definition.h"

namespace cataclysmx {

struct SavedActor {
    PersistentId id;
    TileCoord position;
    bool player;
    std::uint64_t turns;
    std::uint32_t speed = 100;
    Tick remainingTicks = 0;
    bool parked = false;
    std::uint64_t carryCapacityGrams = 100000;
    LightEmission light{};
    // Empty object is a source-construction sentinel, normalized at persistence.
    DataObject controller;
};

struct SavedObject {
    PersistentId id;
    TileCoord position;
    bool enabled;
    std::uint64_t updates;
    std::uint32_t updateInterval = 100;
    Tick remainingTicks = 100;
    ObjectKind kind = ObjectKind::Generic;
    ObjectFacing facing = ObjectFacing::North;
    bool broken = false;
    LightEmission light{};
    ObjectTypeId type{};
    DataObject state;
};

struct SavedItem {
    PersistentId id;
    ItemTypeId type;
    LocationKind location;
    TileCoord position;
    PersistentId owner;
    ItemCapabilityId installation{};
    DataObject state;
};

struct ChunkArchive {
    ChunkCoord position;
    eastl::array<Tile, chunk_size * chunk_size> tiles;
    Vector<SavedActor> actors;
    Vector<SavedObject> objects;
    Vector<SavedItem> items;
};

struct SaveSnapshot {
    std::uint32_t seed = 42;
    std::uint32_t generationVersion = 3;
    PersistentId nextId = 1;
    SessionState session;
    std::uint8_t skyLight = defaultSkyLight;
    Vector<PlayerMemory> memories;
};

}
