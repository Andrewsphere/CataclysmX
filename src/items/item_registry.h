#pragma once

#include "items/item_behavior.h"

namespace cataclysmx {

struct ParsedItemBehavior {
    BehaviorDefinitionPtr definition;
    DataObject config;
};
// All capability identities and shared service definitions are resolved first.
using ParseItemBehavior = ContentResult<ParsedItemBehavior> (*)(const DataObject& input,
    const ItemDefinition& item, ItemCapabilityId self, Diagnostics* diagnostics);

class ItemBehaviorRegistry {
public:
    ContentResult<void> Add(eastl::string_view type, ParseItemBehavior parser);
    ParseItemBehavior Find(eastl::string_view type) const;
private:
    struct Entry { String type; ParseItemBehavior parser = nullptr; };
    Vector<Entry> entries_;
};

void RegisterBuiltinItemBehaviors(ItemBehaviorRegistry& registry);

// Built-ins have the same factory boundary as application/test-supplied behaviors.
ContentResult<ParsedItemBehavior> ParseChargerBehavior(const DataObject& input,
    const ItemDefinition& item, ItemCapabilityId self, Diagnostics* diagnostics);
ContentResult<ParsedItemBehavior> ParseDoorControllerBehavior(const DataObject& input,
    const ItemDefinition& item, ItemCapabilityId self, Diagnostics* diagnostics);

}
