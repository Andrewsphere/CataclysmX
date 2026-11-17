#include "combat/combat_runtime.h"
#include "combat/combat_system.h"
#include "content/armor_definition.h"
#include "skills/learning_system.h"
#include "core/perception.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstdlib>
#include <limits>

namespace cataclysmx::combat_runtime {
namespace {
void Require(bool condition) { if (!condition) { std::abort(); } }
unsigned Bits(HandMask value) { return static_cast<unsigned>(value); }
unsigned Bits(LegMask value) { return static_cast<unsigned>(value); }
HandMask Hands(unsigned bits) { return static_cast<HandMask>(bits); }
LegMask Legs(unsigned bits) { return static_cast<LegMask>(bits); }
bool HandWorks(const Actor& actor, unsigned hand) {
    return Functional(actor.combat.body, hand == 0 ? BodyPart::LeftHand : BodyPart::RightHand);
}
bool LegWorks(const Actor& actor, unsigned leg) {
    return Functional(actor.combat.body, leg == 0 ? BodyPart::LeftLeg : BodyPart::RightLeg);
}
bool BindSource(const World& world, const Actor& actor, const SourceRequirement& requirement,
    unsigned hand, SourceCommitment& binding, bool naturalAttack) {
    const auto& body = actor.combat.body;
    for (unsigned i = 0; i < 2; ++i) {
        if ((Bits(requirement.freeHands) & (1U << i)) != 0) {
            if (actor.equipment.hands[i].item.value != 0 || !HandWorks(actor, i)) { return false; }
            binding.hands[i] = {0, GripKind::OneHanded, true};
            binding.functionalHands = Hands(Bits(binding.functionalHands) | (1U << i));
        }
    }
    binding.functionalHands = Hands(Bits(binding.functionalHands) | Bits(requirement.functionalHands));
    binding.functionalLegs = Legs(Bits(binding.functionalLegs) | Bits(requirement.functionalLegs));
    switch (requirement.kind) {
    case AttackSourceKind::HeldItem: {
        const auto held = actor.equipment.hands[hand];
        const auto* item = world.GetItem(held.item);
        if (!item || item->location != ItemLocation::Carried(world.FindActor(actor.id)) ||
            (requirement.requiresGrip && held.grip != requirement.grip)) { return false; }
        const auto& profile = ItemMelee(world.GetContent().GetItem(item->type));
        bool validGrip = false;
        for (const auto& grip : profile.grips) { validGrip |= grip.kind == held.grip; }
        if (!validGrip) { return false; }
        const unsigned mask = held.grip == GripKind::TwoHanded ? 3U : 1U << hand;
        for (unsigned i = 0; i < 2; ++i) {
            if ((mask & (1U << i)) == 0) { continue; }
            if (!HandWorks(actor, i) || actor.equipment.hands[i] != held ||
                (binding.hands[i].required && binding.hands[i].item != item->id)) { return false; }
            binding.hands[i] = {item->id, held.grip, true};
        }
        binding.functionalHands = Hands(Bits(binding.functionalHands) | mask);
        break;
    }
    case AttackSourceKind::EmptyHand: {
        const unsigned mask = requirement.hand == SourceHand::Both ? 3U : 1U << hand;
        for (unsigned i = 0; i < 2; ++i) {
            if ((mask & (1U << i)) == 0) { continue; }
            if (!HandWorks(actor, i) || actor.equipment.hands[i].item.value != 0 ||
                (binding.hands[i].required && binding.hands[i].item != 0)) { return false; }
            binding.hands[i] = {0, GripKind::OneHanded, true};
        }
        binding.functionalHands = Hands(Bits(binding.functionalHands) | mask);
        break;
    }
    case AttackSourceKind::Kick:
        if (body.kind != BodyKind::Detailed || !LegWorks(actor, hand)) { return false; }
        binding.functionalLegs = Legs(Bits(binding.functionalLegs) | (1U << hand));
        break;
    case AttackSourceKind::Natural:
        if (naturalAttack) {
            if (body.kind != BodyKind::Simple ||
                (requirement.requiresNaturalApparatus && !Functional(body, BodyPart::AttackApparatus))) { return false; }
            binding.naturalApparatus |= requirement.requiresNaturalApparatus;
        } else if (body.kind == BodyKind::Detailed) {
            if (!LegWorks(actor, hand)) { return false; }
            binding.functionalLegs = Legs(Bits(binding.functionalLegs) | (1U << hand));
        } else if (!Functional(body, BodyPart::Locomotion)) { return false; }
        break;
    }
    return SourcesValid(world, actor, binding);
}
bool HandlingAllowed(const CombatMoveDefinition& move, SkillId skill) {
    return move.requirements.handlingSkills.empty() || eastl::find(move.requirements.handlingSkills.begin(),
        move.requirements.handlingSkills.end(), skill) != move.requirements.handlingSkills.end();
}
std::uint32_t Scale(std::uint32_t value, std::uint32_t ratio) {
    const auto result = (std::uint64_t{value} * ratio + ratioScale - 1) / ratioScale;
    Require(result <= UINT32_MAX); return static_cast<std::uint32_t>(result);
}
std::uint32_t LegEncumbrance(const EquipmentModifiers& modifiers, BodyKind body) {
    if (body == BodyKind::Simple) { return modifiers.encumbrance[static_cast<unsigned>(ContactRegion::Locomotion)]; }
    std::uint64_t sum = 0;
    for (const auto region : {ContactRegion::LeftLeg, ContactRegion::RightLeg, ContactRegion::LeftFoot, ContactRegion::RightFoot}) {
        sum += modifiers.encumbrance[static_cast<unsigned>(region)];
    }
    return static_cast<std::uint32_t>((sum + 3) / 4);
}
std::uint32_t TorsoEncumbrance(const EquipmentModifiers& modifiers, BodyKind body) {
    return modifiers.encumbrance[static_cast<unsigned>(body == BodyKind::Simple ? ContactRegion::Core : ContactRegion::Torso)];
}
std::uint32_t EncumberedCost(std::uint32_t base, std::uint64_t encumbrance) {
    constexpr auto percentRatio = ratioScale / 100;
    Require(encumbrance <= (UINT64_MAX - ratioScale) / percentRatio);
    const auto ratio = ratioScale + encumbrance * percentRatio;
    Require(base == 0 || ratio <= (UINT64_MAX - (ratioScale - 1)) / base);
    const auto value = (std::uint64_t{base} * ratio + ratioScale - 1) / ratioScale;
    Require(value <= UINT32_MAX);
    return static_cast<std::uint32_t>(value);
}
ActionCost ModifiedCost(ActionCost cost, int skill, std::uint32_t torso, std::uint32_t handling) {
    const unsigned improvement = skill <= 0 ? 0 : skill >= 10 ? 10 : static_cast<unsigned>(skill);
    const auto time = [&](std::uint32_t base) {
        if (!base) { return 0U; }
        const auto ratio = ratioScale - improvement * (ratioScale / 100);
        return eastl::max(1U, static_cast<std::uint32_t>(std::uint64_t{base} * ratio / ratioScale));
    };
    cost.windupCost = time(cost.windupCost);
    cost.recoveryCost = time(cost.recoveryCost);
    cost.staminaCost = EncumberedCost(DiscountStaminaCost(cost.staminaCost, skill),
        std::uint64_t{handling} + torso);
    return cost;
}
bool AllowsContact(const ReachProfile& actorReach, const ReachProfile& strikeReach, BodyKind body, ContactRegion region) {
    return ReachAllowsContact(actorReach, body, region) && ReachAllowsContact(strikeReach, body, region);
}
bool HasReachableContact(const ReachProfile& actorReach, const StrikeDefinition& strike, BodyKind body) {
    if (!strike.contacts.empty()) {
        for (const auto& contact : strike.contacts) {
            if (contact.weight && AllowsContact(actorReach, strike.reach, body, contact.region)) { return true; }
        }
    } else {
        for (unsigned i = 0; i < contactRegionCount; ++i) {
            if (AllowsContact(actorReach, strike.reach, body, static_cast<ContactRegion>(i))) { return true; }
        }
    }
    return false;
}
}

bool Alive(const Actor* actor) {
    return actor && actor->combat.life == ActorLifeState::Alive && BodyAlive(actor->combat.body);
}
bool Aware(const World& world, const Actor& observer, const Actor& target) {
    return observer.player ? world.CanPlayerSee(world.FindActor(observer.id), target.position) :
        LineVisible(world, observer.position, target.position);
}
CombatStyleId BasicStyle(const ContentCatalog& content, CombatStyle style) {
    switch (style) {
    case CombatStyle::Balanced: return content.FindCombatStyle("basic_balanced");
    case CombatStyle::Aggressive: return content.FindCombatStyle("basic_aggressive");
    case CombatStyle::Defensive: return content.FindCombatStyle("basic_defensive");
    }
    return {};
}
CombatStyleId Style(const World& world, const Actor& actor) {
    (void)world;
    return actor.combat.styleId;
}
CombatRefusal Ready(const World& world, ActorHandle handle) {
    const auto* actor = world.GetActor(handle);
    if (!actor) { return CombatRefusal::InvalidActor; }
    if (!Alive(actor)) { return CombatRefusal::Dead; }
    if (actor->parked || !world.IsActive(actor->position)) { return CombatRefusal::Inactive; }
    if (actor->combat.staggerRemaining) { return CombatRefusal::Staggered; }
    if (actor->combat.action.kind != ActorActionKind::None || actor->actionDeadline.suspended ||
        world.GetActorRemainingTicks(handle) || world.Learning().Busy(handle)) { return CombatRefusal::Busy; }
    if (actor->characterCombat) {
        for (const auto& manual : actor->characterCombat->manuals) {
            if (manual.active) { return CombatRefusal::Busy; }
        }
    }
    return CombatRefusal::None;
}
bool HasArt(const Actor& actor, CombatArtId art) {
    if (!art.value) { return true; }
    if (actor.characterCombat) {
        for (const auto& entry : actor.characterCombat->arts) { if (entry.art == art) { return true; } }
    }
    return false;
}
bool Unlocked(const Actor& actor, const MoveUnlock& unlock, CombatArtId art) {
    if (!HasArt(actor, art) || !MeetsSkillRequirements(actor.learning, {unlock.skills.begin(), unlock.skills.size()})) { return false; }
    if (!unlock.artXp) { return true; }
    if (actor.characterCombat) {
        for (const auto& entry : actor.characterCombat->arts) {
            if (entry.art == art && entry.xp >= unlock.artXp) { return true; }
        }
    }
    return false;
}
bool SourcesValid(const World& world, const Actor& actor, const SourceCommitment& sources) {
    if (!Alive(&actor)) { return false; }
    for (unsigned i = 0; i < 2; ++i) {
        const bool simple = actor.combat.body.kind == BodyKind::Simple;
        if ((Bits(sources.functionalHands) & (1U << i)) &&
            !(simple ? Functional(actor.combat.body, BodyPart::AttackApparatus) : HandWorks(actor, i))) { return false; }
        if ((Bits(sources.functionalLegs) & (1U << i)) &&
            !(simple ? Functional(actor.combat.body, BodyPart::Locomotion) : LegWorks(actor, i))) { return false; }
        const auto& committed = sources.hands[i];
        if (!committed.required) { continue; }
        const auto held = actor.equipment.hands[i];
        const auto* item = world.GetItem(held.item);
        if ((item ? item->id : 0) != committed.item || (committed.item && held.grip != committed.grip)) { return false; }
        if (item && item->location != ItemLocation::Carried(world.FindActor(actor.id))) { return false; }
    }
    return !sources.naturalApparatus || Functional(actor.combat.body, BodyPart::AttackApparatus);
}
void Bind(const World& world, const Actor& actor, const CombatMoveDefinition& move, Vector<SourceCommitment>& output) {
    output.clear();
    for (unsigned hand = 0; hand < 2; ++hand) {
        const auto wanted = move.requirements.primary.hand;
        if ((wanted == SourceHand::Left && hand != 0) || (wanted == SourceHand::Right && hand != 1) ||
            (wanted == SourceHand::Both && hand != 0)) { continue; }
        if (move.requirements.primary.kind == AttackSourceKind::Natural && actor.combat.body.kind == BodyKind::Simple && hand != 0) { continue; }
        if (move.requirements.primary.kind == AttackSourceKind::Kick && move.requirements.primary.functionalLegs == LegMask::Both && hand != 0) { continue; }
        if (move.requirements.primary.kind == AttackSourceKind::HeldItem && wanted == SourceHand::Either && hand == 1 &&
            actor.equipment.hands[hand].grip == GripKind::TwoHanded) { continue; }
        SourceCommitment binding;
        binding.primaryHand = static_cast<Hand>(hand);
        binding.primaryKind = move.requirements.primary.kind;
        binding.secondaryKind = move.requirements.secondary.kind;
        binding.paired = move.requirements.paired;
        if (!BindSource(world, actor, move.requirements.primary, hand, binding, move.execution == MoveExecution::Strikes)) { continue; }
        if (!HandlingAllowed(move, Handling(world, actor, binding))) { continue; }
        if (binding.paired) {
            const auto& secondary = move.requirements.secondary;
            if ((secondary.hand == SourceHand::Left && hand == 0) ||
                (secondary.hand == SourceHand::Right && hand == 1) || secondary.hand == SourceHand::Both) { continue; }
            const auto primaryItem = binding.hands[hand].item;
            if (!BindSource(world, actor, secondary, 1U - hand, binding, move.execution == MoveExecution::Strikes)) { continue; }
            if (primaryItem && primaryItem == binding.hands[1U - hand].item) { continue; }
            if (!HandlingAllowed(move, Handling(world, actor, binding, StrikeSource::Secondary))) { continue; }
        }
        if (eastl::find(output.begin(), output.end(), binding) == output.end()) { output.push_back(binding); }
    }
}
const MeleeProfile* Profile(const World& world, const Actor&, const SourceCommitment& sources, StrikeSource source) {
    const auto kind = source == StrikeSource::Primary ? sources.primaryKind : sources.secondaryKind;
    if (kind != AttackSourceKind::HeldItem) { return nullptr; }
    const auto index = static_cast<unsigned>(sources.primaryHand) ^ (source == StrikeSource::Secondary ? 1U : 0U);
    const auto* item = world.GetItem(world.FindItem(sources.hands[index].item));
    return item ? &ItemMelee(world.GetContent().GetItem(item->type)) : nullptr;
}
CombatRefusal SourceRefusal(const World& world, const Actor& actor, const CombatMoveDefinition& move) {
    Vector<SourceCommitment> bindings; Bind(world, actor, move, bindings);
    if (!bindings.empty()) { return CombatRefusal::None; }
    const auto& requirement = move.requirements.primary;
    if (requirement.freeHands != HandMask::None || requirement.kind == AttackSourceKind::EmptyHand) {
        for (unsigned i = 0; i < 2; ++i) {
            if (actor.equipment.hands[i].item.value && (Bits(requirement.freeHands) & (1U << i))) { return CombatRefusal::NeedsFreeHand; }
        }
    }
    if (requirement.kind == AttackSourceKind::HeldItem) {
        bool itemPresent = false, functional = false, gripValid = false;
        for (unsigned i = 0; i < 2; ++i) {
            if ((requirement.hand == SourceHand::Left && i != 0) || (requirement.hand == SourceHand::Right && i != 1)) { continue; }
            const auto held = actor.equipment.hands[i];
            if (!world.GetItem(held.item)) { continue; }
            itemPresent = true;
            functional |= HandWorks(actor, i) && (held.grip != GripKind::TwoHanded || (HandWorks(actor, 0) && HandWorks(actor, 1)));
            gripValid |= !requirement.requiresGrip || held.grip == requirement.grip;
        }
        if (!itemPresent) { return CombatRefusal::MissingSource; }
        if (!functional) { return CombatRefusal::MissingCapability; }
        if (!gripValid) { return CombatRefusal::InvalidGrip; }
    }
    if (requirement.kind == AttackSourceKind::Kick || requirement.kind == AttackSourceKind::Natural) { return CombatRefusal::MissingCapability; }
    return CombatRefusal::MissingSource;
}
const GripDefinition* Grip(const World& world, const Actor& actor, const SourceCommitment& sources, StrikeSource source) {
    const auto* profile = Profile(world, actor, sources, source);
    if (!profile) { return nullptr; }
    const auto index = static_cast<unsigned>(sources.primaryHand) ^ (source == StrikeSource::Secondary ? 1U : 0U);
    for (const auto& grip : profile->grips) { if (grip.kind == sources.hands[index].grip) { return &grip; } }
    return nullptr;
}
SkillId Handling(const World& world, const Actor& actor, const SourceCommitment& sources, StrikeSource source) {
    const auto* profile = Profile(world, actor, sources, source);
    if (!profile) { return world.GetContent().GetCombatSkills().unarmed; }
    return profile->handlingSkill.value ? profile->handlingSkill : world.GetContent().GetCombatSkills().melee;
}
Tick Cooldown(const CombatState& state, CombatMoveId move) {
    for (const auto& entry : state.moveCooldowns) { if (entry.move == move) { return entry.remaining; } }
    return 0;
}
void SetCooldown(CombatState& state, CombatMoveId move, Tick duration) {
    if (!duration) { return; }
    for (auto& entry : state.moveCooldowns) { if (entry.move == move) { entry.remaining = duration; return; } }
    state.moveCooldowns.push_back({move, duration});
}
bool ClearPath(const World& world, TileCoord from, TileCoord to) {
    if (from.y != to.y) { return false; }
    const int dx = std::abs(to.x - from.x), dz = std::abs(to.z - from.z);
    const int sx = to.x > from.x ? 1 : -1, sz = to.z > from.z ? 1 : -1;
    int ix = 0, iz = 0;
    auto cell = from;
    const auto blocked = [&](TileCoord tile) {
        return tile != from && tile != to && (world.BlocksMovement(tile) || world.ActorAt(tile).value != 0);
    };
    while (ix < dx || iz < dz) {
        const int a = (1 + 2 * ix) * dz, b = (1 + 2 * iz) * dx;
        if (a == b) {
            if (blocked({cell.x + sx, cell.z, cell.y}) || blocked({cell.x, cell.z + sz, cell.y})) { return false; }
            cell.x += sx; cell.z += sz; ++ix; ++iz;
        } else if (a < b) { cell.x += sx; ++ix; }
        else { cell.z += sz; ++iz; }
        if (blocked(cell)) { return false; }
    }
    return true;
}
bool Reach(const World& world, const Actor& actor, const Actor& target, const ReachProfile& reach) {
    if (!Alive(&actor) || !Alive(&target) || actor.id == target.id || actor.parked || target.parked ||
        !world.IsActive(actor.position) || !world.IsActive(target.position) || actor.position.y != target.position.y) { return false; }
    const auto distance = eastl::max(std::abs(actor.position.x - target.position.x), std::abs(actor.position.z - target.position.z));
    const auto maximum = actor.player ? 1 : eastl::min<unsigned>(reach.distance, actor.combat.reach.distance);
    if (distance < 1 || static_cast<unsigned>(distance) > maximum ||
        world.BlocksMovement(actor.position) || world.BlocksMovement(target.position)) { return false; }
    if (distance > 1 && !ClearPath(world, actor.position, target.position)) { return false; }
    for (unsigned i = 0; i < contactRegionCount; ++i) {
        if (AllowsContact(actor.combat.reach, reach, target.combat.body.kind, static_cast<ContactRegion>(i))) { return true; }
    }
    return false;
}
bool LeapValid(const World& world, const Actor& actor, TileCoord to, unsigned maximum) {
    const auto from = actor.position;
    const auto distance = eastl::max(std::abs(to.x - from.x), std::abs(to.z - from.z));
    if (from.y != to.y || distance < 1 || static_cast<unsigned>(distance) > eastl::min(maximum, 2U) ||
        !world.IsActive(to) || world.BlocksMovement(to) || world.ActorAt(to).value || !ClearPath(world, from, to)) { return false; }
    const auto* tile = world.GetTile(to);
    return tile && !world.GetContent().GetTerrain(tile->terrain).openSpace;
}
bool StrikeReach(const World& world, const Actor& actor, const Actor& target, const StrikeDefinition& strike) {
    return Reach(world, actor, target, strike.reach) && HasReachableContact(actor.combat.reach, strike, target.combat.body.kind);
}
ActionCost Cost(const World& world, const Actor& actor, const CombatMoveDefinition& move, const SourceCommitment& sources) {
    auto result = move.cost;
    std::uint32_t timeRatio = 0, staminaRatio = 0;
    for (const auto source : {StrikeSource::Primary, StrikeSource::Secondary}) {
        if (source == StrikeSource::Secondary && !sources.paired) { break; }
        const auto* profile = Profile(world, actor, sources, source);
        const auto* grip = Grip(world, actor, sources, source);
        const auto time64 = profile ? std::uint64_t{Scale(profile->attackCost, grip ? grip->costRatio : ratioScale)} * 100 : ratioScale;
        Require(time64 <= UINT32_MAX);
        const auto time = static_cast<std::uint32_t>(time64);
        timeRatio = eastl::max(timeRatio, time);
        staminaRatio = eastl::max(staminaRatio, grip ? grip->staminaRatio : ratioScale);
    }
    result.windupCost = Scale(result.windupCost, timeRatio);
    result.recoveryCost = Scale(result.recoveryCost, timeRatio);
    const auto skill = EffectiveSkillLevel(actor.learning, move.defense.kind == DefenseKind::Dodge ?
        world.GetContent().GetCombatSkills().dodge : Handling(world, actor, sources));
    result.staminaCost = Scale(result.staminaCost, staminaRatio);
    const auto modifiers = Modifiers(world, actor);
    auto handling = HandlingEncumbrance(modifiers, sources);
    if (sources.paired) {
        handling = static_cast<std::uint32_t>((std::uint64_t{handling} +
            HandlingEncumbrance(modifiers, sources, StrikeSource::Secondary) + 1) / 2);
    }
    if (move.defense.kind == DefenseKind::Dodge) { handling = LegEncumbrance(modifiers, actor.combat.body.kind); }
    return ModifiedCost(result, skill, TorsoEncumbrance(modifiers, actor.combat.body.kind), handling);
}
ActionCost AbilityCost(const World& world, const Actor& actor, const AbilityDefinition& ability, const SourceCommitment& sources) {
    const auto modifiers = Modifiers(world, actor);
    const auto skillId = ability.execution == AbilityExecution::Sweep ? Handling(world, actor, sources) : world.GetContent().GetCombatSkills().melee;
    std::uint32_t handling = 0;
    if (ability.execution == AbilityExecution::Leap) { handling = LegEncumbrance(modifiers, actor.combat.body.kind); }
    if (ability.execution == AbilityExecution::Sweep) {
        handling = HandlingEncumbrance(modifiers, sources);
        if (sources.paired) { handling = static_cast<std::uint32_t>((std::uint64_t{handling} +
            HandlingEncumbrance(modifiers, sources, StrikeSource::Secondary) + 1) / 2); }
    }
    // Ability costs are authored independently of the borrowed move/profile.
    return ModifiedCost(ability.cost, EffectiveSkillLevel(actor.learning, skillId), TorsoEncumbrance(modifiers, actor.combat.body.kind), handling);
}
void Candidates(const World& world, const Actor& actor, CombatTrigger trigger, const Actor* target, Vector<Candidate>& output) {
    output.clear();
    const auto* style = world.GetContent().TryGetCombatStyle(Style(world, actor));
    if (!style || !Alive(&actor) || actor.combat.staggerRemaining) { return; }
    bool affordableNormal = false;
    Vector<SourceCommitment> bindings;
    for (const auto& weighted : style->moves) {
        const auto* move = world.GetContent().TryGetCombatMove(weighted.move);
        if (!move || !weighted.weight || move->trigger != trigger || Cooldown(actor.combat, weighted.move) ||
            !Unlocked(actor, move->unlock, style->art)) { continue; }
        if ((move->requirements.requiresOpening || trigger == CombatTrigger::CounterOpening) &&
            (!target || !HasOpening(actor.combat, target->id))) { continue; }
        if (trigger != CombatTrigger::IncomingStrike) {
            bool reachable = false;
            for (const auto& strike : move->strikes) {
                reachable |= target ? StrikeReach(world, actor, *target, strike) :
                    HasReachableContact(actor.combat.reach, strike, BodyKind::Detailed) || HasReachableContact(actor.combat.reach, strike, BodyKind::Simple);
            }
            if (!reachable) { continue; }
        }
        if (trigger == CombatTrigger::IncomingStrike) {
            if (actor.combat.action.kind == ActorActionKind::DisruptionRecovery || actor.combat.action.kind == ActorActionKind::Aiming) { continue; }
            if (actor.combat.action.kind == ActorActionKind::Recovery && !move->requirements.allowedDuringRecovery) { continue; }
            if (actor.combat.action.kind == ActorActionKind::MeleeWindup || actor.combat.action.kind == ActorActionKind::AbilityWindup) {
                if (!move->requirements.allowedDuringWindup) { continue; }
            }
            if (move->requirements.requiresAwareness && target && !Aware(world, actor, *target)) { continue; }
        }
        if (trigger == CombatTrigger::CounterOpening && (actor.actionDeadline.suspended ||
            (actor.combat.action.kind != ActorActionKind::None && actor.combat.action.kind != ActorActionKind::Recovery))) { continue; }
        Bind(world, actor, *move, bindings);
        for (const auto& binding : bindings) {
            if (trigger == CombatTrigger::IncomingStrike &&
                (actor.combat.action.kind == ActorActionKind::MeleeWindup || actor.combat.action.kind == ActorActionKind::AbilityWindup)) {
                const auto& committed = actor.combat.action.sources;
                if ((Bits(binding.functionalHands) & Bits(committed.functionalHands)) ||
                    (Bits(binding.functionalLegs) & Bits(committed.functionalLegs)) ||
                    (binding.naturalApparatus && committed.naturalApparatus)) { continue; }
            }
            const auto cost = Cost(world, actor, *move, binding);
            if (!actor.combat.inexhaustible && actor.combat.stamina < cost.staminaCost) { continue; }
            affordableNormal |= !move->requirements.exhaustedOnly;
            output.push_back({weighted.move, binding, cost, weighted.weight});
        }
    }
    output.erase(eastl::remove_if(output.begin(), output.end(), [&](const Candidate& candidate) {
        return world.GetContent().GetCombatMove(candidate.move).requirements.exhaustedOnly == affordableNormal;
    }), output.end());
    eastl::sort(output.begin(), output.end(), [&](const Candidate& a, const Candidate& b) {
        if (a.move != b.move) { return world.GetContent().GetCombatMove(a.move).id < world.GetContent().GetCombatMove(b.move).id; }
        if (a.sources.primaryKind != b.sources.primaryKind) { return a.sources.primaryKind < b.sources.primaryKind; }
        if (a.sources.primaryHand != b.sources.primaryHand) { return a.sources.primaryHand < b.sources.primaryHand; }
        return a.sources.functionalLegs < b.sources.functionalLegs;
    });
}
Candidate Select(const World&, const Vector<Candidate>& candidates, std::uint32_t& rng) {
    Require(!candidates.empty());
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i == 0 || candidates[i].move != candidates[i - 1].move) { total += candidates[i].weight; }
    }
    Require(total && total <= UINT32_MAX);
    auto draw = DrawBelow(rng, static_cast<std::uint32_t>(total));
    for (std::size_t begin = 0; begin < candidates.size();) {
        auto end = begin + 1;
        while (end < candidates.size() && candidates[end].move == candidates[begin].move) { ++end; }
        if (draw < candidates[begin].weight) { return candidates[begin + DrawBelow(rng, static_cast<std::uint32_t>(end - begin))]; }
        draw -= candidates[begin].weight; begin = end;
    }
    std::abort();
}
EquipmentModifiers Modifiers(const World& world, const Actor& actor) {
    EquipmentModifiers result;
    eastl::array<eastl::array<unsigned, 4>, contactRegionCount> counts{};
    for (const auto handle : actor.equipment.worn) {
        const auto* item = world.GetItem(handle); if (!item) { continue; }
        const auto& armor = world.GetContent().GetItem(item->type).armor; if (!armor) { continue; }
        for (const auto& region : armor->regions) {
            const auto index = static_cast<unsigned>(region.region);
            result.encumbrance[index] += region.encumbrance;
            if (!region.overlapExempt) { ++counts[index][static_cast<unsigned>(armor->layer)]; }
        }
    }
    for (unsigned i = 0; i < contactRegionCount; ++i) {
        for (const auto count : counts[i]) { result.encumbrance[i] = LayerEncumbrance(result.encumbrance[i], count); }
    }
    return result;
}
std::uint32_t HandlingEncumbrance(const EquipmentModifiers& modifiers, const SourceCommitment& sources, StrikeSource source) {
    const auto kind = source == StrikeSource::Primary ? sources.primaryKind : sources.secondaryKind;
    if (kind == AttackSourceKind::Natural) { return modifiers.encumbrance[static_cast<unsigned>(ContactRegion::AttackApparatus)]; }
    const auto hand = static_cast<unsigned>(sources.primaryHand) ^ (source == StrikeSource::Secondary ? 1U : 0U);
    unsigned mask = 1U << hand;
    if (kind == AttackSourceKind::Kick) {
        if (sources.functionalLegs != LegMask::None) { mask = Bits(sources.functionalLegs); }
    } else if (sources.hands[hand].grip == GripKind::TwoHanded ||
        (kind == AttackSourceKind::EmptyHand && !sources.paired && sources.hands[0].required && sources.hands[1].required)) {
        mask = 3;
    }
    std::uint64_t sum = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < 2; ++i) {
        if (!(mask & (1U << i))) { continue; }
        const auto first = kind == AttackSourceKind::Kick ? (i == 0 ? ContactRegion::LeftLeg : ContactRegion::RightLeg) :
            (i == 0 ? ContactRegion::LeftArm : ContactRegion::RightArm);
        const auto second = kind == AttackSourceKind::Kick ? (i == 0 ? ContactRegion::LeftFoot : ContactRegion::RightFoot) :
            (i == 0 ? ContactRegion::LeftHand : ContactRegion::RightHand);
        sum += modifiers.encumbrance[static_cast<unsigned>(first)];
        sum += modifiers.encumbrance[static_cast<unsigned>(second)];
        count += 2;
    }
    Require(count > 0);
    return static_cast<std::uint32_t>((sum + count - 1) / count);
}
std::uint32_t MovementEncumbranceCost(const World& world, const Actor& actor, std::uint32_t baseCost) {
    return EncumberedCost(baseCost, LegEncumbrance(Modifiers(world, actor), actor.combat.body.kind));
}
ContactRegion Contact(const Actor& target, const StrikeDefinition& strike, const ReachProfile& sourceReach, std::uint32_t& rng) {
    constexpr std::uint32_t weights[] = {8, 1, 1, 1, 40, 8, 8, 3, 3, 10, 10, 2, 2, 20, 20, 60};
    std::uint64_t total = 0;
    const auto weight = [&](unsigned i) {
        if (!AllowsContact(sourceReach, strike.reach, target.combat.body.kind, static_cast<ContactRegion>(i))) { return 0U; }
        if (strike.contacts.empty()) { return weights[i]; }
        for (const auto& entry : strike.contacts) { if (static_cast<unsigned>(entry.region) == i) { return entry.weight; } }
        return 0U;
    };
    for (unsigned i = 0; i < contactRegionCount; ++i) { total += weight(i); }
    if (!total) { return ContactRegion::Count; }
    Require(total <= UINT32_MAX);
    auto draw = DrawBelow(rng, static_cast<std::uint32_t>(total));
    for (unsigned i = 0; i < contactRegionCount; ++i) {
        const auto value = weight(i); if (draw < value) { return static_cast<ContactRegion>(i); } draw -= value;
    }
    std::abort();
}
std::int32_t Rating(const World& world, const Actor& actor, SkillId handling,
    const SourceCommitment& sources, StrikeSource source, DefenseKind defense, std::int32_t bonus, std::int32_t penalty) {
    const auto& skills = world.GetContent().GetCombatSkills();
    const auto modifiers = Modifiers(world, actor);
    const bool dodge = defense == DefenseKind::Dodge;
    const auto encumbrancePenalty = dodge ? std::int64_t{2} *
        (std::int64_t{TorsoEncumbrance(modifiers, actor.combat.body.kind)} + LegEncumbrance(modifiers, actor.combat.body.kind)) :
        std::int64_t{3} * HandlingEncumbrance(modifiers, sources, source);
    std::int64_t rating = defense == DefenseKind::None ? actor.combat.attackRating100 : actor.combat.defenseRating100;
    rating += bonus;
    rating += actor.combat.focusRemaining ? actor.combat.focusRatingBonus100 : 0;
    rating -= std::int64_t{penalty} + static_cast<std::int64_t>(encumbrancePenalty);
    if (!actor.combat.authoredTrainingRanks) {
        rating += dodge ? DodgeRating100(EffectiveSkillLevel(actor.learning, skills.dodge)) :
            AttackRating100(EffectiveSkillLevel(actor.learning, skills.melee), EffectiveSkillLevel(actor.learning, handling));
    }
    Require(rating >= std::numeric_limits<std::int32_t>::min() && rating <= std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(rating);
}
int OffensiveRank(const World& world, const Actor& actor, SkillId handling) {
    if (actor.combat.authoredTrainingRanks) { return actor.combat.offensiveTrainingRank; }
    return AttackRating100(LearnedSkillLevel(actor.learning, world.GetContent().GetCombatSkills().melee),
        LearnedSkillLevel(actor.learning, handling)) / 100;
}
int DefensiveRank(const World& world, const Actor& actor) {
    if (actor.combat.authoredTrainingRanks) { return actor.combat.defensiveTrainingRank; }
    int best = 0;
    const auto* style = world.GetContent().TryGetCombatStyle(Style(world, actor));
    if (!style) { return best; }
    Vector<SourceCommitment> bindings;
    for (const auto& weighted : style->moves) {
        const auto& move = world.GetContent().GetCombatMove(weighted.move);
        if (move.trigger != CombatTrigger::IncomingStrike || !Unlocked(actor, move.unlock, style->art) ||
            Cooldown(actor.combat, weighted.move)) { continue; }
        Bind(world, actor, move, bindings);
        for (const auto& binding : bindings) {
            if (!actor.combat.inexhaustible && actor.combat.stamina < Cost(world, actor, move, binding).staminaCost) { continue; }
            const auto* profile = Profile(world, actor, binding);
            if (profile && !AllowsDefense(profile->allowedResponses, move.defense.kind)) { continue; }
            const int rank = move.defense.kind == DefenseKind::Dodge ? LearnedSkillLevel(actor.learning, world.GetContent().GetCombatSkills().dodge) :
                OffensiveRank(world, actor, Handling(world, actor, binding));
            best = eastl::max(best, rank);
        }
    }
    return best;
}
bool Granted(const World& world, const Actor& actor, AbilityId ability, AbilityGrantKey source) {
    if (!actor.characterCombat) { return false; }
    for (const auto& grant : actor.characterCombat->abilityGrants) {
        if (grant.ability == ability && grant.source == source && source.kind == AbilityGrantSourceKind::Innate && !source.art.value) { return true; }
    }
    if (source.kind == AbilityGrantSourceKind::Art && HasArt(actor, source.art)) {
        const auto* art = world.GetContent().TryGetCombatArt(source.art);
        if (art) { for (const auto& grant : art->abilities) {
            if (grant.ability == ability && Unlocked(actor, grant.unlock, source.art)) { return true; }
        } }
    }
    return false;
}
Tick AbilityCooldownRemaining(const Actor& actor, AbilityId ability, AbilityGrantKey source) {
    if (actor.characterCombat) { for (const auto& entry : actor.characterCombat->abilityCooldowns) {
        if (entry.ability == ability && entry.source == source) { return entry.remaining; }
    } }
    return 0;
}
void RefreshAbilityGrants(World& world, ActorHandle handle) {
    const auto* actor = world.GetActor(handle);
    if (!Alive(actor) || !actor->characterCombat || actor->characterCombat->arts.empty()) { return; }
    const auto& state = *actor->characterCombat;
    std::size_t expected = 0;
    bool stale = false;
    for (const auto& progress : state.arts) {
        const auto& art = world.GetContent().GetCombatArt(progress.art);
        for (const auto& definition : art.abilities) {
            if (!Unlocked(*actor, definition.unlock, progress.art)) { continue; }
            ++expected;
            const AbilityGrant grant{definition.ability, {AbilityGrantSourceKind::Art, progress.art}};
            if (eastl::count(state.abilityGrants.begin(), state.abilityGrants.end(), grant) != 1) { stale = true; }
        }
    }
    const auto actual = static_cast<std::size_t>(eastl::count_if(state.abilityGrants.begin(), state.abilityGrants.end(),
        [](const AbilityGrant& grant) { return grant.source.kind == AbilityGrantSourceKind::Art; }));
    if (stale || actual != expected) {
        // GrantArt is idempotent: an already owned art rebuilds all art grants,
        // preserving innate grants and source-specific cooldown identities.
        const auto ownedArt = state.arts.front().art;
        Require(world.Combat().GrantArt(handle, ownedArt, ArtGrantReason::Administration).has_value());
    }
}
void Opening(CombatState& state, PersistentId opponent, Tick duration) {
    if (!duration) { return; }
    for (auto& entry : state.openings) { if (entry.opponent == opponent) { entry.remaining = eastl::max(entry.remaining, duration); return; } }
    state.openings.push_back({opponent, duration});
}
bool HasOpening(const CombatState& state, PersistentId opponent) {
    for (const auto& entry : state.openings) { if (entry.opponent == opponent && entry.remaining) { return true; } }
    return false;
}
bool EffectApplies(EffectCondition condition, const StrikeResult& result) {
    switch (condition) {
    case EffectCondition::Always: return result.outcome != StrikeOutcome::Invalidated;
    case EffectCondition::Miss: return result.outcome == StrikeOutcome::Miss;
    case EffectCondition::DefenseSucceeded: return result.defenseSucceeded;
    case EffectCondition::Contact: return result.outcome == StrikeOutcome::Contact;
    case EffectCondition::DamagingContact: return result.hpLoss || result.maxHpLoss;
    case EffectCondition::Killed: return result.killed;
    }
    return false;
}
}
