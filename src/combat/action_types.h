#pragma once

#include "combat/combat_ids.h"
#include "combat/equipment_types.h"
#include "combat/damage.h"
#include "core/timing.h"

namespace cataclysmx {

enum class CombatTrigger : std::uint8_t { Attack, IncomingStrike, CounterOpening };
enum class MoveExecution : std::uint8_t { Strikes, Dodge, Parry, Block };
enum class AbilityExecution : std::uint8_t { Focus, Leap, Sweep };
enum class AbilityTargetKind : std::uint8_t { Self, Actor, Tile, CardinalDirection };
enum class CardinalDirection : std::uint8_t { North, East, South, West };
enum class AbilityGrantSourceKind : std::uint8_t { Art, Innate };

struct AbilityGrantKey {
    AbilityGrantSourceKind kind = AbilityGrantSourceKind::Art;
    CombatArtId art{}; // Only Art; Innate is the actor's intrinsic grant.
    bool operator==(const AbilityGrantKey&) const = default;
};
struct AbilityTarget {
    AbilityTargetKind kind = AbilityTargetKind::Self;
    PersistentId actor = 0;
    TileCoord tile{};
    CardinalDirection direction = CardinalDirection::North;
    bool operator==(const AbilityTarget&) const = default;
};
struct AbilityRequest {
    AbilityId ability{};
    AbilityGrantKey source;
    AbilityTarget target;
    bool operator==(const AbilityRequest&) const = default;
};
struct ActionCost {
    std::uint32_t windupCost = 0;
    std::uint32_t recoveryCost = 100;
    std::uint32_t staminaCost = 5;
    bool operator==(const ActionCost&) const = default;
};
enum class CombatRefusal : std::uint8_t {
    None, InvalidActor, Dead, Inactive, Busy, NoTarget, NotHostile, Unperceived,
    OutOfReach, Obstructed, NoMove, Locked, MissingSource, InvalidGrip,
    MissingCapability, NeedsFreeHand, Exhausted, Cooldown, Staggered,
    Unaware, MissingOpening, InvalidTarget, MissingGrant, MissingReference,
    AlreadyKnown, InvalidDefinition
};
struct CombatAvailability {
    CombatRefusal reason = CombatRefusal::None;
    ActionCost cost;
    bool Available() const { return reason == CombatRefusal::None; }
    bool operator==(const CombatAvailability&) const = default;
};
struct StyleChangeResult {
    CombatRefusal reason = CombatRefusal::None;
    Tick cost = 0;
    bool committed = false;
    bool operator==(const StyleChangeResult&) const = default;
};
struct AbilityResult {
    CombatRefusal reason = CombatRefusal::None;
    AbilityId ability{};
    Tick cost = 0;
    bool committed = false;
    bool pending = false;
    bool operator==(const AbilityResult&) const = default;
};
enum class ArtGrantReason : std::uint8_t { CharacterCreation, Teaching, Manual, Administration };
struct ArtStudyResult {
    CombatRefusal reason = CombatRefusal::None;
    Tick duration = 0;
    std::uint32_t successChance = 0;
    bool committed = false;
    bool operator==(const ArtStudyResult&) const = default;
};

}
