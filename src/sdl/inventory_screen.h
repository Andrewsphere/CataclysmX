#pragma once

#include "client/item_ui.h"
#include "sdl/assets.h"
#include "sdl/text.h"

namespace cataclysmx::sdl {

class HeldKey;
// Consumes the chord unconditionally. Screen transitions terminate the hold so
// accepting a row cannot repeat into its examination or quantity dialog.
void HandleInventoryKey(ItemUi& ui, HeldKey& held, const KeyChord& key, const Keymap& keys,
    const WorldView& view, const ContentCatalog& content, const Localization& localization);
SDL_Color ItemArtColor(ArtColor color);
// Fits the actual cropped sprite, independent of world cell size and map zoom.
SDL_FRect FitItemIcon(SDL_FRect bounds, float width, float height);
int ItemPaneScroll(int scroll, std::size_t count, std::size_t selected, bool selectionValid, int visibleRows);

// Render pixels shared with the gameplay map. Empty tile rectangles have no anchor.
struct InventoryMapGeometry {
    SDL_Rect viewport{};
    eastl::array<SDL_Rect, 2> tiles{};
    SDL_Rect protectedArea{};
    int contentTop = 0;
};
bool DrawInventoryScreen(SDL_Renderer* renderer, SdlText& text, const SdlAssets& assets,
    const ContentCatalog& content, ItemUiFrame& frame, int outputWidth, int outputHeight, float uiScale,
    bool retainClosed = false, const InventoryMapGeometry* map = nullptr, const char* footerOverride = nullptr);

}
