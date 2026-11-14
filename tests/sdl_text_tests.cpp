#include "sdl/text.h"

#include <catch2/catch_test_macros.hpp>

namespace {
struct SoftwareDisplay {
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    SoftwareDisplay() {
        SDL_Init(0);
        surface = SDL_CreateSurface(480, 360, SDL_PIXELFORMAT_RGBA32);
        if (surface) { renderer = SDL_CreateSoftwareRenderer(surface); }
    }
    ~SoftwareDisplay() { SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); SDL_Quit(); }
};
}

TEST_CASE("SDL text supports UTF8 and reuses independent text slots", "[sdl][text]") {
    SoftwareDisplay display;
    REQUIRE(display.renderer);
    cataclysmx::sdl::SdlText text(display.renderer);
    REQUIRE(text.Ready());
    const SDL_Color white{255, 255, 255, 255};
    REQUIRE(text.Draw(0, "Настройки / Résumé", 0, 0, white, 400));
    const int originalHeight = text.Height(0);
    REQUIRE(originalHeight > 0);
    const int originalWidth = text.Width(0);
    CHECK(originalWidth > 0);
    REQUIRE(text.Draw(1, "Movement\nWait\nPick up", 0, 40, white));
    CHECK(text.Height(1) > originalHeight);
    REQUIRE(text.Draw(0, "Настройки / Résumé", 10, 10, white, 400));
    CHECK(text.Height(0) == originalHeight);
    CHECK(text.Width(0) == originalWidth);
    REQUIRE(text.Draw(0, "A much longer line wraps onto several rows", 0, 0, white, 80));
    CHECK(text.Height(0) > originalHeight);
    CHECK_FALSE(text.Draw(text.slotCount, "Invalid slot", 0, 0, white));
}

TEST_CASE("SDL font failure stays a startup failure", "[sdl][text]") {
    SoftwareDisplay display;
    REQUIRE(display.renderer);
    cataclysmx::sdl::SdlText text(display.renderer, "/missing/cataclysmx/font.ttf");
    CHECK_FALSE(text.Ready());
    CHECK_FALSE(text.Draw(0, "unavailable", 0, 0, {255, 255, 255, 255}));
}
