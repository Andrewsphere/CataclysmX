#include "sdl/screens.h"
#include "sdl/health.h"
#include "sdl/renderer.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace cataclysmx::sdl {
namespace {
bool SameCombatChoice(const Command& a, const Command& b) {
    return a.kind == b.kind && a.combatStyle == b.combatStyle && a.book == b.book && a.art == b.art
        && a.ability.ability == b.ability.ability && a.ability.source == b.ability.source;
}
void TextLines(eastl::string_view text, Vector<String>& lines) {
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            lines.push_back(String(text.data() + start, i - start)); start = i + 1;
        }
    }
}
}

void CombatUi::Refresh(const ContentCatalog& content, const WorldView& view) {
    if (pendingAbility_ && origin_ != view.player_position) {
        pendingAbility_.reset(); feedback_ = "Player position changed; choose the ability target again.";
    }
    eastl::optional<Command> selection;
    if (selected_ < rows_.size()) { selection = rows_[selected_].command; }
    CollectCombatMenu(content, view, rows_);
    selected_ = 0;
    if (selection) {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (SameCombatChoice(*selection, rows_[i].command)) { selected_ = i; break; }
        }
    }
}
void CombatUi::MoveSelection(int delta) {
    if (rows_.empty()) { return; }
    selected_ = delta < 0 ? (selected_ + rows_.size() - 1) % rows_.size() : (selected_ + 1) % rows_.size();
    feedback_.clear();
}
bool CombatUi::SelectHotkey(InputAction action, const ContentCatalog& content) {
    const char* name = nullptr;
    switch (action) {
    case InputAction::FocusAbility: name = "focus"; break;
    case InputAction::LeapAbility: name = "leap"; break;
    case InputAction::SweepAbility: name = "sweep"; break;
    default: return false;
    }
    const auto id = content.FindAbility(name);
    // Menu order is the deterministic tie breaker among available grants.
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].command.kind == CommandKind::use_ability && rows_[i].command.ability.ability == id && rows_[i].enabled) {
            selected_ = i; feedback_.clear(); return true;
        }
    }
    feedback_ = String(name) + " is not available. Inspect granted abilities and their refusal reasons below.";
    return false;
}
bool CombatUi::BeginChoice(TileCoord origin) {
    if (selected_ >= rows_.size()) { return false; }
    const auto& row = rows_[selected_];
    if (!row.enabled) { feedback_ = row.details; return false; }
    feedback_.clear();
    if (row.command.kind == CommandKind::use_ability) {
        if (row.targeting != AbilityTargetKind::Self && row.targeting != AbilityTargetKind::Tile && row.targeting != AbilityTargetKind::CardinalDirection) {
            feedback_ = "This ability's target selection is not supported."; return false;
        }
        pendingAbility_ = row.command; origin_ = origin;
        if (row.targeting == AbilityTargetKind::Tile) { pendingAbility_->ability.target.tile = origin; }
        pendingAbility_->ability.target.direction = CardinalDirection::North;
    }
    return true;
}
void CombatUi::MoveTarget(InputAction action) {
    if (!pendingAbility_) { return; }
    auto& target = pendingAbility_->ability.target;
    if (target.kind == AbilityTargetKind::CardinalDirection) {
        switch (action) {
        case InputAction::North: target.direction = CardinalDirection::North; break;
        case InputAction::East: target.direction = CardinalDirection::East; break;
        case InputAction::South: target.direction = CardinalDirection::South; break;
        case InputAction::West: target.direction = CardinalDirection::West; break;
        default: break;
        }
        return;
    }
    int dx = 0, dz = 0;
    switch (action) {
    case InputAction::North: dz = -1; break;
    case InputAction::South: dz = 1; break;
    case InputAction::West: dx = -1; break;
    case InputAction::East: dx = 1; break;
    case InputAction::Northwest: dx = -1; dz = -1; break;
    case InputAction::Northeast: dx = 1; dz = -1; break;
    case InputAction::Southwest: dx = -1; dz = 1; break;
    case InputAction::Southeast: dx = 1; dz = 1; break;
    default: return;
    }
    target.tile.x = std::clamp(target.tile.x + dx, origin_.x - 2, origin_.x + 2);
    target.tile.z = std::clamp(target.tile.z + dz, origin_.z - 2, origin_.z + 2);
    target.tile.y = origin_.y;
}
eastl::optional<Command> CombatUi::Accept() {
    const Command* choice = pendingAbility_ ? &*pendingAbility_ : selected_ < rows_.size() ? &rows_[selected_].command : nullptr;
    if (!choice) { return {}; }
    for (const auto& row : rows_) {
        if (!SameCombatChoice(*choice, row.command)) { continue; }
        if (!row.enabled) { feedback_ = row.details; return {}; }
        if (!pendingAbility_ && row.command.kind == CommandKind::use_ability && row.targeting != AbilityTargetKind::Self) { return {}; }
        if (pendingAbility_ && choice->ability.target.kind == AbilityTargetKind::Tile && choice->ability.target.tile == origin_) {
            feedback_ = "Choose a landing tile away from the player."; return {};
        }
        return *choice;
    }
    feedback_ = "Combat choice changed; cancel and choose again."; return {};
}
void CombatUi::Present(ScreenFrame& frame) const {
    frame.title = "Combat | Styles, abilities and art manuals";
    frame.footer = "Up/Down choose | Enter accept | PgUp/PgDn details | Escape close";
    if (Targeting()) {
        frame.presentation = ScreenPresentation::Anchored;
        const auto& target = pendingAbility_->ability.target;
        if (target.kind == AbilityTargetKind::Tile) {
            frame.title = "Leap | Preview landing"; frame.cursor = target.tile;
            frame.highlights.push_back(target.tile);
            frame.rows.push_back({"Move landing cursor within 2 tiles, same Y. Server checks landing on acceptance."});
        } else {
            frame.title = "Directional sweep | Preview three tiles";
            int dx = 0, dz = 0;
            switch (target.direction) {
            case CardinalDirection::North: dz = -1; break;
            case CardinalDirection::East: dx = 1; break;
            case CardinalDirection::South: dz = 1; break;
            case CardinalDirection::West: dx = -1; break;
            }
            frame.cursor = origin_;
            for (int i = -1; i <= 1; ++i) {
                frame.highlights.push_back({origin_.x + dx - dz * i, origin_.z + dz + dx * i, origin_.y});
            }
            frame.rows.push_back({"Choose one of four cardinal directions with arrows."});
        }
        frame.footer = "Arrows preview | Enter accept | Escape cancel target";
    } else {
        for (const auto& row : rows_) { frame.rows.push_back({row.label, !row.enabled}); }
        frame.selected = rows_.empty() ? -1 : static_cast<int>(selected_);
    }
    if (!feedback_.empty()) { TextLines({feedback_.data(), feedback_.size()}, frame.details); }
    if (selected_ < rows_.size()) {
        const auto& details = rows_[selected_].details;
        TextLines({details.data(), details.size()}, frame.details);
    }
}
void PresentCombatInfo(const ContentCatalog& content, const WorldView& view, ScreenFrame& frame) {
    frame.title = "Player combat information";
    frame.scrollText = true;
    const auto text = CombatDetailsText(content, view);
    frame.rows.push_back({text});
    frame.footer = "Up/Down / PgUp/PgDn scroll | Escape / Enter close";
}
void CombatEventCursor::Drain(const ContentCatalog& content, const WorldView& view,
    std::uint64_t generation, Vector<String>& messages) {
    messages.clear();
    if (generation_ != generation) { generation_ = generation; serial_ = 0; }
    for (const auto& event : view.combatEvents) {
        if (event.serial <= serial_) { continue; }
        serial_ = event.serial;
        if (event.kind != CombatEventKind::ActionCommitted) { messages.push_back(CombatEventText(event, content)); }
    }
}

namespace {
bool DrawHealthBar(SDL_Renderer* renderer, SDL_FRect track, const CombatPartView& state) {
    if (!SDL_SetRenderDrawColor(renderer, 70, 78, 82, 255) || !SDL_RenderFillRect(renderer, &track)) { return false; }
    if (!state.hp) { return true; }
    const int width = static_cast<int>(track.w);
    const int filled = state.hp >= state.maximum || width <= 1 ? width
        : std::clamp(static_cast<int>(std::uint64_t{state.hp} * width / state.maximum), 1, width - 1);
    track.w = static_cast<float>(filled);
    const auto color = HealthColor(state);
    return SDL_SetRenderDrawColor(renderer, color.red, color.green, color.blue, 255) && SDL_RenderFillRect(renderer, &track);
}

SDL_Color ToneColor(SettingsTone tone) {
    switch (tone) {
    case SettingsTone::Muted: return {153, 171, 177, 255};
    case SettingsTone::Global: return {122, 218, 237, 255};
    case SettingsTone::World: return {220, 169, 248, 255};
    case SettingsTone::Pending: case SettingsTone::Warning: return {252, 197, 107, 255};
    case SettingsTone::Success: return {145, 223, 163, 255};
    case SettingsTone::Error: return {255, 147, 150, 255};
    case SettingsTone::Normal: return {220, 234, 234, 255};
    }
    std::abort();
}

bool DrawStructuredRows(SDL_Renderer* renderer, SdlText& text, ScreenFrame& frame, SDL_Rect& clip, bool stackedDetails) {
    const int count = static_cast<int>(frame.rows.size());
    const bool hasSelection = frame.selected >= 0 && frame.selected < count;
    const int minHeight = std::max(1, text.LineHeight()) + 6;
    const int valueX = clip.x + clip.w * 53 / 100;
    const int indent = 6;
    const int labelX = clip.x + indent;
    frame.rowHeights.assign(count, 0);
    const auto label = [&](int index) {
        const auto& row = frame.rows[index];
        String result = index == frame.selected ? "> " : "";
        if (row.tone == SettingsTone::Pending) { result += "* "; }
        else if (!row.button && row.tone == SettingsTone::Global) { result += "G "; }
        else if (!row.button && row.tone == SettingsTone::World) { result += "W "; }
        else if (row.tone == SettingsTone::Warning) { result += "! "; }
        if (row.button) { result += "[" + row.text + "]"; }
        else { result += row.text; }
        return result;
    };
    const auto rowText = [&](int index, int y) {
        const auto& row = frame.rows[index];
        const auto name = label(index);
        const int labelWidth = std::max(1, (row.value.empty() ? clip.x + clip.w : valueX) - labelX - indent);
        const int valueWidth = std::max(1, clip.x + clip.w - valueX - indent);
        const auto labelColor = row.button ? ToneColor(row.tone) : row.divider
            ? ToneColor(frame.scopeTone == SettingsTone::Normal ? SettingsTone::Global : frame.scopeTone)
            : ToneColor(row.unavailable ? SettingsTone::Muted : SettingsTone::Normal);
        const auto valueTone = row.tone == SettingsTone::Normal ? SettingsTone::Global : row.tone;
        return text.Draw(10, name.c_str(), static_cast<float>(labelX), static_cast<float>(y), labelColor, labelWidth)
            && (row.value.empty() || text.Draw(11, row.value.c_str(), static_cast<float>(valueX), static_cast<float>(y),
                ToneColor(valueTone), valueWidth));
    };
    const auto height = [&](int index) {
        int& cached = frame.rowHeights[index];
        if (cached) { return cached; }
        if (!rowText(index, -10000)) { return -1; }
        cached = std::max(minHeight, std::max(text.Height(10),
            frame.rows[index].value.empty() ? 0 : text.Height(11)) + 6);
        return cached;
    };
    if (stackedDetails) {
        const int selectedHeight = hasSelection ? height(frame.selected) : minHeight;
        if (selectedHeight < 0) { return false; }
        clip.h = std::min(clip.h, std::max(selectedHeight, clip.h * 3 / 5));
    }
    const auto topFor = [&](int last) {
        int used = 0;
        for (int i = last; i >= 0; --i) {
            const int pixels = height(i);
            if (pixels < 0) { return -1; }
            if (used + pixels > clip.h) { return std::min(last, i + 1); }
            used += pixels;
        }
        return 0;
    };
    const int maximum = count ? topFor(count - 1) : 0;
    if (maximum < 0) { return false; }
    frame.scroll = std::clamp(frame.scroll, 0, maximum);
    if (hasSelection) {
        const int selectionTop = topFor(frame.selected);
        if (selectionTop < 0) { return false; }
        frame.scroll = std::clamp(frame.scroll, selectionTop, frame.selected);
    }
    if (!SDL_SetRenderClipRect(renderer, &clip)) { return false; }
    for (int i = frame.scroll, y = clip.y; i < count && y < clip.y + clip.h; ++i) {
        const auto& row = frame.rows[i];
        const int pixels = height(i);
        if (pixels < 0) { return false; }
        if (i == frame.selected || row.divider) {
            const SDL_Color background = row.divider ? SDL_Color{27, 44, 51, 255} : SDL_Color{37, 57, 70, 255};
            const SDL_FRect band{static_cast<float>(clip.x), static_cast<float>(y), static_cast<float>(clip.w), static_cast<float>(pixels - 1)};
            if (!SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, background.a)
                || !SDL_RenderFillRect(renderer, &band)) { return false; }
        }
        if (!rowText(i, y + 3)) { return false; }
        y += pixels;
    }
    return true;
}

bool DrawSettingsDetails(SDL_Renderer* renderer, SdlText& text, ScreenFrame& frame, SDL_Rect clip) {
    if (!SDL_SetRenderClipRect(renderer, &clip)) { return false; }
    const int lineHeight = std::max(1, text.LineHeight());
    int contentHeight = 0;
    for (const auto& detail : frame.settingsDetails) {
        if (!text.Draw(6, detail.text.c_str(), -10000, -10000, ToneColor(detail.tone), clip.w)) { return false; }
        contentHeight += std::max(lineHeight, text.Height(6)) + (detail.divider ? 6 : 2);
    }
    const int maxScroll = std::max(0, (contentHeight - clip.h + lineHeight - 1) / lineHeight);
    frame.detailScroll = std::clamp(frame.detailScroll, 0, maxScroll);
    int y = clip.y - frame.detailScroll * lineHeight;
    for (const auto& detail : frame.settingsDetails) {
        if (y >= clip.y + clip.h) { break; }
        if (detail.divider) { y += 4; }
        if (!text.Draw(6, detail.text.c_str(), static_cast<float>(clip.x), static_cast<float>(y), ToneColor(detail.tone), clip.w)) { return false; }
        if (detail.divider && text.Width(6) + 12 < clip.w) {
            const float lineY = static_cast<float>(y + lineHeight / 2);
            if (!SDL_SetRenderDrawColor(renderer, 66, 83, 94, 255)
                || !SDL_RenderLine(renderer, static_cast<float>(clip.x + text.Width(6) + 8), lineY,
                    static_cast<float>(clip.x + clip.w - 1), lineY)) { return false; }
        }
        y += std::max(lineHeight, text.Height(6)) + 2;
    }
    return true;
}

bool DrawAnchored(SDL_Renderer* renderer, SdlText& text, ScreenFrame& frame,
    const GameplayLayout& gameplay, const WorldView* view, float uiScale, int width, int height,
    const char* footerText) {
    bool hasText = false, hasHealth = false;
    for (const auto& row : frame.rows) {
        hasText |= !row.text.empty();
        hasHealth |= row.health.has_value();
    }
    for (const auto& detail : frame.details) { hasText |= !detail.empty(); }
    if (!hasText) { return SDL_SetRenderClipRect(renderer, nullptr) && SDL_SetRenderScale(renderer, 1, 1); }
    const SDL_Color normal{210, 220, 218, 255}, dim{145, 160, 163, 255}, selected{255, 230, 90, 255};
    auto viewport = ScaleRect(gameplay.mapViewport, 1.0f / uiScale);
    const int lineHeight = std::max(1, text.LineHeight());
    // Make room for the enemy name and three stacked groups after the chrome.
    const int minimumHeight = lineHeight * (hasHealth ? 9 : 3) + 20;
    // Borrow space below the map when the preferred minimum cannot fit.
    if (viewport.h < minimumHeight + 8) {
        viewport = {0, gameplay.topBar.h, width, std::max(0, height - gameplay.topBar.h)};
    }
    if (viewport.w < 24 || viewport.h < 24) { return true; }
    viewport.x += 4; viewport.y += 4; viewport.w -= 8; viewport.h -= 8;
    SDL_Rect target{viewport.x + viewport.w / 2, viewport.y + viewport.h / 2, 1, 1}, player{};
    if (view) {
        if (frame.cursor) { target = ScaleRect(MapCellRect(gameplay.map,
            frame.cursor->x - view->origin.x, frame.cursor->z - view->origin.z), 1.0f / uiScale); }
        player = ScaleRect(MapCellRect(gameplay.map, view->player_position.x - view->origin.x,
            view->player_position.z - view->origin.z), 1.0f / uiScale);
    }
    const int heightLimit = std::min(viewport.h, std::max(minimumHeight, viewport.h * 60 / 100));
    const auto provisional = PlacePopup(viewport, target, std::min(320, viewport.w), heightLimit, player);
    const int wrap = std::max(1, provisional.w - 16);
    if (!text.Draw(7, "M", -10000, -10000, normal)) { return false; }
    if (!text.Draw(8, frame.title.c_str(), -10000, -10000, selected, wrap)
        || !text.Draw(9, footerText, -10000, -10000, dim, wrap)) { return false; }
    const int titleHeight = std::min(text.Height(8), lineHeight * 2);
    const int footerHeight = std::min(text.Height(9), lineHeight * 2);
    constexpr int healthGap = 8;
    constexpr unsigned healthValueSlot = 71;
    int healthLabelWidth = 0, healthValueWidth = 0;
    for (const auto& row : frame.rows) {
        if (!row.health) { continue; }
        if (!text.Draw(7, row.text.c_str(), -10000, -10000, normal)
            || !text.Draw(healthValueSlot, row.health->value.c_str(), -10000, -10000, normal)) { return false; }
        healthLabelWidth = std::max(healthLabelWidth, text.Width(7));
        healthValueWidth = std::max(healthValueWidth, text.Width(healthValueSlot));
    }
    const int inlineBarWidth = std::min(96, wrap - healthLabelWidth - healthValueWidth - 2 * healthGap);
    const bool stackHealth = inlineBarWidth < 48;
    const int barWidth = stackHealth ? std::min(wrap, 160) : inlineBarWidth;
    const int barHeight = std::clamp(lineHeight / 3, 5, 10);
    int contentHeight = 0;
    frame.rowHeights.resize(frame.rows.size());
    for (std::size_t i = 0; i < frame.rows.size(); ++i) {
        const auto& row = frame.rows[i];
        if (row.health) {
            frame.rowHeights[i] = lineHeight;
            if (stackHealth) {
                const String label = row.text + "  " + row.health->value;
                if (!text.Draw(7, label.c_str(), -10000, -10000, normal, wrap)) { return false; }
                frame.rowHeights[i] = std::max(lineHeight, text.Height(7)) + lineHeight;
            }
        } else if (frame.selected < 0 && row.text.empty()) {
            frame.rowHeights[i] = 8;
        } else {
            if (!text.Draw(7, row.text.c_str(), -10000, -10000, normal,
                frame.selected < 0 ? wrap : std::max(1, wrap - 14))) { return false; }
            frame.rowHeights[i] = std::max(lineHeight, text.Height(7)) + (frame.selected < 0 ? 0 : 4);
        }
        contentHeight += frame.rowHeights[i];
    }
    String details;
    for (const auto& detail : frame.details) { details += detail; details += '\n'; }
    if (!details.empty()) {
        if (!text.Draw(6, details.c_str(), -10000, -10000, dim, wrap)) { return false; }
        contentHeight += text.Height(6) + 4;
    }
    // Reserve one body line even on a compact map. In that case a one-line
    // title/footer takes priority over their optional continuation lines.
    const int maxChrome = std::max(0, heightLimit - lineHeight - 20);
    const int header = std::min(titleHeight, std::max(lineHeight, (maxChrome - lineHeight) / lineHeight * lineHeight));
    const int footer = std::min(footerHeight, std::max(lineHeight, (maxChrome - header) / lineHeight * lineHeight));
    const int desiredHeight = std::min(heightLimit, header + footer + contentHeight + 20);
    const auto panel = PlacePopup(viewport, target, provisional.w, desiredHeight, player);
    const int bodyY = panel.y + header + 10;
    const int footerY = panel.y + panel.h - footer - 6;
    const SDL_Rect bodyClip{panel.x + 8, bodyY, wrap, std::max(1, footerY - bodyY - 4)};
    if (!SDL_SetRenderClipRect(renderer, &viewport)
        || !SDL_SetRenderDrawColor(renderer, 255, 230, 90, 255)) { return false; }
    const float anchorX = float(target.x + target.w / 2), anchorY = float(target.y + target.h / 2);
    const float connectorX = float(std::clamp(target.x + target.w / 2, panel.x, panel.x + panel.w));
    const float connectorY = float(std::clamp(target.y + target.h / 2, panel.y, panel.y + panel.h));
    if (!SDL_RenderLine(renderer, anchorX, anchorY, connectorX, connectorY)) { return false; }
    const SDL_FRect box{float(panel.x), float(panel.y), float(panel.w), float(panel.h)};
    if (!SDL_SetRenderDrawColor(renderer, 16, 24, 29, 255) || !SDL_RenderFillRect(renderer, &box)
        || !SDL_SetRenderDrawColor(renderer, 137, 160, 152, 255) || !SDL_RenderRect(renderer, &box)) { return false; }
    const SDL_Rect titleClip{panel.x + 8, panel.y + 5, wrap, header};
    if (!SDL_SetRenderClipRect(renderer, &titleClip)
        || !text.Draw(8, frame.title.c_str(), float(panel.x + 8), float(panel.y + 5), selected, wrap)) { return false; }
    const int size = static_cast<int>(frame.rows.size());
    if (frame.selected < 0) {
        const int maxScroll = std::max(0, (contentHeight - bodyClip.h + lineHeight - 1) / lineHeight);
        frame.scroll = std::clamp(frame.scroll, 0, maxScroll);
        if (!SDL_SetRenderClipRect(renderer, &bodyClip)) { return false; }
        int y = bodyY - frame.scroll * lineHeight;
        for (int i = 0; i < size; ++i) {
            const auto& row = frame.rows[i];
            if (y < bodyClip.y + bodyClip.h && y + frame.rowHeights[i] > bodyClip.y) {
                if (row.health) {
                    const auto slot = 10 + i % 48;
                    int barX = bodyClip.x;
                    int barY = y + (lineHeight - barHeight) / 2;
                    if (stackHealth) {
                        const String label = row.text + "  " + row.health->value;
                        if (!text.Draw(slot, label.c_str(), float(bodyClip.x), float(y), normal, wrap)) { return false; }
                        barY += frame.rowHeights[i] - lineHeight;
                    } else {
                        barX += healthLabelWidth + healthGap;
                        const int valueRight = barX + barWidth + healthGap + healthValueWidth;
                        if (!text.Draw(slot, row.text.c_str(), float(bodyClip.x), float(y), normal)
                            || !text.Draw(healthValueSlot, row.health->value.c_str(), -10000, -10000, normal)
                            || !text.Draw(healthValueSlot, row.health->value.c_str(),
                                float(valueRight - text.Width(healthValueSlot)), float(y), normal)) { return false; }
                    }
                    if (!DrawHealthBar(renderer, {float(barX), float(barY), float(barWidth), float(barHeight)},
                        row.health->state)) { return false; }
                } else if (row.text.empty()) {
                    if (!SDL_SetRenderDrawColor(renderer, 48, 62, 68, 255)
                        || !SDL_RenderLine(renderer, float(bodyClip.x), float(y + 4),
                            float(bodyClip.x + bodyClip.w - 1), float(y + 4))) { return false; }
                } else if (!text.Draw(10 + i % 48, row.text.c_str(),
                    float(bodyClip.x), float(y), normal, wrap)) { return false; }
            }
            y += frame.rowHeights[i];
        }
        if (!details.empty() && !text.Draw(6, details.c_str(), float(bodyClip.x), float(y + 4), dim, wrap)) { return false; }
    } else {
        const bool inlineDetails = !details.empty() && bodyClip.h < lineHeight * 2 + 8;
        const int detailHeight = details.empty() || inlineDetails ? 0 : std::min(text.Height(6), std::max(lineHeight, bodyClip.h / 3));
        const SDL_Rect listClip{bodyClip.x, bodyClip.y, bodyClip.w, std::max(1, bodyClip.h - detailHeight - (detailHeight ? 4 : 0))};
        frame.scroll = std::clamp(frame.scroll, 0, std::max(0, size - 1));
        if (frame.selected < size) {
            frame.scroll = std::min(frame.scroll, frame.selected);
            int used = 0;
            for (int i = frame.scroll; i <= frame.selected; ++i) { used += frame.rowHeights[i]; }
            while (used > listClip.h && frame.scroll < frame.selected) { used -= frame.rowHeights[frame.scroll++]; }
        }
        if (!SDL_SetRenderClipRect(renderer, &listClip)) { return false; }
        int y = bodyY;
        for (int i = frame.scroll; i < size && y < bodyY + listClip.h; ++i) {
            if (i != frame.selected && y + lineHeight > listClip.y + listClip.h) { break; }
            String label = i == frame.selected ? "> " : "  "; label += frame.rows[i].text;
            if (inlineDetails && i == frame.selected) {
                label += ": "; label += frame.details.front();
            }
            if (!text.Draw(10 + (i - frame.scroll) % 48, label.c_str(), float(panel.x + 8), float(y),
                frame.rows[i].unavailable ? dim : i == frame.selected ? selected : normal, wrap)) { return false; }
            y += frame.rowHeights[i];
        }
        if (detailHeight) {
            const SDL_Rect detailClip{bodyClip.x, bodyClip.y + bodyClip.h - detailHeight, bodyClip.w, detailHeight};
            const int maxScroll = std::max(0, (text.Height(6) - detailHeight + lineHeight - 1) / lineHeight);
            frame.detailScroll = std::clamp(frame.detailScroll, 0, maxScroll);
            if (!SDL_SetRenderClipRect(renderer, &detailClip)
                || !text.Draw(6, details.c_str(), float(detailClip.x), float(detailClip.y - frame.detailScroll * lineHeight), dim, wrap)) { return false; }
        }
    }
    const SDL_Rect footerClip{panel.x + 8, footerY, wrap, footer};
    return SDL_SetRenderClipRect(renderer, &footerClip)
        && text.Draw(9, footerText, float(panel.x + 8), float(footerY), dim, wrap)
        && SDL_SetRenderClipRect(renderer, nullptr) && SDL_SetRenderScale(renderer, 1, 1);
}

}

bool DrawScreen(SDL_Renderer* renderer, SdlText& text, ScreenFrame& frame,
    int width, int height, const WorldView* view, int tileWidth, int tileHeight, int zoom, float uiScale,
    const GameplayLayout* gameplay, const char* footerOverride) {
    if (!frame.visible) { return true; }
    if (!SDL_SetRenderScale(renderer, 1, 1) || !SDL_SetRenderClipRect(renderer, nullptr)) { return false; }
    const auto focus = frame.followCursor && frame.cursor && view
        ? SDL_Point{frame.cursor->x - view->player_position.x, frame.cursor->z - view->player_position.z} : SDL_Point{};
    const auto calculated = gameplay ? GameplayLayout{} : MakeGameplayLayout(width, height,
        view ? view->width : 1, view ? view->height : 1, tileWidth, tileHeight, zoom, uiScale, focus, text.LineHeight());
    const auto& geometry = gameplay ? *gameplay : calculated;
    if (view && (frame.cursor || !frame.highlights.empty())) {
        const auto& layout = geometry.map;
        if (!SDL_SetRenderClipRect(renderer, &geometry.mapViewport)) { return false; }
        const auto mark = [&](TileCoord position, bool cursor) {
            const int x = position.x - view->origin.x, z = position.z - view->origin.z;
            if (x < 0 || z < 0 || x >= view->width || z >= view->height) { return true; }
            SDL_FRect box{static_cast<float>(layout.x + x * layout.cellSize),
                static_cast<float>(layout.y + z * layout.cellHeight), static_cast<float>(layout.cellSize), static_cast<float>(layout.cellHeight)};
            return SDL_SetRenderDrawColor(renderer, cursor ? 255 : 80, cursor ? 230 : 220, cursor ? 70 : 180, 255)
                && SDL_RenderRect(renderer, &box);
        };
        for (auto target : frame.highlights) { if (!mark(target, false)) { return false; } }
        if (frame.cursor && !mark(*frame.cursor, true)) { return false; }
    }
    if (!SDL_SetRenderClipRect(renderer, nullptr) || !SDL_SetRenderScale(renderer, uiScale, uiScale)) { return false; }
    const int w = static_cast<int>(width / uiScale), h = static_cast<int>(height / uiScale);
    const char* footer = footerOverride ? footerOverride : frame.footer.c_str();
    if (frame.presentation == ScreenPresentation::Anchored) {
        return DrawAnchored(renderer, text, frame, geometry, view, uiScale, w, h, footer);
    }
    const int left = 8;
    const int top = geometry.topBar.h;
    SDL_FRect panel{static_cast<float>(left), static_cast<float>(top), static_cast<float>(w - left - 8), static_cast<float>(h - top - 8)};
    if (!SDL_SetRenderDrawColor(renderer, 16, 24, 29, 255) || !SDL_RenderFillRect(renderer, &panel)) { return false; }
    const SDL_Color normal{210,220,218,255}, dim{115,125,130,255}, selected{255,230,90,255};
    const SDL_Rect clip{left + 6, top + 4, std::max(1, w - left - 20), std::max(1, h - top - 16)};
    const int lineHeight = std::max(1, text.LineHeight());
    const auto titleColor = frame.scopeTone == SettingsTone::Normal ? selected : ToneColor(frame.scopeTone);
    if (!text.Draw(8, frame.title.c_str(), -10000, -10000, titleColor, clip.w)) { return false; }
    const bool compact = !frame.compactTitle.empty() && text.Height(8) > clip.h / 3;
    const char* title = compact ? frame.compactTitle.c_str() : frame.title.c_str();
    if (!text.Draw(8, title, -10000, -10000, titleColor, clip.w)) { return false; }
    const int titleHeight = std::min(text.Height(8), std::max(lineHeight, clip.h / 3));
    const SDL_Rect titleClip{clip.x, top + 6, clip.w, titleHeight};
    if (!SDL_SetRenderClipRect(renderer, &titleClip) || !text.Draw(8, title, left + 8, top + 6, titleColor, clip.w)) { return false; }
    int y = top + titleHeight + 16;
    if (!frame.status.empty() && clip.h >= 5 * lineHeight) {
        const SDL_Rect statusClip{clip.x, y - 4, clip.w, lineHeight};
        if (!SDL_SetRenderClipRect(renderer, &statusClip)
            || !text.Draw(12, frame.status.c_str(), left + 8, y - 4, ToneColor(frame.statusTone), clip.w)) { return false; }
        y += lineHeight;
    }
    if (!text.Draw(9,footer,-10000,-10000,dim,clip.w)) { return false; }
    if (!footerOverride && !frame.compactFooter.empty() && text.Height(9) > clip.h / 3) {
        footer = frame.compactFooter.c_str();
        if (!text.Draw(9,footer,-10000,-10000,dim,clip.w)) { return false; }
    }
    const int footerHeight = std::min(text.Height(9), std::max(lineHeight, clip.h / 3));
    const int footerY = std::max(y,h-footerHeight-16);
    const int bodyHeight = std::max(1, footerY - y - 8);
    int listWidth = clip.w;
    const bool settingsDetails = !frame.settingsDetails.empty();
    const bool hasDetails = settingsDetails || !frame.details.empty();
    const bool columns = w >= 850 && hasDetails;
    if (columns) { listWidth = settingsDetails ? clip.w * 2 / 3 : clip.w / 2; }
    if (frame.debugImage || frame.worldgenPreview) {
        const int mapHeight = !columns && !frame.details.empty() ? std::max(1,(bodyHeight-8)/2) : bodyHeight;
        const SDL_Rect mapClip{clip.x,y,listWidth,mapHeight};
        const int previewCell = std::max(8, lineHeight);
        if (frame.worldgenPreview) {
            // Odd dimensions keep selection in the central cell. Edge cells
            // may be partly clipped, but no offscreen region image is built.
            frame.debugImage = &frame.worldgenPreview->Prepare(
                std::max(1, (listWidth + previewCell - 1) / previewCell) | 1,
                std::max(1, (mapHeight + previewCell - 1) / previewCell) | 1);
        }
        const auto& image = *frame.debugImage;
        if (!SDL_SetRenderClipRect(renderer,&mapClip)) { return false; }
        const float cell = frame.worldgenPreview ? static_cast<float>(previewCell)
            : std::min(static_cast<float>(listWidth) / std::max(1,image.width),
                static_cast<float>(mapHeight) / std::max(1,image.height));
        const bool hasCursor = frame.cursor && Contains(image.bounds, *frame.cursor);
        const int cursorColumn = hasCursor ? (frame.cursor->x - image.bounds.min.x) / image.scale : 0;
        const int cursorRow = hasCursor ? (frame.cursor->z - image.bounds.min.z) / image.scale : 0;
        const float originX = frame.worldgenPreview && hasCursor
            ? mapClip.x + mapClip.w / 2.0f - (cursorColumn + 0.5f) * cell : static_cast<float>(left + 8);
        const float originY = frame.worldgenPreview && hasCursor
            ? mapClip.y + mapClip.h / 2.0f - (cursorRow + 0.5f) * cell : static_cast<float>(y);
        const bool glyph = cell >= 8;
        for (int z = 0; z < image.height; ++z) {
            for (int x = 0; x < image.width; ++x) {
                const auto& appearance = image.cells[static_cast<std::size_t>(z * image.width + x)];
                SDL_FRect box{originX + x * cell, originY + z * cell, cell, cell};
                const auto color = glyph || appearance.background != RgbColor{}
                    ? appearance.background : appearance.foreground;
                if (!SDL_SetRenderDrawColor(renderer, color.red, color.green, color.blue, 255)
                    || !SDL_RenderFillRect(renderer, &box)) { return false; }
            }
        }
        if (glyph) {
            const float scale = cell / lineHeight;
            if (!SDL_SetRenderScale(renderer,uiScale*scale,uiScale*scale)) { return false; }
            for (int z = 0; z < image.height; ++z) {
                for (int x = 0; x < image.width; ++x) {
                    const auto& appearance = image.cells[static_cast<std::size_t>(z * image.width + x)];
                    if (appearance.symbol.empty() || appearance.symbol == " ") { continue; }
                    const float cellX = originX + x * cell, cellY = originY + z * cell;
                    const float minX = std::max(cellX, static_cast<float>(mapClip.x));
                    const float minY = std::max(cellY, static_cast<float>(mapClip.y));
                    const float maxX = std::min(cellX + cell, static_cast<float>(mapClip.x + mapClip.w));
                    const float maxY = std::min(cellY + cell, static_cast<float>(mapClip.y + mapClip.h));
                    if (minX >= maxX || minY >= maxY) { continue; }
                    const SDL_Rect glyphClip{static_cast<int>(minX/scale),static_cast<int>(minY/scale),
                        std::max(1,static_cast<int>((maxX-minX)/scale)),
                        std::max(1,static_cast<int>((maxY-minY)/scale))};
                    // Reuse the bounded glyph slots already used for text art.
                    // Unlike slot 7 alternating "M"/symbol, repeated glyphs keep
                    // their text layout across cells and frames.
                    unsigned hash = 2166136261U;
                    for (const unsigned char byte : appearance.symbol) { hash = (hash ^ byte) * 16777619U; }
                    const auto fg = appearance.foreground;
                    if (!SDL_SetRenderClipRect(renderer,&glyphClip)
                        || !text.Draw(256 + hash % 64,appearance.symbol.c_str(),cellX/scale+2,cellY/scale,
                            {fg.red,fg.green,fg.blue,255})) { return false; }
                }
            }
            if (!SDL_SetRenderScale(renderer,uiScale,uiScale) || !SDL_SetRenderClipRect(renderer,&mapClip)) { return false; }
        }
        if (hasCursor) {
            const SDL_FRect box{originX + cursorColumn * cell, originY + cursorRow * cell, cell, cell};
            if (!SDL_SetRenderDrawColor(renderer,255,255,255,255) || !SDL_RenderRect(renderer,&box)) { return false; }
        }
        if (!columns) { y += mapHeight + 8; }
    } else if (frame.scrollText) {
        String body;
        for (const auto& row : frame.rows) { body += row.text; body += '\n'; }
        if (!text.Draw(6, body.c_str(), -10000, -10000, normal, listWidth - 6)) { return false; }
        const int maxScroll = std::max(0, (text.Height(6) - bodyHeight + lineHeight - 1) / lineHeight);
        frame.scroll = std::clamp(frame.scroll, 0, maxScroll);
        const SDL_Rect bodyClip{clip.x, y, listWidth, bodyHeight};
        if (!SDL_SetRenderClipRect(renderer, &bodyClip)
            || !text.Draw(6, body.c_str(), left + 8, y - frame.scroll * lineHeight, normal, listWidth - 6)) { return false; }
    } else if (frame.structuredRows) {
        SDL_Rect listClip{clip.x, y, listWidth, bodyHeight};
        if (!DrawStructuredRows(renderer, text, frame, listClip, !columns && hasDetails)) { return false; }
        if (!columns) { y += listClip.h + 8; }
    } else {
        if (!text.Draw(7,"M",-100,-100,normal)) { return false; }
        const int rowHeight = std::max(24,text.Height(7) * 2 + 6);
        int listHeight = bodyHeight;
        if (!columns && !frame.details.empty()) { listHeight = std::min(bodyHeight, std::max(rowHeight, bodyHeight / 2)); }
        const int count = std::max(1, listHeight / rowHeight);
        if (frame.selected >= 0) {
            if (frame.selected < frame.scroll) { frame.scroll = frame.selected; }
            if (frame.selected >= frame.scroll + count) { frame.scroll = frame.selected - count + 1; }
        }
        frame.scroll = std::clamp(frame.scroll, 0, std::max(0, static_cast<int>(frame.rows.size()) - count));
        SDL_Rect listClip{clip.x,y,listWidth,listHeight};
        if (!SDL_SetRenderClipRect(renderer,&listClip)) { return false; }
        for (int i = 0; i < count && frame.scroll + i < static_cast<int>(frame.rows.size()); ++i) {
            const int index = frame.scroll + i;
            const auto& row = frame.rows[index];
            String label = index == frame.selected ? "> " : "  "; label += row.text;
            if (!text.Draw(10 + i % 48, label.c_str(), left + 8, y + i * rowHeight,
                row.unavailable ? dim : index == frame.selected ? selected : normal, listWidth - 6)) { return false; }
        }
        if (!columns) { y += listHeight + 8; }
    }
    if (settingsDetails && footerY - y >= lineHeight) {
        const int x = columns ? clip.x + listWidth + 12 : clip.x + 2;
        const SDL_Rect detailClip{x, y, clip.x + clip.w - x, footerY - y};
        if (!DrawSettingsDetails(renderer, text, frame, detailClip)) { return false; }
    } else if (!frame.details.empty() && footerY - y >= (frame.structuredRows ? lineHeight : 1)) {
        String details;
        const auto start = frame.structuredRows ? 0 : std::min<std::size_t>(std::max(0, frame.detailScroll), frame.details.size() - 1);
        for (auto i = start; i < frame.details.size(); ++i) { details += frame.details[i]; details += "\n"; }
        const int x = columns ? left + 12 + listWidth : left + 8;
        const SDL_Rect detailClip{x,y,std::max(1,clip.w - (columns ? listWidth : 0)),std::max(1,footerY-y)};
        if (!SDL_SetRenderClipRect(renderer,&detailClip)) { return false; }
        if (frame.structuredRows) {
            if (!text.Draw(6, details.c_str(), -10000, -10000, normal, detailClip.w)) { return false; }
            const int maxScroll = std::max(0, (text.Height(6) - detailClip.h + lineHeight - 1) / lineHeight);
            frame.detailScroll = std::clamp(frame.detailScroll, 0, maxScroll);
        }
        if (!text.Draw(6, details.c_str(), x, frame.structuredRows ? y - frame.detailScroll * lineHeight : y,
            normal, detailClip.w)) { return false; }
    }
    const SDL_Rect footerClip{clip.x, footerY, clip.w, footerHeight};
    if (!SDL_SetRenderClipRect(renderer,&footerClip) || !text.Draw(9,footer,left+8,footerY,dim,clip.w)) { return false; }
    return SDL_SetRenderClipRect(renderer,nullptr) && SDL_SetRenderScale(renderer,1,1);
}

eastl::optional<std::size_t> SoleAvailableAction(const Vector<InteractionChoice>& actions) {
    eastl::optional<std::size_t> found;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (actions[i].reason != ObjectUseReason::None) { continue; }
        if (found) { return {}; }
        found = i;
    }
    return found;
}

void OptionsUi::Open(const SettingsStore& store, const Settings& runtime, bool remote) {
    editor_.Open(store, runtime, true, remote);
}
void OptionsUi::Applied(const SettingsStore& store, const Settings& runtime) {
    editor_.Applied(store, runtime);
}
OptionsEvent OptionsUi::Key(const KeyChord& chord, const SettingsStore& store, const Localization& loc, const char* directory) {
    return editor_.Key(chord, store, loc, directory);
}
void OptionsUi::Text(eastl::string_view text) { editor_.Text(text); }
SettingsResult<SettingsStore> OptionsUi::Prepare(const SettingsStore& store, Diagnostics* diagnostics) const {
    return editor_.Prepare(store, diagnostics);
}
void OptionsUi::RememberScroll(const ScreenFrame& frame) {
    editor_.ListScroll() = frame.scroll;
    editor_.DetailScroll() = frame.detailScroll;
}
void OptionsUi::Present(const SettingsStore& store, const Localization& loc, ScreenFrame& frame) const {
    auto menu = editor_.Present(store, loc);
    frame.visible = true;
    frame.presentation = ScreenPresentation::Large;
    frame.structuredRows = true;
    const auto scope = loc.Text(Scope() == SettingsScope::Global ? "settings.global" : "settings.world");
    frame.title = loc.Text("settings.title") + "  ·  " + scope + "   " + menu.tabs;
    frame.compactTitle = menu.activeTab;
    frame.status = eastl::move(menu.status);
    frame.statusTone = menu.statusTone;
    frame.scopeTone = menu.scopeTone;
    frame.footer.clear();
    for (const auto& hint : menu.hints) {
        if (!frame.footer.empty()) { frame.footer += '\n'; }
        frame.footer += hint;
    }
    frame.compactFooter = eastl::move(menu.compactHint);
    frame.rows.clear();
    for (auto& row : menu.rows) {
        frame.rows.push_back({eastl::move(row.label), row.readOnly, {}, eastl::move(row.value), row.tone, row.divider, row.button});
    }
    frame.details.clear();
    frame.settingsDetails = eastl::move(menu.details);
    frame.selected = menu.selected;
    frame.scroll = editor_.ListScroll();
    frame.detailScroll = editor_.DetailScroll();
}
}
