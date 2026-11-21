#include "sdl/inventory_screen.h"
#include "sdl/input.h"
#include "sdl/layout.h"
#include "client/text.h"

#include <algorithm>
#include <cmath>

namespace cataclysmx::sdl {
namespace {
constexpr SDL_Color normal{210, 220, 218, 255}, dim{145, 160, 163, 255}, accent{255, 230, 90, 255};
constexpr SDL_Color inactiveAnchor{110, 182, 174, 255};

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
    const auto border = active ? accent : inactiveAnchor;
    return SDL_SetRenderDrawColor(renderer, 16, 24, 29, 255) && SDL_RenderFillRect(renderer, &box)
        && SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a)
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
    SDL_Rect bounds, SDL_Color color, const SDL_Rect* parent = nullptr, bool fit = false) {
    if (!text.Draw(slot, value, -10000, -10000, color)) { return false; }
    SDL_Rect clip = bounds;
    if (parent && !SDL_GetRectIntersection(&bounds, parent, &clip)) { return true; }
    const int textWidth = text.Width(slot);
    if (fit && textWidth > bounds.w - 6) {
        const float squeeze = std::max(1, bounds.w - 6) / float(textWidth);
        const int left = static_cast<int>(std::ceil(clip.x / squeeze));
        const int right = static_cast<int>(std::floor((clip.x + clip.w) / squeeze));
        const SDL_Rect fitted{left, clip.y, std::max(0, right - left), clip.h};
        RenderState restore(renderer);
        return SDL_SetRenderScale(renderer, restore.x * squeeze, restore.y)
            && SDL_SetRenderClipRect(renderer, &fitted)
            && text.Draw(slot, value, (bounds.x + bounds.w - 3) / squeeze - textWidth, float(bounds.y), color);
    }
    return SDL_SetRenderClipRect(renderer, &clip)
        && text.Draw(slot, value, float(bounds.x + bounds.w - textWidth - 3), float(bounds.y), color);
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

int PaneRowHeight(int line, bool singleLine) { return singleLine ? line + 5 : std::max(36, line * 2 + 4); }
int MinimumPaneHeight(int line, bool singleLine = false) { return line * 3 + 17 + PaneRowHeight(line, singleLine); }

bool Pane(SDL_Renderer* renderer, SdlText& text, const SdlAssets& assets, const ContentCatalog& content,
    ItemUiPane& pane, SDL_Rect bounds, bool active, int line, int cell, unsigned index, bool singleLine = false) {
    if (!SDL_SetRenderClipRect(renderer, &bounds)) { return false; }
    const int x = bounds.x + 6, width = std::max(1, bounds.w - 12);
    const int rowHeight = PaneRowHeight(line, singleLine);
    const bool compact = bounds.h < MinimumPaneHeight(line, singleLine);
    const int headerLines = compact ? (bounds.h >= line + rowHeight + 12 ? 1 : 0) : 3;
    if (headerLines && !text.Draw(68 + index, pane.title.c_str(), float(x), float(bounds.y + 5), active ? accent : normal)) { return false; }
    if (!compact && !text.Draw(70 + index, pane.capacity.c_str(), float(x), float(bounds.y + 6 + line), dim)) { return false; }
    const int tableCount = singleLine ? cell * 3 + 6 : std::max(64, cell * 5 + 6);
    const int tableMeasure = singleLine ? cell * 6 + 6 : std::max(100, cell * 10 + 6);
    const bool table = singleLine || width >= std::max(470, line * 2 + cell * 6 + tableCount + tableMeasure * 2);
    const int massWidth = table ? tableMeasure : (width - 44) / 2;
    const int volumeWidth = massWidth, countWidth = table ? tableCount : width - massWidth - volumeWidth;
    const int columns = table ? countWidth + volumeWidth + massWidth : 0;
    const int valuesX = table ? x + width - columns : x;
    const int headingY = bounds.y + line * 2 + 8;
    if (!compact && !text.Draw(122 + index, table ? "Item" : "Item  # / Vol / Mass", float(x), float(headingY), dim)) { return false; }
    if (!compact && table) {
        if (!Column(renderer, text, 120, singleLine ? "#" : "Count", {valuesX, headingY, countWidth, line}, dim)
            || !Column(renderer, text, 121, singleLine ? "Vol" : "Volume", {valuesX + countWidth, headingY, volumeWidth, line}, dim)
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
        const int indentSpace = singleLine ? width - columns - iconSize - 4 * cell : width - iconSize - 80;
        const int indent = std::min<int>(row.depth, std::max(0, indentSpace / 10)) * 10;
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
        if (!Column(renderer, text, 120, count.c_str(), {valuesX, valuesY, countWidth, line}, dim, &list, singleLine)
            || !Column(renderer, text, 121, volume.c_str(), {valuesX + countWidth, valuesY, volumeWidth, line}, dim, &list, singleLine)
            || !Column(renderer, text, 124, mass.c_str(), {valuesX + countWidth + volumeWidth, valuesY, massWidth, line}, dim, &list, singleLine)) { return false; }
        if (!SDL_SetRenderClipRect(renderer, &list)) { return false; }
    }
    return true;
}

bool Examination(SDL_Renderer* renderer, SdlText& text, const SdlAssets& assets,
    const ContentCatalog& content, ItemExamination& item, SDL_Rect body, int line, int cell) {
    const int icon = std::max(32, line * 2);
    const int nameWidth = std::max(1, body.w - icon - 8);
    if (!text.Draw(68, item.name.c_str(), -10000, -10000, accent, nameWidth)) { return false; }
    const int header = std::min(std::max(icon, text.Height(68)) + 8, std::max(0, body.h - line));
    const SDL_Rect heading{body.x, body.y, body.w, header};
    if (!SDL_SetRenderClipRect(renderer, &heading)
        || !Icon(renderer, text, assets, content, item.type, {float(body.x), float(body.y), float(icon), float(icon)})
        || !text.Draw(68, item.name.c_str(), float(body.x + icon + 8), float(body.y), accent, nameWidth)) { return false; }
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

bool ChoicePrompt(SDL_Renderer* renderer, SdlText& text, const ItemUiFrame& frame, SDL_Rect body, int line) {
    const String prompt = frame.prompt + (frame.screen == ItemUiScreen::Amount ? " " + frame.amount : "");
    if (!text.Draw(68, prompt.c_str(), -10000, -10000, normal, body.w)) { return false; }
    const int choiceHeight = line + 4;
    const int promptLimit = frame.choices.empty() ? body.h : std::max(0, body.h - choiceHeight - 8);
    const SDL_Rect promptClip{body.x, body.y, body.w, std::min(text.Height(68), promptLimit)};
    if (!SDL_SetRenderClipRect(renderer, &promptClip)
        || !text.Draw(68, prompt.c_str(), float(body.x), float(body.y), normal, body.w)) { return false; }
    const int top = body.y + promptClip.h + 8;
    const SDL_Rect choices{body.x, top, body.w, std::max(0, body.y + body.h - top)};
    if (!SDL_SetRenderClipRect(renderer, &choices)) { return false; }
    const int visible = std::max(1, choices.h / choiceHeight);
    const int first = ItemPaneScroll(0, frame.choices.size(), frame.choice, true, visible);
    for (int i = first; i < static_cast<int>(frame.choices.size()) && i < first + visible; ++i) {
        const bool selected = static_cast<std::size_t>(i) == frame.choice;
        const String choice = (selected ? "> " : "  ") + frame.choices[i];
        if (!text.Draw(72 + (i - first) % 48, choice.c_str(), float(body.x), float(top + (i - first) * choiceHeight),
            selected ? accent : normal, body.w)) { return false; }
    }
    return true;
}

struct InventoryControls {
    int height = 0;
    int wrap = 0;
    float scale = 1.0f;
};

bool MeasureControls(SdlText& text, const char* label, int wrap, int limit, InventoryControls& controls) {
    if (!*label) { return true; }
    for (;;) {
        controls.wrap = std::max(1, static_cast<int>(wrap / controls.scale));
        if (!text.Draw(65, label, -10000, -10000, dim, controls.wrap)) { return false; }
        const int height = static_cast<int>(std::ceil(text.Height(65) * controls.scale));
        controls.height = std::min(limit, height);
        if (height <= limit || controls.scale <= 0.5f) { return true; }
        controls.scale = std::max(0.5f, controls.scale - 0.05f);
    }
}

bool DrawControls(SDL_Renderer* renderer, SdlText& text, const char* label, const InventoryControls& controls,
    int x, int y, float scale) {
    if (!controls.height) { return true; }
    const SDL_Rect controlsClip{static_cast<int>(x / controls.scale), static_cast<int>(y / controls.scale),
        controls.wrap, static_cast<int>(std::ceil(controls.height / controls.scale))};
    return SDL_SetRenderScale(renderer, scale * controls.scale, scale * controls.scale)
        && SDL_SetRenderClipRect(renderer, &controlsClip)
        && text.Draw(65, label, x / controls.scale, y / controls.scale, dim, controls.wrap);
}

SDL_Rect PopupTarget(SDL_Rect tile, SDL_Rect viewport) {
    if (SDL_RectEmpty(&tile) || SDL_RectEmpty(&viewport)) { return {}; }
    SDL_Rect visible{};
    if (SDL_GetRectIntersection(&tile, &viewport, &visible)) { return visible; }
    return {std::clamp(tile.x + tile.w / 2, viewport.x, viewport.x + viewport.w - 1),
        std::clamp(tile.y + tile.h / 2, viewport.y, viewport.y + viewport.h - 1), 1, 1};
}

bool DrawInventoryAnchors(SDL_Renderer* renderer, SDL_Rect content, const InventoryMapGeometry& map,
    const eastl::array<SDL_Rect, 2>& panels, unsigned active) {
    const bool shared = SDL_RectsEqual(&map.tiles[0], &map.tiles[1]);
    for (const unsigned side : {1 - active, active}) {
        const auto& tile = map.tiles[side];
        const auto target = PopupTarget(tile, map.viewport);
        if (SDL_RectEmpty(&target)) { continue; }
        const auto& panel = panels[side];
        const auto color = side == active ? accent : inactiveAnchor;
        const int x = target.x + target.w / 2, y = target.y + target.h / 2;
        if (!SDL_SetRenderClipRect(renderer, &content)
            || !SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a)
            || !SDL_RenderLine(renderer, float(x), float(y),
                float(std::clamp(x, panel.x, panel.x + panel.w - 1)),
                float(std::clamp(y, panel.y, panel.y + panel.h - 1)))) { return false; }
        if (shared && side != active) { continue; }
        if (!SDL_SetRenderClipRect(renderer, &map.viewport)) { return false; }
        if (SDL_HasRectIntersection(&tile, &map.viewport)) {
            const SDL_FRect outline{float(tile.x), float(tile.y), float(tile.w), float(tile.h)};
            if (!SDL_RenderRect(renderer, &outline)) { return false; }
        } else {
            const SDL_FRect edgeMarker{float(x - 2), float(y - 2), 5, 5};
            if (!SDL_RenderFillRect(renderer, &edgeMarker)) { return false; }
        }
    }
    return true;
}

bool DrawAdvancedInventory(SDL_Renderer* renderer, SdlText& text, const SdlAssets& assets,
    const ContentCatalog& content, ItemUiFrame& frame, int outputWidth, int outputHeight, float uiScale,
    const InventoryMapGeometry* map, int line, int cell, const char* footerText) {
    const int minimumWidth = line + cell * 26 + 40;
    const int minimumHeight = MinimumPaneHeight(line, true) + line * 2 + 10;
    const int contentTop = map ? map->contentTop : 0;
    const SDL_Rect contentPixels{0, contentTop, outputWidth, outputHeight - contentTop};
    SDL_Rect placement = contentPixels;
    const SDL_Rect protectedArea = map ? map->protectedArea : SDL_Rect{};
    if (!SDL_RectEmpty(&protectedArea)) {
        placement.x = map->viewport.x;
        placement.w = map->viewport.w;
    }
    const float requestedScale = std::max(0.25f, uiScale);
    const auto panelPixels = PlaceInventoryPopups(placement, protectedArea,
        static_cast<int>(std::max(preferredInventoryPaneWidth, minimumWidth) * requestedScale));
    if (SDL_RectEmpty(&panelPixels[0]) || SDL_RectEmpty(&panelPixels[1])) { return true; }
    const float scale = std::min({requestedScale, std::min(panelPixels[0].w, panelPixels[1].w) / float(minimumWidth),
        panelPixels[0].h / float(minimumHeight)});
    const eastl::array<SDL_Rect, 2> panels{ScaleRect(panelPixels[0], 1.0f / scale), ScaleRect(panelPixels[1], 1.0f / scale)};
    const unsigned active = frame.activePane, inactive = 1 - active;
    if (!SDL_SetRenderScale(renderer, 1, 1)
        || (map && !DrawInventoryAnchors(renderer, contentPixels, *map, panelPixels, active))) { return false; }
    for (unsigned side = 0; side < panelPixels.size(); ++side) {
        if (!SDL_SetRenderClipRect(renderer, &panelPixels[side]) || !Box(renderer, panelPixels[side], side == active)) { return false; }
    }
    if (!SDL_SetRenderScale(renderer, scale, scale)
        || !Pane(renderer, text, assets, content, frame.panes[inactive], panels[inactive], false, line, cell, inactive, true)) { return false; }
    auto panel = panels[active];
    const int wrap = panel.w - 16;
    if (frame.busy || !frame.feedback.empty()) {
        const String status = (frame.busy ? String("Working... ") : String()) + frame.feedback;
        if (!text.Draw(66, status.c_str(), -10000, -10000, accent, wrap)) { return false; }
        const SDL_Rect statusClip{panel.x + 8, panel.y + 5, wrap, std::min(line * 2, text.Height(66))};
        if (!SDL_SetRenderClipRect(renderer, &statusClip)
            || !text.Draw(66, status.c_str(), float(statusClip.x), float(statusClip.y), accent, wrap)) { return false; }
        panel.y += statusClip.h + 5;
        panel.h -= statusClip.h + 5;
    }
    InventoryControls controls;
    const int available = std::max(line, std::min(panel.h / 3, panel.h - MinimumPaneHeight(line, true) - 5));
    if (!MeasureControls(text, footerText, wrap, available, controls)) { return false; }
    if (controls.height) { panel.h -= controls.height + 5; }
    if (frame.screen == ItemUiScreen::Advanced || frame.screen == ItemUiScreen::Closed) {
        if (!Pane(renderer, text, assets, content, frame.panes[active], panel, true, line, cell, active, true)) { return false; }
    } else {
        const SDL_Rect body{panel.x + 8, panel.y + 7, wrap, panel.h - 11};
        if (frame.screen == ItemUiScreen::Examine) {
            if (!Examination(renderer, text, assets, content, frame.examination, body, line, cell)) { return false; }
        } else if (!ChoicePrompt(renderer, text, frame, body, line)) { return false; }
    }
    return DrawControls(renderer, text, footerText, controls, panel.x + 8, panel.y + panel.h, scale);
}
}

SDL_Color ItemArtColor(ArtColor color) {
    return color.useDefault ? normal : SDL_Color{color.red, color.green, color.blue, 255};
}

void HandleInventoryKey(ItemUi& ui, HeldKey& held, const KeyChord& key, const Keymap& keys,
    const WorldView& view, const ContentCatalog& content, const Localization& localization) {
    const auto previous = ui.Frame().screen;
    const bool neededTime = ui.NeedsTime();
    const bool numberKey = IsItemUiNumberKey(previous, key);
    const auto action = ResolveAction(keys, InputContext::Items, key);
    eastl::optional<ItemUiAction> equipment;
    if (action && !numberKey) {
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
    const bool cancelled = !numberKey && (key == KeyChord{"Escape"}
        || ResolveAction(keys, InputContext::Items, key) == InputAction::Cancel
        || ResolveAction(keys, InputContext::Menu, key) == InputAction::Cancel);
    if (previous != ui.Frame().screen || neededTime != ui.NeedsTime() || cancelled
        || (numberKey && previous == ItemUiScreen::Advanced)) { held.Reset(); }
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
    const ContentCatalog& content, ItemUiFrame& frame, int outputWidth, int outputHeight, float uiScale,
    bool retainClosed, const InventoryMapGeometry* map, const char* footerOverride) {
    if (frame.screen == ItemUiScreen::Closed && !retainClosed) { return true; }
    RenderState restore(renderer);
    if (!text.Draw(125, "M", -10000, -10000, normal)) { return false; }
    const int line = std::max(1, text.Height(125)), cell = std::max(1, text.Width(125));
    const char* footerText = footerOverride ? footerOverride : frame.footer.c_str();
    if (frame.advanced) {
        return DrawAdvancedInventory(renderer, text, assets, content, frame, outputWidth, outputHeight, uiScale,
            map, line, cell, footerText);
    }
    const float scale = std::max(0.25f, std::min({uiScale, outputWidth / 460.0f, outputHeight / 380.0f}));
    const int width = static_cast<int>(outputWidth / scale), height = static_cast<int>(outputHeight / scale);
    const SDL_Rect window{8, 8, width - 16, height - 16};
    if (!SDL_SetRenderScale(renderer, scale, scale) || !SDL_SetRenderClipRect(renderer, &window)
        || !Box(renderer, window, true)) { return false; }
    const int wrap = std::max(1, window.w - 16);
    if (!text.Draw(64, frame.title.c_str(), -10000, -10000, accent, wrap)) { return false; }
    const int titleHeight = std::min(line * 2, text.Height(64));
    const String status = (frame.busy ? String("Working... ") : String()) + frame.feedback;
    if (!text.Draw(66, status.c_str(), -10000, -10000, accent, wrap)) { return false; }
    const int statusHeight = status.empty() ? 0 : std::min(line * 2, text.Height(66)) + 4;
    InventoryControls controls;
    const int available = std::min(window.h * 2 / 5, window.h - titleHeight - MinimumPaneHeight(line) - 28);
    if (!MeasureControls(text, footerText, wrap, std::max(line, available - statusHeight), controls)) { return false; }
    const int footerY = window.y + window.h - controls.height - statusHeight - 10;
    const SDL_Rect header{window.x + 8, window.y + 5, wrap, titleHeight};
    if (!SDL_SetRenderClipRect(renderer, &header)
        || !text.Draw(64, frame.title.c_str(), float(header.x), float(header.y), accent, wrap)) { return false; }
    const SDL_Rect body{window.x + 8, header.y + titleHeight + 8, wrap,
        std::max(1, footerY - header.y - titleHeight - 12)};
    if (frame.screen == ItemUiScreen::Examine) {
        if (!Examination(renderer, text, assets, content, frame.examination, body, line, cell)) { return false; }
    } else if (frame.screen == ItemUiScreen::Inventory || frame.screen == ItemUiScreen::Closed) {
        if (!SDL_SetRenderClipRect(renderer, &body) || !Box(renderer, body, true)
            || !Pane(renderer, text, assets, content, frame.panes[0], body, true, line, cell, 0)) { return false; }
    } else if (!ChoicePrompt(renderer, text, frame, body, line)) { return false; }
    if (statusHeight) {
        const SDL_Rect statusClip{window.x + 8, footerY, wrap, statusHeight};
        if (!SDL_SetRenderClipRect(renderer, &statusClip)
            || !text.Draw(66, status.c_str(), float(statusClip.x), float(statusClip.y), accent, wrap)) { return false; }
    }
    return DrawControls(renderer, text, footerText, controls, window.x + 8, footerY + statusHeight, scale);
}
}
