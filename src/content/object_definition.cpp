#include "content/object_definition.h"

#include <limits>

namespace cataclysmx {

bool ValidObjectDefinition(const ObjectDefinition& definition) {
    if (definition.behavior == ObjectBehaviorKind::None) {
        return definition.machine == CraftingMachineDefinition{};
    }
    if (definition.behavior != ObjectBehaviorKind::CraftingMachine) { return false; }
    const auto& machine = definition.machine;
    return machine.input.value != 0 && machine.output.value != 0
        && machine.inputCount > 0 && machine.inputCount <= 1024
        && machine.outputCount > 0 && machine.outputCount <= 1024
        && machine.duration > 0 && machine.duration <= std::numeric_limits<std::uint32_t>::max()
        && machine.energyPerTick > 0 && machine.energyCapacity >= machine.energyPerTick
        && machine.energyCapacity <= std::numeric_limits<std::uint64_t>::max() / 2;
}

}
