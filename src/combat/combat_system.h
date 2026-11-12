#pragma once

#include "combat/combat_types.h"
#include "core/world_result.h"

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
    void Elapse(Tick elapsed);
    void ProcessEvents(std::uint32_t& rng);
    eastl::optional<Tick> NextEventIn() const;

private:
    World& world_;
    Vector<ActorHandle> elapsedActors_;
    Vector<ActorHandle> dueActors_;
    mutable Vector<ActorHandle> queriedActors_;
    AttackResult Strike(ActorHandle actor, ActorHandle target, CombatMove move,
        std::uint8_t accuracyPenalty, std::uint32_t& rng);
    void Record(ActorHandle actor, const AttackResult& result, bool incoming);
};

}
