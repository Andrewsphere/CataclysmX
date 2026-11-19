#include "content/object_definition.h"

#include <limits>

namespace cataclysmx {

bool ValidObjectDefinition(const ObjectDefinition& definition) {
    if ((definition.capacityVolumeMl && *definition.capacityVolumeMl == 0)
        || definition.kind < ObjectKind::Generic || definition.kind > ObjectKind::Lamp
        || definition.moveCost == 0 || definition.moveCost > 1000000
        || !ValidLightEmission(definition.light)) { return false; }
    if ((!definition.toolTraits.Empty() || definition.packingItem.value)
        && definition.behavior != ObjectBehaviorKind::None
        && definition.behavior != ObjectBehaviorKind::Forge) { return false; }
    if (definition.craftingEnergyPerWork
        && (definition.behavior != ObjectBehaviorKind::None || definition.toolTraits.Empty())) { return false; }
    std::uint8_t previous = 0;
    for (const auto& trait : definition.toolTraits) {
        if (trait.trait.value <= previous || !trait.tier) { return false; }
        previous = trait.trait.value;
    }
    if (definition.behavior == ObjectBehaviorKind::Forge) {
        const auto& forge = definition.forge;
        if (definition.generator != GeneratorDefinition{} || definition.machine != CraftingMachineDefinition{}
            || definition.toolTraits.Empty() || forge.fuels.empty()
            || forge.fuelCapacity == 0 || forge.fuelCapacity > std::numeric_limits<std::uint64_t>::max() / 2
            || forge.ignitionCost == 0 || forge.ignitionCost >= forge.fuelCapacity) { return false; }
        for (std::size_t i = 0; i < forge.fuels.size(); ++i) {
            const auto& fuel = forge.fuels[i];
            if (!fuel.item.value || !fuel.heat || fuel.heat > forge.fuelCapacity) { return false; }
            for (std::size_t j = 0; j < i; ++j) {
                if (forge.fuels[j].item == fuel.item) { return false; }
            }
        }
        return true;
    }
    if (definition.forge != ForgeDefinition{}) { return false; }
    if (definition.behavior == ObjectBehaviorKind::Generator) {
        const auto& generator = definition.generator;
        return definition.machine == CraftingMachineDefinition{}
            && generator.fuelItem.value != 0 && generator.energyPerFuel > 0
            && generator.fuelCapacity >= generator.energyPerFuel
            && generator.outputPerTick > 0;
    }
    if (definition.generator != GeneratorDefinition{}) { return false; }
    if (definition.behavior == ObjectBehaviorKind::None || definition.behavior == ObjectBehaviorKind::GridBattery) {
        return definition.machine == CraftingMachineDefinition{}
            && (definition.behavior != ObjectBehaviorKind::GridBattery || !definition.capacityVolumeMl);
    }
    if (definition.behavior != ObjectBehaviorKind::CraftingMachine) { return false; }
    const auto& machine = definition.machine;
    return machine.input.value != 0 && machine.output.value != 0
        && machine.inputCount > 0 && machine.inputCount <= 1024
        && machine.outputCount > 0 && machine.outputCount <= 1024
        && machine.duration > 0 && machine.duration <= std::numeric_limits<std::uint32_t>::max()
        && machine.energyPerTick > 0
        && (machine.gridPowered ? machine.energyCapacity == 0
            : machine.energyCapacity >= machine.energyPerTick
                && machine.energyCapacity <= std::numeric_limits<std::uint64_t>::max() / 2);
}

}
