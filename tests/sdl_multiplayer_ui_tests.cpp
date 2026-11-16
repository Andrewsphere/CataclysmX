#include "sdl/modal.h"

#include <catch2/catch_test_macros.hpp>

using namespace cataclysmx;
using namespace cataclysmx::sdl;

TEST_CASE("SDL LAN and roster transitions release held gameplay input", "[sdl][multiplayer][ui]") {
    HeldKey held;
    ModalController modal(held);
    for (const auto screen : {ModalScreen::Lan, ModalScreen::Players}) {
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.key = SDLK_RIGHT;
        REQUIRE(held.Observe(key, 0));
        REQUIRE(held.Delay(0));
        modal.BeginOpen(screen);
        CHECK(modal.Pending());
        CHECK(modal.CapturesInput());
        CHECK_FALSE(held.Delay(0));
        // A LAN MenuOpened acknowledgement uses the same UI transition as a local pause.
        modal.Paused();
        CHECK(modal.Active() == screen);
        CHECK_FALSE(modal.Pending());
        REQUIRE(held.Observe(key, 300));
        modal.BeginClose();
        CHECK_FALSE(held.Delay(300));
        modal.Closed();
        CHECK_FALSE(modal.CapturesInput());
    }
}

TEST_CASE("SDL epoch reset discards interrupted modal selections", "[sdl][multiplayer][ui]") {
    HeldKey held;
    ModalController modal(held);
    modal.BeginOpen(ModalScreen::Targets);
    modal.Paused();
    modal.Cursor() = {12, 34, 1};
    int scroll = 8;
    modal.EnterActions(scroll);
    modal.ActionSelection() = 3;
    modal.BeginOpen(ModalScreen::Lan);
    modal.Closed(); // A session generation change can interrupt an unacknowledged open.
    CHECK(modal.Active() == ModalScreen::None);
    CHECK_FALSE(modal.Pending());
    CHECK(modal.Cursor() == TileCoord{});
    CHECK(modal.ActionSelection() == 0);
    modal.BeginOpen(ModalScreen::Players);
    modal.Paused();
    CHECK(modal.Active() == ModalScreen::Players);
    CHECK(modal.Cursor() == TileCoord{});
}
