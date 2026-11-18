#include "content/object_definition.h"

#include <limits>

namespace cataclysmx {

bool ValidObjectDefinition(const ObjectDefinition& definition) {
    if ((definition.capacityVolumeMl && *definition.capacityVolumeMl == 0)
        || definition.kind < ObjectKind::Generic || definition.kind > ObjectKind::Lamp
        || definition.moveCost == 0 || definition.moveCost > 1000000
        || !ValidLightEmission(definition.light)) { return false; }
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
