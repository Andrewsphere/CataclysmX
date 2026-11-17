#pragma once

#include "core/world.h"
#include "combat/combat_rules.h"
#include "content/combat_definition.h"

namespace cataclysmx::combat_runtime {

struct Candidate {
    CombatMoveId move{};
    SourceCommitment sources;
    ActionCost cost;
    std::uint32_t weight = 0;
};
bool Alive(const Actor* actor);
bool Aware(const World& world, const Actor& observer, const Actor& target);
CombatStyleId BasicStyle(const ContentCatalog& content, CombatStyle style);
CombatStyleId Style(const World& world, const Actor& actor);
CombatRefusal Ready(const World& world, ActorHandle handle);
bool HasArt(const Actor& actor, CombatArtId art);
bool Unlocked(const Actor& actor, const MoveUnlock& unlock, CombatArtId art);
bool SourcesValid(const World& world, const Actor& actor, const SourceCommitment& sources);
void Bind(const World& world, const Actor& actor, const CombatMoveDefinition& move,
    Vector<SourceCommitment>& output);
CombatRefusal SourceRefusal(const World& world, const Actor& actor, const CombatMoveDefinition& move);
SkillId Handling(const World& world, const Actor& actor, const SourceCommitment& sources,
    StrikeSource source = StrikeSource::Primary);
const MeleeProfile* Profile(const World& world, const Actor& actor,
    const SourceCommitment& sources, StrikeSource source = StrikeSource::Primary);
const GripDefinition* Grip(const World& world, const Actor& actor,
    const SourceCommitment& sources, StrikeSource source = StrikeSource::Primary);
Tick Cooldown(const CombatState& state, CombatMoveId move);
void SetCooldown(CombatState& state, CombatMoveId move, Tick duration);
bool Reach(const World& world, const Actor& actor, const Actor& target, const ReachProfile& reach);
bool StrikeReach(const World& world, const Actor& actor, const Actor& target, const StrikeDefinition& strike);
bool ClearPath(const World& world, TileCoord from, TileCoord to);
bool LeapValid(const World& world, const Actor& actor, TileCoord to, unsigned distance);
void Candidates(const World& world, const Actor& actor, CombatTrigger trigger,
    const Actor* target, Vector<Candidate>& output);
Candidate Select(const World& world, const Vector<Candidate>& candidates, std::uint32_t& rng);
ActionCost Cost(const World& world, const Actor& actor, const CombatMoveDefinition& move,
    const SourceCommitment& sources);
ActionCost AbilityCost(const World& world, const Actor& actor, const AbilityDefinition& ability,
    const SourceCommitment& sources);
EquipmentModifiers Modifiers(const World& world, const Actor& actor);
std::uint32_t HandlingEncumbrance(const EquipmentModifiers& modifiers, const SourceCommitment& sources,
    StrikeSource source = StrikeSource::Primary);
std::uint32_t MovementEncumbranceCost(const World& world, const Actor& actor, std::uint32_t baseCost);
ContactRegion Contact(const Actor& target, const StrikeDefinition& strike, const ReachProfile& sourceReach, std::uint32_t& rng);
std::int32_t Rating(const World& world, const Actor& actor, SkillId handling,
    const SourceCommitment& sources, StrikeSource source, DefenseKind defense,
    std::int32_t bonus = 0, std::int32_t penalty = 0);
int OffensiveRank(const World& world, const Actor& actor, SkillId handling);
int DefensiveRank(const World& world, const Actor& actor);
bool Granted(const World& world, const Actor& actor, AbilityId ability, AbilityGrantKey source);
void RefreshAbilityGrants(World& world, ActorHandle actor);
Tick AbilityCooldownRemaining(const Actor& actor, AbilityId ability, AbilityGrantKey source);
void Opening(CombatState& state, PersistentId opponent, Tick duration);
bool HasOpening(const CombatState& state, PersistentId opponent);
bool EffectApplies(EffectCondition condition, const StrikeResult& result);
}
