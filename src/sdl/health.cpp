#include "sdl/health.h"
#include "sdl/layout.h"

#include "client/text.h"

#include <algorithm>
#include <cmath>

namespace cataclysmx::sdl {
namespace {

String Label(const Localization* localization, const char* key, const char* fallback) {
    return localization ? localization->Text(key, fallback) : String(fallback);
}

void AppendHp(String& output, std::uint32_t hp) {
    output += UnsignedText(hp / 100);
    if (hp % 100) {
        output += '.';
        output += static_cast<char>('0' + hp / 10 % 10);
        output += static_cast<char>('0' + hp % 10);
    }
}

}

RgbColor HealthColor(const CombatPartView& part) {
    if (!part.maximum || !part.hp) { return {70, 78, 82}; }
    const std::uint64_t hp = std::uint64_t{part.hp} * 100;
    if (hp > std::uint64_t{part.maximum} * 75) { return {88, 194, 132}; }
    if (hp > std::uint64_t{part.maximum} * 50) { return {225, 204, 89}; }
    if (hp > std::uint64_t{part.maximum} * 25) { return {229, 145, 69}; }
    return {220, 83, 83};
}

String HealthPartText(BodyPart part, const CombatPartView& state, const Localization* localization) {
    const char* key = "", *fallback = "";
    switch (part) {
    case BodyPart::Head: key = "sdl.health.head"; fallback = "Head"; break;
    case BodyPart::Torso: key = "sdl.health.torso"; fallback = "Torso"; break;
    case BodyPart::LeftHand: key = "sdl.health.left_hand"; fallback = "L hand"; break;
    case BodyPart::RightHand: key = "sdl.health.right_hand"; fallback = "R hand"; break;
    case BodyPart::LeftLeg: key = "sdl.health.left_leg"; fallback = "L leg"; break;
    case BodyPart::RightLeg: key = "sdl.health.right_leg"; fallback = "R leg"; break;
    case BodyPart::AttackApparatus: key = "sdl.health.attack"; fallback = "Attack"; break;
    case BodyPart::Locomotion: key = "sdl.health.locomotion"; fallback = "Move"; break;
    case BodyPart::Core: key = "sdl.health.core"; fallback = "Core"; break;
    case BodyPart::Count: return {};
    }
    String result = Label(localization, key, fallback);
    result += ' ';
    AppendHp(result, state.hp);
    result += '/';
    AppendHp(result, state.maximum);
    if (state.broken) { result += "  "; result += Label(localization, "sdl.health.broken", "Broken"); }
    return result;
}

String CombatSummaryText(const CombatView& view) {
    bool present = false;
    for (const auto& part : view.parts) { present |= part.maximum != 0; }
    if (!present) { return {}; }
    String result = CombatStyleName(view.style);
    result += " | Stamina "; result += UnsignedText(view.stamina); result += '%';
    switch (view.action) {
    case ActorActionKind::None: break;
    case ActorActionKind::Recovery: result += " | Recovering"; break;
    case ActorActionKind::Aiming: result += " | Aiming"; break;
    case ActorActionKind::MeleeWindup: result += " | Windup"; break;
    case ActorActionKind::DisruptionRecovery: result += " | Disrupted"; break;
    }
    if (view.accuracyPenalty) { result += " | Accuracy -"; result += UnsignedText(view.accuracyPenalty); result += '%'; }
    return result;
}

bool DrawHealth(SDL_Renderer* renderer, SdlText& text, const CombatView& view,
    SDL_Rect bounds, int& usedHeight, const Localization* localization) {
    usedHeight = 0;
    if (bounds.w <= 0 || bounds.h <= 0) { return true; }
    bool present = false;
    for (const auto& part : view.parts) { present |= part.maximum != 0; }
    if (!present) { return true; }
    const SDL_Color normal{205, 215, 210, 255};
    eastl::array<String, body_part_count> labels;
    int naturalHeight = 110, naturalWidth = 1, textHeight = 0;
    for (unsigned i = 0; i < body_part_count; ++i) {
        if (!view.parts[i].maximum) { continue; }
        labels[i] = HealthPartText(static_cast<BodyPart>(i), view.parts[i], localization);
        if (!text.Draw(62, labels[i].c_str(), -10000, -10000, normal)) { return false; }
        naturalWidth = std::max(naturalWidth, 68 + text.Width(62));
        textHeight += text.Height(62) + 2;
    }
    naturalHeight = std::max(naturalHeight, textHeight);
    const float fit = std::min({1.0f, float(bounds.w) / naturalWidth, float(bounds.h) / naturalHeight});
    float scaleX = 1, scaleY = 1;
    if (!SDL_GetRenderScale(renderer, &scaleX, &scaleY)
        || !SDL_SetRenderScale(renderer, scaleX * fit, scaleY * fit)) { return false; }
    const auto localBounds = ScaleRect(bounds, 1.0f / fit);
    if (!SDL_SetRenderClipRect(renderer, &localBounds)) { return false; }
    // Front-facing body: the character's left is on the viewer's right.
    constexpr SDL_Rect detailed[] = {{22, 0, 18, 18}, {16, 22, 30, 38},
        {50, 25, 10, 38}, {2, 25, 10, 38}, {34, 64, 12, 42}, {16, 64, 12, 42}};
    constexpr SDL_Rect simple[] = {{2, 25, 10, 38}, {16, 64, 30, 42}, {16, 22, 30, 38}};
    int y = localBounds.y;
    for (unsigned i = 0; i < body_part_count; ++i) {
        const auto& part = view.parts[i];
        if (!part.maximum) { continue; }
        SDL_Rect shape = i < 6 ? detailed[i] : simple[i - 6];
        SDL_FRect box{float(localBounds.x + shape.x), float(localBounds.y + shape.y), float(shape.w), float(shape.h)};
        const auto color = HealthColor(part);
        if (!SDL_SetRenderDrawColor(renderer, color.red, color.green, color.blue, 255)
            || !SDL_RenderFillRect(renderer, &box)
            || !SDL_SetRenderDrawColor(renderer, 170, 183, 180, 255) || !SDL_RenderRect(renderer, &box)) { return false; }
        if (part.broken) {
            if (!SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255)
                || !SDL_RenderLine(renderer, box.x, box.y, box.x + box.w, box.y + box.h)
                || !SDL_RenderLine(renderer, box.x + box.w, box.y, box.x, box.y + box.h)) { return false; }
        }
        if (!text.Draw(62, labels[i].c_str(), float(localBounds.x + 68), float(y), normal)) { return false; }
        y += text.Height(62) + 2;
    }
    usedHeight = std::min(bounds.h, static_cast<int>(std::ceil(naturalHeight * fit)));
    return SDL_SetRenderClipRect(renderer, nullptr) && SDL_SetRenderScale(renderer, scaleX, scaleY);
}

}
