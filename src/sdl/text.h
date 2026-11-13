#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <EASTL/array.h>

#include "core/containers.h"

namespace cataclysmx::sdl {

class SdlText {
public:
    static constexpr unsigned int slotCount = 64;

    explicit SdlText(SDL_Renderer* renderer, const char* fontPath = nullptr, float pointSize = 16.0f);
    ~SdlText();
    SdlText(const SdlText&) = delete;
    SdlText& operator=(const SdlText&) = delete;

    bool Ready() const { return ready_; }
    bool Draw(unsigned int slot, const char* utf8, float x, float y, SDL_Color color, int wrapWidth = 0);
    int Height(unsigned int slot) const;

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
};

}
