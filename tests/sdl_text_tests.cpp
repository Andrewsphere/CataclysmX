#include "sdl/text.h"

#include <catch2/catch_test_macros.hpp>
#include <EASTL/unique_ptr.h>

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

TEST_CASE("SDL text replacement refreshes UTF8 metrics and preserves the active font on failure", "[sdl][text][text-size]") {
    SoftwareDisplay display; REQUIRE(display.renderer);
    eastl::unique_ptr<cataclysmx::sdl::SdlText> active(new cataclysmx::sdl::SdlText(display.renderer, nullptr, 14));
    REQUIRE(active->Ready());
    const SDL_Color white{255,255,255,255};
    REQUIRE(active->Draw(0, "Настройки / Résumé", 0, 0, white));
    const int oldWidth = active->Width(0), oldLine = active->LineHeight();
    {
        cataclysmx::sdl::SdlText failed(display.renderer, "/missing/font.ttf", 24);
        CHECK_FALSE(failed.Ready());
    }
    REQUIRE(active->Draw(0, "Настройки / Résumé", 0, 0, white));
    CHECK(active->Width(0) == oldWidth);
    for (int size : {8, 14, 24, 48}) {
        eastl::unique_ptr<cataclysmx::sdl::SdlText> prepared(new cataclysmx::sdl::SdlText(display.renderer, nullptr, static_cast<float>(size)));
        REQUIRE(prepared->Ready());
        active = eastl::move(prepared);
        REQUIRE(active->Draw(0, "Настройки / Résumé", 0, 0, white));
        if (size > 14) { CHECK(active->Width(0) > oldWidth); CHECK(active->LineHeight() > oldLine); }
        const int unwrapped = active->Height(0);
        REQUIRE(active->Draw(0, "Настройки / Résumé", 0, 0, white, 60));
        CHECK(active->Height(0) > unwrapped);
    }
}
