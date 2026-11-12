#include "client/combat_text.h"
#include "client/localization.h"

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
    }
    return "unknown";
}
void Hp(String& text, std::uint32_t value) {
    text += UnsignedText(value / 100);
    text += '.'; text += static_cast<char>('0' + (value / 10) % 10);
    text += static_cast<char>('0' + value % 10);
}
}

String CombatStatusText(const CombatView& view) {
    bool present = false;
    for (const auto& part : view.parts) { present |= part.maximum != 0; }
    if (!present) { return {}; }
    String text = "Combat: "; text += CombatStyleName(view.style);
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

String CombatFeedbackText(const CombatView& view) {
    const auto& feedback = view.feedback;
    const auto& result = feedback.result;
    const char* event = nullptr;
    switch (result.outcome) {
    case AttackOutcome::Unavailable: return {};
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
    return text;
}

}
