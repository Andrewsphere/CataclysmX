#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <EASTL/array.h>
#include <cstdint>

#include "core/containers.h"

namespace cataclysmx::sdl {

class SdlText {
public:
    static constexpr unsigned int slotCount = 320; // UI 0..127, art runs 128..255, art glyphs 256..319.

    explicit SdlText(SDL_Renderer* renderer, const char* fontPath = nullptr, float pointSize = 16.0f);
    ~SdlText();
    SdlText(const SdlText&) = delete;
    SdlText& operator=(const SdlText&) = delete;

    bool Ready() const { return ready_; }
    bool Draw(unsigned int slot, const char* utf8, float x, float y, SDL_Color color, int wrapWidth = 0);
    // A colored art run on an explicit single-cell grid. Monospaced fonts draw
    // the entire run using its stable slot; proportional fonts use a bounded
    // codepoint cache and explicit advances instead.
    bool DrawCells(unsigned int slot, const char* utf8, std::uint32_t cells,
        float x, float y, SDL_Color color, int cellWidth);
    int Height(unsigned int slot) const;
    int Width(unsigned int slot) const;
    int LineHeight() const;

private:
    struct Slot {
        TTF_Text* text = nullptr;
        String utf8;
        int wrapWidth = 0;
        SDL_Color color{};
        bool hasColor = false;
    };

    TTF_Font* font_ = nullptr;
    TTF_TextEngine* engine_ = nullptr;
    eastl::array<Slot, slotCount> slots_{};
    bool ttfInitialized_ = false;
    bool ready_ = false;
    bool monospaced_ = false;
};

}
