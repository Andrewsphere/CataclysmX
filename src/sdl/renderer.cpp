#include "sdl/renderer.h"
#include "sdl/screens.h"
#include "sdl/health.h"

#include "client/text.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace cataclysmx::sdl {
namespace {

bool Color(SDL_Renderer* renderer, RgbColor color) {
    return SDL_SetRenderDrawColor(renderer, color.red, color.green, color.blue, 255);
}

SDL_Color TextColor(RgbColor color) { return {color.red, color.green, color.blue, 255}; }

constexpr InputAction topActions[] = {InputAction::Help, InputAction::Settings, InputAction::Debug};

struct SpriteGeometry {
    SDL_FPoint points[4];
    SDL_FRect bounds;
};

SpriteGeometry Geometry(const TilesetSprite& sprite, SpriteTransform transform,
    const SDL_FRect& cell, const TilesetDefinition& definition) {
    SpriteGeometry result{};
    const float sourceX[] = {0, float(sprite.width), float(sprite.width), 0};
    const float sourceY[] = {0, 0, float(sprite.height), float(sprite.height)};
    float left = 0, top = 0, right = 0, bottom = 0;
    for (unsigned i = 0; i < 4; ++i) {
        float x = sourceX[i] - sprite.anchorX, y = sourceY[i] - sprite.anchorY;
        if (transform.flipX) { x = -x; } if (transform.flipY) { y = -y; }
        for (unsigned turn = 0; turn < transform.quarterTurns; ++turn) { const float previous = x; x = -y; y = previous; }
        const float attachX = sprite.cellAnchorX < 0 ? definition.cellWidth / 2.0f : sprite.cellAnchorX;
        const float attachY = sprite.cellAnchorY < 0 ? float(definition.cellHeight) : sprite.cellAnchorY;
        result.points[i] = {cell.x + (attachX + x) * cell.w / definition.cellWidth,
            cell.y + (attachY + y) * cell.h / definition.cellHeight};
        const auto point = result.points[i];
        if (i == 0) { left = right = point.x; top = bottom = point.y; }
        else { left = std::min(left, point.x); right = std::max(right, point.x); top = std::min(top, point.y); bottom = std::max(bottom, point.y); }
    }
    result.bounds = {left, top, right - left, bottom - top};
    return result;
}

SDL_FRect CellRect(const MapLayout& layout, int column, int row) {
    return {float(layout.x + column * layout.cellSize), float(layout.y + row * layout.cellHeight),
        float(layout.cellSize), float(layout.cellHeight)};
}

SDL_Rect ViewClip(const MapLayout& layout, SDL_Rect viewport) {
    SDL_Rect grid{layout.x, layout.y, layout.width, layout.height}, clip{};
    SDL_GetRectIntersection(&grid, &viewport, &clip);
    return clip;
}

bool SpriteBoundsVisible(const SDL_FRect& bounds, SpriteLayer layer,
    const MapPacket& packet, const MapLayout& layout, const SDL_Rect& clip) {
    const SDL_FRect viewport{float(clip.x), float(clip.y), float(clip.w), float(clip.h)};
    SDL_FRect visible{};
    if (!SDL_GetRectIntersectionFloat(&bounds, &viewport, &visible)) { return false; }
    // Upright artwork belongs to its visible source cell. Its canopy may cover
    // fog without revealing the hidden terrain or entities underneath.
    if (layer == SpriteLayer::Upright) { return true; }
    const int firstColumn = std::max(0, static_cast<int>((visible.x - layout.x) / layout.cellSize));
    const int firstRow = std::max(0, static_cast<int>((visible.y - layout.y) / layout.cellHeight));
    const int lastColumn = std::min(packet.width - 1, static_cast<int>((visible.x + visible.w - layout.x) / layout.cellSize));
    const int lastRow = std::min(packet.height - 1, static_cast<int>((visible.y + visible.h - layout.y) / layout.cellHeight));
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const auto index = static_cast<std::size_t>(row * packet.width + column);
            if (index >= packet.cells.size() || !packet.cells[index].visible) { continue; }
            const auto cell = CellRect(layout, column, row);
            if (SDL_HasRectIntersectionFloat(&visible, &cell)) { return true; }
        }
    }
    return false;
}

bool DrawLayer(SDL_Renderer* renderer, const SdlAssets& assets, const TilesetMapping& mapping,
    const TerminalAppearance& fallback, const PerceptionTransform& shading, SpriteShader* shader,
    const SDL_FRect& destination, SpriteLayer pass,
    SpriteLayer fallbackLayer = SpriteLayer::Ground) {
    if (mapping.kind == TilesetMappingKind::Missing) {
        if (pass != fallbackLayer) { return true; }
        const auto source = SdlAssets::GlyphRect(fallback.symbol);
        return SDL_SetTextureColorMod(assets.Atlas(), fallback.foreground.red,
                   fallback.foreground.green, fallback.foreground.blue)
            && SDL_RenderTexture(renderer, assets.Atlas(), &source, &destination);
    }
    const auto& definition = assets.Definition();
    const auto& sprite = definition.sprites[mapping.index];
    if (sprite.layer != pass) { return true; }
    const auto geometry = Geometry(sprite, mapping.transform, destination, definition);
    const auto& atlas = definition.atlases[sprite.atlasIndex];
    const float left = float(sprite.x) / atlas.width, top = float(sprite.y) / atlas.height;
    const float right = float(sprite.x + sprite.width) / atlas.width, bottom = float(sprite.y + sprite.height) / atlas.height;
    const SDL_FPoint uv[] = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
    SDL_Vertex vertices[4]{};
    for (unsigned i = 0; i < 4; ++i) { vertices[i] = {geometry.points[i], {1, 1, 1, 1}, uv[i]}; }
    constexpr int indices[] = {0, 1, 2, 0, 2, 3};
    if (shader) {
        if (!shader->Apply(shading)) { return false; }
    } else if (shading != PerceptionTransform{}) {
        return SDL_SetError("Perceived PNG artwork requires the GPU sprite shader");
    }
    const bool drawn = SDL_RenderGeometry(renderer, assets.Texture(sprite.atlasIndex), vertices, 4, indices, 6);
    const bool cleared = !shader || shader->Clear();
    return drawn && cleared;
}

}

MapLayout MakeMapLayout(int outputWidth, int outputHeight, int columns, int rows,
    int tileWidth, int tileHeight, int zoom, float uiScale) {
    return MakeGameplayLayout(outputWidth, outputHeight, columns, rows,
        tileWidth, tileHeight, zoom, uiScale).map;
}

eastl::optional<std::uint64_t> VisibleAnimationDelay(const MapPacket& packet,
    const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds,
    int outputWidth, int outputHeight, int zoom, float uiScale, const SpritePlaybackController* playback, SDL_Point focusOffset) {
    const auto gameplay = MakeGameplayLayout(outputWidth, outputHeight, packet.width, packet.height,
        tileset.cellWidth, tileset.cellHeight, zoom, uiScale, focusOffset);
    const auto& layout = gameplay.map;
    const auto clip = ViewClip(layout, gameplay.mapViewport);
    eastl::optional<std::uint64_t> delay;
    const SpritePlaybackController ambient;
    const auto& controller = playback ? *playback : ambient;
    const auto include = [&](PersistentId item, const TilesetMapping& mapping, const SDL_FRect& cell) {
        const auto sample = controller.Sample(item, mapping, tileset, elapsedMilliseconds);
        if (!sample.delayMilliseconds || !sample.animationIndex) { return; }
        bool visible = false;
        for (const auto& frame : tileset.animations[*sample.animationIndex].frames) {
            const auto& sprite = tileset.sprites[frame.spriteIndex];
            const auto bounds = Geometry(sprite, sample.mapping.transform, cell, tileset).bounds;
            if (SpriteBoundsVisible(bounds, sprite.layer, packet, layout, clip)) { visible = true; break; }
        }
        if (visible && (!delay || *sample.delayMilliseconds < *delay)) { delay = sample.delayMilliseconds; }
    };
    for (int row = 0; row < packet.height; ++row) {
        for (int column = 0; column < packet.width; ++column) {
            const auto index = static_cast<std::size_t>(row) * packet.width + column;
            if (index >= packet.cells.size() || !packet.cells[index].visible) { continue; }
            const auto cell = CellRect(layout, column, row);
            include(0, packet.cells[index].terrainSprite, cell);
            if (packet.cells[index].hasOverlay) { include(packet.cells[index].itemVisual.item, packet.cells[index].overlaySprite, cell); }
        }
    }
    return delay;
}

TerminalAppearance MapAppearance(const ViewCell& cell, PersistentId playerId, const ContentCatalog& content) {
    ViewCell display = cell;
    if (cell.actor != 0 && cell.actor == playerId) { display.player = true; }
    TerminalAppearance appearance = CellAppearance(display, content);
    if (cell.overlayVisibility == Visibility::Visible && cell.actor != 0 && cell.player && cell.actor != playerId) { appearance.symbol = "P"; }
    return appearance;
}

bool DrawFrame(SDL_Renderer* renderer, SdlText& text, const SdlAssets& assets, const WorldView* view,
    const ContentCatalog& content, const char* status, const char* /*hints*/,
    std::uint64_t elapsedMilliseconds, const MapPacket* packet, int zoom, float uiScale, UiPalette palette,
    const SpritePlaybackController* playback, SpriteShader* shader, ScreenFrame* screen, GameplayHud hud) {
    if (!SDL_IsMainThread()) { std::abort(); }
    int width = 0, height = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &width, &height)
        || !SDL_SetRenderScale(renderer, 1, 1) || !Color(renderer, palette.background)
        || !SDL_RenderClear(renderer)) { return false; }
    const int uiWidth = static_cast<int>(width / uiScale);
    const auto focus = screen && screen->visible && screen->followCursor && screen->cursor && view
        ? SDL_Point{screen->cursor->x - view->player_position.x, screen->cursor->z - view->player_position.z} : SDL_Point{};
    const auto gameplay = MakeGameplayLayout(width, height, view ? view->width : 1, view ? view->height : 1,
        assets.Definition().cellWidth, assets.Definition().cellHeight, zoom, uiScale, focus);
    int panelX = gameplay.hud.x, panelY = gameplay.hud.y;
    int panelWidth = gameplay.hud.w;
    String infoText;
    const SpritePlaybackController ambient;
    const auto& controller = playback ? *playback : ambient;
    if (assets.State() == AssetState::Ready && view) {
        const auto& definition = assets.Definition();
        MapPacket immediate;
        if (!packet) { immediate = PrepareMapPacket(*view, content, definition, {}); packet = &immediate; }
        const auto& layout = gameplay.map;
        const auto clip = ViewClip(layout, gameplay.mapViewport);
        if (!SDL_SetRenderClipRect(renderer, &clip)) { return false; }
        for (int row = 0; row < packet->height; ++row) {
            for (int column = 0; column < packet->width; ++column) {
                const auto index = static_cast<std::size_t>(row) * packet->width + column;
                if (index >= packet->cells.size() || !packet->cells[index].visible) { continue; }
                const auto& cell = packet->cells[index];
                const auto destination = CellRect(layout, column, row);
                if (!Color(renderer, cell.terrain.background) || !SDL_RenderFillRect(renderer, &destination)) { return false; }
            }
        }
        // Row-major order is world Z/X order. Ground cannot erase an upright
        // sprite's overhang, and later upright rows naturally occlude earlier ones.
        for (auto pass : {SpriteLayer::Ground, SpriteLayer::Upright}) {
            for (int row = 0; row < packet->height; ++row) {
                for (int column = 0; column < packet->width; ++column) {
                    const auto index = static_cast<std::size_t>(row * packet->width + column);
                    if (index >= packet->cells.size() || !packet->cells[index].visible) { continue; }
                    const auto& cell = packet->cells[index];
                    const auto destination = CellRect(layout, column, row);
                    if (pass == SpriteLayer::Ground && cell.groundSprite.kind != TilesetMappingKind::Missing
                        && !DrawLayer(renderer, assets, cell.groundSprite, cell.terrain, cell.terrainShading, shader, destination, pass)) { return false; }
                    const auto terrainSample = controller.Sample(0, cell.terrainSprite, definition, elapsedMilliseconds);
                    if (!DrawLayer(renderer, assets, terrainSample.mapping, cell.terrain, cell.terrainShading, shader, destination, pass)) { return false; }
                    if (cell.hasOverlay) {
                        const auto overlaySample = controller.Sample(cell.itemVisual.item, cell.overlaySprite, definition, elapsedMilliseconds);
                        if (!DrawLayer(renderer, assets, overlaySample.mapping, cell.overlay, cell.overlayShading, shader, destination, pass, SpriteLayer::Upright)) { return false; }
                    }
                }
            }
            if (pass == SpriteLayer::Ground) {
                // Mask ground overhang before upright sprites so visible trees
                // retain their tops at visibility boundaries.
                if (!Color(renderer, palette.background)) { return false; }
                for (int row = 0; row < packet->height; ++row) {
                    for (int column = 0; column < packet->width; ++column) {
                        const auto index = static_cast<std::size_t>(row * packet->width + column);
                        if (index < packet->cells.size() && packet->cells[index].visible) { continue; }
                        const auto destination = CellRect(layout, column, row);
                        if (!SDL_RenderFillRect(renderer, &destination)) { return false; }
                    }
                }
            }
        }
        if (!SDL_SetRenderClipRect(renderer, nullptr)) { return false; }
        char info[256];
        std::snprintf(info, sizeof(info), "Tick %llu  XYZ %d,%d,%d\n%s%s\nItems %zu  Mass %llug / %llug",
            static_cast<unsigned long long>(view->tick), view->player_position.x, view->player_position.y, view->player_position.z,
            view->remainingTicks == 0 ? "Ready" : "Waiting", view->commandQueued ? "  (queued)" : "",
            static_cast<std::size_t>(view->inventory.size()), static_cast<unsigned long long>(view->carriedMassGrams),
            static_cast<unsigned long long>(view->carryCapacityGrams));
        infoText = info;
        if (!gameplay.sideHud) {
            std::snprintf(info, sizeof(info), "%s | T%llu\nX%d Z%d Y%d\n%zu items | %llu/%llug",
                view->remainingTicks == 0 ? "Ready" : "Waiting",
                static_cast<unsigned long long>(view->tick), view->player_position.x, view->player_position.z, view->player_position.y,
                static_cast<std::size_t>(view->inventory.size()), static_cast<unsigned long long>(view->carriedMassGrams),
                static_cast<unsigned long long>(view->carryCapacityGrams));
            infoText = info;
        }
    } else { infoText = "Loading tileset and session..."; }
    if (!SDL_SetRenderScale(renderer, uiScale, uiScale)) { return false; }
    const SDL_Rect barClip = gameplay.topBar;
    if (!SDL_SetRenderClipRect(renderer, &barClip)
        || !text.Draw(0, "CataclysmX", -10000, -10000, TextColor(palette.hints))) { return false; }
    const auto sessionLabel = hud.localization ? hud.localization->Text("sdl.ui.local", "Local") : String("Local");
    if (!text.Draw(61, sessionLabel.c_str(), -10000, -10000, TextColor(palette.hints))) { return false; }
    constexpr const char* defaultKeys[] = {"F1", "F2", "F11"};
    constexpr const char* defaultLabels[] = {"Help", "Settings", "Debug"};
    constexpr const char* labelKeys[] = {"game.ui.top_help", "game.ui.top_settings", "game.ui.top_debug"};
    String labels[3];
    int labelWidths[3]{};
    for (int i = 0; i < 3; ++i) {
        labels[i] = hud.keys ? BindingHint(*hud.keys, InputContext::Gameplay, topActions[i]) : defaultKeys[i];
        labels[i] += ' ';
        labels[i] += hud.localization ? hud.localization->Text(labelKeys[i]) : String(defaultLabels[i]);
        if (!text.Draw(58 + i, labels[i].c_str(), -10000, -10000, TextColor(palette.text))) { return false; }
        labelWidths[i] = text.Width(58 + i);
    }
    const auto bar = MakeTopBarLayout(uiWidth, text.Width(0), text.Width(61), labelWidths);
    if (bar.showTitle && !text.Draw(0, "CataclysmX", 12, 5, TextColor(palette.hints))) { return false; }
    int labelX = bar.showTitle ? 12 + text.Width(0) + 20 : 12;
    const SDL_Rect keysClip{labelX, 0, std::max(0, uiWidth - text.Width(61) - 24 - labelX), gameplay.topBar.h};
    if (!SDL_SetRenderClipRect(renderer, &keysClip)) { return false; }
    for (int i = 0; i < bar.actionCount; ++i) {
        if (i > 0) {
            if (!SDL_SetRenderDrawColor(renderer, palette.hints.red, palette.hints.green, palette.hints.blue, 255)
                || !SDL_RenderLine(renderer, float(labelX - 10), 10, float(labelX - 10), 18)) { return false; }
        }
        if (!text.Draw(58 + i, labels[i].c_str(), float(labelX), 5, TextColor(palette.text))) { return false; }
        labelX += labelWidths[i] + 20;
    }
    if (!SDL_SetRenderClipRect(renderer, &barClip)
        || !text.Draw(61, sessionLabel.c_str(), float(std::max(12, uiWidth - text.Width(61) - 12)), 5, TextColor(palette.hints))) { return false; }
    int healthHeight = 0;
    if (view) {
        SDL_Rect health = gameplay.hud;
        if (!gameplay.sideHud) { health.w = std::min(244, std::max(1, health.w / 2 - 16)); }
        if (!DrawHealth(renderer, text, view->combat, health, healthHeight, hud.localization)) { return false; }
        if (healthHeight > 0) {
            if (gameplay.sideHud) { panelY += healthHeight + 12; }
            else { panelX += health.w + 12; panelWidth -= health.w + 12; }
        }
    }
    const SDL_Rect panelClip{panelX, panelY, std::max(0, panelWidth), std::max(0, gameplay.hud.y + gameplay.hud.h - panelY)};
    const int wrap = std::max(1, panelWidth - 4);
    const int panelBottom = panelClip.y + panelClip.h;
    const int historyReserve = hud.history ? std::min(64, panelClip.h / 2) : 0;
    const int statusLimit = panelBottom - historyReserve;
    if (!text.Draw(7, "M", -10000, -10000, TextColor(palette.text))) { return false; }
    const int lineHeight = std::max(1, text.Height(7));
    const int infoHeight = std::max(0, statusLimit - panelY) / lineHeight * lineHeight;
    const SDL_Rect infoClip{panelX, panelY, wrap, infoHeight};
    if (!SDL_SetRenderClipRect(renderer, &infoClip)
        || !text.Draw(1, infoText.c_str(), float(panelX), float(panelY), TextColor(palette.text), wrap)) { return false; }
    int statusY = panelY + std::min(infoHeight, text.Height(1)) + 8;
    if (status && *status) {
        const int available = std::max(0, statusLimit - statusY) / lineHeight * lineHeight;
        const SDL_Rect statusClip{panelX, statusY, wrap, available};
        if (!SDL_SetRenderClipRect(renderer, &statusClip)
            || !text.Draw(2, status, float(panelX), float(statusY), TextColor(palette.text), wrap)) { return false; }
        statusY += std::min(available, text.Height(2)) + 8;
    }
    if (hud.history) {
        statusY = std::min(statusY, statusLimit);
        const auto heading = hud.localization ? hud.localization->Text("game.ui.message_history") : String("Message history");
        if (!SDL_SetRenderClipRect(renderer, &panelClip)
            || !text.Draw(3, heading.c_str(), float(panelX), float(statusY), TextColor(palette.hints), wrap)) { return false; }
        statusY += text.Height(3) + 4;
        String messages;
        for (const auto& entry : hud.history->Entries()) {
            if (!messages.empty()) { messages += '\n'; }
            messages += entry;
        }
        const SDL_Rect historyClip{panelX, statusY, wrap, std::max(0, panelBottom - statusY)};
        if (!SDL_SetRenderClipRect(renderer, &historyClip)
            || !text.Draw(4, messages.c_str(), -10000, -10000, TextColor(palette.text), wrap)) { return false; }
        const int y = statusY - std::max(0, text.Height(4) - historyClip.h);
        if (!text.Draw(4, messages.c_str(), float(panelX), float(y), TextColor(palette.text), wrap)) { return false; }
    }
    if (screen && !DrawScreen(renderer, text, *screen, width, height, view,
        assets.Definition().cellWidth, assets.Definition().cellHeight, zoom, uiScale, &gameplay)) { return false; }
    return SDL_SetRenderClipRect(renderer, nullptr) && SDL_SetRenderScale(renderer, 1, 1) && SDL_RenderPresent(renderer);
}

}
