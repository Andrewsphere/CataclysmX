#include "sdl/inventory_screen.h"
#include "sdl/input.h"
#include "client/text.h"

#include <algorithm>
#include <cmath>

namespace cataclysmx::sdl {
namespace {
constexpr SDL_Color normal{210, 220, 218, 255}, dim{145, 160, 163, 255}, accent{255, 230, 90, 255};

struct RenderState {
    SDL_Renderer* renderer;
    float x = 1, y = 1;
    SDL_Rect clip{};
    bool clipped = false;
    explicit RenderState(SDL_Renderer* value) : renderer(value) {
        SDL_GetRenderScale(renderer, &x, &y);
        clipped = SDL_RenderClipEnabled(renderer);
        SDL_GetRenderClipRect(renderer, &clip);
    }
    ~RenderState() {
        SDL_SetRenderScale(renderer, x, y);
        SDL_SetRenderClipRect(renderer, clipped ? &clip : nullptr);
    }
};

bool Box(SDL_Renderer* renderer, SDL_Rect rect, bool active) {
    const SDL_FRect box{float(rect.x), float(rect.y), float(rect.w), float(rect.h)};
    return SDL_SetRenderDrawColor(renderer, 16, 24, 29, 255) && SDL_RenderFillRect(renderer, &box)
        && SDL_SetRenderDrawColor(renderer, active ? 255 : 76, active ? 230 : 96, active ? 90 : 100, 255)
        && SDL_RenderRect(renderer, &box);
}

bool Icon(SDL_Renderer* renderer, SdlText& text, const SdlAssets& assets,
    const ContentCatalog& content, ItemTypeId type, SDL_FRect bounds) {
    const auto& definition = assets.Definition();
    TilesetMapping mapping;
    if (type.value < definition.items.size()) { mapping = definition.items[type.value]; }
    // Type-based identity gives stable variants across regrouping and redraws.
    mapping = ResolveVisual(definition, mapping, 0, 0, 0, type.value);
    if (mapping.kind == TilesetMappingKind::Animation && mapping.index < definition.animations.size()) {
        const auto& clip = definition.animations[mapping.index];
        if (!clip.frames.empty()) { mapping = {TilesetMappingKind::Sprite, clip.frames.front().spriteIndex, {}, mapping.transform}; }
    }
    if (mapping.kind == TilesetMappingKind::Sprite && mapping.index < definition.sprites.size()) {
        const auto& sprite = definition.sprites[mapping.index];
        if (auto* texture = assets.Texture(sprite.atlasIndex); texture && sprite.width && sprite.height) {
            // x/y/width/height describe the image interior, excluding the atlas's
            // extruded border. Anchors affect map placement, not inventory crops.
            const SDL_FRect source{float(sprite.x), float(sprite.y), float(sprite.width), float(sprite.height)};
            const bool sideways = mapping.transform.quarterTurns % 2 != 0;
            auto target = FitItemIcon(bounds, sideways ? float(sprite.height) : float(sprite.width),
                sideways ? float(sprite.width) : float(sprite.height));
            if (sideways) {
                const float centerX = target.x + target.w / 2, centerY = target.y + target.h / 2;
                std::swap(target.w, target.h);
                target.x = centerX - target.w / 2; target.y = centerY - target.h / 2;
            }
            const auto flip = static_cast<SDL_FlipMode>((mapping.transform.flipX ? SDL_FLIP_HORIZONTAL : 0)
                | (mapping.transform.flipY ? SDL_FLIP_VERTICAL : 0));
            return SDL_RenderTextureRotated(renderer, texture, &source, &target,
                mapping.transform.quarterTurns * 90.0, nullptr, flip);
        }
    }
    const auto* item = content.TryGetItem(type);
    const char* symbol = item ? "%" : "?";
    const SDL_Color color = normal;
    if (!text.Draw(127, symbol, -10000, -10000, color)) { return false; }
    return text.Draw(127, symbol, bounds.x + (bounds.w - text.Width(127)) / 2,
        bounds.y + (bounds.h - text.Height(127)) / 2, color);
}

bool Column(SDL_Renderer* renderer, SdlText& text, unsigned slot, const char* value,
    SDL_Rect bounds, SDL_Color color, const SDL_Rect* parent = nullptr) {
    if (!text.Draw(slot, value, -10000, -10000, color)) { return false; }
    SDL_Rect clip = bounds;
    if (parent && !SDL_GetRectIntersection(&bounds, parent, &clip)) { return true; }
    return SDL_SetRenderClipRect(renderer, &clip)
        && text.Draw(slot, value, float(bounds.x + bounds.w - text.Width(slot) - 3), float(bounds.y), color);
}

bool ArtRunText(SdlText& text, const ArtRun& run, unsigned index, float x, float y, int cell) {
    const auto color = ItemArtColor(run.color);
    if (index < 128) { return text.DrawCells(128 + index, run.text.c_str(), run.cells, x, y, color, cell); }
    // Overflow must not overwrite the stable run slots. Reuse the same bounded
    // codepoint-cache range as DrawCells, with stack-only UTF-8 scratch.
    std::uint32_t column = 0;
    for (std::size_t byte = 0; byte < run.text.size() && column < run.cells; ++column) {
        const auto first = static_cast<unsigned char>(run.text[byte]);
        const std::size_t bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        char glyph[5]{};
        unsigned hash = 2166136261U;
        for (std::size_t n = 0; n < bytes; ++n) {
            glyph[n] = run.text[byte + n];
            hash = (hash ^ static_cast<unsigned char>(glyph[n])) * 16777619U;
        }
        if (!text.Draw(256 + hash % 64, glyph, x + column * cell, y, color)) { return false; }
        byte += bytes;
    }
    return true;
}

bool Pane(SDL_Renderer* renderer, SdlText& text, const SdlAssets& assets, const ContentCatalog& content,
    ItemUiPane& pane, SDL_Rect bounds, bool active, int line, int cell, unsigned index) {
    if (!SDL_SetRenderClipRect(renderer, &bounds) || !Box(renderer, bounds, active)) { return false; }
    const int x = bounds.x + 6, width = std::max(1, bounds.w - 12);
    const int rowHeight = std::max(36, line * 2 + 4);
    const bool compact = bounds.h < line * 3 + 12 + rowHeight + 5;
    const int headerLines = compact ? (bounds.h >= line + rowHeight + 12 ? 1 : 0) : 3;
    if (headerLines && !text.Draw(68 + index, pane.title.c_str(), float(x), float(bounds.y + 5), active ? accent : normal)) { return false; }
    if (!compact && !text.Draw(70 + index, pane.capacity.c_str(), float(x), float(bounds.y + 6 + line), dim)) { return false; }
    const int tableCount = std::max(64, cell * 5 + 6), tableMeasure = std::max(100, cell * 10 + 6);
    const bool table = width >= std::max(470, line * 2 + cell * 6 + tableCount + tableMeasure * 2);
    const int massWidth = table ? tableMeasure : (width - 44) / 2;
    const int volumeWidth = massWidth, countWidth = table ? tableCount : width - massWidth - volumeWidth;
    const int columns = table ? countWidth + volumeWidth + massWidth : 0;
    const int valuesX = table ? x + width - columns : x;
    const int headingY = bounds.y + line * 2 + 8;
    if (!compact && !text.Draw(122 + index, table ? "Item" : "Item  # / Vol / Mass", float(x), float(headingY), dim)) { return false; }
    if (!compact && table) {
        if (!Column(renderer, text, 120, "Count", {valuesX, headingY, countWidth, line}, dim)
            || !Column(renderer, text, 121, "Volume", {valuesX + countWidth, headingY, volumeWidth, line}, dim)
            || !Column(renderer, text, 124, "Mass", {valuesX + countWidth + volumeWidth, headingY, massWidth, line}, dim)) { return false; }
    }
    const int top = bounds.y + line * headerLines + (headerLines ? 12 : 5);
    const SDL_Rect list{x, top, width, std::max(0, bounds.y + bounds.h - top - 5)};
    const int visible = std::max(1, list.h / rowHeight);
    pane.scroll = ItemPaneScroll(pane.scroll, pane.rows.size(), pane.selected, pane.selectionValid, visible);
    if (list.h == 0) { return true; }
    if (!SDL_SetRenderClipRect(renderer, &list)) { return false; }
    if (pane.rows.empty()) { return text.Draw(72 + index, "Empty", float(x), float(top), dim); }
    for (int offset = 0; offset < visible && pane.scroll + offset < static_cast<int>(pane.rows.size()); ++offset) {
        const auto rowIndex = static_cast<std::size_t>(pane.scroll + offset);
        const auto& row = pane.rows[rowIndex];
        const int y = top + offset * rowHeight;
        const bool selected = pane.selectionValid && rowIndex == pane.selected;
        if (selected) {
            const SDL_FRect highlight{float(x), float(y), float(width), float(rowHeight)};
            if (!SDL_SetRenderDrawColor(renderer, active ? 48 : 32, active ? 62 : 44, active ? 68 : 48, 255)
                || !SDL_RenderFillRect(renderer, &highlight)) { return false; }
        }
        const int iconSize = table ? rowHeight - 4 : line;
        if (!Icon(renderer, text, assets, content, row.type, {float(x + 2), float(y + 2), float(iconSize), float(iconSize)})) { return false; }
        const int nameX = x + iconSize + 7;
        const int indent = std::min<int>(row.depth, std::max(0, (width - iconSize - 80) / 10)) * 10;
        const SDL_Rect nameBounds{nameX, y, std::max(1, x + width - columns - nameX), line + 3};
        SDL_Rect nameClip{};
        if (!SDL_GetRectIntersection(&nameBounds, &list, &nameClip)) { continue; }
        String label = row.name;
        if (row.wielded && label.find('*') == String::npos) { label = "* " + label; }
        const auto slot = 72 + (index * 24 + offset) % 48;
        if (!SDL_SetRenderClipRect(renderer, &nameClip)
            || !text.Draw(slot, label.c_str(), float(nameX + indent), float(y + 2), selected ? accent : normal)) { return false; }
        const String count = UnsignedText(row.items.size());
        const String volume = ItemVolumeText(row.volumeMl), mass = ItemMassText(row.massGrams);
        const int valuesY = y + (table ? 2 : line + 2);
        if (!Column(renderer, text, 120, count.c_str(), {valuesX, valuesY, countWidth, line}, dim, &list)
            || !Column(renderer, text, 121, volume.c_str(), {valuesX + countWidth, valuesY, volumeWidth, line}, dim, &list)
            || !Column(renderer, text, 124, mass.c_str(), {valuesX + countWidth + volumeWidth, valuesY, massWidth, line}, dim, &list)) { return false; }
        if (!SDL_SetRenderClipRect(renderer, &list)) { return false; }
    }
    return true;
}

bool Examination(SDL_Renderer* renderer, SdlText& text, const SdlAssets& assets,
    const ContentCatalog& content, ItemExamination& item, SDL_Rect body, int line, int cell) {
    const int icon = std::max(32, line * 2);
    if (!SDL_SetRenderClipRect(renderer, &body)
        || !Icon(renderer, text, assets, content, item.type, {float(body.x), float(body.y), float(icon), float(icon)})
        || !text.Draw(68, item.name.c_str(), float(body.x + icon + 8), float(body.y), accent, std::max(1, body.w - icon - 8))) { return false; }
    const int header = std::max(icon, text.Height(68)) + 8;
    const SDL_Rect detail{body.x, body.y + header, body.w, std::max(1, body.h - header)};
    if (!text.Draw(69, item.description.c_str(), -10000, -10000, normal, body.w)) { return false; }
    const int descriptionHeight = item.description.empty() ? 0 : text.Height(69) + 12;
    const int artHeight = item.art ? static_cast<int>(item.art->lines.size()) * line + 12 : 0;
    String stats;
    for (const auto& stat : item.stats) { if (!stats.empty()) { stats += '\n'; } stats += stat; }
    if (!text.Draw(70, stats.c_str(), -10000, -10000, normal, body.w)) { return false; }
    const int statsHeight = stats.empty() ? 0 : text.Height(70);
    const int maxScroll = std::max(0, (descriptionHeight + artHeight + statsHeight - detail.h + line - 1) / line);
    item.scroll = std::clamp(item.scroll, 0, maxScroll);
    int y = detail.y - item.scroll * line;
    if (!SDL_SetRenderClipRect(renderer, &detail)
        || !text.Draw(69, item.description.c_str(), float(body.x), float(y), normal, body.w)) { return false; }
    y += descriptionHeight;
    if (item.art) {
        unsigned runIndex = 0;
        for (const auto& artLine : item.art->lines) {
            std::uint32_t column = 0;
            for (const auto& run : artLine) {
                if (y + line > detail.y && y < detail.y + detail.h && column * cell < static_cast<unsigned>(body.w)) {
                    if (!ArtRunText(text, run, runIndex,
                        float(body.x + column * cell), float(y), cell)) { return false; }
                }
                ++runIndex; // Stable slots also for offscreen runs when scrolling.
                column += run.cells;
            }
            y += line;
        }
        y += 12;
    }
    return text.Draw(70, stats.c_str(), float(body.x), float(y), normal, body.w);
}
}

SDL_Color ItemArtColor(ArtColor color) {
    return color.useDefault ? normal : SDL_Color{color.red, color.green, color.blue, 255};
}

void HandleInventoryKey(ItemUi& ui, HeldKey& held, const KeyChord& key, const Keymap& keys,
    const WorldView& view, const ContentCatalog& content, const Localization& localization) {
    const auto previous = ui.Frame().screen;
    const bool neededTime = ui.NeedsTime();
    const auto action = ResolveAction(keys, InputContext::Items, key);
    eastl::optional<ItemUiAction> equipment;
    if (action) {
        switch (*action) {
        case InputAction::HoldLeft: equipment = ItemUiAction::HoldLeft; break;
        case InputAction::HoldRight: equipment = ItemUiAction::HoldRight; break;
        case InputAction::TwoHanded: equipment = ItemUiAction::TwoHanded; break;
        case InputAction::Regrip: equipment = ItemUiAction::Regrip; break;
        case InputAction::Wear: equipment = ItemUiAction::Wear; break;
        case InputAction::RemoveWorn: equipment = ItemUiAction::Remove; break;
        case InputAction::ReleaseHand: equipment = ItemUiAction::Release; break;
        default: break;
        }
    }
    if (equipment) {
        ui.Refresh(view, content, localization, keys);
        ui.HandleAction(*equipment, view); held.Reset();
    } else { ui.Handle(key, keys, view, content, localization); }
    const bool cancelled = key == KeyChord{"Escape"}
        || ResolveAction(keys, InputContext::Items, key) == InputAction::Cancel
        || ResolveAction(keys, InputContext::Menu, key) == InputAction::Cancel;
    if (previous != ui.Frame().screen || neededTime != ui.NeedsTime() || cancelled) { held.Reset(); }
}

SDL_FRect FitItemIcon(SDL_FRect bounds, float width, float height) {
    if (width <= 0 || height <= 0) { return {bounds.x, bounds.y, 0, 0}; }
    const float scale = std::min(bounds.w / width, bounds.h / height);
    const float w = width * scale, h = height * scale;
    return {bounds.x + (bounds.w - w) / 2, bounds.y + (bounds.h - h) / 2, w, h};
}

int ItemPaneScroll(int scroll, std::size_t count, std::size_t selected, bool selectionValid, int visibleRows) {
    const int visible = std::max(1, visibleRows);
    scroll = std::clamp(scroll, 0, std::max(0, static_cast<int>(count) - visible));
    if (selectionValid && selected < count) {
        scroll = std::min(scroll, static_cast<int>(selected));
        scroll = std::max(scroll, static_cast<int>(selected) - visible + 1);
    }
    return scroll;
}

bool DrawInventoryScreen(SDL_Renderer* renderer, SdlText& text, const SdlAssets& assets,
    const ContentCatalog& content, ItemUiFrame& frame, int outputWidth, int outputHeight, float uiScale, bool retainClosed) {
    if (frame.screen == ItemUiScreen::Closed && !retainClosed) { return true; }
    RenderState restore(renderer);
    // Keep two useful columns even when UI scale exceeds the compact window.
    const float scale = std::max(0.25f, std::min({uiScale, outputWidth / 460.0f, outputHeight / 380.0f}));
    const int width = static_cast<int>(outputWidth / scale), height = static_cast<int>(outputHeight / scale);
    const SDL_Rect window{8, 8, width - 16, height - 16};
    if (!SDL_SetRenderScale(renderer, scale, scale) || !SDL_SetRenderClipRect(renderer, &window)
        || !Box(renderer, window, true) || !text.Draw(125, "M", -10000, -10000, normal)) { return false; }
    const int line = std::max(1, text.Height(125)), cell = std::max(1, text.Width(125));
    const int wrap = std::max(1, window.w - 16);
    if (!text.Draw(64, frame.title.c_str(), -10000, -10000, accent, wrap)
        || !text.Draw(65, frame.footer.c_str(), -10000, -10000, dim, wrap)) { return false; }
    const int titleHeight = std::min(line * 2, text.Height(64));
    const String status = (frame.busy ? String("Working... ") : String()) + frame.feedback;
    if (!text.Draw(66, status.c_str(), -10000, -10000, accent, wrap)) { return false; }
    const int statusHeight = status.empty() ? 0 : std::min(line * 2, text.Height(66)) + 4;
    const int minimumBody = line * 3 + 12 + std::max(36, line * 2 + 4) + 5;
    const int controlsLimit = std::max(line, std::min(window.h * 2 / 5,
        window.h - titleHeight - statusHeight - minimumBody - 28));
    // Fit the supplied controller controls independently of body text. The long
    // advanced footer previously lost its final controls (including Escape).
    float controlsScale = 1.0f;
    int controlsWrap = wrap, controlsHeight = text.Height(65);
    while (controlsHeight > controlsLimit && controlsScale > 0.5f) {
        controlsScale = std::max(0.5f, controlsScale - 0.05f);
        controlsWrap = std::max(1, static_cast<int>(wrap / controlsScale));
        if (!text.Draw(65, frame.footer.c_str(), -10000, -10000, dim, controlsWrap)) { return false; }
        controlsHeight = static_cast<int>(std::ceil(text.Height(65) * controlsScale));
    }
    controlsHeight = std::min(controlsLimit, controlsHeight);
    const int footerY = window.y + window.h - controlsHeight - statusHeight - 10;
    const SDL_Rect header{window.x + 8, window.y + 5, wrap, titleHeight};
    if (!SDL_SetRenderClipRect(renderer, &header)
        || !text.Draw(64, frame.title.c_str(), float(header.x), float(header.y), accent, wrap)) { return false; }
    const SDL_Rect body{window.x + 8, header.y + titleHeight + 8, wrap,
        std::max(1, footerY - header.y - titleHeight - 12)};
    if (frame.screen == ItemUiScreen::Examine) {
        if (!Examination(renderer, text, assets, content, frame.examination, body, line, cell)) { return false; }
    } else if (frame.screen == ItemUiScreen::Inventory || frame.screen == ItemUiScreen::Advanced || frame.screen == ItemUiScreen::Closed) {
        const bool advanced = frame.screen == ItemUiScreen::Advanced;
        SDL_Rect left = body;
        if (advanced) { left.w = (body.w - 8) / 2; }
        if (!Pane(renderer, text, assets, content, frame.panes[0], left, frame.activePane == 0, line, cell, 0)) { return false; }
        if (advanced) {
            const SDL_Rect right{left.x + left.w + 8, body.y, body.w - left.w - 8, body.h};
            if (!Pane(renderer, text, assets, content, frame.panes[1], right, frame.activePane == 1, line, cell, 1)) { return false; }
        }
    } else {
        if (!SDL_SetRenderClipRect(renderer, &body)) { return false; }
        const String prompt = frame.prompt + (frame.screen == ItemUiScreen::Amount ? " " + frame.amount : "");
        if (!text.Draw(68, prompt.c_str(), float(body.x), float(body.y), normal, body.w)) { return false; }
        const int top = body.y + text.Height(68) + 8;
        const int visible = std::max(1, (body.y + body.h - top) / (line + 4));
        const int first = ItemPaneScroll(0, frame.choices.size(), frame.choice, true, visible);
        for (int i = first; i < static_cast<int>(frame.choices.size()) && i < first + visible; ++i) {
            const String choice = (static_cast<std::size_t>(i) == frame.choice ? "> " : "  ") + frame.choices[i];
            if (!text.Draw(72 + (i - first) % 48, choice.c_str(), float(body.x), float(top + (i - first) * (line + 4)),
                static_cast<std::size_t>(i) == frame.choice ? accent : normal, body.w)) { return false; }
        }
    }
    const SDL_Rect statusClip{window.x + 8, footerY, wrap, statusHeight};
    if (!SDL_SetRenderClipRect(renderer, &statusClip)
        || !text.Draw(66, status.c_str(), float(statusClip.x), float(statusClip.y), accent, wrap)) { return false; }
    const SDL_Rect controlsClip{static_cast<int>((window.x + 8) / controlsScale),
        static_cast<int>((footerY + statusHeight) / controlsScale), controlsWrap,
        static_cast<int>(std::ceil(controlsHeight / controlsScale))};
    return SDL_SetRenderScale(renderer, scale * controlsScale, scale * controlsScale)
        && SDL_SetRenderClipRect(renderer, &controlsClip)
        && text.Draw(65, frame.footer.c_str(), (window.x + 8) / controlsScale,
            (footerY + statusHeight) / controlsScale, dim, controlsWrap);
}
}
