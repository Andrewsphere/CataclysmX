#include "sdl/health.h"
#include "sdl/layout.h"
#include "sdl/modal.h"
#include "sdl/renderer.h"
#include "sdl/screens.h"
#include "client/text.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <thread>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::sdl;

struct Display {
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    Display(int width, int height) {
        SDL_Init(0);
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        if (surface) { renderer = SDL_CreateSoftwareRenderer(surface); }
    }
    ~Display() { SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); SDL_Quit(); }
};

bool Contains(SDL_Rect outer, SDL_Rect inner) {
    return inner.x >= outer.x && inner.y >= outer.y
        && inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}

TEST_CASE("SDL compact top bar removes title then low priority shortcuts", "[sdl][ui]") {
    const int widths[]{65, 105, 80};
    const auto wide = MakeTopBarLayout(600, 100, 45, widths);
    CHECK(wide.showTitle); CHECK(wide.actionCount == 3);
    const auto noTitle = MakeTopBarLayout(400, 100, 45, widths);
    CHECK_FALSE(noTitle.showTitle); CHECK(noTitle.actionCount == 3);
    CHECK(MakeTopBarLayout(300, 100, 45, widths).actionCount == 2);
    CHECK(MakeTopBarLayout(200, 100, 45, widths).actionCount == 1);
    const int longBindings[]{140, 240, 180};
    CHECK(MakeTopBarLayout(500, 100, 45, longBindings).actionCount == 2);
}

TEST_CASE("SDL popups flip sides and stay inside the map viewport", "[sdl][ui]") {
    const SDL_Rect viewport{0, 28, 800, 500};
    const auto right = PlacePopup(viewport, {300, 200, 32, 64}, 320, 150);
    CHECK(right.x == 340);
    const auto left = PlacePopup(viewport, {760, 200, 32, 64}, 320, 150);
    CHECK(left.x + left.w <= 752);
    for (const auto target : {SDL_Rect{0, 28, 32, 64}, SDL_Rect{768, 28, 32, 64},
        SDL_Rect{0, 464, 32, 64}, SDL_Rect{768, 464, 32, 64}}) {
        const auto popup = PlacePopup(viewport, target, 320, 150);
        CHECK(Contains(viewport, popup));
        CHECK_FALSE(SDL_HasRectIntersection(&popup, &target));
    }
    const SDL_Rect compact{0, 28, 480, 152}, target{224, 88, 32, 32};
    const auto narrow = PlacePopup(compact, target, 320, 90);
    CHECK(Contains(compact, narrow));
    CHECK(narrow.w >= 180); CHECK(narrow.w < 320);
    CHECK_FALSE(SDL_HasRectIntersection(&narrow, &target));
    CHECK(PlacePopup({}, target, 320, 90).w == 0);
    // Prefer a side that also keeps the player visible.
    const SDL_Rect player{350, 200, 32, 32};
    const auto clear = PlacePopup(viewport, {300, 200, 32, 32}, 200, 100, player);
    CHECK_FALSE(SDL_HasRectIntersection(&clear, &player));
}

TEST_CASE("SDL shared geometry keeps rectangular tiles and cursor anchors aligned", "[sdl][ui]") {
    for (const auto output : {SDL_Point{480, 360}, SDL_Point{800, 600}, SDL_Point{1600, 1200}}) {
        for (const int zoom : {-8, 0, 4, 12}) {
            const float scale = output.x == 1600 ? 2.0f : 1.0f;
            const auto base = MakeGameplayLayout(output.x, output.y, 17, 17, 32, 64, zoom, scale);
            const auto focused = MakeGameplayLayout(output.x, output.y, 17, 17, 32, 64, zoom, scale, {3, -2});
            const auto player = MapCellRect(base.map, 8, 8);
            const auto cursor = MapCellRect(focused.map, 11, 6);
            CHECK(player.x == cursor.x); CHECK(player.y == cursor.y);
            CHECK(base.map.cellHeight == base.map.cellSize * 2);
            const auto uiViewport = ScaleRect(base.mapViewport, 1 / scale);
            CHECK(uiViewport.y >= base.topBar.h);
            CHECK((uiViewport.y + uiViewport.h <= base.hud.y || base.sideHud));
        }
    }
}

TEST_CASE("SDL modal ownership restores parent state and clears held input", "[sdl][ui][input]") {
    HeldKey held;
    ModalController modal(held);
    SDL_Event key{}; key.type = SDL_EVENT_KEY_DOWN;
    key.key.key = SDLK_DOWN; key.key.scancode = SDL_SCANCODE_DOWN;
    REQUIRE(held.Observe(key, 0));
    modal.BeginOpen(ModalScreen::Targets);
    CHECK(modal.CapturesInput()); CHECK(modal.Pending());
    CHECK_FALSE(held.Poll(1000, 10));
    modal.Paused();
    CHECK(modal.Active() == ModalScreen::Targets);
    modal.Cursor() = {2, 3, 1};
    int scroll = 4;
    modal.EnterActions(scroll);
    CHECK(scroll == 0);
    modal.ActionSelection() = 2; scroll = 6;
    modal.BackToTargets(scroll);
    CHECK(scroll == 4); CHECK(modal.Cursor() == TileCoord{2, 3, 1});
    modal.EnterActions(scroll);
    CHECK(scroll == 6); CHECK(modal.ActionSelection() == 2);
    modal.BackToTargets(scroll);
    modal.Cursor() = {3, 3, 1}; modal.EnterActions(scroll);
    CHECK(scroll == 0); CHECK(modal.ActionSelection() == 0);
    REQUIRE(held.Observe(key, 1001));
    modal.BeginClose();
    CHECK(modal.CapturesInput()); CHECK_FALSE(held.Poll(2000, 10));
    modal.Closed();
    CHECK_FALSE(modal.CapturesInput());
}

TEST_CASE("SDL health distinguishes thresholds broken parts and effective maximum", "[sdl][ui]") {
    CHECK(HealthColor({10000, 10000}) == RgbColor{88, 194, 132});
    CHECK(HealthColor({7500, 10000}) == RgbColor{225, 204, 89});
    CHECK(HealthColor({5000, 10000}) == RgbColor{229, 145, 69});
    CHECK(HealthColor({2500, 10000}) == RgbColor{220, 83, 83});
    CHECK(HealthColor({0, 10000, true}) == RgbColor{70, 78, 82});
    CHECK(HealthColor({}) == RgbColor{70, 78, 82});
    CHECK(HealthColor({5000, 5000}) == HealthColor({10000, 10000}));
    CHECK(HealthPartText(BodyPart::LeftHand, {1234, 5000, true}) == "L hand 12.34/50  Broken");
    CHECK(HealthPartText(BodyPart::RightHand, {10000, 10000}) == "R hand 100/100");
    CHECK(HealthPartText(BodyPart::LeftLeg, {1, 9999}) == "L leg 0.01/99.99");
}

TEST_CASE("SDL health fits all labels and restores the renderer scale", "[sdl][ui][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    Server server(TestContent());
    auto combat = server.View().combat;
    for (auto& part : combat.parts) { if (part.maximum) { part.hp = 1; part.maximum = 9999; part.broken = true; } }
    int used = 0;
    REQUIRE(SDL_SetRenderScale(display.renderer, 1.5f, 1.5f));
    REQUIRE(DrawHealth(display.renderer, text, combat, {0, 0, 240, 164}, used));
    CHECK(used > 0); CHECK(used <= 164);
    float x = 0, y = 0;
    REQUIRE(SDL_GetRenderScale(display.renderer, &x, &y));
    CHECK(x == 1.5f); CHECK(y == 1.5f);
    REQUIRE(DrawHealth(display.renderer, text, {}, {0, 0, 240, 164}, used));
    CHECK(used == 0);
}

TEST_CASE("SDL contextual panels scroll long text and keep the world visible", "[sdl][ui][render]") {
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{800, 600}, SDL_Point{1600, 1200}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        REQUIRE(SDL_SetRenderDrawColor(display.renderer, 40, 50, 60, 255));
        REQUIRE(SDL_RenderClear(display.renderer));
        Server server(TestContent()); const auto view = server.View();
        ScreenFrame frame; frame.visible = true; frame.presentation = ScreenPresentation::Anchored;
        frame.cursor = view.player_position; frame.title = "Examine";
        frame.footer = "Escape close | PgUp/PgDn scroll";
        String longDescription;
        for (int i = 0; i < 60; ++i) { longDescription += "A long translated description with details. "; }
        frame.rows.push_back({longDescription}); frame.scroll = 999;
        const float scale = size.x == 1600 ? 2.0f : 1;
        REQUIRE(DrawScreen(display.renderer, text, frame, size.x, size.y, &view, 32, 64, 4, scale));
        CHECK(frame.scroll > 0); CHECK(frame.scroll < 999);
        frame.rows.clear();
        for (int i = 0; i < 20; ++i) { frame.rows.push_back({"Action", i == 19}); }
        frame.selected = 19; frame.scroll = 0; frame.details.push_back("Door is locked");
        REQUIRE(DrawScreen(display.renderer, text, frame, size.x, size.y, &view, 32, 64, 0, scale));
        CHECK(frame.scroll > 0);
        frame.selected = 0;
        REQUIRE(DrawScreen(display.renderer, text, frame, size.x, size.y, &view, 32, 64, -8, scale));
        CHECK(frame.scroll == 0);
        REQUIRE(SDL_RenderPresent(display.renderer));
        Uint8 r = 0, g = 0, b = 0, a = 0;
        REQUIRE(SDL_ReadSurfacePixel(display.surface, size.x - 2, size.y - 2, &r, &g, &b, &a));
        CHECK(r == 40); CHECK(g == 50); CHECK(b == 60);
    }
}

TEST_CASE("SDL anchored windows wait for nonempty body text", "[sdl][ui][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    Server server(TestContent()); const auto view = server.View();
    ScreenFrame frame; frame.visible = true; frame.presentation = ScreenPresentation::Anchored;
    frame.title = "Examine"; frame.footer = "Escape close"; frame.cursor = view.player_position;
    const auto geometry = MakeGameplayLayout(480, 360, view.width, view.height);
    const auto cursor = MapCellRect(geometry.map, frame.cursor->x - view.origin.x, frame.cursor->z - view.origin.z);
    for (const bool emptyRows : {true, false}) {
        if (!emptyRows) { frame.rows.push_back({""}); frame.details.push_back(""); }
        REQUIRE(SDL_SetRenderDrawColor(display.renderer, 40, 50, 60, 255));
        REQUIRE(SDL_RenderClear(display.renderer));
        REQUIRE(DrawScreen(display.renderer, text, frame, 480, 360, &view, 32, 32, 0, 1));
        REQUIRE(SDL_RenderPresent(display.renderer));
        bool unchanged = true;
        for (int y = 32; y < 180; y += 7) {
            for (int x = 0; x < 480; x += 7) {
                if (x >= cursor.x - 1 && x <= cursor.x + cursor.w + 1
                    && y >= cursor.y - 1 && y <= cursor.y + cursor.h + 1) { continue; }
                Uint8 r = 0, g = 0, b = 0, a = 0;
                REQUIRE(SDL_ReadSurfacePixel(display.surface, x, y, &r, &g, &b, &a));
                unchanged &= r == 40 && g == 50 && b == 60;
            }
        }
        CHECK(unchanged);
    }
    frame.rows = {{"Tile description"}};
    REQUIRE(DrawScreen(display.renderer, text, frame, 480, 360, &view, 32, 32, 0, 1));
    CHECK(text.Height(8) > 0);
}

TEST_CASE("SDL HUD and anchored window preview", "[sdl][ui][render]") {
    for (const auto size : {SDL_Point{1120, 780}, SDL_Point{480, 360}, SDL_Point{800, 600}, SDL_Point{1600, 1200}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        SdlAssets assets;
        for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
        REQUIRE(assets.State() == AssetState::Ready);
        Server server(TestContent()); auto view = server.View();
        view.combat.parts[static_cast<unsigned>(BodyPart::LeftHand)] = {3500, 10000};
        view.combat.parts[static_cast<unsigned>(BodyPart::RightLeg)] = {0, 10000, true};
        auto keys = DefaultKeymap();
        Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
        MessageHistory history; history.Add("Local session ready."); history.Add("The door is locked.");
        ScreenFrame frame; frame.visible = true; frame.presentation = ScreenPresentation::Anchored;
        frame.cursor = TileCoord{view.player_position.x + 1, view.player_position.z, view.player_position.y};
        frame.title = "Interact | Door"; frame.footer = "Escape back | Enter use\nUp/Down choose";
        frame.rows = {{"Open", true}, {"Unlock"}, {"Examine"}}; frame.selected = 0;
        frame.details = {"Door is locked"};
        const float scale = size.x == 1600 ? 2.0f : 1;
        REQUIRE(DrawFrame(display.renderer, text, assets, &view, TestContent(), "Balanced | Stamina 100%", "",
            0, nullptr, 0, scale, {}, nullptr, nullptr, &frame, {&history, &keys, &localization}));
        if (const char* path = std::getenv("CATACLYSMX_SDL_UI_PREVIEW")) {
            const auto output = String(path) + "." + NumberText(size.x) + ".png";
            REQUIRE(SDL_SavePNG(display.surface, output.c_str()));
        }
        SetBinding(keys, {InputContext::Gameplay, InputAction::Help, {{"H"}}});
        SetBinding(keys, {InputContext::Gameplay, InputAction::Debug, {}});
        REQUIRE(DrawFrame(display.renderer, text, assets, &view, TestContent(), "", "",
            0, nullptr, 0, scale, {}, nullptr, nullptr, &frame, {&history, &keys, &localization}));
        REQUIRE(text.Draw(63, "H Help", -10000, -10000, {255, 255, 255, 255}));
        CHECK(text.Width(58) == text.Width(63));
        const auto unbound = BindingHint(keys, InputContext::Gameplay, InputAction::Debug) + " Debug";
        REQUIRE(text.Draw(63, unbound.c_str(), -10000, -10000, {255, 255, 255, 255}));
        CHECK(text.Width(60) == text.Width(63));
    }
}

}
