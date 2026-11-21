#pragma once

#include <SDL3/SDL_rect.h>
#include <EASTL/array.h>

namespace cataclysmx::sdl {

struct MapLayout {
    int x = 0;
    int y = 0;
    int cellSize = 1;
    int width = 0;
    int height = 0;
    int cellHeight = 1;
};

struct GameplayLayout {
    SDL_Rect topBar{}; // Logical UI pixels.
    SDL_Rect hud{}; // Logical UI pixels.
    SDL_Rect mapViewport{}; // Render pixels, like map.
    MapLayout map;
    bool sideHud = false;
};

struct TopBarLayout {
    bool showTitle = false;
    int actionCount = 1;
};
TopBarLayout MakeTopBarLayout(int width, int titleWidth, int statusWidth, const int (&actionWidths)[3]);

GameplayLayout MakeGameplayLayout(int width, int height, int columns, int rows,
    int tileWidth = 32, int tileHeight = 32, int zoom = 0, float uiScale = 1.0f,
    SDL_Point focusOffset = {}, int textLineHeight = 0);
SDL_Rect ScaleRect(SDL_Rect rect, float scale);
SDL_Rect MapCellRect(const MapLayout& map, int column, int row);
// All arguments and the result use logical UI pixels. Prefer not to cover avoid.
SDL_Rect PlacePopup(SDL_Rect viewport, SDL_Rect target, int width, int height,
    SDL_Rect avoid = {}, int minimumWidth = 180);
inline constexpr int preferredInventoryPaneWidth = 400; // Logical UI pixels.
// Fits the map around the player and returns the protected 3x3 in render pixels.
SDL_Rect FitInventoryMap(GameplayLayout& gameplay, int outputHeight, float uiScale,
    int columns, int rows, SDL_Point playerCell);
// Content, the protected area, preferred width, and both windows use render pixels.
eastl::array<SDL_Rect, 2> PlaceInventoryPopups(SDL_Rect content, SDL_Rect protectedArea, int preferredWidth);

}
