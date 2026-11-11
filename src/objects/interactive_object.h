#pragma once

#include "content/object_definition.h"
#include "core/light_types.h"
#include "core/perception_types.h"
#include "core/world_types.h"

#include <EASTL/unique_ptr.h>

namespace cataclysmx {

class World;
class ObjectBehavior;

class InteractiveObject {
public:
    InteractiveObject();
    InteractiveObject(PersistentId id, TileCoord position, bool enabled);
    ~InteractiveObject();
    InteractiveObject(InteractiveObject&&) noexcept;
    InteractiveObject& operator=(InteractiveObject&&) noexcept;
    InteractiveObject(const InteractiveObject&) = delete;
    InteractiveObject& operator=(const InteractiveObject&) = delete;

    PersistentId id = 0;
    TileCoord position{};
    bool enabled = false;
    std::uint64_t updates = 0;
    Vector<ItemHandle> contents;
    std::uint32_t updateInterval = 100;
    ActionDeadline updateDeadline{0, 100, true};
    ObjectKind kind = ObjectKind::Generic;
    ObjectFacing facing = ObjectFacing::North;
    bool broken = false;
    LightEmission light{};
    ObjectTypeId type{};
private:
    friend class World;
    eastl::unique_ptr<ObjectBehavior> behavior_;
};

ObjectAppearance DescribeObject(const InteractiveObject& object);

}
