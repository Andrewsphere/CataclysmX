#include "sdl/health.h"
#include "sdl/layout.h"
#include "sdl/screens.h"

#include "client/text.h"
#include "client/tile_inspection.h"
#include "core/ratio.h"

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

String PartLabel(BodyPart part, const Localization* localization) {
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
    return Label(localization, key, fallback);
}

void AppendHpValue(String& value, const CombatPartView& state) {
    AppendHp(value, state.hp);
    value += '/';
    AppendHp(value, state.maximum);
}

bool DrawTilePart(SDL_Renderer* renderer, SDL_FRect track, std::uint16_t ratio) {
    if (!SDL_SetRenderDrawColor(renderer, 66, 74, 80, 255) || !SDL_RenderFillRect(renderer, &track)) { return false; }
    if (!ratio) {
        if (!SDL_SetRenderDrawColor(renderer, 227, 233, 230, 255)) { return false; }
        const float x = track.x + static_cast<int>(track.w) / 2;
        const float y = track.y + static_cast<int>(track.h) / 2;
        if (track.w < 3 || track.h < 3) { return SDL_RenderPoint(renderer, x, y); }
        return SDL_RenderLine(renderer, x - 1, y - 1, x + 1, y + 1)
            && SDL_RenderLine(renderer, x - 1, y + 1, x + 1, y - 1);
    }
    const int width = static_cast<int>(track.w);
    const bool full = ratio == ratioScale;
    const auto color = full ? RgbColor{94, 143, 111} : HealthColor({ratio, ratioScale});
    if (!full && width > 1) {
        track.w = static_cast<float>(std::clamp(static_cast<int>(ScaleRatioFloor(width, ratio)), 1, width - 1));
    }
    return SDL_SetRenderDrawColor(renderer, color.red, color.green, color.blue, 255) && SDL_RenderFillRect(renderer, &track);
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
    if (part == BodyPart::Count) { return {}; }
    String result = PartLabel(part, localization);
    result += ' ';
    AppendHpValue(result, state);
    if (state.broken) { result += "  "; result += Label(localization, "sdl.health.broken", "Broken"); }
    return result;
}

void AppendEnemyExamine(const ExaminedEnemy& enemy, const Localization& localization, ScreenFrame& frame) {
    frame.rows.push_back({enemy.name});
    constexpr BodyPart order[] = {BodyPart::Core, BodyPart::AttackApparatus, BodyPart::Locomotion,
        BodyPart::Head, BodyPart::Torso, BodyPart::LeftHand, BodyPart::RightHand, BodyPart::LeftLeg, BodyPart::RightLeg};
    for (const auto part : order) {
        const auto& state = enemy.combat.parts[static_cast<unsigned>(part)];
        if (!state.maximum) { continue; }
        String label = part == BodyPart::Locomotion ? localization.Text("sdl.examine.movement", "Movement")
            : PartLabel(part, &localization);
        if (part == BodyPart::Core || part == BodyPart::Head || part == BodyPart::Torso) {
            label += " · "; label += localization.Text("sdl.examine.vital", "Vital");
        } else if (state.broken) {
            label += " · "; label += localization.Text("sdl.health.broken", "Broken");
        } else if (!state.hp) {
            label += " · "; label += localization.Text("sdl.examine.disabled", "Disabled");
        }
        String value;
        AppendHpValue(value, state);
        frame.rows.push_back({eastl::move(label), false, ScreenHealthRow{state, eastl::move(value)}});
    }
    frame.rows.push_back({enemy.status});
    frame.rows.push_back({});
}

bool DrawEnemyTileHealth(SDL_Renderer* renderer, SDL_Rect tile, const EnemyHealthView& health) {
    constexpr int columns = 3;
    constexpr BodyPart simple[] = {BodyPart::AttackApparatus, BodyPart::Core, BodyPart::Locomotion};
    constexpr BodyPart detailed[] = {BodyPart::LeftHand, BodyPart::Head, BodyPart::RightHand,
        BodyPart::LeftLeg, BodyPart::Torso, BodyPart::RightLeg};
    const bool simpleBody = health.bodyKind == BodyKind::Simple;
    const auto* parts = simpleBody ? simple : detailed;
    const int rows = simpleBody ? 1 : 2;
    const int size = std::min(tile.w, tile.h);
    const int inset = size >= 12 ? 1 : 0;
    const int padding = size >= 16 ? 1 : 0;
    const int gap = size >= 8 ? 1 : 0;
    const int scale = std::clamp(size / 32, 1, 2);
    const bool pips = size < 24;
    const int width = std::min(pips ? 3 : 8 * scale, (tile.w - inset - 2 * padding - (columns - 1) * gap) / columns);
    const int height = std::min(pips ? width : 3 * scale, (tile.h - inset - 2 * padding - (rows - 1) * gap) / rows);
    if (width <= 0 || height <= 0) { return true; }
    const int panelWidth = columns * width + (columns - 1) * gap + 2 * padding;
    const int panelHeight = rows * height + (rows - 1) * gap + 2 * padding;
    const int x = tile.x + tile.w - inset - panelWidth, y = tile.y + tile.h - inset - panelHeight;
    const SDL_FRect panel{float(x), float(y), float(panelWidth), float(panelHeight)};
    if (!SDL_SetRenderDrawColor(renderer, 16, 22, 27, 255) || !SDL_RenderFillRect(renderer, &panel)) { return false; }
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const SDL_FRect track{float(x + padding + column * (width + gap)),
                float(y + padding + row * (height + gap)), float(width), float(height)};
            const auto part = parts[row * columns + column];
            if (!DrawTilePart(renderer, track, health.partRatios[static_cast<unsigned>(part)])) { return false; }
        }
    }
    return true;
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
