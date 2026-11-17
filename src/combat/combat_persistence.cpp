#include "combat/combat_persistence.h"
#include "combat/combat_types.h"
#include "content/content.h"
#include "actors/actor.h"
#include "core/world.h"
#include "items/item_types.h"
#include <EASTL/algorithm.h>
#include <cstdlib>
#include <initializer_list>

namespace cataclysmx {
namespace {
bool Fields(const DataObject& d, std::initializer_list<const char*> fields) {
    if (d.Type() != DataType::Object || d.Size() != fields.size()) { return false; }
    for (const auto* field : fields) { if (!d.Find(field)) { return false; } }
    return true;
}
template<class T> bool Unsigned(const DataObject& d, const char* key, T& out, std::uint64_t max = UINT64_MAX) {
    const auto n = d.GetUnsigned(key);
    if (!n || *n > max) { return false; }
    out = static_cast<T>(*n); return true;
}
bool Signed(const DataObject& d, const char* key, std::int32_t& out) {
    const auto n = d.GetSigned(key);
    if (!n || *n < INT32_MIN || *n > INT32_MAX) { return false; }
    out = static_cast<std::int32_t>(*n); return true;
}
bool Boolean(const DataObject& d, const char* key, bool& out) {
    const auto v = d.GetBoolean(key); if (!v) { return false; } out = *v; return true;
}
eastl::unexpected<StateError> Invalid(Diagnostics* diagnostics, const char* message,
    StateError error = StateError::InvalidData) {
    if (diagnostics) { diagnostics->Add(message); } return eastl::unexpected(error);
}
template<class Id, class Find> bool Definition(const DataObject& d, const char* key, Id& id, Find find) {
    const auto name = d.GetString(key);
    if (!name) { return false; }
    id = name->empty() ? Id{} : find(*name);
    return name->empty() || id.value != 0;
}
DataObject Grant(const AbilityGrantKey& key, const ContentCatalog& c) {
    DataObject d; d.SetUnsigned("kind", static_cast<unsigned>(key.kind));
    d.SetString("art", key.art.value ? c.GetCombatArt(key.art).id : String{}); return d;
}
bool ReadGrant(const DataObject& d, AbilityGrantKey& key, const ContentCatalog& c) {
    return Fields(d, {"kind", "art"}) && Unsigned(d, "kind", key.kind, 1)
        && Definition(d, "art", key.art, [&c](auto name) { return c.FindCombatArt(name); })
        && (key.kind == AbilityGrantSourceKind::Art ? key.art.value != 0 : key.art.value == 0);
}
bool Terminal(const CombatState& s) {
    return s.body.kind == BodyKind::Detailed
        ? s.body.parts[static_cast<unsigned>(BodyPart::Head)].hp == 0
            || s.body.parts[static_cast<unsigned>(BodyPart::Torso)].hp == 0
        : s.body.parts[static_cast<unsigned>(BodyPart::Core)].hp == 0;
}
CombatState LegacyPart(const CombatState& s) {
    CombatState d;
    d.body = s.body; d.injury = s.injury; d.action = s.action.definitionDriven ? ActorActionState{} : s.action;
    d.style = s.style; d.faction = s.faction; d.protection = s.protection;
    d.attackDamage = s.attackDamage; d.accuracy = s.accuracy; d.stamina = s.stamina;
    d.staminaElapsed = s.staminaElapsed; d.reactionCooldown = s.reactionCooldown; d.heavyCooldown = s.heavyCooldown;
    return d;
}
const char* LegacyStyle(CombatStyle s) {
    switch (s) {
    case CombatStyle::Balanced: return "basic_balanced";
    case CombatStyle::Aggressive: return "basic_aggressive";
    case CombatStyle::Defensive: return "basic_defensive";
    } return "";
}
const char* LegacyMove(CombatMove m) {
    switch (m) {
    case CombatMove::None: return "";
    case CombatMove::PlainAttack: return "basic_fist";
    case CombatMove::HeavyAttack: return "basic_heavy";
    case CombatMove::Bite: return "basic_bite";
    case CombatMove::Parry: return "basic_parry";
    } return "";
}
bool Migrate(CombatState& s, const ContentCatalog& c) {
    s.styleId = c.FindCombatStyle(LegacyStyle(s.style));
    if (!s.styleId.value) { return false; }
    s.legacyAccuracyOverride = true; s.intrinsicMissOverride = (100 - s.accuracy) * 100;
    s.staminaRegenerationRemainder = static_cast<std::uint32_t>(s.staminaElapsed * ratioScale / 10);
    s.staminaElapsed = 0;
    for (const auto old : {CombatMove::HeavyAttack, CombatMove::Parry}) {
        const Tick remaining = old == CombatMove::HeavyAttack ? s.heavyCooldown : s.reactionCooldown;
        if (!remaining) { continue; }
        const auto id = c.FindCombatMove(LegacyMove(old));
        if (!id.value) { return false; } s.moveCooldowns.push_back({id, remaining});
    }
    if (s.action.kind == ActorActionKind::MeleeWindup) {
        auto& a = s.action; a.moveId = c.FindCombatMove(LegacyMove(static_cast<CombatMove>(a.move)));
        if (!a.moveId.value) { return false; }
        a.definitionDriven = true; a.committedStyle = s.styleId;
        a.ratingPenalty100 = a.accuracyPenalty * 100;
        a.sources.primaryKind = a.weapon ? AttackSourceKind::HeldItem : AttackSourceKind::EmptyHand;
        a.sources.hands[0] = {a.weapon, GripKind::OneHanded, true};
        a.sources.functionalHands = HandMask::Left;
    }
    if (Terminal(s)) { s.life = ActorLifeState::Dead; s.action = {}; }
    return true;
}
DataObject Sources(const SourceCommitment& s) {
    DataObject d; DataObject hands = DataObject::List();
    for (const auto& h : s.hands) {
        DataObject v; v.SetUnsigned("item", h.item); v.SetUnsigned("grip", static_cast<unsigned>(h.grip));
        v.SetBoolean("required", h.required); hands.Append(eastl::move(v));
    }
    d.Set("hands", eastl::move(hands));
    d.SetUnsigned("functionalHands", static_cast<unsigned>(s.functionalHands));
    d.SetUnsigned("functionalLegs", static_cast<unsigned>(s.functionalLegs));
    d.SetUnsigned("primaryHand", static_cast<unsigned>(s.primaryHand));
    d.SetUnsigned("primaryKind", static_cast<unsigned>(s.primaryKind));
    d.SetUnsigned("secondaryKind", static_cast<unsigned>(s.secondaryKind));
    d.SetBoolean("naturalApparatus", s.naturalApparatus); d.SetBoolean("paired", s.paired); return d;
}
bool ReadSources(const DataObject& d, SourceCommitment& s) {
    if (!Fields(d, {"hands", "functionalHands", "functionalLegs", "primaryHand", "primaryKind", "secondaryKind", "naturalApparatus", "paired"})) { return false; }
    auto hands = d.Get("hands", DataType::List);
    if (!hands || (*hands)->Size() != 2) { return false; }
    for (unsigned i = 0; i < 2; ++i) {
        const auto& v = (*hands)->At(i); auto& h = s.hands[i];
        if (!Fields(v, {"item", "grip", "required"}) || !Unsigned(v, "item", h.item)
            || !Unsigned(v, "grip", h.grip, 1) || !Boolean(v, "required", h.required)
            || (!h.required && (h.item || h.grip != GripKind::OneHanded))) { return false; }
    }
    const auto& l = s.hands[0]; const auto& r = s.hands[1];
    if ((l.item && l.item == r.item) ? l.grip != GripKind::TwoHanded || r.grip != GripKind::TwoHanded
        : l.grip == GripKind::TwoHanded || r.grip == GripKind::TwoHanded) { return false; }
    return Unsigned(d, "functionalHands", s.functionalHands, 3) && Unsigned(d, "functionalLegs", s.functionalLegs, 3)
        && Unsigned(d, "primaryHand", s.primaryHand, 1) && Unsigned(d, "primaryKind", s.primaryKind, 3)
        && Unsigned(d, "secondaryKind", s.secondaryKind, 3) && Boolean(d, "naturalApparatus", s.naturalApparatus)
        && Boolean(d, "paired", s.paired);
}

bool ArtGrants(const ContentCatalog& c, CombatArtId art, AbilityId ability) {
    const auto* definition = c.TryGetCombatArt(art);
    return definition && eastl::any_of(definition->abilities.begin(), definition->abilities.end(),
        [ability](const ArtAbilityGrant& grant) { return grant.ability == ability; });
}
bool GrantReference(const ContentCatalog& c, AbilityId ability, const AbilityGrantKey& source) {
    if (!c.TryGetAbility(ability)) { return false; }
    switch (source.kind) {
    case AbilityGrantSourceKind::Art: return ArtGrants(c, source.art, ability);
    case AbilityGrantSourceKind::Innate: return !source.art.value;
    }
    return false;
}

// Validate committed identity/requirements, not current ownership or capability.
// Those may have changed during paid windup and are revalidated at execution.
bool SourceBindings(const SourceCommitment& source, const CombatMoveDefinition& move,
    BodyKind body, bool legacyUnarmedHeavy) {
    auto primary = move.requirements.primary;
    if (legacyUnarmedHeavy) { primary.kind = AttackSourceKind::EmptyHand; }
    if (source.primaryKind != primary.kind || source.secondaryKind != move.requirements.secondary.kind
        || source.paired != move.requirements.paired) { return false; }
    unsigned expectedHands = 0, expectedLegs = 0, requiredHands = 0;
    bool natural = false;
    const auto bind = [&](const SourceRequirement& requirement, unsigned hand) {
        if ((requirement.hand == SourceHand::Left && hand != 0)
            || (requirement.hand == SourceHand::Right && hand != 1)
            || (requirement.hand == SourceHand::Both && hand != 0)) { return false; }
        const auto free = static_cast<unsigned>(requirement.freeHands);
        for (unsigned i = 0; i < 2; ++i) {
            if ((free & (1U << i)) && (!source.hands[i].required || source.hands[i].item)) { return false; }
        }
        requiredHands |= free;
        expectedHands |= free | static_cast<unsigned>(requirement.functionalHands);
        expectedLegs |= static_cast<unsigned>(requirement.functionalLegs);
        switch (requirement.kind) {
        case AttackSourceKind::HeldItem: {
            const auto& held = source.hands[hand];
            if (!held.required || !held.item || (requirement.requiresGrip && held.grip != requirement.grip)) { return false; }
            const unsigned mask = held.grip == GripKind::TwoHanded ? 3U : 1U << hand;
            requiredHands |= mask; expectedHands |= mask;
            break;
        }
        case AttackSourceKind::EmptyHand: {
            const unsigned mask = requirement.hand == SourceHand::Both ? 3U : 1U << hand;
            for (unsigned i = 0; i < 2; ++i) {
                if ((mask & (1U << i)) && (!source.hands[i].required || source.hands[i].item)) { return false; }
            }
            requiredHands |= mask; expectedHands |= mask;
            break;
        }
        case AttackSourceKind::Kick:
            if (body != BodyKind::Detailed) { return false; }
            expectedLegs |= 1U << hand; break;
        case AttackSourceKind::Natural:
            if (body != BodyKind::Simple) { return false; }
            natural |= requirement.requiresNaturalApparatus; break;
        }
        return true;
    };
    const auto hand = static_cast<unsigned>(source.primaryHand);
    if (!bind(primary, hand)) { return false; }
    if (source.paired) {
        if (move.requirements.secondary.hand == SourceHand::Both || !bind(move.requirements.secondary, 1U - hand)) { return false; }
        if (source.hands[hand].item && source.hands[hand].item == source.hands[1U - hand].item) { return false; }
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (source.hands[i].required != ((requiredHands & (1U << i)) != 0)) { return false; }
    }
    return static_cast<unsigned>(source.functionalHands) == expectedHands
        && static_cast<unsigned>(source.functionalLegs) == expectedLegs && source.naturalApparatus == natural;
}

bool ActionReferences(const CombatState& state, const ContentCatalog& c) {
    const auto& a = state.action;
    if (!a.definitionDriven) { return true; }
    const auto* style = c.TryGetCombatStyle(a.committedStyle);
    if (!style) { return false; }
    const auto* move = c.TryGetCombatMove(a.moveId);
    if (a.ability.value) {
        const auto* ability = c.TryGetAbility(a.ability);
        if (!ability || !GrantReference(c, a.ability, a.abilitySource) || a.counter || a.target || a.weapon
            || a.move || a.accuracyPenalty || a.ratingPenalty100
            || a.trainingArt != (a.abilitySource.kind == AbilityGrantSourceKind::Art ? a.abilitySource.art : CombatArtId{})
            || a.trainingWork > ability->training.work
            || (a.kind != ActorActionKind::AbilityWindup && a.kind != ActorActionKind::Recovery)
            || (a.kind == ActorActionKind::AbilityWindup && !ability->cost.windupCost)) { return false; }
        if (ability->execution != AbilityExecution::Sweep) {
            return !a.moveId.value && a.sources == SourceCommitment{};
        }
        if (!move || a.moveId != ability->sweepMove || move->execution != MoveExecution::Strikes
            || move->trigger != CombatTrigger::Attack || move->requirements.requiresOpening) { return false; }
        return SourceBindings(a.sources, *move, state.body.kind, false);
    }
    if (!move || move->execution != MoveExecution::Strikes || a.abilitySource != AbilityGrantKey{}
        || a.abilityTarget != AbilityTarget{} || a.trainingArt != style->art || a.trainingWork > move->training.work
        || (a.kind != ActorActionKind::MeleeWindup && a.kind != ActorActionKind::Recovery)
        || (a.kind == ActorActionKind::MeleeWindup && !move->cost.windupCost) || !a.target
        || move->trigger != (a.counter ? CombatTrigger::CounterOpening : CombatTrigger::Attack)
        || eastl::none_of(style->moves.begin(), style->moves.end(), [&a](const WeightedCombatMove& entry) { return entry.move == a.moveId; })
        || a.weapon != a.sources.hands[static_cast<unsigned>(a.sources.primaryHand)].item) { return false; }
    // Explicit v1/v2 adapter: old heavy attacks could be committed unarmed.
    // Preserve that already-paid binding; do not replace it with a held weapon.
    const bool legacyUnarmedHeavy = state.legacyAccuracyOverride && a.move == static_cast<unsigned>(CombatMove::HeavyAttack)
        && a.moveId == c.FindCombatMove("basic_heavy") && !a.weapon;
    return SourceBindings(a.sources, *move, state.body.kind, legacyUnarmedHeavy);
}
}

DataObject CaptureEquipment(const World& world, const Actor& actor) {
    DataObject d; d.SetUnsigned("version", 1); DataObject hands = DataObject::List();
    for (const auto& h : actor.equipment.hands) {
        DataObject v; const auto* item = world.GetItem(h.item);
        if (h.item.value && !item) { std::abort(); }
        v.SetUnsigned("item", item ? item->id : 0); v.SetUnsigned("grip", static_cast<unsigned>(h.grip));
        hands.Append(eastl::move(v));
    }
    d.Set("hands", eastl::move(hands)); DataObject worn = DataObject::List();
    for (auto handle : actor.equipment.worn) {
        const auto* item = world.GetItem(handle); if (!item) { std::abort(); }
        worn.Append(DataObject::Unsigned(item->id));
    }
    d.Set("worn", eastl::move(worn)); if (!ReadEquipment(d)) { std::abort(); } return d;
}
StateResult<SavedEquipment> ReadEquipment(const DataObject& d, Diagnostics* diagnostics) {
    SavedEquipment s;
    if (d.Type() == DataType::Object && d.Size() == 0) { return s; }
    const auto version = d.GetUnsigned("version");
    if (version && *version != 1) { return Invalid(diagnostics, "Unsupported equipment version", StateError::UnsupportedVersion); }
    const auto hands = d.Get("hands", DataType::List); const auto worn = d.Get("worn", DataType::List);
    if (!version || !Fields(d, {"version", "hands", "worn"}) || !hands || (*hands)->Size() != 2 || !worn) { return Invalid(diagnostics, "Invalid equipment"); }
    for (unsigned i = 0; i < 2; ++i) {
        const auto& v = (*hands)->At(i); auto& h = s.hands[i];
        if (!Fields(v, {"item", "grip"}) || !Unsigned(v, "item", h.item) || !Unsigned(v, "grip", h.grip, 1)
            || (!h.item && h.grip != GripKind::OneHanded)) { return Invalid(diagnostics, "Invalid held item"); }
    }
    const auto& l = s.hands[0]; const auto& r = s.hands[1];
    if ((l.item && l.item == r.item) ? l.grip != GripKind::TwoHanded || r.grip != GripKind::TwoHanded
        : l.grip == GripKind::TwoHanded || r.grip == GripKind::TwoHanded) { return Invalid(diagnostics, "Invalid two-handed occupancy"); }
    for (std::size_t i = 0; i < (*worn)->Size(); ++i) {
        const auto& v = (*worn)->At(i);
        if (v.Type() != DataType::Unsigned || !v.UnsignedValue() || v.UnsignedValue() == l.item || v.UnsignedValue() == r.item
            || eastl::find(s.worn.begin(), s.worn.end(), v.UnsignedValue()) != s.worn.end()) { return Invalid(diagnostics, "Invalid worn item"); }
        s.worn.push_back(v.UnsignedValue());
    }
    return s;
}

DataObject CaptureCharacterCombat(const CharacterCombatState* s, const ContentCatalog& c) {
    DataObject d; if (!s) { return d; } d.SetUnsigned("version", 1);
    DataObject arts = DataObject::List(), grants = DataObject::List(), cooldowns = DataObject::List(), manuals = DataObject::List();
    for (const auto& a : s->arts) { DataObject v; v.SetString("art", c.GetCombatArt(a.art).id); v.SetUnsigned("xp", a.xp); arts.Append(eastl::move(v)); }
    for (const auto& g : s->abilityGrants) { DataObject v; v.SetString("ability", c.GetAbility(g.ability).id); v.Set("source", Grant(g.source, c)); grants.Append(eastl::move(v)); }
    for (const auto& g : s->abilityCooldowns) { DataObject v; v.SetString("ability", c.GetAbility(g.ability).id); v.Set("source", Grant(g.source, c)); v.SetUnsigned("remaining", g.remaining); cooldowns.Append(eastl::move(v)); }
    for (const auto& m : s->manuals) {
        DataObject v; v.SetString("book", c.GetBook(m.book).id); v.SetString("art", c.GetCombatArt(m.art).id);
        v.SetUnsigned("remaining", m.remaining); v.SetUnsigned("totalDuration", m.totalDuration);
        v.SetUnsigned("successChance", m.successChance); v.SetBoolean("active", m.active);
        v.SetUnsigned("fundedTicks", m.fundedTicks); v.SetBoolean("pauseRequested", m.pauseRequested);
        manuals.Append(eastl::move(v));
    }
    d.Set("arts", eastl::move(arts)); d.Set("grants", eastl::move(grants)); d.Set("cooldowns", eastl::move(cooldowns)); d.Set("manuals", eastl::move(manuals));
    if (!ReadCharacterCombat(d, c)) { std::abort(); } return d;
}
StateResult<eastl::unique_ptr<CharacterCombatState>> ReadCharacterCombat(const DataObject& d, const ContentCatalog& c, Diagnostics* diagnostics) {
    if (d.Type() == DataType::Object && d.Size() == 0) { return eastl::unique_ptr<CharacterCombatState>{}; }
    const auto version = d.GetUnsigned("version");
    if (version && *version != 1) { return Invalid(diagnostics, "Unsupported character combat version", StateError::UnsupportedVersion); }
    if (!version || !Fields(d, {"version", "arts", "grants", "cooldowns", "manuals"})) { return Invalid(diagnostics, "Invalid character combat"); }
    for (auto name : {"arts", "grants", "cooldowns", "manuals"}) { if (!d.Get(name, DataType::List)) { return Invalid(diagnostics, "Invalid character combat list"); } }
    auto s = eastl::make_unique<CharacterCombatState>();
    const auto& arts = *d.Find("arts");
    for (std::size_t i = 0; i < arts.Size(); ++i) {
        const auto& v = arts.At(i); ArtProgress a;
        if (!Fields(v, {"art", "xp"}) || !Definition(v, "art", a.art, [&c](auto n) { return c.FindCombatArt(n); })
            || !a.art.value || !Unsigned(v, "xp", a.xp)
            || eastl::any_of(s->arts.begin(), s->arts.end(), [&a](const auto& old) { return old.art == a.art; })) { return Invalid(diagnostics, "Invalid art progress"); }
        s->arts.push_back(a);
    }
    for (auto name : {"grants", "cooldowns"}) {
        const bool cooldown = eastl::string_view(name) == "cooldowns"; const auto& list = *d.Find(name);
        for (std::size_t i = 0; i < list.Size(); ++i) {
            const auto& v = list.At(i); AbilityCooldown g;
            if (!(cooldown ? Fields(v, {"ability", "source", "remaining"}) : Fields(v, {"ability", "source"}))
                || !Definition(v, "ability", g.ability, [&c](auto n) { return c.FindAbility(n); }) || !g.ability.value
                || !ReadGrant(*v.Find("source"), g.source, c) || !GrantReference(c, g.ability, g.source)
                || (cooldown && (!Unsigned(v, "remaining", g.remaining) || !g.remaining))) { return Invalid(diagnostics, "Invalid ability grant/cooldown"); }
            if (cooldown) {
                if (eastl::any_of(s->abilityCooldowns.begin(), s->abilityCooldowns.end(), [&g](const auto& old) { return old.ability == g.ability && old.source == g.source; })) { return Invalid(diagnostics, "Duplicate ability cooldown"); }
                s->abilityCooldowns.push_back(g);
            } else {
                if (g.source.kind == AbilityGrantSourceKind::Art && eastl::none_of(s->arts.begin(), s->arts.end(),
                    [&g](const ArtProgress& progress) { return progress.art == g.source.art; })) { return Invalid(diagnostics, "Ability grant art is not learned"); }
                if (eastl::any_of(s->abilityGrants.begin(), s->abilityGrants.end(), [&g](const auto& old) { return old.ability == g.ability && old.source == g.source; })) { return Invalid(diagnostics, "Duplicate ability grant"); }
                s->abilityGrants.push_back({g.ability, g.source});
            }
        }
    }
    const auto& manuals = *d.Find("manuals"); bool active = false;
    for (std::size_t i = 0; i < manuals.Size(); ++i) {
        const auto& v = manuals.At(i); ManualAttemptProgress m;
        if (!Fields(v, {"book", "art", "remaining", "totalDuration", "successChance", "active", "fundedTicks", "pauseRequested"})
            || !Definition(v, "book", m.book, [&c](auto n) { return c.FindBook(n); }) || !m.book.value
            || !Definition(v, "art", m.art, [&c](auto n) { return c.FindCombatArt(n); }) || !m.art.value
            || !Unsigned(v, "remaining", m.remaining) || !Unsigned(v, "totalDuration", m.totalDuration)
            || !m.totalDuration || m.remaining > m.totalDuration
            || !Unsigned(v, "fundedTicks", m.fundedTicks) || m.fundedTicks > m.remaining
            || m.fundedTicks > 100
            || !Boolean(v, "pauseRequested", m.pauseRequested)
            || !Boolean(v, "active", m.active)
            || (!m.active && (m.fundedTicks || m.pauseRequested))
            || !Unsigned(v, "successChance", m.successChance, ratioScale)
            || (active && m.active) || eastl::any_of(s->manuals.begin(), s->manuals.end(), [&m](const auto& old) { return old.book == m.book && old.art == m.art; })) { return Invalid(diagnostics, "Invalid manual attempt"); }
        active |= m.active; s->manuals.push_back(m);
        const auto& book = c.GetBook(m.book);
        if (eastl::none_of(book.artManuals.begin(), book.artManuals.end(), [&m](const ArtManualDefinition& manual) { return manual.art == m.art; })) {
            return Invalid(diagnostics, "Book does not teach the saved manual art");
        }
    }
    return s;
}

bool ValidateCombatState(const CombatState& s, const ContentCatalog& c) {
    if (!ValidateCombatState(LegacyPart(s)) || !ValidateAction(s.action) || !ValidateReachProfile(s.reach)
        || static_cast<unsigned>(s.life) > 1 || static_cast<unsigned>(s.exertion) > 3
        || s.offensiveTrainingRank > 10 || s.defensiveTrainingRank > 10 || s.staminaRegenerationRemainder >= ratioScale
        || (s.stamina == 100 && s.staminaRegenerationRemainder) || s.intrinsicMissOverride > ratioScale
        || (s.styleId.value && !c.TryGetCombatStyle(s.styleId))
        || (s.life == ActorLifeState::Alive && (Terminal(s) || s.corpse))
        || (s.life == ActorLifeState::Dead && (!Terminal(s) || s.action.kind != ActorActionKind::None))) { return false; }
    const auto& a = s.action;
    if (!a.definitionDriven) {
        const ActorActionState projection{a.kind, a.target, a.move, a.recoveryTicks, a.accuracyPenalty, a.weapon};
        if (projection != a) { return false; }
    } else if (!s.styleId.value || !a.committedStyle.value
        || (a.kind == ActorActionKind::MeleeWindup && (!a.target || !a.moveId.value || a.ability.value))) { return false; }
    if (static_cast<unsigned>(a.abilityTarget.kind) > 3 || static_cast<unsigned>(a.abilityTarget.direction) > 3
        || !ValidPosition(a.abilityTarget.tile)) { return false; }
    SourceCommitment checkedSources;
    if (!ReadSources(Sources(a.sources), checkedSources)) { return false; }
    if (a.ability.value) {
        switch (a.abilitySource.kind) {
        case AbilityGrantSourceKind::Art: if (!c.TryGetCombatArt(a.abilitySource.art)) { return false; } break;
        case AbilityGrantSourceKind::Innate: if (a.abilitySource.art.value) { return false; } break;
        default: return false;
        }
    }
    if ((a.moveId.value && !c.TryGetCombatMove(a.moveId)) || (a.committedStyle.value && !c.TryGetCombatStyle(a.committedStyle))
        || (a.trainingArt.value && !c.TryGetCombatArt(a.trainingArt)) || (a.ability.value && !c.TryGetAbility(a.ability))) { return false; }
    if (a.ability.value && c.GetAbility(a.ability).targeting != a.abilityTarget.kind) { return false; }
    if (!ActionReferences(s, c)) { return false; }
    for (std::size_t i = 0; i < s.moveCooldowns.size(); ++i) {
        const auto& cd = s.moveCooldowns[i]; if (!cd.remaining || !c.TryGetCombatMove(cd.move)) { return false; }
        for (std::size_t j = 0; j < i; ++j) { if (s.moveCooldowns[j].move == cd.move) { return false; } }
    }
    for (std::size_t i = 0; i < s.openings.size(); ++i) {
        if (!s.openings[i].opponent || !s.openings[i].remaining) { return false; }
        for (std::size_t j = 0; j < i; ++j) { if (s.openings[j].opponent == s.openings[i].opponent) { return false; } }
    }
    return true;
}

DataObject CaptureCombatState(const CombatState& s, const ContentCatalog& c) {
    if (!s.styleId.value) {
        if (s != LegacyPart(s)) { std::abort(); }
        auto migrated = s; if (!Migrate(migrated, c)) { std::abort(); } return CaptureCombatState(migrated, c);
    }
    if (!ValidateCombatState(s, c)) { std::abort(); }
    DataObject d; d.SetUnsigned("version", 3); d.Set("legacy", CaptureCombatState(LegacyPart(s)));
    d.SetString("style", c.GetCombatStyle(s.styleId).id);
    d.SetUnsigned("life", static_cast<unsigned>(s.life)); d.SetUnsigned("corpse", s.corpse);
    d.SetBoolean("inexhaustible", s.inexhaustible); d.SetBoolean("authoredTrainingRanks", s.authoredTrainingRanks);
    d.SetUnsigned("offensiveTrainingRank", s.offensiveTrainingRank); d.SetUnsigned("defensiveTrainingRank", s.defensiveTrainingRank);
    d.SetSigned("attackRating100", s.attackRating100); d.SetSigned("defenseRating100", s.defenseRating100);
    DataObject reach; reach.SetUnsigned("distance", s.reach.distance); reach.SetUnsigned("height", static_cast<unsigned>(s.reach.height)); reach.SetUnsigned("regions", s.reach.regions); d.Set("reach", eastl::move(reach));
    d.SetUnsigned("staggerRemaining", s.staggerRemaining); d.SetUnsigned("focusRemaining", s.focusRemaining); d.SetSigned("focusRatingBonus100", s.focusRatingBonus100);
    d.SetUnsigned("combatLockRemaining", s.combatLockRemaining); d.SetUnsigned("exertion", static_cast<unsigned>(s.exertion));
    d.SetUnsigned("staminaRegenerationRemainder", s.staminaRegenerationRemainder);
    d.SetBoolean("legacyAccuracyOverride", s.legacyAccuracyOverride); d.SetUnsigned("intrinsicMissOverride", s.intrinsicMissOverride);
    DataObject cds = DataObject::List(), openings = DataObject::List();
    for (const auto& cd : s.moveCooldowns) { DataObject v; v.SetString("move", c.GetCombatMove(cd.move).id); v.SetUnsigned("remaining", cd.remaining); cds.Append(eastl::move(v)); }
    for (const auto& o : s.openings) { DataObject v; v.SetUnsigned("opponent", o.opponent); v.SetUnsigned("remaining", o.remaining); openings.Append(eastl::move(v)); }
    d.Set("moveCooldowns", eastl::move(cds)); d.Set("openings", eastl::move(openings));
    const auto& a = s.action; DataObject action;
    action.SetUnsigned("kind", static_cast<unsigned>(a.kind)); action.SetUnsigned("target", a.target);
    action.SetUnsigned("move", a.move); action.SetUnsigned("weapon", a.weapon); action.SetUnsigned("accuracyPenalty", a.accuracyPenalty); action.SetUnsigned("recoveryTicks", a.recoveryTicks);
    action.SetString("moveId", a.moveId.value ? c.GetCombatMove(a.moveId).id : String{});
    action.SetString("committedStyle", a.committedStyle.value ? c.GetCombatStyle(a.committedStyle).id : String{});
    action.SetString("trainingArt", a.trainingArt.value ? c.GetCombatArt(a.trainingArt).id : String{});
    action.SetString("ability", a.ability.value ? c.GetAbility(a.ability).id : String{});
    action.Set("abilitySource", Grant(a.abilitySource, c)); action.Set("sources", Sources(a.sources));
    DataObject target; target.SetUnsigned("kind", static_cast<unsigned>(a.abilityTarget.kind)); target.SetUnsigned("actor", a.abilityTarget.actor);
    target.SetSigned("x", a.abilityTarget.tile.x); target.SetSigned("z", a.abilityTarget.tile.z); target.SetSigned("y", a.abilityTarget.tile.y);
    target.SetUnsigned("direction", static_cast<unsigned>(a.abilityTarget.direction)); action.Set("abilityTarget", eastl::move(target));
    action.SetSigned("ratingPenalty100", a.ratingPenalty100); action.SetUnsigned("trainingWork", a.trainingWork);
    action.SetBoolean("counter", a.counter); action.SetBoolean("definitionDriven", a.definitionDriven); d.Set("action", eastl::move(action)); return d;
}

StateResult<CombatState> ReadCombatState(const DataObject& d, const ContentCatalog& c, Diagnostics* diagnostics) {
    if (d.Type() == DataType::Object && d.Size() == 0) { auto s = MakeCombatState(false); if (!Migrate(s, c)) { return Invalid(diagnostics, "Missing baseline combat definition"); } return s; }
    const auto version = d.GetUnsigned("version");
    if (!version) { return Invalid(diagnostics, "Missing combat version"); }
    if (*version == 1 || *version == 2) {
        auto s = ReadCombatState(d, diagnostics); if (!s) { return s; }
        if (!Migrate(*s, c) || !ValidateCombatState(*s, c)) { return Invalid(diagnostics, "Invalid legacy combat migration or missing definition"); } return s;
    }
    if (*version != 3) { return Invalid(diagnostics, "Unsupported combat version", StateError::UnsupportedVersion); }
    if (!Fields(d, {"version", "legacy", "style", "life", "corpse", "inexhaustible", "authoredTrainingRanks", "offensiveTrainingRank", "defensiveTrainingRank", "attackRating100", "defenseRating100", "reach", "staggerRemaining", "focusRemaining", "focusRatingBonus100", "combatLockRemaining", "exertion", "staminaRegenerationRemainder", "legacyAccuracyOverride", "intrinsicMissOverride", "moveCooldowns", "openings", "action"})) { return Invalid(diagnostics, "Invalid combat envelope"); }
    const auto& legacyData = *d.Find("legacy");
    const auto legacyVersion = legacyData.GetUnsigned("version");
    if (!legacyVersion || *legacyVersion != 2) { return Invalid(diagnostics, "Invalid combat legacy projection"); }
    auto result = ReadCombatState(legacyData, diagnostics); if (!result) { return result; }
    const auto legacy = *result; auto& s = *result;
    if (!Definition(d, "style", s.styleId, [&c](auto n) { return c.FindCombatStyle(n); }) || !s.styleId.value
        || !Unsigned(d, "life", s.life, 1) || !Unsigned(d, "corpse", s.corpse)
        || !Boolean(d, "inexhaustible", s.inexhaustible) || !Boolean(d, "authoredTrainingRanks", s.authoredTrainingRanks)
        || !Unsigned(d, "offensiveTrainingRank", s.offensiveTrainingRank, 10) || !Unsigned(d, "defensiveTrainingRank", s.defensiveTrainingRank, 10)
        || !Signed(d, "attackRating100", s.attackRating100) || !Signed(d, "defenseRating100", s.defenseRating100)
        || !Unsigned(d, "staggerRemaining", s.staggerRemaining) || !Unsigned(d, "focusRemaining", s.focusRemaining)
        || !Signed(d, "focusRatingBonus100", s.focusRatingBonus100) || !Unsigned(d, "combatLockRemaining", s.combatLockRemaining)
        || !Unsigned(d, "exertion", s.exertion, 3) || !Unsigned(d, "staminaRegenerationRemainder", s.staminaRegenerationRemainder, ratioScale - 1)
        || !Boolean(d, "legacyAccuracyOverride", s.legacyAccuracyOverride) || !Unsigned(d, "intrinsicMissOverride", s.intrinsicMissOverride, ratioScale)) { return Invalid(diagnostics, "Invalid combat fields"); }
    const auto& reach = *d.Find("reach");
    if (!Fields(reach, {"distance", "height", "regions"}) || !Unsigned(reach, "distance", s.reach.distance, 2)
        || !Unsigned(reach, "height", s.reach.height, 2) || !Unsigned(reach, "regions", s.reach.regions, allContactMask)) { return Invalid(diagnostics, "Invalid combat reach"); }
    for (auto name : {"moveCooldowns", "openings"}) {
        const auto list = d.Get(name, DataType::List); if (!list) { return Invalid(diagnostics, "Invalid combat list"); }
        for (std::size_t i = 0; i < (*list)->Size(); ++i) {
            const auto& v = (*list)->At(i);
            if (eastl::string_view(name) == "moveCooldowns") {
                MoveCooldown cd; if (!Fields(v, {"move", "remaining"}) || !Definition(v, "move", cd.move, [&c](auto n) { return c.FindCombatMove(n); }) || !Unsigned(v, "remaining", cd.remaining)) { return Invalid(diagnostics, "Invalid move cooldown"); } s.moveCooldowns.push_back(cd);
            } else { CounterOpening o; if (!Fields(v, {"opponent", "remaining"}) || !Unsigned(v, "opponent", o.opponent) || !Unsigned(v, "remaining", o.remaining)) { return Invalid(diagnostics, "Invalid opening"); } s.openings.push_back(o); }
        }
    }
    const auto& v = *d.Find("action"); auto& a = s.action;
    if (!Fields(v, {"kind", "target", "move", "weapon", "accuracyPenalty", "recoveryTicks", "moveId", "committedStyle", "trainingArt", "ability", "abilitySource", "sources", "abilityTarget", "ratingPenalty100", "trainingWork", "counter", "definitionDriven"})
        || !Unsigned(v, "kind", a.kind, static_cast<unsigned>(ActorActionKind::AbilityWindup)) || !Unsigned(v, "target", a.target)
        || !Unsigned(v, "move", a.move, static_cast<unsigned>(CombatMove::Parry)) || !Unsigned(v, "weapon", a.weapon)
        || !Unsigned(v, "accuracyPenalty", a.accuracyPenalty, 30) || !Unsigned(v, "recoveryTicks", a.recoveryTicks)
        || !Definition(v, "moveId", a.moveId, [&c](auto n) { return c.FindCombatMove(n); })
        || !Definition(v, "committedStyle", a.committedStyle, [&c](auto n) { return c.FindCombatStyle(n); })
        || !Definition(v, "trainingArt", a.trainingArt, [&c](auto n) { return c.FindCombatArt(n); })
        || !Definition(v, "ability", a.ability, [&c](auto n) { return c.FindAbility(n); })
        || !ReadSources(*v.Find("sources"), a.sources) || !Signed(v, "ratingPenalty100", a.ratingPenalty100)
        || !Unsigned(v, "trainingWork", a.trainingWork) || !Boolean(v, "counter", a.counter) || !Boolean(v, "definitionDriven", a.definitionDriven)) { return Invalid(diagnostics, "Invalid combat action"); }
    // An absent ability has the default, intentionally unbound grant key.
    const auto& source = *v.Find("abilitySource");
    if (a.ability.value ? !ReadGrant(source, a.abilitySource, c)
        : !Fields(source, {"kind", "art"}) || !Unsigned(source, "kind", a.abilitySource.kind, 1)
            || !Definition(source, "art", a.abilitySource.art, [&c](auto n) { return c.FindCombatArt(n); })) { return Invalid(diagnostics, "Invalid action ability source"); }
    const auto& t = *v.Find("abilityTarget"); auto& target = a.abilityTarget;
    if (!Fields(t, {"kind", "actor", "x", "z", "y", "direction"}) || !Unsigned(t, "kind", target.kind, 3)
        || !Unsigned(t, "actor", target.actor) || !Signed(t, "x", target.tile.x) || !Signed(t, "z", target.tile.z)
        || !Signed(t, "y", target.tile.y) || !Unsigned(t, "direction", target.direction, 3)
        || LegacyPart(s) != legacy || !ValidateCombatState(s, c)) { return Invalid(diagnostics, "Invalid combat target or state"); }
    return result;
}
}
