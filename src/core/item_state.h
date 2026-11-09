#pragma once

#include "content/item_definition.h"
#include <EASTL/unique_ptr.h>

namespace cataclysmx {

class ItemBehavior;
class ItemSystem;
struct ItemEnergyState {
    ItemCapabilityId capability{};
    std::uint32_t energy = 0;
};

// Move-only runtime ownership. Public inspection captures plain values instead
// of exposing mutable polymorphic objects through const smart pointers.
class ItemRuntime {
public:
    ItemRuntime();
    ~ItemRuntime();
    ItemRuntime(ItemRuntime&&) noexcept;
    ItemRuntime& operator=(ItemRuntime&&) noexcept;
    ItemRuntime(const ItemRuntime&) = delete;
    ItemRuntime& operator=(const ItemRuntime&) = delete;
private:
    friend class ItemSystem;
    friend ItemRuntime MakeItemState(const ItemDefinition&);
    friend void WriteItemState(const ItemDefinition&, const ItemRuntime&, DataObject&);
    friend StateResult<ItemRuntime> ReadItemState(const ItemDefinition&, const DataObject&, Diagnostics*);
    Vector<ItemEnergyState> energy_;
    Vector<eastl::unique_ptr<ItemBehavior>> behaviors_;
};

ItemRuntime MakeItemState(const ItemDefinition& definition);
void WriteItemState(const ItemDefinition& definition, const ItemRuntime& state, DataObject& output);
StateResult<ItemRuntime> ReadItemState(const ItemDefinition& definition, const DataObject& input,
    Diagnostics* diagnostics = nullptr);

}
