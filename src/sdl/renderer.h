#pragma once

#include "sdl/assets.h"
#include "sdl/animation.h"
#include "sdl/map_packet.h"
#include "sdl/text.h"
#include "sdl/sprite_shader.h"

namespace cataclysmx::sdl {

struct MapLayout {
    int x = 0;
    int y = 0;
    int cellSize = 1; // Cell width in render pixels.
    int width = 0;
    int height = 0;
    int cellHeight = 1;
};

struct UiPalette {
    RgbColor background{12, 16, 20};
    RgbColor text{205, 215, 210};
    RgbColor hints{137, 160, 152};
};

MapLayout MakeMapLayout(int outputWidth, int outputHeight, int columns, int rows,
    int tileWidth = 32, int tileHeight = 32, int zoom = 0, float uiScale = 1.0f);
bool MapCellAt(const MapLayout& layout, int x, int y, int& column, int& row);
// Coordinates are render pixels, after SDL_RenderCoordinatesFromWindow conversion.
eastl::optional<std::size_t> MapItemAt(const MapPacket& packet, const TilesetDefinition& tileset,
    float x, float y, int outputWidth, int outputHeight, int zoom = 0, float uiScale = 1.0f);
eastl::optional<std::uint64_t> VisibleAnimationDelay(const MapPacket& packet,
    const TilesetDefinition& tileset, std::uint64_t elapsedMilliseconds,
    int outputWidth, int outputHeight, int zoom = 0, float uiScale = 1.0f,
    const SpritePlaybackController* playback = nullptr);
TerminalAppearance MapAppearance(const ViewCell& cell, PersistentId playerId, const ContentCatalog& content);
// An injected presentation clock samples clips without rebuilding the supplied packet.
// A null packet prepares the small map synchronously for isolated rendering tests.
// Nonidentity PNG shading requires a shader created for this renderer. A null shader
// supports glyph-only and ordinary, depth-zero artwork tests on a software renderer.
bool DrawFrame(SDL_Renderer* renderer, SdlText& text, const SdlAssets& assets, const WorldView* view,
    const ContentCatalog& content, const char* status, const char* hints,
    std::uint64_t elapsedMilliseconds = 0, const MapPacket* packet = nullptr,
    int zoom = 0, float uiScale = 1.0f, UiPalette palette = {},
    const SpritePlaybackController* playback = nullptr, SpriteShader* shader = nullptr);

}
