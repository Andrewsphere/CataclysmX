#pragma once

#include "core/data_object.h"
#include "items/item_types.h"
#include <EASTL/optional.h>
#include <EASTL/shared_ptr.h>

namespace cataclysmx {

class ItemBehaviorDefinition;
using BehaviorDefinitionPtr = eastl::shared_ptr<const ItemBehaviorDefinition>;

struct ContainerDefinition {
    std::uint32_t capacityVolumeMl = 0;
    bool operator==(const ContainerDefinition&) const = default;
};
struct EnergyStoreDefinition {
    std::uint32_t capacityEnergy = 0;
    std::uint32_t initialEnergy = 0;
    bool rechargeable = false;
    String batteryClass;
    bool operator==(const EnergyStoreDefinition&) const = default;
};
struct ItemCapabilityDefinition {
    String id;
    String type;
    DataObject config; // Canonical effective parameters, including defaults.
    bool operator==(const ItemCapabilityDefinition&) const = default;
};
struct ItemContainerDefinition {
    ItemCapabilityId capability{};
    ContainerDefinition config;
    bool operator==(const ItemContainerDefinition&) const = default;
};
struct ItemEnergyDefinition {
    ItemCapabilityId capability{};
    EnergyStoreDefinition config;
    bool operator==(const ItemEnergyDefinition&) const = default;
};
struct ItemBehaviorBinding {
    ItemCapabilityId capability{};
    BehaviorDefinitionPtr definition;
};
struct ItemDefinition {
    String id;
    String name;
    std::uint32_t massGrams = 0;
    std::uint32_t volumeMl = 0;
    Vector<ItemCapabilityDefinition> capabilities; // Sorted by stable name.
    eastl::optional<ItemContainerDefinition> container;
    Vector<ItemEnergyDefinition> energyStores;
    Vector<ItemBehaviorBinding> behaviors;
    bool operator==(const ItemDefinition& other) const;
};

ItemCapabilityId FindItemCapability(const ItemDefinition& definition, eastl::string_view name);
const ItemCapabilityDefinition* TryGetItemCapability(const ItemDefinition& definition, ItemCapabilityId id);
const ContainerDefinition* ItemContainer(const ItemDefinition& definition);
const EnergyStoreDefinition* ItemEnergyStore(const ItemDefinition& definition, ItemCapabilityId id);
const ItemBehaviorDefinition* ItemBehaviorConfig(const ItemDefinition& definition, ItemCapabilityId id);

}
