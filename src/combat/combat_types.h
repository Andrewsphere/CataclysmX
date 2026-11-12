#pragma once

#include "actors/action.h"
#include "combat/body.h"
#include "core/data_object.h"
#include "core/state_result.h"

namespace cataclysmx {

class World;

enum class CombatStyle : std::uint8_t { Balanced, Aggressive, Defensive };
enum class CombatFaction : std::uint8_t { Neutral, Survivors, Undead };
enum class DamageType : std::uint8_t { Blunt, Slashing, Piercing, Ballistic, Heat, Chemical, Choking, Count };
enum class CombatMove : std::uint32_t { None, PlainAttack, HeavyAttack, Bite, Parry };
inline constexpr std::size_t damage_type_count = static_cast<std::size_t>(DamageType::Count);

// Values are hundredths of HP; protection is local and damage-type-specific.
// Whole post-protection injury is passed to the body model. Sub-1 injury is blocked.
struct DamagePacket {
    DamageType type = DamageType::Blunt;
    std::uint32_t amount = 0;
    std::uint32_t penetration = 0;
};

struct CombatState {
    BodyState body = MakeBody(BodyKind::Detailed);
    InjuryProfile injury{};
    ActorActionState action{};
    CombatStyle style = CombatStyle::Balanced;
    CombatFaction faction = CombatFaction::Neutral;
    eastl::array<eastl::array<std::uint32_t, damage_type_count>, body_part_count> protection{};
    std::uint32_t attackDamage = 12; // Whole HP before protection.
    std::uint8_t accuracy = 90; // Percent; explicit RNG resolves contact, not selection.
    std::uint32_t stamina = 100;
    Tick staminaElapsed = 0;
    Tick reactionCooldown = 0; // Remaining active simulation time.
    Tick heavyCooldown = 0;
    bool operator==(const CombatState&) const = default;
};

enum class AttackOutcome : std::uint8_t {
    Unavailable, NotReady, NoTarget, OutOfReach, NoMove, Windup, Miss, Parried, Blocked, Hit, Defeated
};
struct AttackResult {
    AttackOutcome outcome = AttackOutcome::Unavailable;
    CombatMove move = CombatMove::None;
    PersistentId target = 0;
    BodyPart part = BodyPart::Count;
    std::uint32_t injury = 0;
    Tick cost = 0;
    bool committed = false;
    bool contact = false;
    bool lostCapability = false;
    DisruptionResponse disruption = DisruptionResponse::Ignored;
    bool operator==(const AttackResult&) const = default;
};

// Presentation-only last interaction, intentionally not part of replay/save state.
struct CombatFeedback {
    AttackResult result{};
    bool incoming = false;
    Tick tick = 0;
    bool operator==(const CombatFeedback&) const = default;
};
struct CombatPartView {
    std::uint32_t hp = 0; // Hundredths, as in BodyState.
    std::uint32_t maximum = 0;
    bool broken = false;
    bool operator==(const CombatPartView&) const = default;
};
struct CombatView {
    BodyKind bodyKind = BodyKind::Detailed;
    CombatStyle style = CombatStyle::Balanced;
    ActorActionKind action = ActorActionKind::None;
    std::uint32_t stamina = 100;
    std::uint8_t accuracyPenalty = 0;
    eastl::array<CombatPartView, body_part_count> parts{};
    CombatFeedback feedback{};
    bool operator==(const CombatView&) const = default;
};

CombatState MakeCombatState(bool player);
bool Hostile(CombatFaction a, CombatFaction b);
bool ValidateCombatState(const CombatState& state);
DataObject CaptureCombatState(const CombatState& state);
StateResult<CombatState> ReadCombatState(const DataObject& data, Diagnostics* diagnostics = nullptr);
const char* CombatStyleName(CombatStyle style);

}
