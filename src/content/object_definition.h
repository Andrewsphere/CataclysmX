#pragma once

#include "items/item_types.h"
#include "core/object_appearance.h"
#include "core/light_types.h"

#include <EASTL/optional.h>

namespace cataclysmx {

struct ObjectTypeId {
    std::uint32_t value = 0;
    bool operator==(const ObjectTypeId&) const = default;
};

enum class ObjectBehaviorKind { None, CraftingMachine };

struct CraftingMachineDefinition {
    ItemTypeId input{};
    std::uint32_t inputCount = 0;
    ItemTypeId output{};
    std::uint32_t outputCount = 0;
    Tick duration = 0;
    std::uint64_t energyCapacity = 0;
    std::uint32_t energyPerTick = 0;
    bool operator==(const CraftingMachineDefinition&) const = default;
};

struct ObjectDefinition {
    String id;
    String name;
    ObjectBehaviorKind behavior = ObjectBehaviorKind::None;
    CraftingMachineDefinition machine;
    // Positive when present. Absence exposes no player storage; legacy internal
    // machine/generic contents remain supported independently of this property.
    eastl::optional<std::uint32_t> capacityVolumeMl;
    ObjectKind kind = ObjectKind::Generic;
    bool blocksMovement = false;
    bool blocksSight = false;
    bool movable = false;
    std::uint32_t moveCost = ticks_per_turn; // Base ticks per cardinal dragged step.
    LightEmission light{}; // Initial emission, gated by the object's enabled state.
    bool operator==(const ObjectDefinition&) const = default;
};

// Structural checks; content loading additionally resolves item references.
bool ValidObjectDefinition(const ObjectDefinition& definition);

}
