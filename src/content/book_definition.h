#pragma once

#include "content/crafting_definition.h"

namespace cataclysmx {
// Information identity, shared by equivalent printed/electronic carriers.
struct BookDefinition {
    String id;
    String name;
    std::uint32_t studyWork = 1000;
    SmallVector<SkillBookBenefit, 1> benefits;
    Vector<CraftingRecipeId> recipes;
};
}
