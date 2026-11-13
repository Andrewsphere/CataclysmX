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
    CHECK(sdl::TranslateKey(Key(SDLK_T, SDL_SCANCODE_T, SDL_KMOD_ALT)) == KeyChord{"t",false,true});
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

TEST_CASE("SDL Tab resolves by input context", "[sdl][input]") {
    const auto chord = sdl::TranslateKey(Key(SDLK_TAB)); REQUIRE(chord);
    const auto keys = DefaultKeymap();
    CHECK(ResolveAction(keys, InputContext::Gameplay, *chord) == InputAction::Attack);
    CHECK(ResolveAction(keys, InputContext::Menu, *chord) == InputAction::Next);
    CHECK(ResolveAction(keys, InputContext::Debug, *chord) == InputAction::Next);
    const auto one = sdl::TranslateKey(Key(SDLK_1)); REQUIRE(one);
    const auto two = sdl::TranslateKey(Key(SDLK_2)); REQUIRE(two);
    const auto three = sdl::TranslateKey(Key(SDLK_3)); REQUIRE(three);
    CHECK(ResolveAction(keys, InputContext::Gameplay, *one) == InputAction::StyleBalanced);
    CHECK(ResolveAction(keys, InputContext::Gameplay, *two) == InputAction::StyleAggressive);
    CHECK(ResolveAction(keys, InputContext::Gameplay, *three) == InputAction::StyleDefensive);
    CHECK(ResolveAction(keys, InputContext::Debug, *one) == InputAction::Layer1);
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


}

TEST_CASE("SDL modifiers resolve diagonal and vertical actions without arrow aliases", "[sdl][input]") {
    using namespace cataclysmx;
    const auto keys = DefaultKeymap();
    REQUIRE(ValidateKeymap(keys));
    struct Binding { SDL_Keycode key; SDL_Keymod mod; InputAction action; };
    for (const auto binding : {Binding{SDLK_LEFT,SDL_KMOD_CTRL,InputAction::Southwest},
        {SDLK_RIGHT,SDL_KMOD_CTRL,InputAction::Southeast}, {SDLK_LEFT,SDL_KMOD_SHIFT,InputAction::Northwest},
        {SDLK_RIGHT,SDL_KMOD_SHIFT,InputAction::Northeast}, {SDLK_UP,SDL_KMOD_ALT,InputAction::MoveUp},
        {SDLK_DOWN,SDL_KMOD_ALT,InputAction::MoveDown}}) {
        const auto chord = sdl::TranslateKey(Key(binding.key,SDL_SCANCODE_UNKNOWN,binding.mod));
        REQUIRE(chord);
        CHECK(ResolveAction(keys,InputContext::Gameplay,*chord) == binding.action);
    }
    CHECK(ResolveAction(keys,InputContext::Gameplay,*sdl::TranslateKey(Key(SDLK_LEFT))) == InputAction::West);
    CHECK_FALSE(ResolveAction(keys,InputContext::Gameplay,*sdl::TranslateKey(Key(SDLK_LEFT,SDL_SCANCODE_UNKNOWN,SDL_KMOD_ALT))));
    CHECK(sdl::MapZoomChange({"z",false,true}) == 0);
}

TEST_CASE("Held keys repeat every action at a configurable rate without catch-up", "[sdl][input]") {
    using namespace cataclysmx::sdl;
    HeldKey held;
    SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key = Key(SDLK_F2);
    REQUIRE(held.Observe(event,100));
    CHECK_FALSE(held.Poll(349,10));
    REQUIRE(held.Poll(350,10));
    CHECK_FALSE(held.Poll(449,10));
    REQUIRE(held.Poll(450,10));
    REQUIRE(held.Poll(1000,20));
    CHECK_FALSE(held.Poll(1000,20));
    CHECK_FALSE(held.Poll(1049,20));
    REQUIRE(held.Poll(1050,20));
    event.key.repeat = true; CHECK_FALSE(held.Observe(event,1051));
    event.type = SDL_EVENT_KEY_UP; event.key.repeat = false;
    REQUIRE(held.Observe(event,1052)); CHECK_FALSE(held.Poll(5000,10));
    event.type = SDL_EVENT_KEY_DOWN; event.key = Key(SDLK_LEFT);
    REQUIRE(held.Observe(event,6000));
    event.key = Key(SDLK_LSHIFT,SDL_SCANCODE_LSHIFT,SDL_KMOD_SHIFT);
    REQUIRE(held.Observe(event,6100));
    const auto repeated = held.Poll(6250,10); REQUIRE(repeated);
    CHECK(TranslateKey(*repeated) == cataclysmx::KeyChord{"Left",false,false,true});
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST; REQUIRE(held.Observe(event,6260));
    CHECK_FALSE(held.Poll(9000,10));
}

TEST_CASE("Held text ignores native repeats and a newer key replaces the previous hold", "[sdl][input]") {
    using namespace cataclysmx::sdl;
    HeldKey held; SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN; event.key = Key(SDLK_1);
    REQUIRE(held.Observe(event,0));
    event.type = SDL_EVENT_TEXT_INPUT; event.text.text = "1";
    REQUIRE(held.Observe(event,1)); CHECK(held.Text() == "1");
    event.type = SDL_EVENT_KEY_DOWN; event.key = Key(SDLK_1); event.key.repeat = true;
    CHECK_FALSE(held.Observe(event,200));
    event.type = SDL_EVENT_TEXT_INPUT; event.text.text = "1";
    CHECK_FALSE(held.Observe(event,201));
    REQUIRE(held.Poll(250,10)); CHECK(held.Text() == "1");
    event.type = SDL_EVENT_KEY_DOWN; event.key = Key(SDLK_RETURN);
    REQUIRE(held.Observe(event,260)); CHECK(held.Text().empty());
    CHECK_FALSE(held.Poll(510-1,10));
    const auto enter = held.Poll(510,10); REQUIRE(enter); CHECK(enter->key == SDLK_RETURN);
    held.Reset(); CHECK_FALSE(held.Poll(9999,10));
}
