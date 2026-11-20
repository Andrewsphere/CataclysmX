#pragma once

#include "content/crafting_definition.h"

namespace cataclysmx {

struct InformationId {
    std::uint32_t value = 0;
    bool operator==(const InformationId&) const = default;
};
struct MachineRecipeId {
    std::uint32_t value = 0;
    bool operator==(const MachineRecipeId&) const = default;
};
enum class DigitalDeviceKind : std::uint8_t { None, Storage, Reader, Controller };
struct InformationDefinition {
    String id;
    String name;
    Vector<BookId> books;
    Vector<MachineRecipeId> designs;
    bool software = false;
};
struct MachineCompletionLearning {
    SkillId skill{};
    std::uint8_t level = 0;
    std::uint32_t xpPerExecution = 0;
    bool operator==(const MachineCompletionLearning&) const = default;
};
struct MachineRecipeDefinition {
    String id;
    String name;
    StaticList<RecipeInput, maxRecipeInputs> inputs;
    ItemOutput result;
    Tick duration = 0;
    std::uint32_t energyPerTick = 0;
    std::uint32_t setupWork = 200;
    std::uint32_t setupWorkPerUnit = 20;
    SmallVector<SkillRequirement, 1> skills;
    MachineCompletionLearning learning;
    InformationId requiredSoftware{};
};

}
