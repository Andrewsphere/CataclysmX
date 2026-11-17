#pragma once

#include "combat/character_combat.h"
#include "combat/equipment_types.h"
#include "core/data_object.h"
#include "core/state_result.h"

namespace cataclysmx {
class ContentCatalog;
class World;
class Actor;

struct SavedHeldItem {
    PersistentId item = 0;
    GripKind grip = GripKind::OneHanded;
    bool operator==(const SavedHeldItem&) const = default;
};
struct SavedEquipment {
    eastl::array<SavedHeldItem, 2> hands{};
    Vector<PersistentId> worn;
};

DataObject CaptureEquipment(const World& world, const Actor& actor);
StateResult<SavedEquipment> ReadEquipment(const DataObject& data, Diagnostics* diagnostics = nullptr);
DataObject CaptureCharacterCombat(const CharacterCombatState* state, const ContentCatalog& content);
StateResult<eastl::unique_ptr<CharacterCombatState>> ReadCharacterCombat(
    const DataObject& data, const ContentCatalog& content, Diagnostics* diagnostics = nullptr);
}
