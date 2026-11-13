#include "sdl/input.h"
#include "sdl/renderer.h"

#include <SDL3/SDL_keyboard.h>
#include <algorithm>

namespace cataclysmx::sdl {
eastl::optional<SpriteClickResult> ActivateMapItem(SDL_Renderer* renderer, const SDL_MouseButtonEvent& event,
    bool inputBlocked, const MapPacket& packet, const TilesetDefinition& tileset,
    SpritePlaybackController& playback, std::uint64_t elapsedMilliseconds, int zoom, float uiScale) {
    if (inputBlocked || event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button != SDL_BUTTON_LEFT) { return {}; }
    float x = 0, y = 0;
    int width = 0, height = 0;
    if (!SDL_RenderCoordinatesFromWindow(renderer, event.x, event.y, &x, &y)
        || !SDL_GetCurrentRenderOutputSize(renderer, &width, &height)) { return {}; }
    const auto index = MapItemAt(packet, tileset, x, y, width, height, zoom, uiScale);
    if (!index) { return {}; }
    const auto& cell = packet.cells[*index];
    return SpriteClickResult{cell.itemVisual,
        playback.Activate(cell.itemVisual.item, cell.overlaySprite, tileset, elapsedMilliseconds)};
}

eastl::optional<KeyChord> TranslateKey(const SDL_KeyboardEvent& event) {
    if (event.type != SDL_EVENT_KEY_DOWN || (event.mod & SDL_KMOD_GUI)) {
        return eastl::nullopt;
    }
    const bool alt = (event.mod & SDL_KMOD_ALT) != 0;
    const bool ctrl = (event.mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (event.mod & SDL_KMOD_SHIFT) != 0;
    const char* name = nullptr;
    switch (event.key) {
    case SDLK_LEFT: name = "Left"; break;
    case SDLK_RIGHT: name = "Right"; break;
    case SDLK_UP: name = "Up"; break;
    case SDLK_DOWN: name = "Down"; break;
    case SDLK_ESCAPE: name = "Escape"; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: name = "Enter"; break;
    case SDLK_TAB: name = shift ? "BackTab" : "Tab"; break;
    case SDLK_BACKSPACE: name = "Backspace"; break;
    case SDLK_DELETE: name = "Delete"; break;
    case SDLK_INSERT: name = "Insert"; break;
    case SDLK_HOME: name = "Home"; break;
    case SDLK_END: name = "End"; break;
    case SDLK_PAGEUP: name = "PageUp"; break;
    case SDLK_PAGEDOWN: name = "PageDown"; break;
    case SDLK_F1: name = "F1"; break;
    case SDLK_F2: name = "F2"; break;
    case SDLK_F3: name = "F3"; break;
    case SDLK_F4: name = "F4"; break;
    case SDLK_F5: name = "F5"; break;
    case SDLK_F6: name = "F6"; break;
    case SDLK_F7: name = "F7"; break;
    case SDLK_F8: name = "F8"; break;
    case SDLK_F9: name = "F9"; break;
    case SDLK_F10: name = "F10"; break;
    case SDLK_F11: name = "F11"; break;
    case SDLK_F12: name = "F12"; break;
    default: break;
    }
    if (name) { return KeyChord{String(name), ctrl, alt, shift && event.key != SDLK_TAB}; }

    SDL_Keycode key = event.key;
    if ((event.mod & (SDL_KMOD_SHIFT | SDL_KMOD_CAPS)) && event.scancode != SDL_SCANCODE_UNKNOWN) {
        key = SDL_GetKeyFromScancode(event.scancode, event.mod & (SDL_KMOD_SHIFT | SDL_KMOD_CAPS), false);
    }
    if (key < 32 || (key >= 127 && key <= 159) || key > 0x10ffff || (key >= 0xd800 && key <= 0xdfff)) {
        return eastl::nullopt;
    }
    String text;
    if (key < 0x80) { text += static_cast<char>(key); }
    else if (key < 0x800) {
        text += static_cast<char>(0xc0 | (key >> 6));
        text += static_cast<char>(0x80 | (key & 0x3f));
    } else if (key < 0x10000) {
        text += static_cast<char>(0xe0 | (key >> 12));
        text += static_cast<char>(0x80 | ((key >> 6) & 0x3f));
        text += static_cast<char>(0x80 | (key & 0x3f));
    } else {
        text += static_cast<char>(0xf0 | (key >> 18));
        text += static_cast<char>(0x80 | ((key >> 12) & 0x3f));
        text += static_cast<char>(0x80 | ((key >> 6) & 0x3f));
        text += static_cast<char>(0x80 | (key & 0x3f));
    }
    KeyChord chord{text, ctrl, alt};
    return ValidKey(chord) ? eastl::optional<KeyChord>(chord) : eastl::nullopt;
}

int MapZoomChange(const KeyChord& chord) {
    if (chord.ctrl || chord.alt || chord.shift) { return 0; }
    if (chord.key == "z") { return 1; }
    if (chord.key == "Z") { return -1; }
    return 0;
}

void HeldKey::Reset() {
    key_.reset(); text_.clear(); suppressText_ = false;
}
bool HeldKey::Observe(const SDL_Event& event, std::uint64_t now) {
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) { Reset(); return true; }
    if (event.type == SDL_EVENT_KEY_UP) {
        modifiers_ = event.key.mod;
        if (key_ && key_->key == event.key.key) { Reset(); }
        return true;
    }
    if (event.type == SDL_EVENT_TEXT_INPUT) {
        if (suppressText_) { return false; }
        if (key_) { text_ = event.text.text; }
        return true;
    }
    if (event.type != SDL_EVENT_KEY_DOWN) { return true; }
    modifiers_ = event.key.mod;
    if (event.key.repeat) { suppressText_ = true; return false; }
    const auto key = event.key.key;
    if (key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LCTRL || key == SDLK_RCTRL
        || key == SDLK_LALT || key == SDLK_RALT || key == SDLK_LGUI || key == SDLK_RGUI) { return true; }
    key_ = event.key; text_.clear(); suppressText_ = false; next_ = now + 250;
    return true;
}
eastl::optional<std::uint64_t> HeldKey::Delay(std::uint64_t now) const {
    if (!key_) { return {}; }
    return next_ > now ? next_ - now : 0;
}
eastl::optional<SDL_KeyboardEvent> HeldKey::Poll(std::uint64_t now, std::uint32_t rate) {
    if (!key_ || now < next_) { return {}; }
    next_ = now + (1000 + std::clamp(rate, 1U, 60U) - 1) / std::clamp(rate, 1U, 60U);
    auto result = *key_;
    result.mod = modifiers_;
    result.repeat = false;
    return result;
}

}
