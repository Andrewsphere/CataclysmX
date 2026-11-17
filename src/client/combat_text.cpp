#include "client/combat_text.h"
#include "client/localization.h"
#include "client/combat_menu.h"

namespace cataclysmx {
namespace {
const char* PartName(BodyPart part) {
    switch (part) {
    case BodyPart::Head: return "head";
    case BodyPart::Torso: return "torso";
    case BodyPart::LeftHand: return "left hand";
    case BodyPart::RightHand: return "right hand";
    case BodyPart::LeftLeg: return "left leg";
    case BodyPart::RightLeg: return "right leg";
    case BodyPart::AttackApparatus: return "attack apparatus";
    case BodyPart::Locomotion: return "locomotion";
    case BodyPart::Core: return "core";
    case BodyPart::Count: return "";
    }
    return "";
}
const char* ActionName(ActorActionKind action) {
    switch (action) {
    case ActorActionKind::None: return "ready";
    case ActorActionKind::Recovery: return "recovering";
    case ActorActionKind::Aiming: return "aiming";
    case ActorActionKind::MeleeWindup: return "windup";
    case ActorActionKind::DisruptionRecovery: return "disrupted";
    case ActorActionKind::AbilityWindup: return "ability windup";
    }
    return "unknown";
}
void Hp(String& text, std::uint64_t value) {
    text += UnsignedText(value / 100);
    text += '.'; text += static_cast<char>('0' + (value / 10) % 10);
    text += static_cast<char>('0' + value % 10);
}
}

const char* CombatRegionName(ContactRegion region) {
    switch (region) {
    case ContactRegion::Head: return "head";
    case ContactRegion::Eyes: return "eyes";
    case ContactRegion::Mouth: return "mouth";
    case ContactRegion::Ears: return "ears";
    case ContactRegion::Torso: return "torso";
    case ContactRegion::LeftArm: return "left arm";
    case ContactRegion::RightArm: return "right arm";
    case ContactRegion::LeftHand: return "left hand";
    case ContactRegion::RightHand: return "right hand";
    case ContactRegion::LeftLeg: return "left leg";
    case ContactRegion::RightLeg: return "right leg";
    case ContactRegion::LeftFoot: return "left foot";
    case ContactRegion::RightFoot: return "right foot";
    case ContactRegion::AttackApparatus: return "attack apparatus";
    case ContactRegion::Locomotion: return "locomotion";
    case ContactRegion::Core: return "core";
    case ContactRegion::Count: return "unknown region";
    }
    return "unknown region";
}

String CombatEventText(const CombatEvent& event, const ContentCatalog& content) {
    const auto* move = content.TryGetCombatMove(event.move.value ? event.move : event.strike.move);
    const auto* ability = content.TryGetAbility(event.ability);
    String text = "[" + UnsignedText(event.tick) + "] #" + UnsignedText(event.actor);
    switch (event.kind) {
    case CombatEventKind::ActionCommitted:
        text += " committed " + (ability ? ability->name : move ? move->name : String("combat action"))
            + " | " + UnsignedText(event.cost) + " ticks, " + UnsignedText(event.staminaSpent) + " stamina";
        break;
    case CombatEventKind::ActionInvalidated:
        text += " action invalidated: " + CombatRefusalText(event.reason); break;
    case CombatEventKind::TrainingSuppressed:
        text += " no combat XP for suppressed training awards";
        if (event.opponent) { text += " against #" + UnsignedText(event.opponent); }
        text += event.reason == CombatRefusal::None
            ? ": opponent training rank too low for affected learned skill"
            : ": " + CombatRefusalText(event.reason);
        break;
    case CombatEventKind::ArtStudyCompleted: {
        const auto* art = content.TryGetCombatArt(event.art);
        text += " art study " + (art ? art->name : String("unknown art"))
            + (event.learnedArt ? ": learned" : ": failed attempt; may retry");
        break;
    }
    case CombatEventKind::ActorDied: text += " died"; break;
    case CombatEventKind::Strike: {
        const auto& strike = event.strike;
        text = "[" + UnsignedText(event.tick) + "] #" + UnsignedText(strike.attacker)
            + " -> #" + UnsignedText(strike.target) + " " + (move ? move->name : String("strike")) + ": ";
        switch (strike.outcome) {
        case StrikeOutcome::Invalidated: text += "invalidated: " + CombatRefusalText(strike.reason); break;
        case StrikeOutcome::Miss: text += "miss"; break;
        case StrikeOutcome::Dodged: text += "dodged"; break;
        case StrikeOutcome::Parried: text += "parried"; break;
        case StrikeOutcome::Contact: text += "contact ("; text += CombatRegionName(strike.region); text += ')'; break;
        }
        if (strike.response == DefenseKind::Block) {
            text += strike.defenseSucceeded ? "; block succeeded" : "; block failed";
        }
        if (strike.blockAbsorbed) { text += "; block absorbed "; Hp(text, strike.blockAbsorbed); }
        if (strike.armorAbsorbed) { text += "; armor absorbed "; Hp(text, strike.armorAbsorbed); }
        if (strike.armorStopped) { text += "; armor stopped damage"; }
        if (strike.outcome == StrikeOutcome::Contact) { text += "; HP lost "; Hp(text, strike.hpLoss); }
        if (strike.maxHpLoss) { text += "; maximum HP lost "; Hp(text, strike.maxHpLoss); }
        if (strike.counter) { text += "; counter"; }
        if (strike.killed) { text += "; killed"; }
        if (strike.disruption == DisruptionResponse::Canceled) { text += "; action canceled"; }
        else if (strike.disruption == DisruptionResponse::AccuracyReduced) { text += "; accuracy reduced"; }
        break;
    }
    }
    return text;
}

namespace {
String StatusText(const CombatView& view, const String& styleName) {
    bool present = false;
    for (const auto& part : view.parts) { present |= part.maximum != 0; }
    if (!present) { return {}; }
    String text = view.life == ActorLifeState::Dead ? "Combat: dead" : "Combat: ";
    if (view.life != ActorLifeState::Dead) {
        text += styleName;
    }
    text += "\nstamina "; text += UnsignedText(view.stamina); text += "% | "; text += ActionName(view.action);
    if (view.accuracyPenalty) { text += "\naccuracy -"; text += UnsignedText(view.accuracyPenalty); text += "%"; }
    for (std::size_t i = 0; i < body_part_count; ++i) {
        const auto& part = view.parts[i];
        if (!part.maximum || (part.hp == part.maximum && !part.broken)) { continue; }
        text += "\n"; text += PartName(static_cast<BodyPart>(i)); text += " ";
        Hp(text, part.hp); text += "/"; Hp(text, part.maximum);
        if (part.broken) { text += " broken"; }
    }
    return text;
}
}

String CombatStatusText(const CombatView& view) {
    return StatusText(view, CombatStyleName(view.style));
}

String CombatStatusText(const WorldView& view, const ContentCatalog& content) {
    const auto* style = content.TryGetCombatStyle(view.combat.styleId);
    return StatusText(view.combat, style ? style->name : String(CombatStyleName(view.combat.style)));
}

String CombatFeedbackText(const CombatView& view) {
    const auto& feedback = view.feedback;
    const auto& result = feedback.result;
    const char* event = nullptr;
    switch (result.outcome) {
    case AttackOutcome::Unavailable:
        if (result.reason == CombatRefusal::None) { return {}; }
        event = "unavailable"; break;
    case AttackOutcome::NotReady: event = "not ready"; break;
    case AttackOutcome::NoTarget: event = "no target"; break;
    case AttackOutcome::OutOfReach: event = "out of reach"; break;
    case AttackOutcome::NoMove: event = "no combat move"; break;
    case AttackOutcome::Windup: event = "windup"; break;
    case AttackOutcome::Miss: event = "missed"; break;
    case AttackOutcome::Parried: event = "parried"; break;
    case AttackOutcome::Blocked: event = "blocked"; break;
    case AttackOutcome::Hit: event = "hit"; break;
    case AttackOutcome::Defeated: event = "defeated"; break;
    }
    if (!event) { return {}; }
    String text = feedback.incoming ? "Received: " : "Attack: "; text += event;
    if (result.part != BodyPart::Count) { text += " ("; text += PartName(result.part); text += ")"; }
    if (result.injury) { text += " injury "; text += UnsignedText(result.injury); }
    if (result.lostCapability) { text += "; capability lost"; }
    if (result.disruption == DisruptionResponse::AccuracyReduced) { text += "; accuracy reduced"; }
    else if (result.disruption == DisruptionResponse::Canceled) { text += "; action canceled"; }
    if (result.reason != CombatRefusal::None) { text += "; " + CombatRefusalText(result.reason); }
    if (result.resolvedStrikes) {
        text += "; strikes " + UnsignedText(result.resolvedStrikes) + "/" + UnsignedText(result.strikeCount);
        text += "; HP lost "; Hp(text, result.totalHpLoss);
    }
    return text;
}

}
