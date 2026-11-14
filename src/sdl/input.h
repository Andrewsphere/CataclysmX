#pragma once

#include "client/input.h"

#include <SDL3/SDL_events.h>

namespace cataclysmx::sdl {

eastl::optional<KeyChord> TranslateKey(const SDL_KeyboardEvent& event);
int MapZoomChange(const KeyChord& chord);
// Frontend clock only. One latest key, no catch-up burst after stalls.
class HeldKey {
public:
    bool Observe(const SDL_Event& event, std::uint64_t nowMilliseconds);
    eastl::optional<SDL_KeyboardEvent> Poll(std::uint64_t nowMilliseconds, std::uint32_t rate);
    void Reset();
    eastl::optional<std::uint64_t> Delay(std::uint64_t nowMilliseconds) const;
    const String& Text() const { return text_; }
private:
    eastl::optional<SDL_KeyboardEvent> key_;
    String text_;
    SDL_Keymod modifiers_ = SDL_KMOD_NONE;
    std::uint64_t next_ = 0;
    bool suppressText_ = false;
};

}
