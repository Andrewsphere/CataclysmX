#include "content/item_definition.h"
#include "items/item_behavior.h"

namespace cataclysmx {

ItemCapabilityId FindItemCapability(const ItemDefinition& definition, eastl::string_view name) {
    std::size_t begin = 0, end = definition.capabilities.size();
    while (begin < end) {
        const std::size_t middle = begin + (end - begin) / 2;
        const auto& id = definition.capabilities[middle].id;
        if (eastl::string_view(id.data(), id.size()) < name) { begin = middle + 1; }
        else { end = middle; }
    }
    if (begin == definition.capabilities.size()) { return {}; }
    const auto& id = definition.capabilities[begin].id;
    if (eastl::string_view(id.data(), id.size()) != name) { return {}; }
    return {static_cast<std::uint16_t>(begin + 1)};
}

const ItemCapabilityDefinition* TryGetItemCapability(const ItemDefinition& definition, ItemCapabilityId id) {
    if (id.value == 0 || id.value > definition.capabilities.size()) { return nullptr; }
    return &definition.capabilities[id.value - 1];
}

const ContainerDefinition* ItemContainer(const ItemDefinition& definition) {
    return definition.container ? &definition.container->config : nullptr;
}

const EnergyStoreDefinition* ItemEnergyStore(const ItemDefinition& definition, ItemCapabilityId id) {
    for (const auto& store : definition.energyStores) {
        if (store.capability == id) { return &store.config; }
    }
    return nullptr;
}

const ItemBehaviorDefinition* ItemBehaviorConfig(const ItemDefinition& definition, ItemCapabilityId id) {
    for (const auto& behavior : definition.behaviors) {
        if (behavior.capability == id) { return behavior.definition.get(); }
    }
    return nullptr;
}

bool ItemDefinition::operator==(const ItemDefinition& other) const {
    if (id != other.id || name != other.name || massGrams != other.massGrams
        || volumeMl != other.volumeMl || capabilities != other.capabilities
        || container != other.container || energyStores != other.energyStores
        || behaviors.size() != other.behaviors.size()) { return false; }
    for (std::size_t i = 0; i < behaviors.size(); ++i) {
        const auto& left = behaviors[i];
        const auto& right = other.behaviors[i];
        if (left.capability != right.capability || bool(left.definition) != bool(right.definition)) { return false; }
        if (left.definition && left.definition->Schema() != right.definition->Schema()) { return false; }
    }
    return true;
}

}
