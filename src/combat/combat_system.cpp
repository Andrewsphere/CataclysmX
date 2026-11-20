#include "combat/combat_system.h"
#include "combat/combat_runtime.h"
#include "core/static_list.h"
#include "skills/learning_system.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstdlib>
#include <limits>

namespace cataclysmx {
namespace {
namespace runtime = combat_runtime;
constexpr Tick trainingEncounterInactivity = 30 * ticks_per_turn;
void Require(bool condition) { if (!condition) { std::abort(); } }
Tick Reduced(Tick value, Tick elapsed) { return value > elapsed ? value - elapsed : 0; }
bool Available(const World& world, const Actor* actor) {
    return runtime::Alive(actor) && !actor->parked && world.IsActive(actor->position);
}
CombatMove Projection(const CombatMoveDefinition& move) {
    if (move.execution == MoveExecution::Parry) { return CombatMove::Parry; }
    return move.cost.windupCost ? CombatMove::HeavyAttack : CombatMove::PlainAttack;
}
CombatStyle StyleProjection(const ContentCatalog& content, CombatStyleId style) {
    if (style == runtime::BasicStyle(content, CombatStyle::Aggressive)) { return CombatStyle::Aggressive; }
    if (style == runtime::BasicStyle(content, CombatStyle::Defensive)) { return CombatStyle::Defensive; }
    return CombatStyle::Balanced;
}
AttackOutcome RefusedOutcome(CombatRefusal reason) {
    switch (reason) {
    case CombatRefusal::Busy: case CombatRefusal::Staggered: return AttackOutcome::NotReady;
    case CombatRefusal::NoTarget: case CombatRefusal::NotHostile: case CombatRefusal::Unperceived: return AttackOutcome::NoTarget;
    case CombatRefusal::OutOfReach: case CombatRefusal::Obstructed: return AttackOutcome::OutOfReach;
    case CombatRefusal::NoMove: case CombatRefusal::Exhausted: case CombatRefusal::MissingSource:
    case CombatRefusal::MissingCapability: case CombatRefusal::InvalidGrip: case CombatRefusal::NeedsFreeHand:
    case CombatRefusal::Cooldown: case CombatRefusal::Locked: return AttackOutcome::NoMove;
    default: return AttackOutcome::Unavailable;
    }
}
ActorActionState Commitment(const World& world, const Actor& actor, const runtime::Candidate& candidate, PersistentId target) {
    const auto& move = world.GetContent().GetCombatMove(candidate.move);
    ActorActionState action;
    action.kind = candidate.cost.windupCost ? ActorActionKind::MeleeWindup : ActorActionKind::Recovery;
    action.target = target;
    action.move = static_cast<std::uint32_t>(Projection(move));
    action.moveId = candidate.move;
    action.committedStyle = runtime::Style(world, actor);
    action.trainingArt = world.GetContent().GetCombatStyle(action.committedStyle).art;
    action.sources = candidate.sources;
    action.recoveryTicks = ActionTicks(candidate.cost.recoveryCost, actor.speed);
    action.weapon = candidate.sources.hands[static_cast<unsigned>(candidate.sources.primaryHand)].item;
    action.trainingWork = move.training.work;
    action.definitionDriven = true;
    return action;
}
void Pay(CombatState& state, std::uint32_t stamina, Tick lock) {
    if (!state.inexhaustible) { Require(state.stamina >= stamina); state.stamina -= stamina; }
    state.combatLockRemaining = eastl::max(state.combatLockRemaining, lock);
}
CombatEvent CommitmentEvent(const World& world, const Actor& actor, const ActorActionState& action,
    Tick cost, std::uint32_t stamina) {
    CombatEvent event;
    event.tick = world.GetTick(); event.kind = CombatEventKind::ActionCommitted;
    event.actor = actor.id; event.opponent = action.target; event.move = action.moveId;
    event.ability = action.ability; event.art = action.trainingArt; event.cost = cost;
    event.staminaSpent = actor.combat.inexhaustible ? 0 : stamina;
    return event;
}
bool TrainSkillWork(World& world, ActorHandle handle, SkillId skill, int opponentRank, std::uint64_t work) {
    if (!skill.value) { return false; }
    // Split precisely at learned-level boundaries so difficulty is recomputed
    // after every level. Train owns multiplier/reserve accounting.
    while (work) {
        const auto* actor = world.GetActor(handle);
        if (!actor) { return false; }
        const auto level = LearnedSkillLevel(actor->learning, skill);
        const auto ratio = CombatTrainingRatio(opponentRank, level);
        if (!ratio) { return true; }
        const auto& definition = world.GetContent().GetSkill(skill);
        if (level == definition.maxLevel) { return false; }
        const auto* progress = FindSkillProgress(actor->learning, skill);
        const auto required = SkillXpRequired(definition, level);
        const auto missing = required - (progress ? progress->xp : 0);
        const auto multiplier = progress ? progress->multiplier : ratioScale;
        const auto boostRemainder = progress ? progress->boostRemainder : 0;
        const auto workRemainder = progress ? progress->workRemainder : 0;
        const auto numerator = missing * ratioScale - boostRemainder;
        const auto rawNeeded = numerator / multiplier + (numerator % multiplier != 0);
        const auto workNumerator = rawNeeded * ratioScale - workRemainder;
        const auto untilLevel = workNumerator / ratio + (workNumerator % ratio != 0);
        const auto chunk = eastl::min(work, eastl::max(std::uint64_t{1}, untilLevel));
        Require(world.Learning().Train(handle, {skill, definition.maxLevel, ratio}, SkillActivity::Combat, chunk).has_value());
        work -= chunk;
    }
    return false;
}
bool TrainWork(World& world, ActorHandle handle, SkillId handling, DefenseKind kind,
    int opponentRank, std::uint64_t work, const CombatTrainingDefinition& training) {
    const auto& skills = world.GetContent().GetCombatSkills();
    if (kind == DefenseKind::Dodge) { return TrainSkillWork(world, handle, skills.dodge, opponentRank, work); }
    const auto melee = work / ratioScale * training.meleeShare + work % ratioScale * training.meleeShare / ratioScale;
    const auto handled = work - melee; // Assign the integer remainder; never lose action work.
    if (handling == skills.melee) { return TrainSkillWork(world, handle, handling, opponentRank, melee + handled); }
    const bool meleeSuppressed = TrainSkillWork(world, handle, skills.melee, opponentRank, melee);
    const bool handlingSuppressed = TrainSkillWork(world, handle, handling, opponentRank, handled);
    return meleeSuppressed || handlingSuppressed;
}
void Packets(const World& world, const Actor& actor, const ActorActionState& action,
    const StrikeDefinition& strike, StaticList<DamagePacket, damage_type_count>& output) {
    const auto* profile = runtime::Profile(world, actor, action.sources, strike.source);
    const auto* grip = runtime::Grip(world, actor, action.sources, strike.source);
    const auto append = [&](DamagePacket packet) {
        const auto amount = std::uint64_t{packet.amount} * strike.damageRatio / ratioScale;
        const auto scaled = amount * (grip ? grip->damageRatio : ratioScale) / ratioScale;
        Require(scaled <= UINT32_MAX); packet.amount = static_cast<std::uint32_t>(scaled);
        Require(output.PushBack(packet));
    };
    if (!strike.damage.empty()) { for (const auto packet : strike.damage) { append(packet); } }
    else if (profile) { for (const auto packet : profile->damage) { append(packet); } }
    else { append({DamageType::Blunt, actor.combat.attackDamage * body_hp_scale, 0}); }
}
}

CombatSystem::CombatSystem(World& world) : world_(world) {}
void CombatSystem::SetTrainingEnabled(bool enabled) { trainingEnabled_ = enabled; }

WorldResult<void> CombatSystem::Configure(ActorHandle handle, const CombatState& state) {
    const auto* actor = world_.GetActor(handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!ValidateCombatState(state) || state.action.kind != ActorActionKind::None ||
        actor->combat.action.kind != ActorActionKind::None || world_.GetActorRemainingTicks(handle)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    auto bound = state;
    if (!bound.styleId.value) { bound.styleId = runtime::BasicStyle(world_.GetContent(), bound.style); }
    if (!world_.GetContent().TryGetCombatStyle(bound.styleId)) { return eastl::unexpected(WorldError::invalid_request); }
    bound.style = StyleProjection(world_.GetContent(), bound.styleId);
    const auto remainder = std::uint64_t{bound.staminaRegenerationRemainder} + bound.staminaElapsed * ratioScale / 10;
    if (remainder >= ratioScale) { return eastl::unexpected(WorldError::invalid_request); }
    bound.staminaRegenerationRemainder = static_cast<std::uint32_t>(remainder);
    bound.staminaElapsed = 0;
    if (!ValidateCombatState(bound, world_.GetContent())) { return eastl::unexpected(WorldError::invalid_request); }
    // Installing a valid terminal setup must still take World's live-to-dead
    // transition: pre-setting Dead would make MarkActorDead skip occupancy,
    // deadlines, study cancellation and the one terminal notification.
    if (bound.life == ActorLifeState::Dead && actor->combat.life == ActorLifeState::Alive) {
        bound.life = ActorLifeState::Alive;
    }
    world_.MutableActor(handle)->combat = eastl::move(bound);
    world_.ActorChanged(handle);
    if (!runtime::Alive(world_.GetActor(handle))) { world_.MarkActorDead(handle); }
    return {};
}
WorldResult<void> CombatSystem::SetStyle(ActorHandle handle, CombatStyle style) {
    if (!world_.GetActor(handle)) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto id = runtime::BasicStyle(world_.GetContent(), style);
    if (!id.value) { return eastl::unexpected(WorldError::invalid_request); }
    auto* actor = world_.MutableActor(handle);
    actor->combat.styleId = id; actor->combat.style = style;
    world_.ActorChanged(handle);
    return {};
}
StyleChangeResult CombatSystem::ChangeStyle(ActorHandle handle, CombatStyleId styleId) {
    StyleChangeResult result;
    result.reason = runtime::Ready(world_, handle);
    if (result.reason != CombatRefusal::None) { return result; }
    const auto* style = world_.GetContent().TryGetCombatStyle(styleId);
    if (!style) { result.reason = CombatRefusal::InvalidDefinition; return result; }
    if (!runtime::HasArt(*world_.GetActor(handle), style->art)) { result.reason = CombatRefusal::Locked; return result; }
    if (runtime::Style(world_, *world_.GetActor(handle)) == styleId) { return result; }
    const auto speed = world_.GetActor(handle)->speed;
    Require(world_.ScheduleActorAction(handle, style->switchCost).has_value());
    auto* actor = world_.MutableActor(handle);
    actor->combat.styleId = styleId; actor->combat.style = StyleProjection(world_.GetContent(), styleId);
    actor->combat.action = {ActorActionKind::Recovery};
    result.cost = ActionTicks(style->switchCost, speed); result.committed = true;
    Pay(actor->combat, 0, result.cost);
    EmitEvent(CommitmentEvent(world_, *actor, actor->combat.action, result.cost, 0));
    world_.ActorChanged(handle);
    return result;
}
CombatView CombatSystem::Describe(ActorHandle handle) const {
    CombatView view;
    const auto* actor = world_.GetActor(handle); if (!actor) { return view; }
    const auto& state = actor->combat;
    view.bodyKind = state.body.kind; view.style = state.style; view.styleId = runtime::Style(world_, *actor);
    view.action = state.action.kind; view.stamina = state.stamina; view.life = state.life;
    view.inexhaustible = state.inexhaustible; view.staggerRemaining = state.staggerRemaining;
    view.focusRemaining = state.focusRemaining; view.accuracyPenalty = state.action.accuracyPenalty;
    view.feedback = actor->combatFeedback;
    for (std::size_t i = 0; i < body_part_count; ++i) {
        const auto part = static_cast<BodyPart>(i);
        if (HasPart(state.body.kind, part)) { view.parts[i] = {state.body.parts[i].hp, EffectiveMax(state.body, part), state.body.parts[i].broken}; }
    }
    return view;
}
void CombatSystem::Record(ActorHandle handle, const AttackResult& result, bool incoming) {
    if (auto* actor = world_.MutableActor(handle)) {
        actor->combatFeedback = {result, incoming, world_.GetTick()}; world_.ActorChanged(handle);
    }
}
void CombatSystem::EmitEvent(const CombatEvent& event) {
    constexpr std::size_t maximumEvents = 4096;
    if (events_.size() == maximumEvents) { events_.erase(events_.begin()); }
    if (event.kind == CombatEventKind::Strike && event.strike.killed) {
        // World marks life immediately and publishes its one death notification
        // during damage resolution. Complete this atomic contact's presentation
        // envelope before that notification; no caller can drain mid-resolution.
        const auto death = eastl::find_if(events_.begin(), events_.end(), [&](const CombatEvent& queued) {
            return queued.kind == CombatEventKind::ActorDied && queued.tick == event.tick &&
                queued.actor == event.strike.target && queued.opponent == event.strike.attacker;
        });
        if (death != events_.end()) { events_.insert(death, event); return; }
    }
    events_.push_back(event);
}
void CombatSystem::DrainEvents(Vector<CombatEvent>& output) {
    output.clear(); output.insert(output.end(), events_.begin(), events_.end()); events_.clear();
}
bool CombatSystem::CanAttack(ActorHandle handle) const {
    const auto* actor = world_.GetActor(handle);
    if (!Available(world_, actor) || world_.Learning().Busy(handle)) { return false; }
    Vector<runtime::Candidate> candidates;
    runtime::Candidates(world_, *actor, CombatTrigger::Attack, nullptr, candidates);
    return !candidates.empty();
}
bool CombatSystem::CanReach(ActorHandle handle, ActorHandle target) const {
    const auto* actor = world_.GetActor(handle); const auto* other = world_.GetActor(target);
    if (!actor || !other) { return false; }
    return runtime::Reach(world_, *actor, *other, actor->combat.reach);
}
CombatAvailability CombatSystem::QueryAttack(ActorHandle handle, ActorHandle target) const {
    CombatAvailability result;
    result.reason = runtime::Ready(world_, handle); if (!result.Available()) { return result; }
    const auto* actor = world_.GetActor(handle); const auto* other = world_.GetActor(target);
    if (!Available(world_, other) || handle == target) { result.reason = CombatRefusal::NoTarget; return result; }
    if (!Hostile(actor->combat.faction, other->combat.faction)) { result.reason = CombatRefusal::NotHostile; return result; }
    if (!runtime::Aware(world_, *actor, *other)) { result.reason = CombatRefusal::Unperceived; return result; }
    if (!CanReach(handle, target)) { result.reason = CombatRefusal::OutOfReach; return result; }
    Vector<runtime::Candidate> candidates;
    runtime::Candidates(world_, *actor, CombatTrigger::Attack, other, candidates);
    if (candidates.empty()) {
        result.reason = CombatRefusal::NoMove;
        const auto* style = world_.GetContent().TryGetCombatStyle(runtime::Style(world_, *actor));
        if (style) { for (const auto& weighted : style->moves) {
            const auto& move = world_.GetContent().GetCombatMove(weighted.move);
            if (move.trigger != CombatTrigger::Attack || move.requirements.exhaustedOnly) { continue; }
            if (!runtime::Unlocked(*actor, move.unlock, style->art)) { result.reason = CombatRefusal::Locked; continue; }
            if (runtime::Cooldown(actor->combat, weighted.move)) { result.reason = CombatRefusal::Cooldown; continue; }
            const auto sourceReason = runtime::SourceRefusal(world_, *actor, move);
            if (sourceReason != CombatRefusal::None) { result.reason = sourceReason; continue; }
            bool reachable = false;
            for (const auto& strike : move.strikes) { reachable |= runtime::StrikeReach(world_, *actor, *other, strike); }
            if (!reachable) { result.reason = CombatRefusal::OutOfReach; continue; }
            Vector<SourceCommitment> bindings; runtime::Bind(world_, *actor, move, bindings);
            result.cost = runtime::Cost(world_, *actor, move, bindings.front());
            result.reason = CombatRefusal::Exhausted;
        } }
    }
    else { result.cost = candidates.front().cost; }
    return result;
}
void CombatSystem::CollectAdjacentTargets(ActorHandle handle, Vector<ActorHandle>& output) const {
    output.clear(); const auto* actor = world_.GetActor(handle);
    if (!Available(world_, actor) || !CanAttack(handle)) { return; }
    const auto origin = actor->position;
    for (int z = -1; z <= 1; ++z) { for (int x = -1; x <= 1; ++x) {
        if (!x && !z) { continue; }
        const auto candidate = world_.ActorAt({origin.x + x, origin.z + z, origin.y});
        const auto* other = world_.GetActor(candidate);
        if (!Available(world_, other) || !Hostile(actor->combat.faction, other->combat.faction) ||
            !CanReach(handle, candidate) || !runtime::Aware(world_, *actor, *other)) { continue; }
        Vector<runtime::Candidate> moves;
        runtime::Candidates(world_, *actor, CombatTrigger::Attack, other, moves);
        if (!moves.empty()) { output.push_back(candidate); }
    } }
    eastl::sort(output.begin(), output.end(), [&](ActorHandle a, ActorHandle b) { return world_.GetActor(a)->id < world_.GetActor(b)->id; });
}
ActorHandle CombatSystem::FindTarget(ActorHandle handle) const {
    CollectAdjacentTargets(handle, queriedActors_); return queriedActors_.empty() ? ActorHandle{} : queriedActors_.front();
}
ActorHandle CombatSystem::ChooseAdjacentTarget(ActorHandle handle, std::uint32_t& rng) {
    CollectAdjacentTargets(handle, queriedActors_);
    return queriedActors_.empty() ? ActorHandle{} : queriedActors_[DrawBelow(rng, static_cast<std::uint32_t>(queriedActors_.size()))];
}
AttackResult CombatSystem::Attack(ActorHandle handle, ActorHandle target, std::uint32_t& rng) {
    AttackResult result;
    const auto availability = QueryAttack(handle, target);
    if (!availability.Available()) { result.reason = availability.reason; result.outcome = RefusedOutcome(result.reason); return result; }
    const auto* actor = world_.GetActor(handle); const auto* other = world_.GetActor(target);
    Vector<runtime::Candidate> candidates;
    runtime::Candidates(world_, *actor, CombatTrigger::Attack, other, candidates);
    const auto candidate = runtime::Select(world_, candidates, rng);
    const auto& move = world_.GetContent().GetCombatMove(candidate.move);
    const auto action = Commitment(world_, *actor, candidate, other->id);
    const auto initial = candidate.cost.windupCost ? candidate.cost.windupCost : candidate.cost.recoveryCost;
    const auto cost = ActionTicks(initial, actor->speed);
    const auto fullCost = candidate.cost.windupCost ? CheckedTickAdd(cost, action.recoveryTicks) : cost;
    CheckedTickAdd(world_.GetTick(), fullCost);
    Require(world_.ScheduleActorAction(handle, initial).has_value());
    auto* mutableActor = world_.MutableActor(handle);
    mutableActor->combat.styleId = action.committedStyle;
    mutableActor->combat.action = action;
    Pay(mutableActor->combat, candidate.cost.staminaCost, fullCost);
    runtime::SetCooldown(mutableActor->combat, candidate.move, move.cooldown);
    EmitEvent(CommitmentEvent(world_, *mutableActor, action, fullCost, candidate.cost.staminaCost));
    if (move.preparation.exposeOpening) {
        runtime::Opening(world_.MutableActor(target)->combat, mutableActor->id, move.preparation.exposeOpening);
        world_.ActorChanged(target);
    }
    world_.ActorChanged(handle);
    result.outcome = AttackOutcome::Windup; result.move = Projection(move); result.moveId = candidate.move;
    result.target = action.target; result.strikeCount = static_cast<std::uint32_t>(move.strikes.size());
    if (!candidate.cost.windupCost) { result = Strike(handle, target, result.move, 0, rng); }
    result.committed = true; result.cost = cost; Record(handle, result, false);
    return result;
}
WorldResult<void> CombatSystem::StartAiming(ActorHandle handle, ActorHandle target, std::uint32_t baseCost) {
    if (!world_.GetActor(handle)) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!baseCost || !QueryAttack(handle, target).Available()) { return eastl::unexpected(WorldError::invalid_request); }
    const auto targetId = world_.GetActor(target)->id;
    const auto result = world_.ScheduleActorAction(handle, baseCost); if (!result) { return result; }
    world_.MutableActor(handle)->combat.action = {ActorActionKind::Aiming, targetId}; world_.ActorChanged(handle);
    return {};
}
DisruptionResult CombatSystem::Disrupt(ActorHandle handle, const Disruption& disruption) {
    auto* actor = world_.MutableActor(handle);
    if (!actor || !runtime::Alive(actor)) { return {DisruptionResponse::Ignored, eastl::nullopt}; }
    const auto action = actor->combat.action;
    if (action.kind == ActorActionKind::AbilityWindup) {
        const auto* ability = world_.GetContent().TryGetAbility(action.ability);
        if (ability && ((disruption.cause == DisruptionCause::DamagingHit && !ability->interruptOnDamage) ||
            (disruption.cause == DisruptionCause::ForcedMovement && !ability->interruptOnForcedMovement))) {
            return {DisruptionResponse::Ignored, eastl::nullopt};
        }
    }
    const auto result = ApplyDisruption(actor->combat.action, disruption);
    if (result.nextDelay) { actor->actionDeadline.Set(world_.GetTick(), *result.nextDelay); }
    if (result.response == DisruptionResponse::Canceled && action.definitionDriven) {
        CombatEvent event; event.tick = world_.GetTick(); event.kind = CombatEventKind::ActionInvalidated;
        event.actor = actor->id; event.opponent = action.target; event.move = action.moveId; event.ability = action.ability;
        event.reason = disruption.cause == DisruptionCause::Stagger ? CombatRefusal::Staggered : CombatRefusal::MissingCapability;
        EmitEvent(event);
    }
    if (result.response != DisruptionResponse::Ignored) { world_.ActorChanged(handle); }
    return result;
}

AttackResult CombatSystem::Damage(ActorHandle handle, ActorHandle source, BodyPart part, DamagePacket damage) {
    return DamageContact(handle, source, part, {&damage, 1});
}
AttackResult CombatSystem::DamageContact(ActorHandle handle, ActorHandle source, BodyPart part, eastl::span<const DamagePacket> packets) {
    auto* actor = world_.MutableActor(handle);
    if (!Available(world_, actor)) { return {}; }
    Require(HasPart(actor->combat.body.kind, part));
    AttackResult result; result.target = actor->id; result.part = part; result.contact = true;
    std::uint64_t amount = 0;
    // Legacy/admin adapter deliberately bypasses worn armor (intrinsic protection
    // remains compatible with old setup callers).
    for (const auto& packet : packets) {
        Require(packet.type < DamageType::Count);
        const auto protection = actor->combat.protection[static_cast<unsigned>(part)][static_cast<unsigned>(packet.type)];
        const auto reduction = protection > packet.penetration ? protection - packet.penetration : 0;
        amount += packet.amount > reduction ? packet.amount - reduction : 0;
    }
    Require(amount / body_hp_scale <= UINT32_MAX);
    result.injury = static_cast<std::uint32_t>(amount / body_hp_scale);
    if (!result.injury) { result.outcome = AttackOutcome::Blocked; Record(handle, result, true); return result; }
    const auto before = actor->combat.body;
    const auto summary = ApplyInjury(actor->combat.body, part, result.injury, actor->combat.injury);
    result.totalHpLoss = before.parts[static_cast<unsigned>(summary.affectedPart)].hp -
        actor->combat.body.parts[static_cast<unsigned>(summary.affectedPart)].hp;
    result.lostCapability = summary.lostCapability;
    result.outcome = summary.defeated ? AttackOutcome::Defeated : AttackOutcome::Hit;
    const auto* sourceActor = world_.GetActor(source); const auto sourceId = sourceActor ? sourceActor->id : 0;
    world_.ActorChanged(handle);
    result.disruption = Disrupt(handle, {DisruptionCause::DamagingHit, sourceId}).response;
    actor = world_.MutableActor(handle);
    if (actor && actor->combat.action.definitionDriven && !runtime::SourcesValid(world_, *actor, actor->combat.action.sources)) {
        const auto disrupted = Disrupt(handle, {DisruptionCause::RequiredCapabilityLost, sourceId});
        if (disrupted.response != DisruptionResponse::Ignored) { result.disruption = disrupted.response; }
    }
    Record(handle, result, true);
    if (summary.defeated) {
        world_.MarkActorDead(handle, sourceId);
    }
    return result;
}
StrikeResult CombatSystem::DamageContact(ActorHandle handle, ActorHandle source, ContactRegion region,
    eastl::span<const DamagePacket> packets, std::uint32_t& rng) {
    StrikeResult result;
    const auto* actor = world_.GetActor(handle); const auto* attacker = world_.GetActor(source);
    result.attacker = attacker ? attacker->id : 0; result.target = actor ? actor->id : 0;
    if (!Available(world_, actor) || !HasContact(actor->combat.body.kind, region)) { result.reason = CombatRefusal::Dead; return result; }
    result.region = region; result.injuryPart = InjuryPartFor(region); result.outcome = StrikeOutcome::Contact;
    eastl::array<std::uint64_t, damage_type_count> protection{};
    for (unsigned i = 0; i < damage_type_count; ++i) {
        protection[i] = actor->combat.protection[static_cast<unsigned>(result.injuryPart)][i];
    }
    Vector<ItemHandle> worn = actor->equipment.worn;
    eastl::sort(worn.begin(), worn.end(), [&](ItemHandle a, ItemHandle b) {
        const auto* first = world_.GetItem(a); const auto* second = world_.GetItem(b);
        return (first ? first->id : 0) < (second ? second->id : 0);
    });
    for (const auto handleItem : worn) {
        const auto* item = world_.GetItem(handleItem); if (!item) { continue; }
        const auto& armor = world_.GetContent().GetItem(item->type).armor; if (!armor) { continue; }
        for (const auto& coverage : armor->regions) {
            if (coverage.region != region || !coverage.coverage) { continue; }
            if (coverage.coverage != ratioScale && DrawBelow(rng, ratioScale) >= coverage.coverage) { break; }
            for (unsigned i = 0; i < damage_type_count; ++i) { protection[i] += armor->protection[i]; }
            break;
        }
    }
    for (const auto& packet : packets) {
        Require(packet.type < DamageType::Count);
        result.incomingDamage += packet.amount;
        const auto value = protection[static_cast<unsigned>(packet.type)];
        const auto reduced = value > packet.penetration ? value - packet.penetration : 0;
        const auto absorbed = eastl::min<std::uint64_t>(packet.amount, reduced);
        result.armorAbsorbed += absorbed; result.postProtectionDamage += packet.amount - absorbed;
    }
    result.armorStopped = result.incomingDamage != 0 && result.postProtectionDamage == 0;
    const auto injury = result.postProtectionDamage / body_hp_scale;
    Require(injury <= UINT32_MAX);
    if (!injury) { return result; }
    auto* mutableActor = world_.MutableActor(handle);
    const auto before = mutableActor->combat.body;
    const auto summary = ApplyInjury(mutableActor->combat.body, result.injuryPart,
        static_cast<std::uint32_t>(injury), mutableActor->combat.injury);
    result.injuryPart = summary.affectedPart;
    const auto index = static_cast<unsigned>(summary.affectedPart);
    result.hpLoss = before.parts[index].hp - mutableActor->combat.body.parts[index].hp;
    result.maxHpLoss = mutableActor->combat.body.parts[index].maxLoss - before.parts[index].maxLoss;
    result.killed = summary.defeated;
    world_.ActorChanged(handle);
    result.disruption = Disrupt(handle, {DisruptionCause::DamagingHit, result.attacker}).response;
    mutableActor = world_.MutableActor(handle);
    if (mutableActor && mutableActor->combat.action.definitionDriven &&
        !runtime::SourcesValid(world_, *mutableActor, mutableActor->combat.action.sources)) {
        const auto disrupted = Disrupt(handle, {DisruptionCause::RequiredCapabilityLost, result.attacker});
        if (disrupted.response != DisruptionResponse::Ignored) { result.disruption = disrupted.response; }
    }
    if (result.killed) { world_.MarkActorDead(handle, result.attacker); }
    return result;
}

AttackResult CombatSystem::Strike(ActorHandle initialHandle, ActorHandle initialTarget, CombatMove, std::uint8_t, std::uint32_t& rng) {
    struct ResolutionJob { ActorHandle actor; ActorHandle target; };
    Vector<ResolutionJob> jobs;
    jobs.push_back({initialHandle, initialTarget});
    AttackResult rootResult;
    for (std::size_t jobIndex = 0; jobIndex < jobs.size(); ++jobIndex) {
    const auto handle = jobs[jobIndex].actor;
    auto target = jobs[jobIndex].target;
    AttackResult aggregate;
    const auto* actor = world_.GetActor(handle);
    if (!Available(world_, actor)) { continue; }
    auto action = actor->combat.action;
    Require(action.definitionDriven);
    const auto* ability = world_.GetContent().TryGetAbility(action.ability);
    const auto invalidate = [&](CombatRefusal reason) {
        CombatEvent event; event.tick = world_.GetTick(); event.kind = CombatEventKind::ActionInvalidated;
        event.actor = world_.GetActor(handle)->id; event.opponent = action.target; event.move = action.moveId;
        event.ability = action.ability; event.reason = reason; EmitEvent(event);
    };
    // Transition before resolution so reactions and disruptions cannot overwrite
    // the already-captured post-action recovery.
    auto* mutableActor = world_.MutableActor(handle);
    mutableActor->combat.action = {ActorActionKind::Recovery};
    if (action.kind == ActorActionKind::MeleeWindup || action.kind == ActorActionKind::AbilityWindup) {
        mutableActor->actionDeadline.Set(world_.GetTick(), action.recoveryTicks);
    }
    world_.ActorChanged(handle);
    if (action.ability.value) {
        actor = world_.GetActor(handle);
        if (!ability || !runtime::Granted(world_, *actor, action.ability, action.abilitySource)) {
            invalidate(CombatRefusal::MissingGrant); return aggregate;
        }
        switch (ability->execution) {
        case AbilityExecution::Focus:
            mutableActor = world_.MutableActor(handle);
            mutableActor->combat.focusRatingBonus100 = ability->focusRatingBonus100;
            mutableActor->combat.focusRemaining = ability->focusDuration;
            world_.ActorChanged(handle); return aggregate;
        case AbilityExecution::Leap:
            if (!runtime::LeapValid(world_, *actor, action.abilityTarget.tile, ability->leapDistance)) {
                invalidate(CombatRefusal::InvalidTarget); return aggregate;
            }
            if (!world_.MoveActor(handle, action.abilityTarget.tile)) { invalidate(CombatRefusal::InvalidTarget); }
            return aggregate;
        case AbilityExecution::Sweep: break;
        }
    }
    const auto* move = world_.GetContent().TryGetCombatMove(action.moveId);
    if (!move) { invalidate(CombatRefusal::InvalidDefinition); return aggregate; }
    if (!runtime::HasArt(*world_.GetActor(handle), action.trainingArt)) { invalidate(CombatRefusal::MissingGrant); return aggregate; }
    aggregate.move = Projection(*move); aggregate.moveId = action.moveId; aggregate.target = action.target;
    // Sweep snapshots each tile's identity once, before any knockback/death.
    eastl::array<PersistentId, 3> victims{action.target, 0, 0};
    unsigned tiles = 1;
    if (ability && ability->execution == AbilityExecution::Sweep) {
        tiles = 3;
        const auto origin = world_.GetActor(handle)->position;
        int dx = 0, dz = 0;
        switch (action.abilityTarget.direction) {
        case CardinalDirection::North: dz = -1; break;
        case CardinalDirection::East: dx = 1; break;
        case CardinalDirection::South: dz = 1; break;
        case CardinalDirection::West: dx = -1; break;
        }
        for (int i = -1; i <= 1; ++i) {
            const auto victim = world_.ActorAt({origin.x + dx - dz * i, origin.z + dz + dx * i, origin.y});
            const auto* other = world_.GetActor(victim); victims[static_cast<unsigned>(i + 1)] = other ? other->id : 0;
        }
    }
    struct PendingCounter { ActorHandle actor; ActorHandle target; };
    Vector<PendingCounter> counters;
    CombatTrainingBudget budget{action.trainingWork};
    const auto planned = static_cast<std::uint32_t>(move->strikes.size()) * tiles;
    aggregate.strikeCount = planned;
    unsigned opportunity = 0;
    const auto& training = ability ? ability->training : move->training;
    Vector<runtime::Candidate> responses;
    const auto refreshEncounter = [&](const Actor& learner, const Actor& opponent) {
        if (!trainingEnabled_ || learner.combat.authoredTrainingRanks) { return; }
        const auto notice = eastl::find_if(trainingNotices_.begin(), trainingNotices_.end(), [&](const TrainingNotice& entry) {
            return entry.learner == learner.id && entry.opponent == opponent.id;
        });
        if (notice != trainingNotices_.end()) { notice->remaining = trainingEncounterInactivity; }
        else { trainingNotices_.push_back({learner.id, opponent.id, trainingEncounterInactivity, false}); }
    };
    const auto train = [&](ActorHandle learner, ActorHandle opponent, SkillId handling, DefenseKind kind,
        CombatArtId art, std::uint64_t work, const CombatTrainingDefinition& definition, int opponentRank) {
        const auto* learningActor = world_.GetActor(learner); const auto* opposingActor = world_.GetActor(opponent);
        if (!trainingEnabled_ || !learningActor || !opposingActor || !work || learningActor->combat.authoredTrainingRanks) { return; }
        const int composite = kind == DefenseKind::Dodge ? LearnedSkillLevel(learningActor->learning, world_.GetContent().GetCombatSkills().dodge) :
            runtime::OffensiveRank(world_, *learningActor, handling);
        const auto artRatio = CombatTrainingRatio(opponentRank, composite);
        const auto learnerId = learningActor->id, opponentId = opposingActor->id;
        const bool skillSuppressed = TrainWork(world_, learner, handling, kind, opponentRank, work, definition);
        const bool suppressed = skillSuppressed || (art.value && definition.artXpPerWork && !artRatio);
        if (suppressed) {
            const auto notice = eastl::find_if(trainingNotices_.begin(), trainingNotices_.end(), [&](const TrainingNotice& entry) {
                return entry.learner == learnerId && entry.opponent == opponentId;
            });
            Require(notice != trainingNotices_.end());
            if (!notice->suppressed) {
                notice->suppressed = true;
                CombatEvent event; event.tick = world_.GetTick(); event.kind = CombatEventKind::TrainingSuppressed;
                event.actor = learnerId; event.opponent = opponentId; event.art = art; EmitEvent(event);
            }
        }
        auto* trained = world_.MutableActor(learner);
        if (art.value && trained && trained->characterCombat) {
            for (auto& progress : trained->characterCombat->arts) {
                if (progress.art != art) { continue; }
                Require(work <= UINT64_MAX / eastl::max(1U, definition.artXpPerWork));
                const auto xp = work * definition.artXpPerWork / ratioScale * artRatio / ratioScale;
                Require(progress.xp <= UINT64_MAX - xp); progress.xp += xp;
                world_.ActorChanged(learner); break;
            }
        }
        runtime::RefreshAbilityGrants(world_, learner);
    };
    for (unsigned tile = 0; tile < tiles; ++tile) {
        for (const auto& strike : move->strikes) {
            const auto work = TakeTrainingShare(budget, planned - opportunity++);
            target = world_.FindActor(victims[tile]);
            actor = world_.GetActor(handle); const auto* other = world_.GetActor(target);
            StrikeResult result; result.attacker = actor ? actor->id : 0; result.target = victims[tile];
            result.move = action.moveId; result.counter = action.counter;
            if (!Available(world_, actor) || !Available(world_, other)) { result.reason = CombatRefusal::NoTarget; }
            else if (!runtime::SourcesValid(world_, *actor, action.sources)) { result.reason = CombatRefusal::MissingSource; }
            else if (!runtime::StrikeReach(world_, *actor, *other, strike)) { result.reason = CombatRefusal::OutOfReach; }
            if (result.reason != CombatRefusal::None) {
                CombatEvent event; event.tick = world_.GetTick(); event.kind = CombatEventKind::ActionInvalidated;
                event.actor = result.attacker; event.opponent = result.target; event.move = action.moveId;
                event.ability = action.ability; event.reason = result.reason; EmitEvent(event); continue;
            }
            ++aggregate.resolvedStrikes;
            refreshEncounter(*actor, *other);
            refreshEncounter(*other, *actor);
            const auto handling = runtime::Handling(world_, *actor, action.sources, strike.source);
            const auto* grip = runtime::Grip(world_, *actor, action.sources, strike.source);
            const auto attackRating = runtime::Rating(world_, *actor, handling, action.sources, strike.source, DefenseKind::None,
                strike.ratingBonus100 + (grip ? grip->ratingBonus100 : 0), action.ratingPenalty100);
            const auto opponentRank = runtime::DefensiveRank(world_, *other);
            const auto offensiveRank = runtime::OffensiveRank(world_, *actor, handling);
            StaticList<DamagePacket, damage_type_count> packets;
            Packets(world_, *actor, action, strike, packets);
            runtime::Candidates(world_, *other, CombatTrigger::IncomingStrike, actor, responses);
            responses.erase(eastl::remove_if(responses.begin(), responses.end(), [&](const runtime::Candidate& candidate) {
                const auto& defense = world_.GetContent().GetCombatMove(candidate.move).defense;
                const auto* profile = runtime::Profile(world_, *other, candidate.sources);
                return !AllowsDefense(strike.defenses, defense.kind) ||
                    (profile && !AllowsDefense(profile->allowedResponses, defense.kind)) ||
                    (defense.kind == DefenseKind::Parry && runtime::Profile(world_, *actor, action.sources, strike.source) &&
                        !profile && !defense.permitsArmedAttack);
            }), responses.end());
            runtime::Candidate response;
            const CombatMoveDefinition* defenseMove = nullptr;
            SkillId defenseHandling{};
            std::uint32_t defenseChance = 0;
            if (!responses.empty()) {
                response = runtime::Select(world_, responses, rng);
                defenseMove = &world_.GetContent().GetCombatMove(response.move);
                defenseHandling = runtime::Handling(world_, *other, response.sources);
                result.response = defenseMove->defense.kind; result.responseMove = response.move;
                const auto* defenseGrip = runtime::Grip(world_, *other, response.sources);
                const auto rating = runtime::Rating(world_, *other, defenseHandling, response.sources, StrikeSource::Primary, result.response,
                    defenseMove->defense.ratingBonus100 + (defenseGrip ? defenseGrip->ratingBonus100 : 0));
                defenseChance = DefenseChance(result.response, attackRating, rating);
                const auto responseTicks = CheckedTickAdd(ActionTicks(response.cost.recoveryCost, other->speed),
                    response.cost.windupCost ? ActionTicks(response.cost.windupCost, other->speed) : 0);
                auto* defender = world_.MutableActor(target);
                Pay(defender->combat, response.cost.staminaCost, responseTicks);
                runtime::SetCooldown(defender->combat, response.move, defenseMove->cooldown);
                auto reaction = Commitment(world_, *defender, response, result.attacker);
                EmitEvent(CommitmentEvent(world_, *defender, reaction, responseTicks, response.cost.staminaCost));
                world_.ActorChanged(target);
            }
            const auto intrinsicMiss = actor->combat.legacyAccuracyOverride ? actor->combat.intrinsicMissOverride : strike.intrinsicMiss;
            const auto draw = ResolveStrikeDraw(DrawBelow(rng, ratioScale), intrinsicMiss, defenseChance);
            result.defenseSucceeded = draw == StrikeDrawOutcome::DefenseSucceeded;
            result.outcome = draw == StrikeDrawOutcome::Miss ? StrikeOutcome::Miss : StrikeOutcome::Contact;
            if (result.defenseSucceeded && result.response == DefenseKind::Dodge) { result.outcome = StrikeOutcome::Dodged; }
            if (result.defenseSucceeded && result.response == DefenseKind::Parry) { result.outcome = StrikeOutcome::Parried; }
            if (result.outcome == StrikeOutcome::Contact) {
                std::uint64_t block = 0;
                for (const auto& packet : packets) { result.incomingDamage += packet.amount; }
                if (result.defenseSucceeded && result.response == DefenseKind::Block) {
                    other = world_.GetActor(target);
                    std::uint64_t capacity = defenseMove->defense.blockCapacity;
                    const auto* profile = runtime::Profile(world_, *other, response.sources);
                    const auto* defenseGrip = runtime::Grip(world_, *other, response.sources);
                    if (profile && defenseMove->defense.usesSourceCapacity) {
                        capacity += std::uint64_t{profile->blockCapacity} * (defenseGrip ? defenseGrip->blockRatio : ratioScale) / ratioScale;
                    }
                    Require(capacity <= UINT32_MAX);
                    eastl::array<std::uint32_t, damage_type_count> absorbed{};
                    block = AllocateBlockAbsorption({packets.begin(), packets.Size()},
                        IncreaseBlockCapacity(static_cast<std::uint32_t>(capacity), EffectiveSkillLevel(other->learning, defenseHandling)),
                        {absorbed.data(), packets.Size()});
                    for (unsigned i = 0; i < packets.Size(); ++i) { packets[i].amount -= absorbed[i]; }
                }
                other = world_.GetActor(target);
                const auto region = runtime::Contact(*other, strike, world_.GetActor(handle)->combat.reach, rng);
                if (region == ContactRegion::Count) { result.outcome = StrikeOutcome::Invalidated; result.reason = CombatRefusal::OutOfReach; }
                else {
                    const auto incoming = result.incomingDamage;
                    const auto contact = DamageContact(target, handle, region, {packets.begin(), packets.Size()}, rng);
                    result.region = contact.region; result.injuryPart = contact.injuryPart;
                    result.hpLoss = contact.hpLoss; result.maxHpLoss = contact.maxHpLoss; result.killed = contact.killed;
                    result.armorAbsorbed = contact.armorAbsorbed; result.armorStopped = contact.armorStopped;
                    result.postProtectionDamage = contact.postProtectionDamage; result.disruption = contact.disruption;
                    result.incomingDamage = incoming; result.blockAbsorbed = block;
                }
            }
            train(handle, target, handling, DefenseKind::None, action.trainingArt, work, training, opponentRank);
            if (defenseMove) {
                other = world_.GetActor(target);
                const auto* style = world_.GetContent().TryGetCombatStyle(runtime::Style(world_, *other));
                train(target, handle, defenseHandling, result.response, style ? style->art : CombatArtId{},
                    defenseMove->training.work, defenseMove->training, offensiveRank);
            }
            CombatEvent event; event.tick = world_.GetTick(); event.strike = result; EmitEvent(event);
            const auto effects = [&](ActorHandle receiver, ActorHandle origin, const MoveFollowThrough& effect, bool defensive) {
                if (!runtime::EffectApplies(effect.condition, result)) { return; }
                const auto* sourceActor = world_.GetActor(origin);
                auto* receiverActor = world_.MutableActor(receiver);
                if (!Available(world_, receiverActor) || !sourceActor) { return; }
                const auto sourceId = sourceActor->id;
                if (defensive && effect.counterOpening && !action.counter) {
                    runtime::Opening(receiverActor->combat, sourceId, effect.counterOpening);
                }
                const auto affected = defensive ? origin : receiver;
                const auto effectSource = defensive ? receiver : origin;
                if (effect.stagger) {
                    auto* affectedActor = world_.MutableActor(affected);
                    if (Available(world_, affectedActor)) {
                        affectedActor->combat.staggerRemaining = eastl::max(affectedActor->combat.staggerRemaining, effect.stagger);
                        Disrupt(affected, {DisruptionCause::Stagger, world_.GetActor(effectSource)->id});
                        world_.ActorChanged(affected);
                    }
                }
                world_.ActorChanged(receiver);
                if (effect.knockbackTiles) {
                    const auto from = world_.GetActor(effectSource)->position;
                    for (unsigned step = 0; step < effect.knockbackTiles; ++step) {
                        const auto* current = world_.GetActor(affected); if (!Available(world_, current)) { break; }
                        const auto position = current->position;
                        const TileCoord direction{(position.x > from.x) - (position.x < from.x),
                            (position.z > from.z) - (position.z < from.z), 0};
                        if (!world_.TryActorStep(affected, direction, true)) { break; }
                    }
                }
            };
            effects(target, handle, move->followThrough, false);
            if (defenseMove) { effects(target, handle, defenseMove->followThrough, true); }
            other = world_.GetActor(target);
            if (!action.counter && Available(world_, other) && runtime::HasOpening(other->combat, result.attacker)) {
                counters.push_back({target, handle});
            }
            aggregate.totalHpLoss += result.hpLoss;
            aggregate.injury += static_cast<std::uint32_t>(result.postProtectionDamage / body_hp_scale);
            aggregate.part = result.injuryPart; aggregate.contact |= result.outcome == StrikeOutcome::Contact;
            aggregate.disruption = result.disruption;
            aggregate.outcome = result.killed ? AttackOutcome::Defeated : result.outcome == StrikeOutcome::Parried ? AttackOutcome::Parried :
                result.outcome != StrikeOutcome::Contact ? AttackOutcome::Miss : result.hpLoss || result.maxHpLoss ? AttackOutcome::Hit : AttackOutcome::Blocked;
            Record(target, aggregate, true);
        }
    }
    // Finish all contacts/follow-through before consuming target-specific openings.
    // Counter commitments cannot produce further counters, bounding this queue.
    for (std::size_t i = 0; i < counters.size(); ++i) {
        const auto pending = counters[i];
        const auto* counterActor = world_.GetActor(pending.actor); const auto* opponent = world_.GetActor(pending.target);
        if (!Available(world_, counterActor) || !Available(world_, opponent)) { continue; }
        Vector<runtime::Candidate> choices;
        runtime::Candidates(world_, *counterActor, CombatTrigger::CounterOpening, opponent, choices);
        if (choices.empty()) { continue; }
        const auto selected = runtime::Select(world_, choices, rng);
        auto counterAction = Commitment(world_, *counterActor, selected, opponent->id); counterAction.counter = true;
        const auto windup = selected.cost.windupCost ? ActionTicks(selected.cost.windupCost, counterActor->speed) : 0;
        const auto duration = eastl::max(counterActor->actionDeadline.Remaining(world_.GetTick()),
            CheckedTickAdd(windup, counterAction.recoveryTicks));
        CheckedTickAdd(world_.GetTick(), duration);
        counterAction.recoveryTicks = duration - windup;
        auto* counterMutable = world_.MutableActor(pending.actor);
        counterMutable->combat.openings.erase(eastl::remove_if(counterMutable->combat.openings.begin(), counterMutable->combat.openings.end(),
            [&](const CounterOpening& opening) { return opening.opponent == opponent->id; }), counterMutable->combat.openings.end());
        const auto& counterMove = world_.GetContent().GetCombatMove(selected.move);
        Pay(counterMutable->combat, selected.cost.staminaCost, duration);
        runtime::SetCooldown(counterMutable->combat, selected.move, counterMove.cooldown);
        // Immediate counters join this action-local work queue, after all of
        // the triggering action's contacts, without recursive execution.
        counterAction.kind = ActorActionKind::MeleeWindup;
        counterMutable->combat.action = counterAction;
        counterMutable->actionDeadline.Set(world_.GetTick(), windup);
        EmitEvent(CommitmentEvent(world_, *counterMutable, counterAction, duration, selected.cost.staminaCost));
        world_.ActorChanged(pending.actor);
        if (!selected.cost.windupCost) { jobs.push_back({pending.actor, pending.target}); }
    }
    Record(handle, aggregate, false);
    if (jobIndex == 0) { rootResult = aggregate; }
    }
    return rootResult;
}

CombatAvailability CombatSystem::QueryAbility(ActorHandle handle, const AbilityRequest& request) const {
    CombatAvailability result; result.reason = runtime::Ready(world_, handle); if (!result.Available()) { return result; }
    const auto* actor = world_.GetActor(handle); const auto* ability = world_.GetContent().TryGetAbility(request.ability);
    if (!ability) { result.reason = CombatRefusal::InvalidDefinition; return result; }
    result.cost = runtime::AbilityCost(world_, *actor, *ability, {});
    if (!runtime::Granted(world_, *actor, request.ability, request.source)) { result.reason = CombatRefusal::MissingGrant; return result; }
    if (runtime::AbilityCooldownRemaining(*actor, request.ability, request.source)) { result.reason = CombatRefusal::Cooldown; return result; }
    if (request.target.kind != ability->targeting) { result.reason = CombatRefusal::InvalidTarget; return result; }
    if (ability->execution == AbilityExecution::Leap && !runtime::LeapValid(world_, *actor, request.target.tile, ability->leapDistance)) {
        result.reason = CombatRefusal::InvalidTarget; return result;
    }
    if (ability->execution == AbilityExecution::Sweep) {
        if (request.target.direction > CardinalDirection::West) { result.reason = CombatRefusal::InvalidTarget; return result; }
        const auto* move = world_.GetContent().TryGetCombatMove(ability->sweepMove);
        if (!move || move->execution != MoveExecution::Strikes || move->requirements.requiresOpening) { result.reason = CombatRefusal::InvalidDefinition; return result; }
        Vector<SourceCommitment> bindings; runtime::Bind(world_, *actor, *move, bindings);
        if (bindings.empty()) { result.reason = CombatRefusal::MissingSource; return result; }
        result.cost = runtime::AbilityCost(world_, *actor, *ability, bindings.front());
        for (const auto& binding : bindings) {
            const auto cost = runtime::AbilityCost(world_, *actor, *ability, binding);
            if (actor->combat.inexhaustible || actor->combat.stamina >= cost.staminaCost) { result.cost = cost; return result; }
        }
    }
    if (!actor->combat.inexhaustible && actor->combat.stamina < result.cost.staminaCost) { result.reason = CombatRefusal::Exhausted; }
    return result;
}
AbilityResult CombatSystem::UseAbility(ActorHandle handle, const AbilityRequest& request, std::uint32_t& rng) {
    AbilityResult result; result.ability = request.ability;
    const auto availability = QueryAbility(handle, request); result.reason = availability.reason;
    if (!availability.Available()) { return result; }
    runtime::RefreshAbilityGrants(world_, handle);
    const auto& definition = world_.GetContent().GetAbility(request.ability);
    const auto* actor = world_.GetActor(handle);
    auto cost = availability.cost;
    ActorActionState action;
    action.definitionDriven = true; action.ability = request.ability; action.abilitySource = request.source; action.abilityTarget = request.target;
    action.trainingArt = request.source.kind == AbilityGrantSourceKind::Art ? request.source.art : CombatArtId{};
    action.trainingWork = definition.training.work; action.committedStyle = runtime::Style(world_, *actor);
    if (definition.execution == AbilityExecution::Sweep) {
        action.moveId = definition.sweepMove;
        Vector<SourceCommitment> bindings; runtime::Bind(world_, *actor, world_.GetContent().GetCombatMove(action.moveId), bindings);
        bindings.erase(eastl::remove_if(bindings.begin(), bindings.end(), [&](const SourceCommitment& binding) {
            return !actor->combat.inexhaustible && actor->combat.stamina < runtime::AbilityCost(world_, *actor, definition, binding).staminaCost;
        }), bindings.end());
        Require(!bindings.empty());
        action.sources = bindings[DrawBelow(rng, static_cast<std::uint32_t>(bindings.size()))];
        cost = runtime::AbilityCost(world_, *actor, definition, action.sources);
    }
    action.kind = cost.windupCost ? ActorActionKind::AbilityWindup : ActorActionKind::Recovery;
    action.recoveryTicks = ActionTicks(cost.recoveryCost, actor->speed);
    const auto base = cost.windupCost ? cost.windupCost : cost.recoveryCost;
    result.cost = ActionTicks(base, actor->speed);
    const auto full = cost.windupCost ? CheckedTickAdd(result.cost, action.recoveryTicks) : result.cost;
    CheckedTickAdd(world_.GetTick(), full);
    Require(world_.ScheduleActorAction(handle, base).has_value());
    auto* mutableActor = world_.MutableActor(handle); mutableActor->combat.action = action;
    Pay(mutableActor->combat, cost.staminaCost, full);
    if (definition.cooldown) {
        Require(mutableActor->characterCombat != nullptr);
        mutableActor->characterCombat->abilityCooldowns.push_back({request.ability, request.source, definition.cooldown});
    }
    EmitEvent(CommitmentEvent(world_, *mutableActor, action, full, cost.staminaCost));
    world_.ActorChanged(handle); result.committed = true; result.pending = cost.windupCost != 0;
    if (!result.pending) { Strike(handle, {}, CombatMove::None, 0, rng); }
    return result;
}
void CombatSystem::CollectInfo(ActorHandle handle, CombatInfoView& output) const {
    output.combat = Describe(handle); output.equipment.worn.clear(); output.moves.clear(); output.abilities.clear(); output.arts.clear(); output.manuals.clear();
    output.equipment.hands = {}; output.equipment.encumbrance = {};
    const auto* actor = world_.GetActor(handle); if (!actor) { return; }
    output.equipment.encumbrance = runtime::Modifiers(world_, *actor).encumbrance;
    for (unsigned i = 0; i < 2; ++i) {
        const auto held = actor->equipment.hands[i]; const auto* item = world_.GetItem(held.item);
        bool usable = Functional(actor->combat.body, i == 0 ? BodyPart::LeftHand : BodyPart::RightHand);
        if (held.grip == GripKind::TwoHanded) { usable &= Functional(actor->combat.body, BodyPart::LeftHand) && Functional(actor->combat.body, BodyPart::RightHand); }
        output.equipment.hands[i] = {item ? item->id : 0, held.grip, item && usable};
    }
    for (const auto worn : actor->equipment.worn) { if (const auto* item = world_.GetItem(worn)) { output.equipment.worn.push_back(item->id); } }
    eastl::sort(output.equipment.worn.begin(), output.equipment.worn.end());
    const auto* style = world_.GetContent().TryGetCombatStyle(runtime::Style(world_, *actor));
    Vector<SourceCommitment> bindings;
    Vector<runtime::Candidate> availableAttacks;
    runtime::Candidates(world_, *actor, CombatTrigger::Attack, nullptr, availableAttacks);
    if (style) { for (const auto& weighted : style->moves) {
        const auto& definition = world_.GetContent().GetCombatMove(weighted.move);
        CombatMoveView view; view.move = weighted.move; view.unlocked = runtime::Unlocked(*actor, definition.unlock, style->art);
        view.cooldownRemaining = runtime::Cooldown(actor->combat, weighted.move);
        view.availability.reason = runtime::Ready(world_, handle);
        if (!view.unlocked) { view.availability.reason = CombatRefusal::Locked; }
        else if (view.cooldownRemaining) { view.availability.reason = CombatRefusal::Cooldown; }
        runtime::Bind(world_, *actor, definition, bindings);
        if (bindings.empty()) { view.availability.reason = runtime::SourceRefusal(world_, *actor, definition); }
        else {
            view.availability.cost = runtime::Cost(world_, *actor, definition, bindings.front());
            if (!actor->combat.inexhaustible && actor->combat.stamina < view.availability.cost.staminaCost) { view.availability.reason = CombatRefusal::Exhausted; }
        }
        if (definition.requirements.exhaustedOnly && view.availability.Available() &&
            eastl::none_of(availableAttacks.begin(), availableAttacks.end(), [&](const runtime::Candidate& candidate) { return candidate.move == view.move; })) {
            view.availability.reason = CombatRefusal::NoMove;
        }
        if (definition.trigger == CombatTrigger::Attack && view.availability.Available() &&
            eastl::none_of(availableAttacks.begin(), availableAttacks.end(), [&](const runtime::Candidate& candidate) { return candidate.move == view.move; })) {
            view.availability.reason = definition.requirements.requiresOpening ? CombatRefusal::MissingOpening : CombatRefusal::OutOfReach;
        }
        output.moves.push_back(view);
    } }
    eastl::sort(output.moves.begin(), output.moves.end(), [&](const CombatMoveView& a, const CombatMoveView& b) {
        return world_.GetContent().GetCombatMove(a.move).id < world_.GetContent().GetCombatMove(b.move).id;
    });
    if (actor->characterCombat) {
        output.arts.assign(actor->characterCombat->arts.begin(), actor->characterCombat->arts.end());
        const auto addAbility = [&](AbilityId id, AbilityGrantKey source) {
            if (eastl::any_of(output.abilities.begin(), output.abilities.end(), [&](const AbilityView& view) { return view.ability == id && view.source == source; })) { return; }
            const auto* ability = world_.GetContent().TryGetAbility(id); if (!ability) { return; }
            AbilityRequest request{id, source, {ability->targeting}};
            AbilityView view; view.ability = id; view.source = source; view.targeting = ability->targeting;
            view.cooldownRemaining = runtime::AbilityCooldownRemaining(*actor, id, source); view.availability = QueryAbility(handle, request);
            // Target picking follows inspection. Report target-independent
            // eligibility here rather than pretending the origin is a landing.
            if (view.availability.reason == CombatRefusal::InvalidTarget && ability->targeting != AbilityTargetKind::Self) {
                view.availability.reason = CombatRefusal::None;
                if (!actor->combat.inexhaustible && actor->combat.stamina < view.availability.cost.staminaCost) {
                    view.availability.reason = CombatRefusal::Exhausted;
                }
            }
            output.abilities.push_back(view);
        };
        for (const auto& grant : actor->characterCombat->abilityGrants) { addAbility(grant.ability, grant.source); }
        for (const auto& progress : actor->characterCombat->arts) {
            const auto* art = world_.GetContent().TryGetCombatArt(progress.art); if (!art) { continue; }
            for (const auto& grant : art->abilities) { addAbility(grant.ability, {AbilityGrantSourceKind::Art, progress.art}); }
        }
    }
    eastl::sort(output.abilities.begin(), output.abilities.end(), [&](const AbilityView& a, const AbilityView& b) {
        const auto& first = world_.GetContent().GetAbility(a.ability).id;
        const auto& second = world_.GetContent().GetAbility(b.ability).id;
        if (first != second) { return first < second; }
        if (a.source.kind != b.source.kind) { return a.source.kind < b.source.kind; }
        if (!a.source.art.value || !b.source.art.value) { return a.source.art.value < b.source.art.value; }
        return world_.GetContent().GetCombatArt(a.source.art).id < world_.GetContent().GetCombatArt(b.source.art).id;
    });
    for (std::uint32_t i = 1; i <= world_.GetContent().BookCount(); ++i) {
        const BookId book{i}; const auto& definition = world_.GetContent().GetBook(book);
        for (const auto& manual : definition.artManuals) {
            const ArtManualRequest request{book, manual.art}; const auto query = QueryArtStudy(handle, request);
            ArtManualView view; view.book = book; view.art = manual.art; view.reason = query.reason;
            view.successChance = manual.successChance;
            const auto learned = LearnedSkillLevel(actor->learning, manual.prerequisite.skill);
            view.duration = ArtStudyDuration(manual.baseDuration, learned > manual.prerequisite.level ? learned - manual.prerequisite.level : 0);
            if (actor->characterCombat) { for (const auto& attempt : actor->characterCombat->manuals) {
                if (attempt.book == book && attempt.art == manual.art) {
                    view.remaining = attempt.remaining; view.duration = attempt.totalDuration;
                    view.successChance = attempt.successChance; view.active = attempt.active;
                }
            } }
            output.manuals.push_back(view);
        }
    }
}

void CombatSystem::Elapse(Tick elapsed) {
    if (!elapsed) { return; }
    for (auto& notice : trainingNotices_) { notice.remaining = Reduced(notice.remaining, elapsed); }
    trainingNotices_.erase(eastl::remove_if(trainingNotices_.begin(), trainingNotices_.end(),
        [](const TrainingNotice& notice) { return notice.remaining == 0; }), trainingNotices_.end());
    world_.CollectActiveActors(elapsedActors_);
    for (const auto handle : elapsedActors_) {
        auto* actor = world_.MutableActor(handle); if (!Available(world_, actor)) { continue; }
        auto& state = actor->combat;
        // Fixed-size body snapshots are cheap. Never copy CombatState's vectors.
        const auto previousBody = state.body;
        bool changed = false;
        const auto reduce = [&](Tick& value) { const auto next = Reduced(value, elapsed); changed |= next != value; value = next; };
        const auto locked = eastl::min(elapsed, state.combatLockRemaining);
        reduce(state.reactionCooldown); reduce(state.heavyCooldown); reduce(state.staggerRemaining);
        reduce(state.focusRemaining); if (!state.focusRemaining && state.focusRatingBonus100) { state.focusRatingBonus100 = 0; changed = true; }
        reduce(state.combatLockRemaining);
        for (auto& entry : state.moveCooldowns) { reduce(entry.remaining); }
        state.moveCooldowns.erase(eastl::remove_if(state.moveCooldowns.begin(), state.moveCooldowns.end(), [](const MoveCooldown& entry) { return !entry.remaining; }), state.moveCooldowns.end());
        for (auto& entry : state.openings) { reduce(entry.remaining); }
        state.openings.erase(eastl::remove_if(state.openings.begin(), state.openings.end(), [](const CounterOpening& entry) { return !entry.remaining; }), state.openings.end());
        if (actor->characterCombat) {
            auto& cooldowns = actor->characterCombat->abilityCooldowns;
            for (auto& entry : cooldowns) { reduce(entry.remaining); }
            cooldowns.erase(eastl::remove_if(cooldowns.begin(), cooldowns.end(), [](const AbilityCooldown& entry) { return !entry.remaining; }), cooldowns.end());
        }
        const auto unlocked = elapsed - locked;
        // Rest/working restore 0.1 stamina/tick; moving restores half that.
        // Combat activity is represented by the precise lock interval, not a
        // sticky exertion flag after the action's recovery expires.
        const std::uint32_t rate = state.exertion == ExertionKind::Moving ? 500 : 1000;
        if (state.stamina == 100) {
            changed |= state.staminaRegenerationRemainder != 0; state.staminaRegenerationRemainder = 0;
        } else if (unlocked) {
            const auto missing = 100U - state.stamina;
            const auto needed = std::uint64_t{missing} * ratioScale - state.staminaRegenerationRemainder;
            const auto ticksToFull = (needed + rate - 1) / rate;
            if (unlocked >= ticksToFull) { state.stamina = 100; state.staminaRegenerationRemainder = 0; }
            else {
                const auto fraction = unlocked * rate + state.staminaRegenerationRemainder;
                state.stamina += static_cast<std::uint32_t>(fraction / ratioScale);
                state.staminaRegenerationRemainder = static_cast<std::uint32_t>(fraction % ratioScale);
            }
            changed = true;
        }
        ElapseBody(state.body, state.injury, elapsed); changed |= state.body != previousBody;
        if (changed) { world_.ActorChanged(handle); }
    }
    ElapseArtStudy(elapsed);
}
void CombatSystem::ProcessEvents(std::uint32_t& rng) {
    // Other learning paths can change learned gates between combat actions.
    // Refresh sparse stored grants only when their derived membership changed.
    world_.CollectActiveActors(elapsedActors_);
    for (const auto handle : elapsedActors_) { runtime::RefreshAbilityGrants(world_, handle); }
    // Re-gather only when a contact queued a zero-delay counter; no recursion and
    // no reference into a due cohort is retained across death finalization.
    for (;;) {
        world_.CollectActiveActors(dueActors_);
        dueActors_.erase(eastl::remove_if(dueActors_.begin(), dueActors_.end(), [&](ActorHandle handle) {
            const auto* actor = world_.GetActor(handle);
            return !Available(world_, actor) || actor->combat.action.kind == ActorActionKind::None || actor->actionDeadline.suspended ||
                actor->actionDeadline.Remaining(world_.GetTick()) != 0;
        }), dueActors_.end());
        if (dueActors_.empty()) { break; }
        eastl::sort(dueActors_.begin(), dueActors_.end(), [&](ActorHandle a, ActorHandle b) { return world_.GetActor(a)->id < world_.GetActor(b)->id; });
        for (const auto handle : dueActors_) {
            auto* actor = world_.MutableActor(handle);
            if (!Available(world_, actor) || actor->actionDeadline.suspended || actor->actionDeadline.Remaining(world_.GetTick()) ||
                actor->combat.action.kind == ActorActionKind::None) { continue; }
            const auto action = actor->combat.action;
            if ((action.kind == ActorActionKind::MeleeWindup || action.kind == ActorActionKind::AbilityWindup) && action.definitionDriven) {
                const auto result = Strike(handle, world_.FindActor(action.target), CombatMove::None, 0, rng); Record(handle, result, false);
            } else { actor->combat.action = {}; world_.ActorChanged(handle); }
        }
    }
    ProcessArtStudyEvents(rng);
    world_.FinalizeActorDeaths();
}
eastl::optional<Tick> CombatSystem::NextEventIn() const {
    world_.CollectActiveActors(queriedActors_); auto next = NextArtStudyEventIn();
    for (const auto handle : queriedActors_) {
        const auto* actor = world_.GetActor(handle);
        if (!Available(world_, actor) || actor->actionDeadline.suspended || actor->combat.action.kind == ActorActionKind::None) { continue; }
        const auto remaining = actor->actionDeadline.Remaining(world_.GetTick()); if (!next || remaining < *next) { next = remaining; }
    }
    return next;
}
}
