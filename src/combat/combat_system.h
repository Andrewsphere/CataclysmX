#pragma once

#include "combat/combat_types.h"
#include "combat/action_types.h"
#include "core/world_result.h"
#include <EASTL/span.h>

namespace cataclysmx {

// World owns storage and this coordinator. All mutable actor access is scoped
// to these calls. Public actor inspection remains const; target references in
// pending actions are persistent IDs and may legitimately become stale.
class CombatSystem {
public:
    explicit CombatSystem(World& world);
    WorldResult<void> Configure(ActorHandle actor, const CombatState& state);
    WorldResult<void> SetStyle(ActorHandle actor, CombatStyle style);
    CombatView Describe(ActorHandle actor) const;
    // Own repertoire/capability query; does not discover a target or spend time.
    bool CanAttack(ActorHandle actor) const;
    ActorHandle FindTarget(ActorHandle actor) const;
    bool CanReach(ActorHandle actor, ActorHandle target) const;
    AttackResult Attack(ActorHandle actor, ActorHandle target, std::uint32_t& rng);
    WorldResult<void> StartAiming(ActorHandle actor, ActorHandle target, std::uint32_t baseCost);
    DisruptionResult Disrupt(ActorHandle actor, const Disruption& disruption);
    AttackResult Damage(ActorHandle actor, ActorHandle source, BodyPart part, DamagePacket damage);
    AttackResult DamageContact(ActorHandle actor, ActorHandle source, BodyPart part,
        eastl::span<const DamagePacket> damage);
    void Elapse(Tick elapsed);
    void ProcessEvents(std::uint32_t& rng);
    eastl::optional<Tick> NextEventIn() const;

    // Expansion entry points. Pure queries do not select or consume RNG.
    CombatAvailability QueryAttack(ActorHandle actor, ActorHandle target) const;
    void CollectAdjacentTargets(ActorHandle actor, Vector<ActorHandle>& output) const;
    ActorHandle ChooseAdjacentTarget(ActorHandle actor, std::uint32_t& rng);
    StyleChangeResult ChangeStyle(ActorHandle actor, CombatStyleId style);
    CombatAvailability QueryAbility(ActorHandle actor, const AbilityRequest& request) const;
    AbilityResult UseAbility(ActorHandle actor, const AbilityRequest& request, std::uint32_t& rng);
    WorldResult<void> GrantArt(ActorHandle actor, CombatArtId art, ArtGrantReason reason);
    // Learning integration owns work funding/completion; this facade returns
    // commitment/refusal without leaking skill/book implementation into clients.
    CombatAvailability QueryArtStudy(ActorHandle actor, const ArtManualRequest& request) const;
    ArtStudyResult StudyArt(ActorHandle actor, const ArtManualRequest& request);
    WorldResult<void> PauseArtStudy(ActorHandle actor);
    void ElapseArtStudy(Tick elapsed);
    void ProcessArtStudyEvents(std::uint32_t& rng);
    eastl::optional<Tick> NextArtStudyEventIn() const;
    void EmitEvent(const CombatEvent& event);
    // Experiment policy only: preserves skill levels while disabling combat XP.
    void SetTrainingEnabled(bool enabled);
    void CollectInfo(ActorHandle actor, CombatInfoView& output) const;
    // Physical contact uses clothing coverage RNG; old BodyPart overloads remain
    // raw/legacy adapters and must not be used by expanded physical strikes.
    StrikeResult DamageContact(ActorHandle actor, ActorHandle source, ContactRegion region,
        eastl::span<const DamagePacket> damage, std::uint32_t& rng);
    void DrainEvents(Vector<CombatEvent>& output);

private:
    World& world_;
    Vector<ActorHandle> elapsedActors_;
    Vector<ActorHandle> dueActors_;
    mutable Vector<ActorHandle> queriedActors_;
    Vector<CombatEvent> events_;
    struct TrainingNotice {
        PersistentId learner = 0;
        PersistentId opponent = 0;
        Tick remaining = 0;
        bool suppressed = false;
    };
    Vector<TrainingNotice> trainingNotices_;
    bool trainingEnabled_ = true;
    AttackResult Strike(ActorHandle actor, ActorHandle target, CombatMove move,
        std::uint8_t accuracyPenalty, std::uint32_t& rng);
    void Record(ActorHandle actor, const AttackResult& result, bool incoming);
};

}
