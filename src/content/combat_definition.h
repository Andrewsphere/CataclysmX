#pragma once

#include "combat/action_types.h"
#include "combat/contact.h"
#include "content/skill_definition.h"

namespace cataclysmx {

struct CombatSkillIds {
    SkillId melee{}, unarmed{}, knives{}, swords{}, clubsHammers{};
    SkillId axes{}, polearms{}, shields{}, dodge{};
    bool operator==(const CombatSkillIds&) const = default;
};
struct MoveUnlock {
    SmallVector<SkillRequirement, 1> skills; // Learned levels, all required.
    std::uint64_t artXp = 0;
    bool operator==(const MoveUnlock&) const = default;
};
struct MoveRequirements {
    // Primary and secondary describe explicit paired sources, never substitution.
    SourceRequirement primary;
    SourceRequirement secondary;
    bool paired = false;
    SmallVector<SkillId, 1> handlingSkills; // Empty allows any handling family.
    bool requiresAwareness = true;
    bool requiresOpening = false;
    bool allowedDuringRecovery = true;
    bool allowedDuringWindup = false; // Also requires disjoint committed limbs.
    bool exhaustedOnly = false;
    bool operator==(const MoveRequirements&) const = default;
};
enum class StrikeSource : std::uint8_t { Primary, Secondary };
struct StrikeDefinition {
    StrikeSource source = StrikeSource::Primary;
    ReachProfile reach;
    SmallVector<ContactWeight, 2> contacts; // Empty uses anatomy baseline weights.
    // Empty damage uses the committed weapon/natural profile, otherwise replaces it.
    SmallVector<DamagePacket, 1> damage;
    std::uint32_t damageRatio = ratioScale;
    std::int32_t ratingBonus100 = 0;
    std::uint32_t intrinsicMiss = 500;
    DefenseMask defenses = DefenseMask::Melee;
    bool operator==(const StrikeDefinition&) const = default;
};
struct DefenseDefinition {
    DefenseKind kind = DefenseKind::None;
    std::int32_t ratingBonus100 = 0;
    std::uint32_t blockCapacity = 0; // Hundredths HP, added to source capacity.
    bool usesSourceCapacity = true;
    // Bare-handed parry must explicitly opt in to armed attacks.
    bool permitsArmedAttack = false;
    bool operator==(const DefenseDefinition&) const = default;
};
enum class EffectCondition : std::uint8_t {
    Always, Miss, DefenseSucceeded, Contact, DamagingContact, Killed
};
struct MovePreparation {
    // An authored vulnerability is granted to the target against the actor.
    Tick exposeOpening = 0;
    bool operator==(const MovePreparation&) const = default;
};
struct MoveFollowThrough {
    EffectCondition condition = EffectCondition::DamagingContact;
    Tick stagger = 0;
    std::uint8_t knockbackTiles = 0;
    // Successful defender gains an opening against incoming attacker.
    Tick counterOpening = 0;
    bool operator==(const MoveFollowThrough&) const = default;
};
struct CombatTrainingDefinition {
    std::uint64_t work = ratioScale; // Total per action, split over valid strikes.
    std::uint32_t meleeShare = 4000;
    std::uint32_t handlingShare = 6000;
    std::uint32_t artXpPerWork = ratioScale;
    bool operator==(const CombatTrainingDefinition&) const = default;
};
struct CombatMoveDefinition {
    String id;
    String name;
    CombatTrigger trigger = CombatTrigger::Attack;
    MoveExecution execution = MoveExecution::Strikes;
    MoveRequirements requirements;
    MoveUnlock unlock;
    ActionCost cost;
    Tick cooldown = 0;
    Vector<StrikeDefinition> strikes;
    DefenseDefinition defense;
    MovePreparation preparation;
    MoveFollowThrough followThrough;
    CombatTrainingDefinition training;
    bool operator==(const CombatMoveDefinition&) const = default;
};
struct WeightedCombatMove {
    CombatMoveId move{};
    std::uint32_t weight = 1;
    bool operator==(const WeightedCombatMove&) const = default;
};
struct CombatStyleDefinition {
    String id;
    String name;
    CombatArtId art{};
    Vector<WeightedCombatMove> moves;
    std::uint32_t switchCost = 20;
    SmallVector<SkillId, 1> excludedAttackHandlingSkills; // Held-item strike sources only.
    bool operator==(const CombatStyleDefinition&) const = default;
};
struct ArtAbilityGrant {
    AbilityId ability{};
    MoveUnlock unlock;
    bool operator==(const ArtAbilityGrant&) const = default;
};
struct CombatArtDefinition {
    String id;
    String name;
    Vector<CombatStyleId> styles;
    Vector<ArtAbilityGrant> abilities;
    bool operator==(const CombatArtDefinition&) const = default;
};
struct AbilityDefinition {
    String id;
    String name;
    AbilityExecution execution = AbilityExecution::Focus;
    AbilityTargetKind targeting = AbilityTargetKind::Self;
    ActionCost cost;
    Tick cooldown = 0;
    bool interruptOnDamage = false;
    bool interruptOnForcedMovement = true;
    Tick focusDuration = 0;
    std::int32_t focusRatingBonus100 = 0;
    std::uint8_t leapDistance = 2;
    // Sweep borrows a strike move's sources/strikes; ability pays its own cost
    // once and owns one training budget. No automatic move selection here.
    CombatMoveId sweepMove{};
    CombatTrainingDefinition training;
    bool operator==(const AbilityDefinition&) const = default;
};
struct ArtManualDefinition {
    CombatArtId art{};
    SkillRequirement prerequisite;
    std::uint32_t successChance = 0;
    Tick baseDuration = 0;
    bool operator==(const ArtManualDefinition&) const = default;
};

}
