#pragma once

#include "content/item_definition.h"
#include <EASTL/unique_ptr.h>

namespace cataclysmx {

class ItemBehavior;
class ItemSystem;
class CraftingSystem;
struct CraftingJobState;
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
    friend class CraftingSystem;
    friend ItemRuntime MakeItemState(const ItemDefinition&);
    friend void WriteItemState(const ItemDefinition&, const ItemRuntime&, DataObject&);
    friend StateResult<ItemRuntime> ReadItemState(const ItemDefinition&, const DataObject&, Diagnostics*);
    SmallVector<ItemEnergyState, 1> energy_;
    eastl::unique_ptr<CraftingJobState> crafting_;
    SmallVector<eastl::unique_ptr<ItemBehavior>, 1> behaviors_;
};

ItemRuntime MakeItemState(const ItemDefinition& definition);
void WriteItemState(const ItemDefinition& definition, const ItemRuntime& state, DataObject& output);
StateResult<ItemRuntime> ReadItemState(const ItemDefinition& definition, const DataObject& input,
    Diagnostics* diagnostics = nullptr);

}
