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
    int tileWidth, int tileHeight, int zoom, float uiScale, SDL_Point focusOffset) {
    if (width <= 0 || height <= 0 || columns <= 0 || rows <= 0
        || tileWidth <= 0 || tileHeight <= 0 || !std::isfinite(uiScale) || uiScale <= 0) { return {}; }
    const int w = std::max(1, static_cast<int>(width / uiScale));
    const int h = std::max(1, static_cast<int>(height / uiScale));
    GameplayLayout result;
    result.sideHud = w >= 700;
    result.topBar = {0, 0, w, std::min(28, h)};
    const int hudWidth = w >= 900 ? 390 : 230;
    result.hud = result.sideHud ? SDL_Rect{w - hudWidth, 36, hudWidth - 8, std::max(0, h - 44)}
        : SDL_Rect{8, std::max(28, h - 172), std::max(0, w - 16), std::min(164, std::max(0, h - 36))};
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

SDL_Rect PlacePopup(SDL_Rect viewport, SDL_Rect target, int width, int height, SDL_Rect avoid) {
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
    // the preferred width. Narrow the panel rather than hide its anchor.
    if (bestScore >= 1000) {
        const int rightSpace = viewport.x + viewport.w - right;
        const int leftSpace = target.x - 8 - viewport.x;
        if (std::max(rightSpace, leftSpace) >= 180) {
            best.w = std::min(width, std::max(rightSpace, leftSpace));
            best.x = rightSpace >= leftSpace ? right : target.x - 8 - best.w;
            best.y = std::clamp(target.y, viewport.y, viewport.y + viewport.h - height);
        }
    }
    return best;
}

}
