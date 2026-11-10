#include "sdl/renderer.h"

#include "client/text.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace cataclysmx::sdl {
namespace {

bool Color(SDL_Renderer* renderer, RgbColor color) {
    return SDL_SetRenderDrawColor(renderer, color.red, color.green, color.blue, 255);
}

SDL_Color TextColor(RgbColor color) { return {color.red, color.green, color.blue, 255}; }

SDL_Rect MapClip(int width, int height, float uiScale) {
    const int uiWidth = static_cast<int>(width / uiScale), uiHeight = static_cast<int>(height / uiScale);
    const bool side = uiWidth >= 700;
    const int panelX = side ? uiWidth - (uiWidth >= 900 ? 390 : 230) : 10;
    const int panelY = side ? 36 : std::max(32, uiHeight - 200);
    const int top = static_cast<int>(32 * uiScale);
    return {0, top, side ? static_cast<int>((panelX - 10) * uiScale) : width,
        side ? std::max(0, height - top) : std::max(0, static_cast<int>((panelY - 36) * uiScale))};
}

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
    const TerminalAppearance& fallback, const SDL_FRect& destination, SpriteLayer pass,
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
    return SDL_RenderGeometry(renderer, assets.Texture(sprite.atlasIndex), vertices, 4, indices, 6);
}

}

MapLayout MakeMapLayout(int outputWidth, int outputHeight, int columns, int rows,
    int tileWidth, int tileHeight, int zoom, float uiScale) {
    if (columns <= 0 || rows <= 0 || outputWidth <= 0 || outputHeight <= 0
        || tileWidth <= 0 || tileHeight <= 0 || uiScale <= 0) { return {}; }
    const bool side = outputWidth / uiScale >= 700;
    const int panelWidth = static_cast<int>((outputWidth / uiScale >= 900 ? 400 : 240) * uiScale);
    const int top = static_cast<int>(32 * uiScale);
    const int availableWidth = side ? std::max(1, outputWidth - panelWidth) : outputWidth;
    const int availableHeight = std::max(1, outputHeight - static_cast<int>((side ? 50 : 240) * uiScale));
    const double fit = std::min(double(availableWidth) / columns / tileWidth,
        double(availableHeight) / rows / tileHeight);
    // Keep rectangular pack cells in their native aspect ratio, independently of UI scale.
    const double scale = std::max(1.0 / std::max(tileWidth, tileHeight), fit + zoom * 0.25);
    const int cellWidth = std::max(1, static_cast<int>(tileWidth * scale));
    const int cellHeight = std::max(1, static_cast<int>(tileHeight * scale));
    const int width = columns * cellWidth;
    const int height = rows * cellHeight;
    // World views center the character. Allow negative origins so zoom crops
    // around that center instead of pinning an oversized map to its corner.
    return {availableWidth / 2 - width / 2, top + availableHeight / 2 - height / 2,
        cellWidth, width, height, cellHeight};
}

bool MapCellAt(const MapLayout& layout, int x, int y, int& column, int& row) {
    if (layout.cellSize <= 0 || layout.cellHeight <= 0 || x < layout.x || y < layout.y
        || x >= layout.x + layout.width || y >= layout.y + layout.height) { return false; }
    column = (x - layout.x) / layout.cellSize;
    row = (y - layout.y) / layout.cellHeight;
    return true;
}

eastl::optional<std::size_t> MapItemAt(const MapPacket& packet, const TilesetDefinition& tileset,
    float x, float y, int outputWidth, int outputHeight, int zoom, float uiScale) {
    const auto clip = MapClip(outputWidth, outputHeight, uiScale);
    if (!(x >= clip.x && y >= clip.y && x < clip.x + clip.w && y < clip.y + clip.h)) { return {}; }
    const auto layout = MakeMapLayout(outputWidth, outputHeight, packet.width, packet.height,
        tileset.cellWidth, tileset.cellHeight, zoom, uiScale);
    int column = 0, row = 0;
    if (!MapCellAt(layout, static_cast<int>(x), static_cast<int>(y), column, row)) { return {}; }
    const auto index = static_cast<std::size_t>(row) * packet.width + column;
    if (index >= packet.cells.size()) { return {}; }
    const auto& cell = packet.cells[index];
    if (!cell.visible || !cell.hasOverlay || !cell.itemVisual.item) { return {}; }
    return index;
}

eastl::optional<std::uint64_t> VisibleAnimationDelay(const MapPacket& packet,
    const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds,
    int outputWidth, int outputHeight, int zoom, float uiScale, const SpritePlaybackController* playback) {
    const auto layout = MakeMapLayout(outputWidth, outputHeight, packet.width, packet.height,
        tileset.cellWidth, tileset.cellHeight, zoom, uiScale);
    const auto clip = ViewClip(layout, MapClip(outputWidth, outputHeight, uiScale));
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
    const ContentCatalog& content, const char* status, const char* hints,
    std::uint64_t elapsedMilliseconds, const MapPacket* packet, int zoom, float uiScale, UiPalette palette,
    const SpritePlaybackController* playback) {
    if (!SDL_IsMainThread()) { std::abort(); }
    int width = 0, height = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &width, &height)
        || !SDL_SetRenderScale(renderer, 1, 1) || !Color(renderer, palette.background)
        || !SDL_RenderClear(renderer)) { return false; }
    const int uiWidth = static_cast<int>(width / uiScale), uiHeight = static_cast<int>(height / uiScale);
    const bool side = uiWidth >= 700;
    const int panelX = side ? uiWidth - (uiWidth >= 900 ? 390 : 230) : 10;
    const int panelY = side ? 36 : std::max(32, uiHeight - 200);
    String infoText;
    const SpritePlaybackController ambient;
    const auto& controller = playback ? *playback : ambient;
    if (assets.State() == AssetState::Ready && view) {
        const auto& definition = assets.Definition();
        MapPacket immediate;
        if (!packet) { immediate = PrepareMapPacket(*view, content, definition, {}); packet = &immediate; }
        const auto layout = MakeMapLayout(width, height, packet->width, packet->height,
            definition.cellWidth, definition.cellHeight, zoom, uiScale);
        const auto clip = ViewClip(layout, MapClip(width, height, uiScale));
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
                        && !DrawLayer(renderer, assets, cell.groundSprite, cell.terrain, destination, pass)) { return false; }
                    const auto terrainSample = controller.Sample(0, cell.terrainSprite, definition, elapsedMilliseconds);
                    if (!DrawLayer(renderer, assets, terrainSample.mapping, cell.terrain, destination, pass)) { return false; }
                    if (cell.hasOverlay) {
                        const auto overlaySample = controller.Sample(cell.itemVisual.item, cell.overlaySprite, definition, elapsedMilliseconds);
                        if (!DrawLayer(renderer, assets, overlaySample.mapping, cell.overlay, destination, pass, SpriteLayer::Upright)) { return false; }
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
    } else { infoText = "Loading tileset and session..."; }
    if (!SDL_SetRenderScale(renderer, uiScale, uiScale)
        || !text.Draw(0, "CATACLYSM X  /  LOCAL", 12, 6, TextColor(palette.hints))) { return false; }
    const SDL_Rect panelClip{panelX, panelY, std::max(0, uiWidth - panelX), std::max(0, uiHeight - panelY)};
    const int wrap = std::max(1, uiWidth - panelX - 10);
    if (!SDL_SetRenderClipRect(renderer, &panelClip)
        || !text.Draw(1, infoText.c_str(), panelX, panelY, TextColor(palette.text), wrap)) { return false; }
    const int statusY = panelY + text.Height(1) + 8;
    if (!text.Draw(2, status, panelX, statusY, TextColor(palette.text), wrap)) { return false; }
    const int hintsY = statusY + text.Height(2) + 12;
    if (side) {
        if (!text.Draw(3, hints, panelX, hintsY, TextColor(palette.hints), wrap)) { return false; }
    } else {
        String left, right;
        const eastl::string_view bindings(hints ? hints : "");
        std::size_t start = 0;
        unsigned line = 0;
        for (std::size_t end = 0; end <= bindings.size(); ++end) {
            if (end != bindings.size() && bindings[end] != '\n') { continue; }
            auto& target = line++ < 6 ? left : right;
            target.append(bindings.data() + start, end - start); target += '\n';
            start = end + 1;
        }
        if (!text.Draw(4, left.c_str(), panelX, hintsY, TextColor(palette.hints), wrap / 2)
            || !text.Draw(5, right.c_str(), panelX + wrap / 2, hintsY, TextColor(palette.hints), wrap / 2)) { return false; }
    }
    return SDL_SetRenderClipRect(renderer, nullptr) && SDL_SetRenderScale(renderer, 1, 1) && SDL_RenderPresent(renderer);
}

}
