#pragma once

#include "core/containers.h"
#include "core/world_types.h"
#include "core/ratio.h"
#include <EASTL/array.h>

namespace cataclysmx {

enum class Hand : std::uint8_t { Left, Right };
enum class HandMask : std::uint8_t { None = 0, Left = 1, Right = 2, Both = 3 };
enum class LegMask : std::uint8_t { None = 0, Left = 1, Right = 2, Both = 3 };
enum class GripKind : std::uint8_t { OneHanded, TwoHanded };

struct GripDefinition {
    GripKind kind = GripKind::OneHanded;
    std::uint32_t damageRatio = ratioScale;
    std::uint32_t costRatio = ratioScale;
    std::uint32_t staminaRatio = ratioScale;
    std::uint32_t blockRatio = ratioScale;
    std::int32_t ratingBonus100 = 0;
    bool operator==(const GripDefinition&) const = default;
};
struct HeldItem {
    ItemHandle item{};
    GripKind grip = GripKind::OneHanded;
    bool operator==(const HeldItem&) const = default;
};
struct EquipmentState {
    // Two-handed occupancy repeats the reference, never ownership or mass.
    eastl::array<HeldItem, 2> hands{};
    Vector<ItemHandle> worn;
    bool operator==(const EquipmentState&) const = default;
};

enum class AttackSourceKind : std::uint8_t { HeldItem, EmptyHand, Kick, Natural };
enum class SourceHand : std::uint8_t { Either, Left, Right, Both };
struct SourceRequirement {
    AttackSourceKind kind = AttackSourceKind::EmptyHand;
    SourceHand hand = SourceHand::Either;
    HandMask freeHands = HandMask::None;
    HandMask functionalHands = HandMask::None;
    LegMask functionalLegs = LegMask::None;
    bool requiresGrip = false;
    GripKind grip = GripKind::OneHanded;
    bool requiresNaturalApparatus = true;
    bool operator==(const SourceRequirement&) const = default;
};

// Exactly two hand bindings, independent of authored strike count. Empty-hand
// commitments use item=0 and required=true. Unrelated equipment is not captured.
struct CommittedHand {
    PersistentId item = 0;
    GripKind grip = GripKind::OneHanded;
    bool required = false;
    bool operator==(const CommittedHand&) const = default;
};
struct SourceCommitment {
    eastl::array<CommittedHand, 2> hands{};
    HandMask functionalHands = HandMask::None;
    LegMask functionalLegs = LegMask::None;
    // Resolves Either/paired ordering without consulting current equipment.
    Hand primaryHand = Hand::Left;
    bool naturalApparatus = false;
    AttackSourceKind primaryKind = AttackSourceKind::EmptyHand;
    AttackSourceKind secondaryKind = AttackSourceKind::EmptyHand;
    bool paired = false;
    bool operator==(const SourceCommitment&) const = default;
};

enum class EquipmentOperation : std::uint8_t { Hold, Release, Regrip, Wear, Remove };
struct EquipmentRequest {
    EquipmentOperation operation = EquipmentOperation::Hold;
    PersistentId item = 0;
    Hand hand = Hand::Left;
    GripKind grip = GripKind::OneHanded;
    bool operator==(const EquipmentRequest&) const = default;
};

}
