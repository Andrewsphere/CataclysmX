#include "items/item_registry.h"

#include <cstdlib>

namespace cataclysmx {
namespace {
bool ValidType(eastl::string_view type) {
    if (type.empty() || type[0] < 'a' || type[0] > 'z') { return false; }
    for (char ch : type) {
        if ((ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '_') { return false; }
    }
    return true;
}
}

ContentResult<void> ItemBehaviorRegistry::Add(eastl::string_view type, ParseItemBehavior parser) {
    if (!ValidType(type) || type == "container" || type == "energy_store" || !parser || Find(type)) {
        return eastl::unexpected(ContentError::InvalidData);
    }
    entries_.push_back({String(type.data(), type.size()), parser});
    return {};
}

ParseItemBehavior ItemBehaviorRegistry::Find(eastl::string_view type) const {
    for (const auto& entry : entries_) {
        if (eastl::string_view(entry.type.data(), entry.type.size()) == type) { return entry.parser; }
    }
    return nullptr;
}

void RegisterBuiltinItemBehaviors(ItemBehaviorRegistry& registry) {
    if (!registry.Add("charger", ParseChargerBehavior)
        || !registry.Add("door_controller", ParseDoorControllerBehavior)) { std::abort(); }
}
}
