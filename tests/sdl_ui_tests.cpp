#include "sdl/health.h"
#include "sdl/layout.h"
#include "sdl/modal.h"
#include "sdl/renderer.h"
#include "sdl/screens.h"
#include "client/text.h"
#include "client/tile_inspection.h"
#include "core/ratio.h"
#include "settings_fixture.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

TEST_CASE("SDL text sizes keep options controls accessible and tile dimensions unchanged", "[sdl][ui][text-size]") {
    SettingsDirectory files;
    SettingsStore store(String(files.root.string().c_str()));
    Localization loc; REQUIRE(loc.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 780}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        for (int points : {8, 14, 24, 48}) {
            REQUIRE(SDL_SetRenderDrawColor(display.renderer, 0, 0, 0, 255));
            REQUIRE(SDL_RenderClear(display.renderer));
            SdlText text(display.renderer, nullptr, static_cast<float>(points)); REQUIRE(text.Ready());
            const auto geometry = MakeGameplayLayout(size.x, size.y, 17, 17, 32, 32, 0, 1, {}, text.LineHeight());
            CHECK(geometry.map.cellSize == 32); CHECK(geometry.map.cellHeight == 32);
            CHECK(geometry.topBar.h >= text.LineHeight() + 8);
            CHECK(geometry.mapViewport.y >= geometry.topBar.h);
            OptionsUi options; options.Open(store, store.Effective());
            for (int i = 0; i < 4; ++i) { options.Key({"Down"}, store, loc, CATACLYSMX_LOC_DIRECTORY); }
            ScreenFrame frame; options.Present(store, loc, frame);
            REQUIRE(DrawScreen(display.renderer, text, frame, size.x, size.y, nullptr, 32, 32, 0, 1, &geometry));
            REQUIRE(SDL_RenderPresent(display.renderer));
            CHECK(frame.selected >= frame.scroll);
            CHECK(text.Width(9) > 0);
            int yellowTop = 0, yellowBody = 0, footerPixels = 0;
            const int bodyStart = geometry.topBar.h + text.Height(8) + 16;
            const int footerStart = size.y - text.Height(9) - 16;
            for (int y = geometry.topBar.h; y < size.y - 8; ++y) {
                for (int x = 8; x < size.x - 8; ++x) {
                    Uint8 r, g, b, a;
                    REQUIRE(SDL_ReadSurfacePixel(display.surface, x, y, &r, &g, &b, &a));
                    if (r > 200 && g > 180 && b < 120) {
                        if (y < bodyStart) { ++yellowTop; }
                        else if (y < footerStart) { ++yellowBody; }
                    }
                    if (y >= footerStart && r > 60 && g > 70 && b > 70) { ++footerPixels; }
                }
            }
            CHECK(yellowTop > 0); CHECK(yellowBody > 0); CHECK(footerPixels > 0);
            if (const char* path = std::getenv("CATACLYSMX_TEXT_SIZE_PREVIEW")) {
                const auto output = String(path) + "." + NumberText(size.x) + "." + NumberText(points) + ".png";
                REQUIRE(SDL_SavePNG(display.surface, output.c_str()));
            }
        }
    }
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

TEST_CASE("SDL gameplay draws all injured enemy body groups inside adjacent tiles", "[sdl][ui][render][enemy-health]") {
    Display display(800, 600); REQUIRE(display.renderer);
    SdlText text(display.renderer, nullptr, 14); REQUIRE(text.Ready());
    SdlAssets assets;
    for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
    REQUIRE(assets.State() == AssetState::Ready);
    REQUIRE(assets.Definition().cellWidth == 32);
    REQUIRE(assets.Definition().cellHeight == 32);

    Server server(TestContent());
    auto view = server.View(3);
    const auto index = static_cast<std::uint16_t>(view.width * 3 + 4);
    for (unsigned i = 0; i < 2; ++i) {
        auto& cell = view.cells[index + i];
        cell.terrainVisibility = cell.overlayVisibility = Visibility::Visible;
        cell.tile.terrain = TestTerrain("floor");
        cell.object = 0; cell.objectAppearance = {}; cell.actor = 100 + i;
    }
    EnemyHealthView detailed{index, BodyKind::Detailed};
    detailed.partRatios = {5000, 2500, 0, ratioScale, ratioScale, 3000};
    EnemyHealthView simple{static_cast<std::uint16_t>(index + 1), BodyKind::Simple};
    simple.partRatios = {0, 0, 0, 0, 0, 0, ratioScale, 0, 2500};
    view.enemyHealth = {detailed, simple};
    const auto packet = PrepareMapPacket(view, TestContent(), assets.Definition(), {});
    CHECK(packet.enemyHealth == view.enemyHealth);

    const auto pixel = [&](int x, int y) {
        Uint8 r, g, b, a;
        REQUIRE(SDL_ReadSurfacePixel(display.surface, x, y, &r, &g, &b, &a));
        return RgbColor{r, g, b};
    };
    const RgbColor full{94, 143, 111}, empty{66, 74, 80}, disabled{227, 233, 230};
    const RgbColor orange{229, 145, 69}, red{220, 83, 83};
    for (const int zoom : {0, -8}) {
        REQUIRE(DrawFrame(display.renderer, text, assets, &view, TestContent(), "", "", 0, &packet, zoom));
        const auto map = MakeGameplayLayout(800, 600, view.width, view.height, 32, 32, zoom, 1, {}, text.LineHeight()).map;
        const auto left = MapCellRect(map, 4, 3);
        const auto right = MapCellRect(map, 5, 3);
        const bool pips = zoom < 0;
        const int start = pips ? 0 : 4, step = pips ? 3 : 9;
        const int top = left.y + (pips ? 3 : 23), bottom = left.y + (pips ? 6 : 27);
        CHECK(pixel(left.x + start + (pips ? 1 : 4), top + 1) == disabled); // L hand
        CHECK(pixel(left.x + start + step, top) == orange); // Head
        CHECK(pixel(left.x + start + step + (pips ? 1 : 4), top) == empty);
        CHECK(pixel(left.x + start + step * 2, top) == full); // R hand
        CHECK(pixel(left.x + start, bottom) == full); // L leg
        CHECK(pixel(left.x + start + step, bottom) == red); // Torso
        CHECK(pixel(left.x + start + step * 2, bottom) == orange); // R leg
        CHECK(pixel(right.x + start, bottom) == full); // Attack
        CHECK(pixel(right.x + start + step, bottom) == red); // Core
        CHECK(pixel(right.x + start + step * 2 + (pips ? 1 : 4), bottom + 1) == disabled); // Movement
        ScreenFrame popup;
        popup.visible = true; popup.presentation = ScreenPresentation::Anchored;
        REQUIRE(DrawFrame(display.renderer, text, assets, &view, TestContent(), "", "", 0, &packet, zoom,
            1, {}, nullptr, nullptr, &popup));
        CHECK(pixel(right.x + start, bottom) != full);
    }
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

TEST_CASE("SDL enemy Examine shows proportional bars for every present body group", "[sdl][ui][render]") {
    const auto makeFrame = [](const ExaminedEnemy& enemy, TileCoord cursor) {
        ScreenFrame frame;
        frame.visible = true;
        frame.presentation = ScreenPresentation::Anchored;
        frame.followCursor = true;
        frame.title = "Examine";
        frame.footer = "Escape close | PgUp/PgDn scroll";
        frame.cursor = cursor;
        AppendEnemyExamine(enemy, Localization{}, frame);
        frame.rows.push_back({"Floor"});
        return frame;
    };
    ExaminedEnemy simple{"cellar zombie", "Speed 100", {}};
    simple.combat.bodyKind = BodyKind::Simple;
    simple.combat.parts[static_cast<unsigned>(BodyPart::Core)] = {4000, 10000};
    simple.combat.parts[static_cast<unsigned>(BodyPart::AttackApparatus)] = {6000, 6000};
    simple.combat.parts[static_cast<unsigned>(BodyPart::Locomotion)] = {0, 6000};
    Server server(TestContent());
    const auto view = server.View();
    const TileCoord cursor{view.player_position.x + 1, view.player_position.z, view.player_position.y};
    {
        Display display(1120, 600); REQUIRE(display.renderer);
        SdlText text(display.renderer, nullptr, 14); REQUIRE(text.Ready());
        auto frame = makeFrame(simple, cursor);
        REQUIRE(frame.rows.size() == 7);
        CHECK(frame.rows[0].text == "cellar zombie");
        CHECK(frame.rows[1].text == "Core · Vital");
        REQUIRE(frame.rows[1].health);
        CHECK(frame.rows[1].health->value == "40/100");
        CHECK(frame.rows[2].text == "Attack");
        CHECK(frame.rows[3].text == "Movement · Disabled");
        REQUIRE(DrawScreen(display.renderer, text, frame, 1120, 600, &view, 32, 64, 0, 1));
        REQUIRE(SDL_RenderPresent(display.renderer));
        CHECK(frame.rowHeights[1] == text.LineHeight());
        int full = 0, partial = 0, empty = 0;
        for (int y = 0; y < display.surface->h; ++y) {
            int green = 0, orange = 0, gray = 0;
            for (int x = 0; x < display.surface->w; ++x) {
                Uint8 r, g, b, a;
                if (!SDL_ReadSurfacePixel(display.surface, x, y, &r, &g, &b, &a)) { FAIL(SDL_GetError()); }
                green += r == 88 && g == 194 && b == 132;
                orange += r == 229 && g == 145 && b == 69;
                gray += r == 70 && g == 78 && b == 82;
            }
            full = std::max(full, green);
            partial = std::max(partial, orange);
            empty = std::max(empty, gray);
        }
        CHECK(full >= 48);
        CHECK(partial == full * 40 / 100);
        CHECK(empty == full);
    }
    {
        Display display(480, 360); REQUIRE(display.renderer);
        SdlText text(display.renderer, nullptr, 24); REQUIRE(text.Ready());
        ExaminedEnemy detailed{"humanoid", "Speed 100", view.combat};
        detailed.combat.parts[static_cast<unsigned>(BodyPart::LeftHand)] = {0, 7534, true};
        auto frame = makeFrame(detailed, cursor);
        REQUIRE(frame.rows.size() == 10);
        CHECK(frame.rows[1].text == "Head · Vital");
        CHECK(frame.rows[2].text == "Torso · Vital");
        CHECK(frame.rows[3].text == "L hand · Broken");
        REQUIRE(frame.rows[3].health);
        CHECK(frame.rows[3].health->value == "0/75.34");
        REQUIRE(DrawScreen(display.renderer, text, frame, 480, 360, &view, 32, 64, 0, 1));
        CHECK(frame.rowHeights[3] >= text.LineHeight() * 2);
        frame.scroll = 999;
        REQUIRE(DrawScreen(display.renderer, text, frame, 480, 360, &view, 32, 64, 0, 1));
        CHECK(frame.scroll > 0);
        CHECK(frame.scroll < 999);
    }
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

TEST_CASE("SDL examine separators stay subtle and scroll with wrapped text", "[sdl][ui][render]") {
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{800, 600}, SDL_Point{1600, 1200}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        Server server(TestContent()); const auto view = server.View();
        ScreenFrame frame; frame.visible = true; frame.presentation = ScreenPresentation::Anchored;
        frame.title = "Examine"; frame.footer = "Escape close"; frame.cursor = view.player_position;
        String description;
        for (int i = 0; i < 60; ++i) { description += "Wrapped item description. "; }
        frame.rows = {{"Passable"}, {""}, {description}};
        const float scale = size.x == 1600 ? 2.0f : 1.0f;
        const auto separatorPixels = [&] {
            int count = 0;
            for (int y = 0; y < size.y; ++y) {
                for (int x = 0; x < size.x; x += 4) {
                    Uint8 r = 0, g = 0, b = 0, a = 0;
                    REQUIRE(SDL_ReadSurfacePixel(display.surface, x, y, &r, &g, &b, &a));
                    count += r == 48 && g == 62 && b == 68;
                }
            }
            return count;
        };
        REQUIRE(SDL_SetRenderDrawColor(display.renderer, 0, 0, 0, 255));
        REQUIRE(SDL_RenderClear(display.renderer));
        REQUIRE(DrawScreen(display.renderer, text, frame, size.x, size.y, &view, 32, 32, 0, scale));
        REQUIRE(SDL_RenderPresent(display.renderer));
        CHECK(separatorPixels() > 20);
        CHECK(frame.rowHeights[1] == 8);
        CHECK(frame.rowHeights[2] > frame.rowHeights[0]);
        frame.scroll = 999;
        REQUIRE(SDL_SetRenderDrawColor(display.renderer, 0, 0, 0, 255));
        REQUIRE(SDL_RenderClear(display.renderer));
        REQUIRE(DrawScreen(display.renderer, text, frame, size.x, size.y, &view, 32, 32, 0, scale));
        REQUIRE(SDL_RenderPresent(display.renderer));
        CHECK(frame.scroll > 0); CHECK(frame.scroll < 999);
        CHECK(separatorPixels() == 0);
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
        for (int points : {8, 14, 24, 48}) {
            SdlText text(display.renderer, nullptr, static_cast<float>(points)); REQUIRE(text.Ready());
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
            const auto multiplayer = String("F9 ") + localization.Text("sdl.ui.multiplayer", "Multiplayer");
            REQUIRE(text.Draw(63, multiplayer.c_str(), -10000, -10000, {255, 255, 255, 255}));
            CHECK(text.Width(60) == text.Width(63));
        }
    }
}

}
