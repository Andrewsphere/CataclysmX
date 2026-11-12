#include "combat/combat_system.h"
#include "core/world.h"

#include <EASTL/sort.h>
#include <EASTL/algorithm.h>
#include <cstdlib>
#include <limits>

namespace cataclysmx {
namespace {

void Require(bool condition) {
    if (!condition) { std::abort(); }
}

bool Available(const World& world, const Actor* actor) {
    return actor && !actor->parked && world.IsActive(actor->position) &&
        (actor->combat.body.kind != BodyKind::Simple ||
            Functional(actor->combat.body, BodyPart::Core));
}

bool HeavyCapable(const BodyState& body) {
    if (body.kind == BodyKind::Simple) {
        return Functional(body, BodyPart::Core) && Functional(body, BodyPart::AttackApparatus) &&
            Functional(body, BodyPart::Locomotion);
    }
    return Functional(body, BodyPart::LeftHand) && Functional(body, BodyPart::RightHand);
}

bool CanParry(const CombatState& state) {
    const auto& body = state.body;
    return state.style == CombatStyle::Defensive && state.stamina >= 10 &&
        state.reactionCooldown == 0 && (body.kind == BodyKind::Simple ?
            Functional(body, BodyPart::Core) && Functional(body, BodyPart::AttackApparatus) :
            Functional(body, BodyPart::LeftHand) || Functional(body, BodyPart::RightHand));
}

bool MoveAvailable(const CombatState& state, CombatMove move) {
    const auto& body = state.body;
    switch (move) {
    case CombatMove::HeavyAttack:
        return state.style == CombatStyle::Aggressive && state.stamina >= 20
            && state.heavyCooldown == 0 && HeavyCapable(body);
    case CombatMove::Bite:
        return body.kind == BodyKind::Simple && Functional(body, BodyPart::Core)
            && !Functional(body, BodyPart::AttackApparatus);
    case CombatMove::PlainAttack:
        if (body.kind == BodyKind::Simple) {
            return Functional(body, BodyPart::Core) && Functional(body, BodyPart::AttackApparatus);
        }
        return Functional(body, BodyPart::LeftHand) || Functional(body, BodyPart::RightHand)
            || Functional(body, BodyPart::LeftLeg) || Functional(body, BodyPart::RightLeg);
    case CombatMove::None: case CombatMove::Parry: return false;
    }
    return false;
}

CombatMove SelectMove(const CombatState& state) {
    // Initial authored repertoire. Stable IDs break equal priorities; container
    // position, actor handles and RNG are never inputs to move selection.
    struct Choice { CombatMove move; unsigned priority; };
    constexpr Choice choices[] = {{CombatMove::PlainAttack, 10},
        {CombatMove::HeavyAttack, 100}, {CombatMove::Bite, 10}};
    CombatMove best = CombatMove::None;
    unsigned priority = 0;
    for (const auto choice : choices) {
        if (MoveAvailable(state, choice.move) && (best == CombatMove::None || choice.priority > priority
            || (choice.priority == priority && choice.move < best))) {
            best = choice.move;
            priority = choice.priority;
        }
    }
    return best;
}

std::uint32_t PlainCost(const BodyState& body) {
    const bool walking = body.kind == BodyKind::Simple ? Functional(body, BodyPart::Locomotion) :
        Functional(body, BodyPart::LeftLeg) || Functional(body, BodyPart::RightLeg);
    return walking ? 100 : 150;
}

std::uint32_t Draw(std::uint32_t& rng) {
    rng = rng * 1'664'525U + 1'013'904'223U;
    return rng;
}

Tick Reduced(Tick value, Tick elapsed) { return value > elapsed ? value - elapsed : 0; }

}

CombatSystem::CombatSystem(World& world) : world_(world) {}

WorldResult<void> CombatSystem::Configure(ActorHandle handle, const CombatState& state) {
    const auto* actor = world_.GetActor(handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    if (!ValidateCombatState(state) || state.action.kind != ActorActionKind::None ||
        actor->combat.action.kind != ActorActionKind::None || world_.GetActorRemainingTicks(handle) != 0) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    world_.MutableActor(handle)->combat = state;
    world_.ActorChanged(handle);
    return {};
}

WorldResult<void> CombatSystem::SetStyle(ActorHandle handle, CombatStyle style) {
    if (!world_.GetActor(handle)) { return eastl::unexpected(WorldError::invalid_handle); }
    switch (style) {
    case CombatStyle::Balanced: case CombatStyle::Aggressive: case CombatStyle::Defensive: break;
    default: return eastl::unexpected(WorldError::invalid_request);
    }
    auto* actor = world_.MutableActor(handle);
    if (actor->combat.style != style) {
        actor->combat.style = style;
        world_.ActorChanged(handle);
    }
    return {};
}

CombatView CombatSystem::Describe(ActorHandle handle) const {
    const auto* actor = world_.GetActor(handle);
    CombatView view;
    if (!actor) { return view; }
    const auto& state = actor->combat;
    view.bodyKind = state.body.kind;
    view.style = state.style;
    view.action = state.action.kind;
    view.stamina = state.stamina;
    view.accuracyPenalty = state.action.accuracyPenalty;
    view.feedback = actor->combatFeedback;
    for (std::size_t i = 0; i < body_part_count; ++i) {
        const auto part = static_cast<BodyPart>(i);
        if (HasPart(state.body.kind, part)) {
            view.parts[i] = {state.body.parts[i].hp, EffectiveMax(state.body, part), state.body.parts[i].broken};
        }
    }
    return view;
}

void CombatSystem::Record(ActorHandle handle, const AttackResult& result, bool incoming) {
    if (auto* actor = world_.MutableActor(handle)) {
        actor->combatFeedback = {result, incoming, world_.GetTick()};
        world_.ActorChanged(handle);
    }
}

bool CombatSystem::CanReach(ActorHandle handle, ActorHandle target) const {
    if (handle == target) { return false; }
    const auto* actor = world_.GetActor(handle);
    const auto* other = world_.GetActor(target);
    if (!Available(world_, actor) || !Available(world_, other)) { return false; }
    const auto a = actor->position;
    const auto b = other->position;
    if (a.y != b.y || std::abs(a.x - b.x) + std::abs(a.z - b.z) != 1) { return false; }
    const auto* first = world_.GetTile(a);
    const auto* second = world_.GetTile(b);
    return first && second && Passable(world_.GetContent(), first->terrain) &&
        Passable(world_.GetContent(), second->terrain);
}

ActorHandle CombatSystem::FindTarget(ActorHandle handle) const {
    const auto* actor = world_.GetActor(handle);
    if (!Available(world_, actor) || SelectMove(actor->combat) == CombatMove::None) { return {}; }
    const auto position = actor->position;
    const auto faction = actor->combat.faction;
    const bool player = actor->player;
    constexpr TileCoord directions[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    ActorHandle best{};
    PersistentId bestId = 0;
    for (const auto direction : directions) {
        const TileCoord tile{position.x + direction.x, position.z + direction.z, position.y};
        if (player && !world_.CanPlayerSee(handle, tile)) { continue; }
        const auto candidate = world_.ActorAt(tile);
        const auto* other = world_.GetActor(candidate);
        if (!other || !Hostile(faction, other->combat.faction) || !CanReach(handle, candidate)) { continue; }
        if (best.value == 0 || other->id < bestId) { best = candidate; bestId = other->id; }
    }
    return best;
}

AttackResult CombatSystem::Attack(ActorHandle handle, ActorHandle target, std::uint32_t& rng) {
    const auto* actor = world_.GetActor(handle);
    if (!Available(world_, actor)) { return {}; }
    AttackResult result;
    if (actor->combat.action.kind != ActorActionKind::None || world_.GetActorRemainingTicks(handle) != 0 ||
        actor->actionDeadline.suspended) { result.outcome = AttackOutcome::NotReady; return result; }
    const auto* other = world_.GetActor(target);
    if (!Available(world_, other) || !Hostile(actor->combat.faction, other->combat.faction)) {
        result.outcome = AttackOutcome::NoTarget; return result;
    }
    result.target = other->id;
    if (!CanReach(handle, target)) { result.outcome = AttackOutcome::OutOfReach; return result; }
    const auto move = SelectMove(actor->combat);
    if (move == CombatMove::None) { result.outcome = AttackOutcome::NoMove; return result; }
    const auto speed = actor->speed;
    const auto baseCost = move == CombatMove::HeavyAttack ? 80U : PlainCost(actor->combat.body);
    if (move == CombatMove::HeavyAttack) {
        // Both committed phases must fit, even if the target later vanishes.
        CheckedTickAdd(CheckedTickAdd(world_.GetTick(), ActionTicks(baseCost, speed)), ActionTicks(120, speed));
    }
    const auto targetId = other->id;
    Require(world_.ScheduleActorAction(handle, baseCost).has_value());
    auto* mutableActor = world_.MutableActor(handle);
    if (move == CombatMove::HeavyAttack) {
        mutableActor->combat.action = {ActorActionKind::MeleeWindup, targetId,
            static_cast<std::uint32_t>(move), ActionTicks(120, speed), 0};
        mutableActor->combat.stamina -= 20;
        mutableActor->combat.heavyCooldown = 400;
        result.outcome = AttackOutcome::Windup;
        result.move = move;
    } else {
        mutableActor->combat.action = {ActorActionKind::Recovery};
    }
    world_.ActorChanged(handle);
    if (move != CombatMove::HeavyAttack) { result = Strike(handle, target, move, 0, rng); }
    result.committed = true;
    result.cost = ActionTicks(baseCost, speed);
    Record(handle, result, false);
    return result;
}

WorldResult<void> CombatSystem::StartAiming(ActorHandle handle, ActorHandle target, std::uint32_t baseCost) {
    const auto* actor = world_.GetActor(handle);
    if (!actor) { return eastl::unexpected(WorldError::invalid_handle); }
    const auto* other = world_.GetActor(target);
    if (!Available(world_, actor) || !Available(world_, other) || baseCost == 0 ||
        actor->actionDeadline.suspended || actor->combat.action.kind != ActorActionKind::None ||
        world_.GetActorRemainingTicks(handle) != 0 || !CanReach(handle, target) ||
        !Hostile(actor->combat.faction, other->combat.faction)) {
        return eastl::unexpected(WorldError::invalid_request);
    }
    const auto targetId = other->id;
    const auto scheduled = world_.ScheduleActorAction(handle, baseCost);
    if (!scheduled) { return scheduled; }
    world_.MutableActor(handle)->combat.action = {ActorActionKind::Aiming, targetId};
    world_.ActorChanged(handle);
    return {};
}

DisruptionResult CombatSystem::Disrupt(ActorHandle handle, const Disruption& disruption) {
    auto* actor = world_.MutableActor(handle);
    if (!actor) { return {DisruptionResponse::Ignored, eastl::nullopt}; }
    const auto result = ApplyDisruption(actor->combat.action, disruption);
    if (result.nextDelay) { actor->actionDeadline.Set(world_.GetTick(), *result.nextDelay); }
    if (result.response != DisruptionResponse::Ignored) { world_.ActorChanged(handle); }
    return result;
}

AttackResult CombatSystem::Damage(ActorHandle handle, ActorHandle source, BodyPart part, DamagePacket damage) {
    auto* actor = world_.MutableActor(handle);
    if (!Available(world_, actor)) { return {}; }
    Require(damage.type < DamageType::Count && HasPart(actor->combat.body.kind, part));
    AttackResult result;
    result.target = actor->id;
    result.part = part;
    result.contact = true;
    const auto protection = actor->combat.protection[static_cast<std::size_t>(part)][static_cast<std::size_t>(damage.type)];
    const auto reduction = protection > damage.penetration ? protection - damage.penetration : 0;
    const auto injury = (damage.amount > reduction ? damage.amount - reduction : 0) / body_hp_scale;
    if (injury == 0) {
        result.outcome = AttackOutcome::Blocked;
        Record(handle, result, true);
        return result;
    }
    const auto* sourceActor = world_.GetActor(source);
    const auto sourceId = sourceActor ? sourceActor->id : 0;
    const auto summary = ApplyInjury(actor->combat.body, part, injury, actor->combat.injury);
    const bool capabilityLost = summary.lostCapability &&
        actor->combat.action.kind == ActorActionKind::MeleeWindup &&
        actor->combat.action.move == static_cast<std::uint32_t>(CombatMove::HeavyAttack) &&
        !HeavyCapable(actor->combat.body);
    const bool defeated = summary.defeated && !actor->player;
    result.injury = injury;
    result.lostCapability = summary.lostCapability;
    result.outcome = defeated ? AttackOutcome::Defeated : AttackOutcome::Hit;
    world_.ActorChanged(handle);
    result.disruption = Disrupt(handle, {DisruptionCause::DamagingHit, sourceId}).response;
    if (capabilityLost) {
        const auto disrupted = Disrupt(handle, {DisruptionCause::RequiredCapabilityLost, sourceId});
        if (disrupted.response != DisruptionResponse::Ignored) { result.disruption = disrupted.response; }
    }
    Record(handle, result, true);
    if (defeated) { Require(world_.DestroyActor(handle).has_value()); }
    return result;
}

AttackResult CombatSystem::Strike(ActorHandle handle, ActorHandle target, CombatMove move,
    std::uint8_t accuracyPenalty, std::uint32_t& rng) {
    const auto* actor = world_.GetActor(handle);
    const auto* other = world_.GetActor(target);
    AttackResult result;
    result.move = move;
    if (!Available(world_, actor) || !Available(world_, other) ||
        !Hostile(actor->combat.faction, other->combat.faction)) {
        result.outcome = AttackOutcome::NoTarget; return result;
    }
    result.target = other->id;
    if (!CanReach(handle, target)) { result.outcome = AttackOutcome::OutOfReach; return result; }
    const auto penalty = accuracyPenalty > 30 ? 30U : accuracyPenalty;
    const auto accuracy = actor->combat.accuracy > penalty ? actor->combat.accuracy - penalty : 0;
    if (Draw(rng) % 100 >= accuracy) {
        result.outcome = AttackOutcome::Miss;
        Record(target, result, true);
        return result;
    }
    if (CanParry(other->combat)) {
        auto* defender = world_.MutableActor(target);
        defender->combat.stamina -= 10;
        defender->combat.reactionCooldown = 100;
        world_.ActorChanged(target);
        result.outcome = AttackOutcome::Parried;
        result.contact = true;
        Record(target, result, true);
        return result;
    }
    BodyPart parts[body_part_count];
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < body_part_count; ++i) {
        const auto part = static_cast<BodyPart>(i);
        if (HasPart(other->combat.body.kind, part)) { parts[count++] = part; }
    }
    Require(count != 0);
    const auto part = parts[Draw(rng) % count];
    auto damage = actor->combat.attackDamage;
    if (move == CombatMove::Bite && damage != 0) { damage = eastl::max(1U, damage / 2); }
    const std::uint64_t amount = std::uint64_t{damage} * body_hp_scale *
        (move == CombatMove::HeavyAttack ? 2U : 1U);
    Require(amount <= std::numeric_limits<std::uint32_t>::max());
    result = Damage(target, handle, part, {DamageType::Blunt, static_cast<std::uint32_t>(amount), 0});
    result.move = move;
    return result;
}

void CombatSystem::Elapse(Tick elapsed) {
    if (elapsed == 0) { return; }
    world_.CollectActiveActors(elapsedActors_);
    for (const auto handle : elapsedActors_) {
        auto* actor = world_.MutableActor(handle);
        if (!actor || actor->parked || !world_.IsActive(actor->position)) { continue; }
        auto& state = actor->combat;
        const auto previous = state;
        state.reactionCooldown = Reduced(state.reactionCooldown, elapsed);
        state.heavyCooldown = Reduced(state.heavyCooldown, elapsed);
        // Split quotient/remainder avoids overflow for arbitrary elapsed time.
        const Tick remainder = elapsed % 10 + state.staminaElapsed;
        const Tick gained = elapsed / 10 + remainder / 10;
        const auto missing = 100U - state.stamina;
        if (gained >= missing) { state.stamina = 100; state.staminaElapsed = 0; }
        else { state.stamina += static_cast<std::uint32_t>(gained); state.staminaElapsed = remainder % 10; }
        ElapseBody(state.body, state.injury, elapsed);
        if (!(state == previous)) { world_.ActorChanged(handle); }
    }
}

void CombatSystem::ProcessEvents(std::uint32_t& rng) {
    world_.CollectActiveActors(dueActors_);
    dueActors_.erase(eastl::remove_if(dueActors_.begin(), dueActors_.end(), [this](ActorHandle handle) {
        const auto* actor = world_.GetActor(handle);
        return actor->combat.action.kind == ActorActionKind::None || actor->actionDeadline.suspended
            || actor->actionDeadline.Remaining(world_.GetTick()) != 0;
    }), dueActors_.end());
    eastl::sort(dueActors_.begin(), dueActors_.end(), [this](ActorHandle a, ActorHandle b) {
        return world_.GetActor(a)->id < world_.GetActor(b)->id;
    });
    for (const auto handle : dueActors_) {
        auto* actor = world_.MutableActor(handle);
        if (!actor || actor->parked || !world_.IsActive(actor->position) || actor->actionDeadline.suspended ||
            actor->actionDeadline.Remaining(world_.GetTick()) != 0 ||
            actor->combat.action.kind == ActorActionKind::None) { continue; }
        const auto action = actor->combat.action;
        if (action.kind == ActorActionKind::MeleeWindup) {
            Require(action.move == static_cast<std::uint32_t>(CombatMove::HeavyAttack) && action.recoveryTicks != 0);
            actor->combat.action = {ActorActionKind::Recovery};
            actor->actionDeadline.Set(world_.GetTick(), action.recoveryTicks);
            world_.ActorChanged(handle);
            const auto result = Strike(handle, world_.FindActor(action.target), static_cast<CombatMove>(action.move), action.accuracyPenalty, rng);
            Record(handle, result, false);
        } else {
            actor->combat.action = {};
            world_.ActorChanged(handle);
        }
    }
}

eastl::optional<Tick> CombatSystem::NextEventIn() const {
    world_.CollectActiveActors(queriedActors_);
    eastl::optional<Tick> next;
    for (const auto handle : queriedActors_) {
        const auto* actor = world_.GetActor(handle);
        if (!actor || actor->parked || !world_.IsActive(actor->position) || actor->actionDeadline.suspended ||
            actor->combat.action.kind == ActorActionKind::None) { continue; }
        const auto remaining = actor->actionDeadline.Remaining(world_.GetTick());
        if (!next || remaining < *next) { next = remaining; }
    }
    return next;
}

}
