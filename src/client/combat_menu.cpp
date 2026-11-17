#include "client/combat_menu.h"
#include "client/combat_text.h"
#include "client/localization.h"

namespace cataclysmx {
namespace {
String CostText(const ActionCost& cost) {
    return "windup " + UnsignedText(cost.windupCost) + " + recovery "
        + UnsignedText(cost.recoveryCost) + " base ticks; stamina " + UnsignedText(cost.staminaCost);
}
String Percent(std::uint32_t ratio) {
    String result = UnsignedText(ratio / (ratioScale / 100));
    const auto fraction = ratio % (ratioScale / 100);
    if (fraction) {
        result += '.';
        if (fraction < 10) { result += '0'; }
        result += UnsignedText(fraction);
    }
    return result + "%";
}
bool KnowsArt(const WorldView& view, CombatArtId id) {
    if (!id.value) { return true; }
    for (const auto& art : view.combatInfo.arts) { if (art.art == id) { return true; } }
    return false;
}
const char* TargetName(AbilityTargetKind kind) {
    switch (kind) {
    case AbilityTargetKind::Self: return "self";
    case AbilityTargetKind::Actor: return "actor";
    case AbilityTargetKind::Tile: return "tile";
    case AbilityTargetKind::CardinalDirection: return "cardinal direction";
    }
    return "unknown";
}
void Availability(CombatMenuEntry& row, CombatRefusal reason) {
    row.enabled = reason == CombatRefusal::None;
    row.details += " | " + CombatRefusalText(reason);
}
CombatRefusal Readiness(const WorldView& view) {
    if (view.combatInfo.combat.life == ActorLifeState::Dead) { return CombatRefusal::Dead; }
    if (!view.characterId) { return CombatRefusal::InvalidActor; }
    if (view.remainingTicks || view.commandQueued) { return CombatRefusal::Busy; }
    return CombatRefusal::None;
}
}

String CombatRefusalText(CombatRefusal reason) {
    switch (reason) {
    case CombatRefusal::None: return "ready";
    case CombatRefusal::InvalidActor: return "character unavailable";
    case CombatRefusal::Dead: return "character is dead";
    case CombatRefusal::Inactive: return "character is inactive";
    case CombatRefusal::Busy: return "not ready; action or queued command pending";
    case CombatRefusal::NoTarget: return "no target";
    case CombatRefusal::NotHostile: return "target is not hostile";
    case CombatRefusal::Unperceived: return "target is not perceived";
    case CombatRefusal::OutOfReach: return "out of reach";
    case CombatRefusal::Obstructed: return "path obstructed";
    case CombatRefusal::NoMove: return "no eligible combat move";
    case CombatRefusal::Locked: return "learned skill or art XP requirement not met";
    case CombatRefusal::MissingSource: return "required weapon or attack source missing";
    case CombatRefusal::InvalidGrip: return "unsupported grip";
    case CombatRefusal::MissingCapability: return "required limb or capability unavailable";
    case CombatRefusal::NeedsFreeHand: return "requires a free hand";
    case CombatRefusal::Exhausted: return "insufficient stamina";
    case CombatRefusal::Cooldown: return "cooldown pending";
    case CombatRefusal::Staggered: return "staggered";
    case CombatRefusal::Unaware: return "requires awareness";
    case CombatRefusal::MissingOpening: return "requires a counter opening";
    case CombatRefusal::InvalidTarget: return "invalid target";
    case CombatRefusal::MissingGrant: return "ability source or learned art unavailable";
    case CombatRefusal::MissingReference: return "accessible manual required";
    case CombatRefusal::AlreadyKnown: return "art already learned";
    case CombatRefusal::InvalidDefinition: return "invalid combat definition";
    }
    return "unknown combat refusal";
}

void CollectCombatMenu(const ContentCatalog& content, const WorldView& view,
    Vector<CombatMenuEntry>& rows) {
    rows.clear();
    if (view.combatInfo.combat.life == ActorLifeState::Dead) {
        CombatMenuEntry row;
        row.label = "Create new character";
        row.details = "Start a fresh character; the dead character's belongings remain in the world.";
        row.command.kind = CommandKind::new_character;
        rows.push_back(eastl::move(row));
        return;
    }
    const auto readiness = Readiness(view);
    for (std::uint32_t i = 1; i <= content.CombatStyleCount(); ++i) {
        const CombatStyleId id{i};
        const auto& style = content.GetCombatStyle(id);
        if (!KnowsArt(view, style.art)) { continue; }
        CombatMenuEntry row;
        row.label = "Style: " + style.name;
        if (id == view.combatInfo.combat.styleId) { row.label += " [selected]"; }
        row.command.kind = CommandKind::select_combat_style; row.command.combatStyle = id;
        row.details = "Switch cost " + UnsignedText(style.switchCost)
            + " base ticks. Stance changes move/defense selection weights, not a flat defense bonus.";
        for (const auto& weighted : style.moves) {
            const auto* move = content.TryGetCombatMove(weighted.move);
            if (!move) { continue; }
            row.details += "\n" + move->name + " weight " + UnsignedText(weighted.weight)
                + (move->trigger == CombatTrigger::IncomingStrike ? " [defense]"
                    : move->trigger == CombatTrigger::CounterOpening ? " [counter]" : " [attack]")
                + " | " + CostText(move->cost);
            for (const auto& available : view.combatInfo.moves) {
                if (available.move != weighted.move) { continue; }
                row.details += " | " + CombatRefusalText(available.availability.reason)
                    + " | cooldown " + UnsignedText(available.cooldownRemaining) + " ticks";
                break;
            }
        }
        Availability(row, readiness);
        rows.push_back(eastl::move(row));
    }
    for (const auto& ability : view.combatInfo.abilities) {
        const auto* definition = content.TryGetAbility(ability.ability);
        if (!definition) { continue; }
        CombatMenuEntry row;
        row.label = "Ability: " + definition->name;
        row.command.kind = CommandKind::use_ability;
        row.command.ability.ability = ability.ability; row.command.ability.source = ability.source;
        row.targeting = ability.targeting; row.command.ability.target.kind = ability.targeting;
        const auto* art = content.TryGetCombatArt(ability.source.art);
        row.label += ability.source.kind == AbilityGrantSourceKind::Innate ? " [innate]"
            : " [" + (art ? art->name : String("unknown art")) + "]";
        row.details = "Authored " + CostText(definition->cost) + " | target: " + TargetName(row.targeting)
            + " | cooldown " + UnsignedText(ability.cooldownRemaining) + "/"
            + UnsignedText(definition->cooldown) + " ticks";
        if (ability.availability.Available()) { row.details += " | current " + CostText(ability.availability.cost); }
        Availability(row, readiness != CombatRefusal::None ? readiness : ability.availability.reason);
        rows.push_back(eastl::move(row));
    }
    bool activeManual = false;
    for (const auto& manual : view.combatInfo.manuals) {
        const auto* art = content.TryGetCombatArt(manual.art);
        const auto* book = content.TryGetBook(manual.book);
        CombatMenuEntry row;
        row.label = String(manual.active ? "Studying " : manual.remaining ? "Resume art study: " : "Study art: ")
            + (art ? art->name : String("unknown art")) + " / " + (book ? book->name : String("unknown manual"));
        row.command.kind = CommandKind::study_art; row.command.book = manual.book; row.command.art = manual.art;
        row.details = "Success chance " + Percent(manual.successChance) + " per completed attempt | duration "
            + UnsignedText(manual.duration) + " ticks | remaining " + UnsignedText(manual.remaining)
            + " ticks. Failed attempts may be retried; paused attempts retain progress.";
        if (book) {
            for (const auto& definition : book->artManuals) {
                if (definition.art != manual.art) { continue; }
                const auto* skill = content.TryGetSkill(definition.prerequisite.skill);
                row.details += " | requires learned " + (skill ? skill->name : String("unknown skill"))
                    + " " + UnsignedText(definition.prerequisite.level);
            }
        }
        Availability(row, manual.active ? CombatRefusal::Busy
            : readiness != CombatRefusal::None ? readiness : manual.reason);
        activeManual |= manual.active;
        rows.push_back(eastl::move(row));
    }
    if (activeManual) {
        CombatMenuEntry row;
        row.label = "Pause art study"; row.details = "Request pause at the paid study slice boundary; retain attempt progress.";
        row.command.kind = CommandKind::pause_art_study;
        rows.push_back(eastl::move(row));
    }
}

String CombatDetailsText(const ContentCatalog& content, const WorldView& view) {
    const auto& info = view.combatInfo;
    String text = "Character #" + UnsignedText(view.characterId)
        + (info.combat.life == ActorLifeState::Dead ? " [dead]" : " [alive]");
    text += "\n" + CombatStatusText(view, content);
    text += "\nSpeed " + UnsignedText(view.speed) + "; remaining action " + UnsignedText(view.remainingTicks)
        + " ticks. Action ticks = ceil(base cost x speed/100); lower speed values mean shorter actions.";
    text += "\nStamina regeneration: rest/work 0.1 per tick, moving 0.05; none during combat lock/recovery."
        "\nAttack rating = 0.4 x effective melee + 0.6 x effective weapon-family skill + bonuses - penalties."
        "\nDodge rating = effective dodge + bonuses - penalties. Defense chance: dodge/parry 40%, block 60%,"
        " +50 percentage points per defender-attacker rating level; clamped 5%-90%, after intrinsic miss."
        "\nStance weights eligible defenses; block absorbs capacity and can still allow damage."
        "\nEffective skill discounts stamina by 2%/level (max 20%, cost rounded up) and increases block capacity by 2%/level."
        "\nLearned levels unlock moves and determine training difficulty; effective levels drive execution."
        "\nTraining: equal-or-harder opponent 100%, one rank below 50%, two below 10%, three below no XP."
        " No-opponent abilities grant no combat XP; event feedback reports suppressed training.";
    for (const auto& skill : view.learning.skills) {
        const auto* definition = content.TryGetSkill(skill.progress.skill);
        if (!definition || definition->category != SkillCategory::Combat) { continue; }
        text += "\n" + definition->name + ": learned " + UnsignedText(skill.progress.level)
            + " / effective " + NumberText(skill.effectiveLevel);
    }
    for (std::size_t hand = 0; hand < info.equipment.hands.size(); ++hand) {
        const auto& held = info.equipment.hands[hand];
        text += hand == 0 ? "\nL: " : "\nR: ";
        if (!held.item) { text += "empty"; continue; }
        text += "#" + UnsignedText(held.item) + (held.grip == GripKind::TwoHanded ? " [2H]" : " [1H]")
            + (held.usable ? " [usable]" : " [unusable]");
        for (const auto& item : view.inventory) {
            if (item.id != held.item) { continue; }
            const auto& definition = content.GetItem(item.type);
            text += " " + definition.name;
            const auto authoredFamily = ItemMelee(definition).handlingSkill;
            const auto family = authoredFamily.value ? authoredFamily : content.GetCombatSkills().melee;
            const auto* skill = content.TryGetSkill(family);
            text += " | weapon family: " + (skill ? skill->name : String("improvised fallback"));
            for (const auto& progress : view.learning.skills) {
                if (progress.progress.skill != family) { continue; }
                text += " learned " + UnsignedText(progress.progress.level)
                    + " / effective " + NumberText(progress.effectiveLevel);
                break;
            }
            break;
        }
    }
    for (const auto& art : info.arts) {
        const auto* definition = content.TryGetCombatArt(art.art);
        text += "\nArt: " + (definition ? definition->name : String("unknown"))
            + " | XP " + UnsignedText(art.xp) + "/" + UnsignedText(ratioScale) + " (fixed-point)";
    }
    text += "\nRegional encumbrance (head/face and feet are independent):";
    for (std::size_t i = 0; i < contactRegionCount; ++i) {
        if (!HasContact(info.combat.bodyKind, static_cast<ContactRegion>(i))) { continue; }
        text += "\n" + String(CombatRegionName(static_cast<ContactRegion>(i))) + ": "
            + UnsignedText(info.equipment.encumbrance[i]);
    }
    text += "\nWeapon damage shown in item inspection is authored base damage; actual contact depends on move, grip, defense, armor and injury.";
    return text;
}
}
