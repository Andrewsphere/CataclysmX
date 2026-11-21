#include "sdl/layout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cataclysmx::sdl {

TopBarLayout MakeTopBarLayout(int width, int titleWidth, int statusWidth, const int (&actionWidths)[3]) {
    const int available = width - statusWidth - 36;
    int used = actionWidths[0] + actionWidths[1] + actionWidths[2] + 40;
    if (used + titleWidth + 20 <= available) { return {true, 3}; }
    if (used <= available) { return {false, 3}; }
    used -= actionWidths[2] + 20;
    return {false, used <= available ? 2 : 1};
}

SDL_Rect ScaleRect(SDL_Rect rect, float scale) {
    const int x = static_cast<int>(rect.x * scale);
    const int y = static_cast<int>(rect.y * scale);
    return {x, y, static_cast<int>((rect.x + rect.w) * scale) - x,
        static_cast<int>((rect.y + rect.h) * scale) - y};
}

GameplayLayout MakeGameplayLayout(int width, int height, int columns, int rows,
    int tileWidth, int tileHeight, int zoom, float uiScale, SDL_Point focusOffset, int textLineHeight) {
    if (width <= 0 || height <= 0 || columns <= 0 || rows <= 0
        || tileWidth <= 0 || tileHeight <= 0 || !std::isfinite(uiScale) || uiScale <= 0) { return {}; }
    const int w = std::max(1, static_cast<int>(width / uiScale));
    const int h = std::max(1, static_cast<int>(height / uiScale));
    GameplayLayout result;
    result.sideHud = w >= 700;
    const int barHeight = std::min(std::max(28, textLineHeight + 8), h);
    result.topBar = {0, 0, w, barHeight};
    const int hudWidth = w >= 900 ? 390 : 230;
    result.hud = result.sideHud ? SDL_Rect{w - hudWidth, barHeight + 8, hudWidth - 8, std::max(0, h - barHeight - 16)}
        : SDL_Rect{8, std::max(barHeight, h - 172), std::max(0, w - 16), std::min(164, std::max(0, h - barHeight - 8))};
    const SDL_Rect viewport{0, result.topBar.h,
        result.sideHud ? std::max(1, result.hud.x - 10) : w,
        std::max(1, (result.sideHud ? h - 8 : result.hud.y - 8) - result.topBar.h)};
    result.mapViewport = ScaleRect(viewport, uiScale);
    const double scale = std::exp2(std::clamp(zoom, -8, 12) / 4.0);
    const int cellWidth = std::max(1, static_cast<int>(tileWidth * scale));
    const int cellHeight = std::max(1, static_cast<int>(tileHeight * scale));
    const int mapWidth = columns * cellWidth, mapHeight = rows * cellHeight;
    result.map = {result.mapViewport.x + (result.mapViewport.w - mapWidth) / 2 - focusOffset.x * cellWidth,
        result.mapViewport.y + (result.mapViewport.h - mapHeight) / 2 - focusOffset.y * cellHeight,
        cellWidth, mapWidth, mapHeight, cellHeight};
    return result;
}

SDL_Rect MapCellRect(const MapLayout& map, int column, int row) {
    return {map.x + column * map.cellSize, map.y + row * map.cellHeight, map.cellSize, map.cellHeight};
}

SDL_Rect PlacePopup(SDL_Rect viewport, SDL_Rect target, int width, int height, SDL_Rect avoid, int minimumWidth) {
    if (viewport.w <= 0 || viewport.h <= 0) { return {}; }
    width = std::clamp(width, 1, viewport.w);
    height = std::clamp(height, 1, viewport.h);
    const int right = target.x + target.w + 8, left = target.x - width - 8;
    SDL_Rect candidates[] = {{right, target.y, width, height}, {left, target.y, width, height},
        {target.x, target.y + target.h + 8, width, height}, {target.x, target.y - height - 8, width, height}};
    SDL_Rect best{};
    std::int64_t bestScore = -1;
    for (auto candidate : candidates) {
        candidate.x = std::clamp(candidate.x, viewport.x, viewport.x + viewport.w - width);
        candidate.y = std::clamp(candidate.y, viewport.y, viewport.y + viewport.h - height);
        SDL_Rect overlap{};
        std::int64_t score = 0;
        if (SDL_GetRectIntersection(&candidate, &target, &overlap)) { score += std::int64_t{overlap.w} * overlap.h * 1000; }
        if (SDL_GetRectIntersection(&candidate, &avoid, &overlap)) { score += std::int64_t{overlap.w} * overlap.h; }
        if (bestScore < 0 || score < bestScore) { best = candidate; bestScore = score; }
    }
    // A centered tile on a compact map can leave neither side wide enough for
    // the preferred width. Narrow the panel if its contents still fit.
    if (bestScore >= 1000) {
        const int rightSpace = viewport.x + viewport.w - right;
        const int leftSpace = target.x - 8 - viewport.x;
        if (std::max(rightSpace, leftSpace) >= minimumWidth) {
            best.w = std::min(width, std::max(rightSpace, leftSpace));
            best.x = rightSpace >= leftSpace ? right : target.x - 8 - best.w;
            best.y = std::clamp(target.y, viewport.y, viewport.y + viewport.h - height);
        }
    }
    return best;
}

namespace {
constexpr int inventoryMargin = 4;
constexpr int inventoryTileGap = 8;
}

SDL_Rect FitInventoryMap(GameplayLayout& gameplay, int outputHeight, float uiScale,
    int columns, int rows, SDL_Point playerCell) {
    auto& viewport = gameplay.mapViewport;
    auto& map = gameplay.map;
    if (viewport.h < 3 * std::min(16, map.cellHeight)) {
        // Very large UI text can leave only a sliver above the bottom HUD.
        const int top = ScaleRect(gameplay.topBar, uiScale).h;
        if (outputHeight - top > viewport.h) {
            viewport.y = top;
            viewport.h = outputHeight - top;
            if (!gameplay.sideHud) { gameplay.hud = {}; }
        }
    }
    const int availableWidth = viewport.w - 2 * (inventoryTileGap + inventoryMargin);
    const int minimumPaneWidth = std::min(200, viewport.w / 3);
    const double fittedTileWidth = std::max(1.0, std::min({double(map.cellSize),
        (availableWidth - 2 * minimumPaneWidth) / 3.0, viewport.h * double(map.cellSize) / (3.0 * map.cellHeight)}));
    const double preferredTileWidth = (availableWidth - 2.0 * preferredInventoryPaneWidth * uiScale) / 3.0;
    // Give each window up to 12 extra pixels where its preferred width cannot fit.
    const double minimumTileWidth = std::max(std::min(24.0, fittedTileWidth), fittedTileWidth - 8.0);
    const double tileWidth = std::clamp(preferredTileWidth, minimumTileWidth, fittedTileWidth);
    const int cellWidth = static_cast<int>(tileWidth);
    const int cellHeight = std::max(1, static_cast<int>(tileWidth * map.cellHeight / map.cellSize));
    const int playerX = viewport.x + (viewport.w - cellWidth) / 2;
    const int playerY = viewport.y + (viewport.h - cellHeight) / 2;
    map = {playerX - playerCell.x * cellWidth, playerY - playerCell.y * cellHeight, cellWidth,
        columns * cellWidth, rows * cellHeight, cellHeight};
    return {playerX - cellWidth, playerY - cellHeight, 3 * cellWidth, 3 * cellHeight};
}

eastl::array<SDL_Rect, 2> PlaceInventoryPopups(SDL_Rect content, SDL_Rect protectedArea, int preferredWidth) {
    if (content.w <= 2 * inventoryMargin || content.h <= 2 * inventoryMargin) { return {}; }
    if (SDL_RectEmpty(&protectedArea)) {
        protectedArea = {content.x + content.w / 2, content.y + content.h / 2, 0, 0};
    }
    const int leftEnd = protectedArea.x - inventoryTileGap;
    const int rightStart = protectedArea.x + protectedArea.w + inventoryTileGap;
    const int leftWidth = std::clamp(leftEnd - content.x - inventoryMargin, 0, preferredWidth);
    const int rightWidth = std::clamp(content.x + content.w - inventoryMargin - rightStart, 0, preferredWidth);
    const int height = (content.h - 2 * inventoryMargin) * 3 / 4;
    const int y = std::clamp(protectedArea.y + (protectedArea.h - height) / 2,
        content.y + inventoryMargin, content.y + content.h - height - inventoryMargin);
    return {{{leftEnd - leftWidth, y, leftWidth, height}, {rightStart, y, rightWidth, height}}};
}

}
