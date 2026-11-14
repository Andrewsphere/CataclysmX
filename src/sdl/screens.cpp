#include "sdl/screens.h"
#include "sdl/renderer.h"

#include <algorithm>
#include <iterator>

namespace cataclysmx::sdl {
namespace {
constexpr InputContext contexts[] = {InputContext::Gameplay, InputContext::Interaction, InputContext::Menu,
    InputContext::Debug, InputContext::Items};
constexpr SettingsField fields[] = {SettingsField::Language, SettingsField::ViewRadius,
    SettingsField::KeyboardRepeatRate, SettingsField::MessageHistorySize, SettingsField::Seed, SettingsField::ActiveRadius, SettingsField::ResidentRadius,
    SettingsField::ResidencyMode, SettingsField::ResidentRadiusTiles, SettingsField::TicksPerSecond,
    SettingsField::RateLimited, SettingsField::PauseWhenIdle, SettingsField::AutosaveInterval};
constexpr unsigned categories[] = {0,0,0,0,1,1,1,1,1,2,2,2,3};
constexpr const char* categoryNames[] = {"interface", "world", "timing", "saving"};
String Local(const Localization& loc, eastl::string_view prefix, eastl::string_view suffix) {
    String key(prefix.data(), prefix.size()); key.append(suffix.data(), suffix.size());
    return loc.Text({key.data(), key.size()}, suffix);
}
String Tab(const char* name, bool selected, bool focused) {
    String result = focused ? "> " : "";
    result += selected ? "[" : " "; result += name; result += selected ? "] " : "  ";
    return result;
}

bool DrawAnchored(SDL_Renderer* renderer, SdlText& text, ScreenFrame& frame,
    const GameplayLayout& gameplay, const WorldView* view, float uiScale) {
    bool hasText = false;
    for (const auto& row : frame.rows) { hasText |= !row.text.empty(); }
    for (const auto& detail : frame.details) { hasText |= !detail.empty(); }
    if (!hasText) { return SDL_SetRenderClipRect(renderer, nullptr) && SDL_SetRenderScale(renderer, 1, 1); }
    const SDL_Color normal{210, 220, 218, 255}, dim{145, 160, 163, 255}, selected{255, 230, 90, 255};
    auto viewport = ScaleRect(gameplay.mapViewport, 1.0f / uiScale);
    if (viewport.w < 24 || viewport.h < 24) { return true; }
    viewport.x += 4; viewport.y += 4; viewport.w -= 8; viewport.h -= 8;
    SDL_Rect target{viewport.x + viewport.w / 2, viewport.y + viewport.h / 2, 1, 1}, player{};
    if (view) {
        if (frame.cursor) { target = ScaleRect(MapCellRect(gameplay.map,
            frame.cursor->x - view->origin.x, frame.cursor->z - view->origin.z), 1.0f / uiScale); }
        player = ScaleRect(MapCellRect(gameplay.map, view->player_position.x - view->origin.x,
            view->player_position.z - view->origin.z), 1.0f / uiScale);
    }
    const int heightLimit = std::max(1, viewport.h * 60 / 100);
    const auto provisional = PlacePopup(viewport, target, std::min(320, viewport.w), heightLimit, player);
    const int wrap = std::max(1, provisional.w - 16);
    if (!text.Draw(7, "M", -10000, -10000, normal)) { return false; }
    const int lineHeight = std::max(1, text.Height(7));
    if (!text.Draw(8, frame.title.c_str(), -10000, -10000, selected, wrap)
        || !text.Draw(9, frame.footer.c_str(), -10000, -10000, dim, wrap)) { return false; }
    const int titleHeight = std::min(text.Height(8), lineHeight * 2);
    const int footerHeight = std::min(text.Height(9), lineHeight * 2);
    int contentHeight = 0;
    frame.rowHeights.resize(frame.rows.size());
    for (std::size_t i = 0; i < frame.rows.size(); ++i) {
        if (!text.Draw(7, frame.rows[i].text.c_str(), -10000, -10000, normal, std::max(1, wrap - 14))) { return false; }
        frame.rowHeights[i] = std::max(lineHeight, text.Height(7)) + 4;
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
        String content;
        for (const auto& row : frame.rows) { if (!content.empty()) { content += '\n'; } content += row.text; }
        if (!details.empty()) { content += '\n'; content += details; }
        if (!text.Draw(6, content.c_str(), -10000, -10000, normal, wrap)) { return false; }
        const int maxScroll = std::max(0, (text.Height(6) - bodyClip.h + lineHeight - 1) / lineHeight);
        frame.scroll = std::clamp(frame.scroll, 0, maxScroll);
        if (!SDL_SetRenderClipRect(renderer, &bodyClip)
            || !text.Draw(6, content.c_str(), float(bodyClip.x), float(bodyY - frame.scroll * lineHeight), normal, wrap)) { return false; }
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
        && text.Draw(9, frame.footer.c_str(), float(panel.x + 8), float(footerY), dim, wrap)
        && SDL_SetRenderClipRect(renderer, nullptr) && SDL_SetRenderScale(renderer, 1, 1);
}

}

bool DrawScreen(SDL_Renderer* renderer, SdlText& text, ScreenFrame& frame,
    int width, int height, const WorldView* view, int tileWidth, int tileHeight, int zoom, float uiScale,
    const GameplayLayout* gameplay) {
    if (!frame.visible) { return true; }
    if (!SDL_SetRenderScale(renderer, 1, 1) || !SDL_SetRenderClipRect(renderer, nullptr)) { return false; }
    const auto focus = frame.followCursor && frame.cursor && view
        ? SDL_Point{frame.cursor->x - view->player_position.x, frame.cursor->z - view->player_position.z} : SDL_Point{};
    const auto calculated = gameplay ? GameplayLayout{} : MakeGameplayLayout(width, height,
        view ? view->width : 1, view ? view->height : 1, tileWidth, tileHeight, zoom, uiScale, focus);
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
    if (frame.presentation == ScreenPresentation::Anchored) { return DrawAnchored(renderer, text, frame, geometry, view, uiScale); }
    const int w = static_cast<int>(width / uiScale), h = static_cast<int>(height / uiScale);
    const int left = 8;
    const int top = geometry.topBar.h;
    SDL_FRect panel{static_cast<float>(left), static_cast<float>(top), static_cast<float>(w - left - 8), static_cast<float>(h - top - 8)};
    if (!SDL_SetRenderDrawColor(renderer, 16, 24, 29, 255) || !SDL_RenderFillRect(renderer, &panel)) { return false; }
    const SDL_Color normal{210,220,218,255}, dim{115,125,130,255}, selected{255,230,90,255};
    const SDL_Rect clip{left + 6, top + 4, std::max(1, w - left - 20), std::max(1, h - top - 16)};
    if (!SDL_SetRenderClipRect(renderer, &clip) || !text.Draw(8, frame.title.c_str(), left + 8, top + 6, selected, clip.w)) { return false; }
    int y = top + text.Height(8) + 16;
    if (!text.Draw(9,frame.footer.c_str(),-10000,-10000,dim,clip.w)) { return false; }
    const int footerHeight = std::min(text.Height(9),std::max(48,h/3));
    const int footerY = std::max(y,h-footerHeight-16);
    const int bodyHeight = std::max(1, footerY - y - 8);
    int listWidth = clip.w;
    const bool columns = w >= 850 && !frame.details.empty();
    if (columns) { listWidth = clip.w / 2; }
    if (frame.debugImage) {
        const auto& image = *frame.debugImage;
        const int mapHeight = !columns && !frame.details.empty() ? std::max(1,(bodyHeight-8)/2) : bodyHeight;
        const SDL_Rect mapClip{clip.x,y,listWidth,mapHeight};
        if (!SDL_SetRenderClipRect(renderer,&mapClip)) { return false; }
        const float cell = std::min(static_cast<float>(listWidth) / std::max(1,image.width),
            static_cast<float>(mapHeight) / std::max(1,image.height));
        for (int z = 0; z < image.height; ++z) {
            for (int x = 0; x < image.width; ++x) {
                const auto& appearance = image.cells[static_cast<std::size_t>(z * image.width + x)];
                SDL_FRect box{left + 8 + x * cell, y + z * cell, cell, cell};
                const bool glyph = cell >= 8;
                const auto color = glyph || appearance.background != RgbColor{}
                    ? appearance.background : appearance.foreground;
                if (!SDL_SetRenderDrawColor(renderer, color.red, color.green, color.blue, 255)
                    || !SDL_RenderFillRect(renderer, &box)) { return false; }
                if (glyph) {
                    if (!text.Draw(7,"M",-10000,-10000,normal)) { return false; }
                    const float scale = cell / std::max(1,text.Height(7));
                    const SDL_Rect glyphClip{static_cast<int>(box.x/scale),static_cast<int>(box.y/scale),
                        static_cast<int>(cell/scale)+1,static_cast<int>(cell/scale)+1};
                    const auto fg = appearance.foreground;
                    if (!SDL_SetRenderScale(renderer,uiScale*scale,uiScale*scale) || !SDL_SetRenderClipRect(renderer,&glyphClip)
                        || !text.Draw(7,appearance.symbol.c_str(),box.x/scale+3,box.y/scale,{fg.red,fg.green,fg.blue,255})
                        || !SDL_SetRenderScale(renderer,uiScale,uiScale) || !SDL_SetRenderClipRect(renderer,&mapClip)) { return false; }
                }
                if (frame.cursor && (frame.cursor->x - image.bounds.min.x) / image.scale == x
                    && (frame.cursor->z - image.bounds.min.z) / image.scale == z) {
                    if (!SDL_SetRenderDrawColor(renderer,255,255,255,255) || !SDL_RenderRect(renderer,&box)) { return false; }
                }
            }
        }
        if (!columns) { y += mapHeight + 8; }
    } else {
        if (!text.Draw(7,"M",-100,-100,normal)) { return false; }
        const int rowHeight = std::max(24,text.Height(7) * 2 + 6);
        int listHeight = bodyHeight;
        if (!columns && !frame.details.empty()) { listHeight = std::max(rowHeight, bodyHeight / 2); }
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
    if (!frame.details.empty()) {
        String details;
        const auto start = std::min<std::size_t>(std::max(0, frame.detailScroll), frame.details.size() - 1);
        for (auto i = start; i < frame.details.size(); ++i) { details += frame.details[i]; details += "\n"; }
        const int x = columns ? left + 12 + listWidth : left + 8;
        const SDL_Rect detailClip{x,y,std::max(1,clip.w - (columns ? listWidth : 0)),std::max(1,footerY-y)};
        if (!SDL_SetRenderClipRect(renderer,&detailClip) || !text.Draw(6,details.c_str(),x,y,normal,detailClip.w)) { return false; }
    }
    if (!SDL_SetRenderClipRect(renderer,&clip) || !text.Draw(9,frame.footer.c_str(),left+8,footerY,dim,clip.w)) { return false; }
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

void OptionsUi::Open(const SettingsStore& store, const Settings& runtime) {
    runtime_ = runtime;
    for (unsigned i = 0; i < 2; ++i) {
        const auto scope = i == 0 ? SettingsScope::Global : SettingsScope::World;
        drafts_[i] = store.Overrides(scope); keys_[i] = store.KeyOverrides(scope);
    }
    mode_ = Mode::Browse; feedback_.clear(); selected_ = 0; detailScroll_ = 0;
}
void OptionsUi::Applied(const SettingsStore& store, const Settings& runtime, bool reloaded) {
    runtime_ = runtime;
    if (reloaded) {
        for (unsigned i = 0; i < 2; ++i) {
            const auto scope = i == 0 ? SettingsScope::Global : SettingsScope::World;
            drafts_[i] = store.Overrides(scope); keys_[i] = store.KeyOverrides(scope);
        }
    } else {
        drafts_[Index()] = store.Overrides(scope_); keys_[Index()] = store.KeyOverrides(scope_);
    }
    mode_ = Mode::Browse;
}
Settings OptionsUi::Preview(const SettingsStore& store) const {
    auto global = scope_ == SettingsScope::Global ? drafts_[0] : store.Overrides(SettingsScope::Global);
    const auto world = scope_ == SettingsScope::World ? drafts_[1] : SettingsOverrides{};
    if (!HasResidencyOverride(global) && !HasResidencyOverride(world)) {
        auto result = ResolveSettings(global,world);
        result.activeRadius = runtime_.activeRadius; result.residencyMode = runtime_.residencyMode;
        result.residentRadius = runtime_.residentRadius; result.residentRadiusTiles = runtime_.residentRadiusTiles;
        return result;
    }
    return ResolveSettings(global, world);
}
Keymap OptionsUi::PreviewKeys(const SettingsStore& store) const {
    return scope_ == SettingsScope::Global ? ResolveKeymap(keys_[0],{}) : ResolveKeymap(store.KeyOverrides(SettingsScope::Global),keys_[1]);
}
void OptionsUi::Fields(Vector<SettingsField>& output) const {
    output.clear();
    for (std::size_t i = 0; i < std::size(fields); ++i) { if (categories[i] == category_) { output.push_back(fields[i]); } }
}
void OptionsUi::Bindings(const SettingsStore& store, Keymap& output) const {
    output.clear();
    for (const auto& binding : PreviewKeys(store)) { if (binding.context == contexts[category_]) { output.push_back(binding); } }
}
void OptionsUi::Begin(const SettingsStore& store, const char* directory, bool append) {
    if (bindings_) { append_ = append; mode_ = Mode::Capture; return; }
    Vector<SettingsField> visible; Fields(visible);
    if (selected_ >= visible.size()) { return; }
    const auto field = visible[selected_];
    const auto preview = Preview(store);
    if (field == SettingsField::Seed && scope_ == SettingsScope::World) { return; }
    if (field == SettingsField::RateLimited || field == SettingsField::PauseWhenIdle) {
        const auto result = SetOverride(drafts_[Index()],field,SettingValue(preview,field) == "true" ? "false" : "true");
        if (!result) { feedback_ = "Invalid value"; }
    } else if (field == SettingsField::ResidencyMode) {
        auto& draft = drafts_[Index()];
        draft.residencyMode = preview.residencyMode == ResidencyMode::Circle ? ResidencyMode::Square : ResidencyMode::Circle;
        if (*draft.residencyMode == ResidencyMode::Square) { draft.residentRadiusTiles.reset(); }
        else { draft.residentRadius.reset(); }
    } else if (field == SettingsField::Language) {
        languages_ = AvailableLanguages(directory); language_ = 0;
        for (std::size_t i = 0; i < languages_.size(); ++i) { if (languages_[i] == preview.language) { language_ = i; } }
        mode_ = Mode::Language;
    } else { mode_ = Mode::Number; edit_ = SettingValue(preview,field); replace_ = true; }
}
void OptionsUi::Text(eastl::string_view text) {
    if (mode_ != Mode::Number) { return; }
    if (replace_) { edit_.clear(); replace_ = false; }
    if (edit_.size() + text.size() <= 128) { edit_.append(text.data(),text.size()); }
}
OptionsEvent OptionsUi::Key(const KeyChord& chord, const SettingsStore& store, const Localization& loc, const char* directory) {
    const auto action = ResolveAction(store.Keys(),InputContext::Menu,chord);
    if (chord.key == "Escape" && !chord.ctrl && !chord.alt && !chord.shift) {
        if (mode_ != Mode::Browse) { mode_ = Mode::Browse; return OptionsEvent::None; }
        return OptionsEvent::Close;
    }
    if (mode_ == Mode::Inspect) { feedback_ = ChordName(chord); return OptionsEvent::None; }
    if (mode_ == Mode::Capture) {
        Keymap visible; Bindings(store,visible);
        if (selected_ >= visible.size()) { return OptionsEvent::None; }
        auto binding = visible[selected_];
        if (!append_) { binding.keys.clear(); }
        binding.keys.push_back(chord);
        auto candidate = keys_[Index()]; SetBinding(candidate,eastl::move(binding));
        const auto resolved = scope_ == SettingsScope::Global ? ResolveKeymap(candidate,{}) : ResolveKeymap(store.KeyOverrides(SettingsScope::Global),candidate);
        Diagnostics diagnostics;
        if (ValidateKeymap(resolved,&diagnostics)) { keys_[Index()] = eastl::move(candidate); mode_ = Mode::Browse; feedback_.clear(); }
        else { feedback_ = FormatDiagnostics(diagnostics); }
        return OptionsEvent::None;
    }
    if (mode_ == Mode::Number) {
        if (chord.key == "Backspace" && !chord.ctrl) {
            if (!edit_.empty()) { edit_.pop_back(); } replace_ = false;
        } else if (chord.key == "Enter" && !chord.ctrl) {
            Vector<SettingsField> visible; Fields(visible);
            auto candidate = drafts_[Index()];
            const auto field = visible[selected_];
            if (field == SettingsField::ResidentRadius) { candidate.residentRadiusTiles.reset(); candidate.residencyMode.reset(); }
            if (field == SettingsField::ResidentRadiusTiles) { candidate.residentRadius.reset(); candidate.residencyMode.reset(); }
            Diagnostics diagnostics;
            if (SetOverride(candidate,field,{edit_.data(),edit_.size()},&diagnostics) && ValidateOverrides(candidate,&diagnostics)) {
                drafts_[Index()] = eastl::move(candidate); mode_ = Mode::Browse; feedback_.clear();
            } else { feedback_ = FormatDiagnostics(diagnostics); }
        }
        return OptionsEvent::None;
    }
    if (mode_ == Mode::Language) {
        if (languages_.empty()) { mode_ = Mode::Browse; return OptionsEvent::None; }
        if (action == InputAction::North) { language_ = (language_ + languages_.size() - 1) % languages_.size(); }
        if (action == InputAction::South) { language_ = (language_ + 1) % languages_.size(); }
        if (action == InputAction::Accept) { drafts_[Index()].language = languages_[language_]; mode_ = Mode::Browse; }
        return OptionsEvent::None;
    }
    if (!action) { return OptionsEvent::None; }
    switch (*action) {
    case InputAction::Next: focus_ = (focus_ + 1) % 5; break;
    case InputAction::Previous: focus_ = (focus_ + 4) % 5; break;
    case InputAction::West: case InputAction::East: {
        const int delta = *action == InputAction::West ? -1 : 1;
        if (focus_ == 0) { scope_ = scope_ == SettingsScope::Global ? SettingsScope::World : SettingsScope::Global; }
        if (focus_ == 1) { bindings_ = !bindings_; category_ = 0; }
        if (focus_ == 2) { const unsigned count = bindings_ ? std::size(contexts) : std::size(categoryNames); category_ = (category_ + count + delta) % count; }
        selected_ = 0; detailScroll_ = 0; break;
    }
    case InputAction::North: case InputAction::South: {
        const bool down = *action == InputAction::South;
        if (focus_ == 4) { detailScroll_ = std::max(0,detailScroll_ + (down ? 1 : -1)); break; }
        if (focus_ == 2) { const unsigned count = bindings_ ? std::size(contexts) : std::size(categoryNames); category_ = (category_ + count + (down ? 1 : -1)) % count; selected_ = 0; break; }
        Vector<SettingsField> visible; Keymap bindings;
        Fields(visible); Bindings(store,bindings);
        const auto count = bindings_ ? bindings.size() : visible.size();
        if (count) { selected_ = (selected_ + count + (down ? 1 : -1)) % count; }
        detailScroll_ = 0; break;
    }
    case InputAction::Accept: if (focus_ != 3) { focus_ = 3; } else { Begin(store,directory,false); } break;
    case InputAction::AddBinding: if (bindings_) { Begin(store,directory,true); } break;
    case InputAction::Unbind: {
        if (bindings_) { Keymap visible; Bindings(store,visible); auto binding = visible[selected_]; binding.keys.clear(); SetBinding(keys_[Index()],eastl::move(binding)); }
        break;
    }
    case InputAction::Reset:
        if (bindings_) { Keymap visible; Bindings(store,visible); ResetBinding(keys_[Index()],visible[selected_].context,visible[selected_].action); }
        else { Vector<SettingsField> visible; Fields(visible); if (visible[selected_] != SettingsField::Seed || scope_ != SettingsScope::World) { ResetOverride(drafts_[Index()],visible[selected_]); } }
        break;
    case InputAction::Inspect: mode_ = Mode::Inspect; feedback_ = loc.Text("settings.inspecting","Press a key to inspect"); break;
    case InputAction::Apply: return OptionsEvent::Save;
    case InputAction::SaveSettings: return OptionsEvent::Save;
    case InputAction::Reload: break;
    default: break;
    }
    return OptionsEvent::None;
}
SettingsResult<void> OptionsUi::Prepare(const SettingsStore& store, SettingsStore& candidate, Diagnostics* diagnostics) const {
    candidate = store;
    return candidate.SetOverrides(scope_,drafts_[Index()],keys_[Index()],diagnostics);
}
void OptionsUi::Present(const SettingsStore& store, const Localization& loc, ScreenFrame& frame) const {
    frame.visible = true; frame.presentation = ScreenPresentation::Large; frame.title = loc.Text("settings.title","Options");
    frame.title += "\n";
    frame.title += Tab("Global",scope_ == SettingsScope::Global,focus_ == 0);
    frame.title += Tab("World",scope_ == SettingsScope::World,focus_ == 0);
    frame.title += " | "; frame.title += Tab("Preferences",!bindings_,focus_ == 1); frame.title += Tab("Keybindings",bindings_,focus_ == 1);
    frame.title += "\n";
    const unsigned count = bindings_ ? std::size(contexts) : std::size(categoryNames);
    for (unsigned i = 0; i < count; ++i) {
        const auto name = bindings_ ? Local(loc,"context.",ContextName(contexts[i])) : Local(loc,"settings.ui.category.",categoryNames[i]);
        frame.title += Tab(name.c_str(),i == category_,focus_ == 2);
    }
    const bool pending = drafts_[Index()] != store.Overrides(scope_) || keys_[Index()] != store.KeyOverrides(scope_);
    frame.title += pending ? "\nUnapplied edits" : store.Dirty(scope_) ? "\nApplied, unsaved" : "";
    frame.rows.clear(); frame.details.clear(); frame.selected = static_cast<int>(selected_); frame.detailScroll = detailScroll_;
    const auto preview = Preview(store);
    if (bindings_) {
        Keymap visible; Bindings(store,visible);
        const auto all = PreviewKeys(store);
        for (const auto& binding : visible) {
            String row = Local(loc,"action.",ActionName(binding.action)); row += "   "; row += BindingHint(all,binding.context,binding.action);
            frame.rows.push_back({eastl::move(row)});
        }
        if (selected_ < visible.size()) { frame.details.push_back(Local(loc,"settings.ui.action_help.",ActionName(visible[selected_].action))); }
    } else {
        Vector<SettingsField> visible; Fields(visible);
        for (auto field : visible) {
            String row = field == SettingsField::Seed && scope_ == SettingsScope::World
                ? loc.Text("settings.ui.world_seed","World seed") : Local(loc,"settings.ui.label.",SettingName(field));
            row += "   ";
            row += field == SettingsField::Seed && scope_ == SettingsScope::World ? UnsignedText(runtime_.seed) : SettingValue(preview,field);
            row += HasOverride(drafts_[Index()],field) ? " [override]" : " [inherited]";
            if ((field == SettingsField::ResidentRadius && preview.residencyMode == ResidencyMode::Circle)
                || (field == SettingsField::ResidentRadiusTiles && preview.residencyMode == ResidencyMode::Square)) { row += " [inactive]"; }
            frame.rows.push_back({eastl::move(row),field == SettingsField::Seed && scope_ == SettingsScope::World});
        }
        if (selected_ < visible.size()) {
            const auto name = SettingName(visible[selected_]);
            frame.details.push_back(Local(loc,"settings.ui.description.",name));
            frame.details.push_back(Local(loc,"settings.ui.constraint.",name));
            frame.details.push_back(Local(loc,"settings.ui.effect.",name));
            frame.details.push_back(visible[selected_] == SettingsField::Seed ? "Seed applies when creating a world." : "Takes effect after Save and apply.");
        }
    }
    if (focus_ == 4) { frame.details.insert(frame.details.begin(),String("> Details | Up/Down: scroll")); }
    if (scope_ == SettingsScope::World && store.WorldDirectory().empty()) { frame.details.push_back("World overrides are memory-only in this temporary SDL world."); }
    if (mode_ == Mode::Number) { frame.details.push_back("Value: " + edit_ + " | Enter confirms; Escape cancels"); }
    if (mode_ == Mode::Capture) { frame.details.push_back("Press a key combination; Escape cancels"); }
    if (mode_ == Mode::Language && !languages_.empty()) { frame.details.push_back("Language: " + languages_[language_] + " | Up/Down, Enter"); }
    if (!feedback_.empty()) { frame.details.push_back(feedback_); }
    frame.footer = "Tab/Shift+Tab: focus | Arrows: navigate | Enter: edit\n";
    const InputAction footerActions[] = {InputAction::SaveSettings,InputAction::Reset,InputAction::AddBinding,InputAction::Unbind,InputAction::Inspect};
    const char* footerLabels[] = {"Save and apply","Reset","Bind","Unbind","Inspect"};
    for (std::size_t i = 0; i < std::size(footerActions); ++i) {
        if (i == 2) { frame.footer += "\n"; }
        frame.footer += BindingHint(store.Keys(),InputContext::Menu,footerActions[i]); frame.footer += " ";
        frame.footer += footerLabels[i]; frame.footer += " | ";
    }
    frame.footer += "Esc Close";
}
}
