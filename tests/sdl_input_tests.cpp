#include "sdl/input.h"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;

SDL_KeyboardEvent Key(SDL_Keycode key, SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN, SDL_Keymod mod = SDL_KMOD_NONE) {
    SDL_KeyboardEvent event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key = key;
    event.scancode = scancode;
    event.mod = mod;
    return event;
}

TEST_CASE("SDL input preserves case and Ctrl chords", "[sdl][input]") {
    CHECK(sdl::TranslateKey(Key(SDLK_T, SDL_SCANCODE_T)) == KeyChord{"t", false});
    CHECK(sdl::TranslateKey(Key(SDLK_T, SDL_SCANCODE_T, SDL_KMOD_SHIFT)) == KeyChord{"T", false});
    CHECK(sdl::TranslateKey(Key(SDLK_T, SDL_SCANCODE_T, SDL_KMOD_CTRL)) == KeyChord{"t", true});
    CHECK(sdl::TranslateKey(Key(SDLK_T, SDL_SCANCODE_T, SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == KeyChord{"T", true});
    CHECK(sdl::TranslateKey(Key(SDLK_T, SDL_SCANCODE_T, SDL_KMOD_CAPS)) == KeyChord{"T", false});
    CHECK(sdl::TranslateKey(Key(SDLK_T, SDL_SCANCODE_T, SDL_KMOD_CAPS | SDL_KMOD_SHIFT)) == KeyChord{"t", false});
    CHECK(sdl::TranslateKey(Key(SDLK_EQUALS, SDL_SCANCODE_EQUALS, SDL_KMOD_SHIFT)) == KeyChord{"+", false});
    CHECK(sdl::TranslateKey(Key(0x754c)) == KeyChord{"界", false});
}

TEST_CASE("SDL input handles named keys and ignores unsupported events", "[sdl][input]") {
    CHECK(sdl::TranslateKey(Key(SDLK_TAB)) == KeyChord{"Tab", false});
    CHECK(sdl::TranslateKey(Key(SDLK_TAB, SDL_SCANCODE_TAB, SDL_KMOD_SHIFT | SDL_KMOD_CTRL)) == KeyChord{"BackTab", true});
    CHECK(sdl::TranslateKey(Key(SDLK_LEFT, SDL_SCANCODE_LEFT, SDL_KMOD_CTRL)) == KeyChord{"Left", true});
    CHECK(sdl::TranslateKey(Key(SDLK_F12)) == KeyChord{"F12", false});
    CHECK(sdl::TranslateKey(Key(SDLK_KP_ENTER)) == KeyChord{"Enter", false});
    CHECK_FALSE(sdl::TranslateKey(Key(SDLK_T, SDL_SCANCODE_T, SDL_KMOD_ALT)));
    CHECK_FALSE(sdl::TranslateKey(Key(SDLK_1, SDL_SCANCODE_1, SDL_KMOD_CTRL)));
    CHECK_FALSE(sdl::TranslateKey(Key(SDLK_LSHIFT)));
    auto event = Key(SDLK_T);
    event.repeat = true;
    CHECK(sdl::TranslateKey(event) == KeyChord{"t", false});
    event.type = SDL_EVENT_KEY_UP;
    CHECK_FALSE(sdl::TranslateKey(event));
    event.repeat = false;
    CHECK_FALSE(sdl::TranslateKey(event));
}

TEST_CASE("SDL map zoom uses case-sensitive z shortcuts", "[sdl][input]") {
    const SDL_Keymod modifiers[]{SDL_KMOD_NONE, SDL_KMOD_SHIFT, SDL_KMOD_CAPS,
        SDL_KMOD_CAPS | SDL_KMOD_SHIFT};
    for (const auto mod : modifiers) {
        const auto chord = sdl::TranslateKey(Key(SDLK_Z, SDL_SCANCODE_Z, mod));
        REQUIRE(chord);
        const bool uppercase = mod == SDL_KMOD_SHIFT || mod == SDL_KMOD_CAPS;
        CHECK(sdl::MapZoomChange(*chord) == (uppercase ? -1 : 1));
    }
    CHECK(sdl::MapZoomChange({"z", true}) == 0);
    CHECK(sdl::MapZoomChange({"Z", true}) == 0);
    CHECK(sdl::MapZoomChange({"+", false}) == 0);
    CHECK(sdl::MapZoomChange({"-", false}) == 0);
}

TEST_CASE("SDL input repeats resolved movement bindings only in gameplay", "[sdl][input]") {
    const auto keys = DefaultKeymap();
    struct MovementKey {
        SDL_Keycode key;
        InputAction action;
    };
    for (const auto& binding : {
        MovementKey{SDLK_LEFT, InputAction::West}, MovementKey{SDLK_H, InputAction::West},
        MovementKey{SDLK_RIGHT, InputAction::East}, MovementKey{SDLK_L, InputAction::East},
        MovementKey{SDLK_UP, InputAction::North}, MovementKey{SDLK_K, InputAction::North},
        MovementKey{SDLK_DOWN, InputAction::South}, MovementKey{SDLK_J, InputAction::South},
        MovementKey{'<', InputAction::MoveDown}, MovementKey{'>', InputAction::MoveUp}}) {
        auto event = Key(binding.key);
        event.repeat = true;
        const auto chord = sdl::TranslateKey(event);
        REQUIRE(chord);
        const auto action = ResolveAction(keys, InputContext::Gameplay, *chord);
        REQUIRE(action == binding.action);
        CHECK(sdl::AllowsKeyRepeat(InputContext::Gameplay, *action));
        for (const auto context : {InputContext::Interaction, InputContext::Menu,
            InputContext::Debug, InputContext::Items}) {
            CHECK_FALSE(sdl::AllowsKeyRepeat(context, *action));
        }
        event.type = SDL_EVENT_KEY_UP;
        CHECK_FALSE(sdl::TranslateKey(event));
    }
    for (const auto action : {InputAction::Wait, InputAction::PickUp, InputAction::Drop,
        InputAction::Interact, InputAction::Save, InputAction::Quit, InputAction::Help,
        InputAction::Cancel, InputAction::Accept, InputAction::Settings, InputAction::ToggleIdle}) {
        CHECK_FALSE(sdl::AllowsKeyRepeat(InputContext::Gameplay, action));
    }
}

TEST_CASE("SDL input repeat policy follows rebound actions instead of physical keys", "[sdl][input]") {
    auto keys = DefaultKeymap();
    SetBinding(keys, {InputContext::Gameplay, InputAction::East, {{"g", false}}});
    SetBinding(keys, {InputContext::Gameplay, InputAction::PickUp, {{"l", false}}});
    REQUIRE(ValidateKeymap(keys));
    auto event = Key(SDLK_G);
    event.repeat = true;
    auto chord = sdl::TranslateKey(event);
    REQUIRE(chord);
    auto action = ResolveAction(keys, InputContext::Gameplay, *chord);
    REQUIRE(action == InputAction::East);
    CHECK(sdl::AllowsKeyRepeat(InputContext::Gameplay, *action));

    event.key = SDLK_L;
    chord = sdl::TranslateKey(event);
    REQUIRE(chord);
    action = ResolveAction(keys, InputContext::Gameplay, *chord);
    REQUIRE(action == InputAction::PickUp);
    CHECK_FALSE(sdl::AllowsKeyRepeat(InputContext::Gameplay, *action));
}

}
