#pragma once

#include <SDL3/SDL_rect.h>

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
    SDL_Point focusOffset = {});
SDL_Rect ScaleRect(SDL_Rect rect, float scale);
SDL_Rect MapCellRect(const MapLayout& map, int column, int row);
// All arguments and the result use logical UI pixels. Prefer not to cover avoid.
SDL_Rect PlacePopup(SDL_Rect viewport, SDL_Rect target, int width, int height,
    SDL_Rect avoid = {});

}
