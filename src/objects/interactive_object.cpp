#include "objects/interactive_object.h"
#include "objects/object_behavior.h"

namespace cataclysmx {

InteractiveObject::InteractiveObject() = default;
InteractiveObject::InteractiveObject(PersistentId objectId, TileCoord objectPosition, bool objectEnabled)
    : id(objectId), position(objectPosition), enabled(objectEnabled) {}
InteractiveObject::~InteractiveObject() = default;
InteractiveObject::InteractiveObject(InteractiveObject&&) noexcept = default;
InteractiveObject& InteractiveObject::operator=(InteractiveObject&&) noexcept = default;

}
