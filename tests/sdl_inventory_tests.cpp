#include "sdl/inventory_screen.h"
#include "sdl/input.h"
#include "sdl/modal.h"
#include "sdl/renderer.h"
#include "client/item_browser.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace cataclysmx;
using namespace cataclysmx::sdl;

extern const char* const expansionItemIds[80];

namespace {
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
std::uint64_t SurfaceHash(SDL_Surface* surface, int firstRow = 0, int rowCount = -1) {
    std::uint64_t hash = 14695981039346656037ULL;
    const int end = rowCount < 0 ? surface->h : firstRow + rowCount;
    for (int y = firstRow; y < end; ++y) {
        for (int x = 0; x < surface->w; ++x) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
            for (const auto value : {r, g, b, a}) { hash = (hash ^ value) * 1099511628211ULL; }
        }
    }
    return hash;
}

bool HasColor(SDL_Surface* surface, SDL_Rect area, SDL_Color color) {
    const SDL_Rect bounds{0, 0, surface->w, surface->h};
    SDL_Rect clip{};
    if (!SDL_GetRectIntersection(&area, &bounds, &clip)) { return false; }
    for (int y = clip.y; y < clip.y + clip.h; ++y) {
        for (int x = clip.x; x < clip.x + clip.w; ++x) {
            Uint8 r, g, b, a;
            REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
            if (r == color.r && g == color.g && b == color.b) { return true; }
        }
    }
    return false;
}
}

TEST_CASE("SDL item icons fit the cropped image rather than map cells", "[sdl][inventory]") {
    const auto tall = FitItemIcon({10, 20, 40, 40}, 32, 64);
    CHECK(tall.x == 20); CHECK(tall.y == 20); CHECK(tall.w == 20); CHECK(tall.h == 40);
    const auto wide = FitItemIcon({10, 20, 40, 40}, 128, 32);
    CHECK(wide.x == 10); CHECK(wide.y == 35); CHECK(wide.w == 40); CHECK(wide.h == 10);
    CHECK(FitItemIcon({}, 0, 0).w == 0);
}

TEST_CASE("SDL item pane scrolling retains selected rows and handles empty lists", "[sdl][inventory]") {
    CHECK(ItemPaneScroll(999, 0, 0, false, 4) == 0);
    CHECK(ItemPaneScroll(0, 30, 29, true, 4) == 26);
    CHECK(ItemPaneScroll(26, 30, 0, true, 4) == 0);
    CHECK(ItemPaneScroll(20, 2, 1, true, 8) == 0);
    CHECK(ItemPaneScroll(-1, 30, 29, true, 0) == 29);
    CHECK(ItemPaneScroll(999, 30, 100, false, 4) == 26);
}

TEST_CASE("SDL inventory text sizes retain selected rows in compact and wide windows", "[sdl][inventory][text-size]") {
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 780}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        const auto footerHash = [&] {
            std::uint64_t hash = 14695981039346656037ULL;
            for (int y = size.y - 32; y < size.y - 8; ++y) {
                for (int x = 8; x < size.x - 8; ++x) {
                    Uint8 r, g, b, a;
                    REQUIRE(SDL_ReadSurfacePixel(display.surface, x, y, &r, &g, &b, &a));
                    for (const auto value : {r, g, b, a}) { hash = (hash ^ value) * 1099511628211ULL; }
                }
            }
            return hash;
        };
        SdlAssets assets;
        for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
        REQUIRE(assets.State() == AssetState::Ready);
        for (int points : {8, 14, 24, 48}) {
            SdlText text(display.renderer, nullptr, static_cast<float>(points)); REQUIRE(text.Ready());
            ItemUiFrame frame; frame.screen = ItemUiScreen::Inventory;
            frame.title = "Inventory"; frame.footer = "Escape close";
            frame.panes[0].title = "Inventory";
            for (int i = 0; i < 20; ++i) {
                ItemUiRow row; row.type = TestContent().FindItem("rock"); row.name = "Rock";
                row.items = {static_cast<PersistentId>(i + 1)};
                frame.panes[0].rows.push_back(eastl::move(row));
            }
            frame.panes[0].selected = 19; frame.panes[0].selectionValid = true;
            REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 1));
            CHECK(frame.panes[0].scroll > 0); CHECK(frame.panes[0].scroll <= 19);
            REQUIRE(SDL_RenderPresent(display.renderer));
            const auto before = footerHash();
            frame.panes[0].rows.back().volumeMl = 999999;
            frame.panes[0].rows.back().massGrams = 333333;
            REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 1));
            REQUIRE(SDL_RenderPresent(display.renderer));
            CHECK(footerHash() == before);
            if (const char* path = std::getenv("CATACLYSMX_TEXT_SIZE_PREVIEW")) {
                const auto output = String(path) + ".inventory." + NumberText(size.x) + "." + NumberText(points) + ".png";
                REQUIRE(SDL_SavePNG(display.surface, output.c_str()));
            }
        }
    }
}

TEST_CASE("SDL inventory renders both capacities empty panes and compact selected rows", "[sdl][inventory][render]") {
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 780}, SDL_Point{1600, 1200}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        SdlAssets assets;
        for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
        REQUIRE(assets.State() == AssetState::Ready);
        ItemUiFrame frame; frame.screen = ItemUiScreen::Advanced; frame.advanced = true;
        frame.title = "Advanced inventory";
        frame.panes[0].title = "Inventory"; frame.panes[0].capacity = "1 kg / 100 kg";
        frame.panes[1].title = "Floor"; frame.panes[1].capacity = "500 ml / unlimited";
        for (int i = 0; i < 40; ++i) {
            ItemUiRow row; row.type = TestContent().FindItem("rock"); row.name = "A named rock";
            row.items = {static_cast<PersistentId>(i + 1)}; row.volumeMl = 250; row.massGrams = 500;
            row.depth = i % 4; row.wielded = i == 39;
            frame.panes[0].rows.push_back(eastl::move(row));
        }
        frame.panes[0].selected = 39; frame.panes[0].selectionValid = true;
        REQUIRE(SDL_SetRenderScale(display.renderer, 1.5f, 1.5f));
        const SDL_Rect originalClip{2, 3, 100, 80};
        REQUIRE(SDL_SetRenderClipRect(display.renderer, &originalClip));
        REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 2));
        CHECK(frame.panes[0].scroll > 0); CHECK(frame.panes[0].scroll <= 39);
        CHECK(frame.panes[1].scroll == 0);
        CHECK(text.Width(70) > 0); CHECK(text.Width(71) > 0); // Both capacity lines.
        float x = 0, y = 0; REQUIRE(SDL_GetRenderScale(display.renderer, &x, &y));
        CHECK(x == 1.5f); CHECK(y == 1.5f);
        SDL_Rect clip{}; REQUIRE(SDL_GetRenderClipRect(display.renderer, &clip));
        CHECK(clip.x == 2); CHECK(clip.y == 3); CHECK(clip.w == 100); CHECK(clip.h == 80);
        frame.panes[0].selected = 0;
        REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 1));
        CHECK(frame.panes[0].scroll == 0);
        frame.screen = ItemUiScreen::Inventory; frame.advanced = false;
        REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 1));
        CHECK(text.Width(122) > 0); // Table labels; missing tileset mapping uses a glyph icon.
        CHECK(text.Width(127) > 0);
    }
}

TEST_CASE("SDL aggregate row names retain source labels and render destination modal", "[sdl][inventory][render]") {
    Display display(1120, 780); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready()); SdlAssets assets;
    for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
    REQUIRE(assets.State() == AssetState::Ready);
    ItemUiFrame frame; frame.screen = ItemUiScreen::Advanced; frame.advanced = true;
    frame.panes[0].title = "Inventory"; frame.panes[1].title = "All nearby";
    frame.panes[1].mode = ItemUiPaneMode::AllNearby;
    ItemUiRow row; row.type = TestItem("rock"); row.name = "rock [7 floor]"; row.items = {4};
    frame.panes[1].rows = {row};
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 1120, 780, 1));
    const auto labelledWidth = text.Width(96);
    frame.panes[1].rows[0].name = "rock";
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 1120, 780, 1));
    CHECK(labelledWidth > text.Width(96));
    frame.screen = ItemUiScreen::Destination; frame.prompt = "Choose destination";
    frame.choices = {"[7] Northwest — floor", "[6] East — cupboard"};
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 1120, 780, 1));
    CHECK(text.Width(68) > 0); CHECK(text.Width(72) > 0); CHECK(text.Width(73) > 0);
    CHECK(text.Width(69) > 0); // The right pane still has its own title during the left prompt.
}

TEST_CASE("SDL advanced inventory fits a protected 3x3 and two tall windows", "[sdl][inventory]") {
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 780}, SDL_Point{1600, 1000}}) {
        for (const int zoom : {-8, 0, 12}) {
            const bool largeUi = size.x == 480 && zoom == 12;
            const float uiScale = largeUi ? 4.0f : 1.0f;
            const int grid = largeUi ? 1 : 17; // Radius zero still reserves nine positions.
            auto layout = MakeGameplayLayout(size.x, size.y, grid, grid, 32, 48, zoom, uiScale, {}, largeUi ? 55 : 16);
            const auto original = layout;
            const auto protectedArea = FitInventoryMap(layout, size.y, uiScale, grid, grid, {grid / 2, grid / 2});
            SDL_Rect clipped{};
            REQUIRE(SDL_GetRectIntersection(&protectedArea, &layout.mapViewport, &clipped));
            CHECK(SDL_RectsEqual(&protectedArea, &clipped));
            CHECK(protectedArea.w == 3 * layout.map.cellSize);
            CHECK(protectedArea.h == 3 * layout.map.cellHeight);
            CHECK(layout.map.width == grid * layout.map.cellSize);
            CHECK(layout.map.height == grid * layout.map.cellHeight);
            CHECK(std::abs(layout.map.cellHeight * 2 - layout.map.cellSize * 3) <= 2);
            if (zoom == -8 || (size.x == 1600 && zoom == 0)) {
                CHECK(layout.map.cellSize == original.map.cellSize);
                CHECK(layout.map.cellHeight == original.map.cellHeight);
            } else { CHECK(layout.map.cellSize < original.map.cellSize); }
            if (largeUi) { CHECK(SDL_RectEmpty(&layout.hud)); }
            const int top = ScaleRect(layout.topBar, uiScale).h;
            const SDL_Rect content{layout.mapViewport.x, top, layout.mapViewport.w, size.y - top};
            const auto panels = PlaceInventoryPopups(content, protectedArea,
                static_cast<int>(preferredInventoryPaneWidth * uiScale));
            REQUIRE(panels[0].x + panels[0].w < panels[1].x);
            CHECK(panels[0].y == panels[1].y);
            for (const auto& panel : panels) {
                CHECK(panel.h == (content.h - 8) * 3 / 4);
                CHECK(panel.w > 0);
                if (zoom == 0) {
                    const int expectedWidth = size.x == 480 ? 192 : size.x == 1120 ? 312 : 400;
                    CHECK(panel.w == expectedWidth);
                }
                REQUIRE(SDL_GetRectIntersection(&panel, &content, &clipped));
                CHECK(SDL_RectsEqual(&panel, &clipped));
                CHECK_FALSE(SDL_HasRectIntersection(&panel, &protectedArea));
            }
        }
    }
}

TEST_CASE("SDL advanced inventory draws tile anchors and keeps the map visible in prompts and at high zoom", "[sdl][inventory][render]") {
    Server server(TestContent());
    REQUIRE(server.GetWorld().CreateItem(TestItem("rock"), ItemLocation::Carried(server.GetPlayer())));
    const auto view = server.View(4);
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    const auto keys = DefaultKeymap();
    const SDL_Color active{255, 230, 90, 255}, inactive{110, 182, 174, 255};
    for (const bool compact : {false, true}) {
        CAPTURE(compact);
        const int width = compact ? 480 : 1120, height = compact ? 360 : 780;
        const int zoom = compact ? 12 : 0;
        Display display(width, height); REQUIRE(display.renderer);
        SdlText text(display.renderer, nullptr, compact ? 48.0f : 16.0f); REQUIRE(text.Ready());
        SdlAssets assets;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (assets.Poll(display.renderer) == AssetState::Loading && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        REQUIRE(assets.State() == AssetState::Ready);
        auto geometry = MakeGameplayLayout(width, height, view.width, view.height,
            assets.Definition().cellWidth, assets.Definition().cellHeight, zoom, 1, {}, text.LineHeight());
        const auto protectedArea = FitInventoryMap(geometry, height, 1, view.width, view.height,
            {view.player_position.x - view.origin.x, view.player_position.z - view.origin.z});
        const auto player = MapCellRect(geometry.map, view.player_position.x - view.origin.x,
            view.player_position.z - view.origin.z);
        const auto north = MapCellRect(geometry.map, view.player_position.x - view.origin.x,
            view.player_position.z - view.origin.z - 1);
        const SDL_Rect playerTop{player.x + player.w / 2 - 2, player.y - 1, 5, 3};
        const SDL_Rect northTop{north.x + north.w / 2 - 2, north.y - 1, 5, 3};
        const auto draw = [&](ItemUiFrame* frame) {
            GameplayLayout drawn;
            REQUIRE(DrawFrame(display.renderer, text, assets, &view, TestContent(), "", "", 0,
                nullptr, zoom, 1, {}, nullptr, nullptr, nullptr, {}, frame, &drawn));
            CHECK(SDL_RectsEqual(&drawn.mapViewport, &geometry.mapViewport));
            CHECK(drawn.map.cellSize == geometry.map.cellSize);
        };
        ItemUi ui; ui.Open(true); ui.Refresh(view, TestContent(), localization, keys);
        draw(&ui.Frame());
        CHECK(HasColor(display.surface, playerTop, active));
        CHECK_FALSE(HasColor(display.surface, playerTop, inactive));
        CHECK(HasColor(display.surface, {player.x + player.w / 2 + 2, player.y + player.h / 2, 10, 1}, inactive));
        const int mapX = protectedArea.x + player.w / 2, mapY = protectedArea.y + protectedArea.h - player.h / 2;
        Uint8 mapR, mapG, mapB, mapA;
        REQUIRE(SDL_ReadSurfacePixel(display.surface, mapX, mapY, &mapR, &mapG, &mapB, &mapA));
        ui.Handle({"Right"}, keys, view, TestContent(), localization);
        ui.Handle({"8"}, keys, view, TestContent(), localization);
        ui.Refresh(view, TestContent(), localization, keys);
        draw(&ui.Frame());
        CHECK(HasColor(display.surface, northTop, active));
        CHECK(HasColor(display.surface, {player.x - 1, player.y + player.h / 2 - 2, 3, 5}, inactive));
        CHECK(HasColor(display.surface, {mapX, mapY, 1, 1}, {mapR, mapG, mapB, mapA}));
        ui.Handle({"Left"}, keys, view, TestContent(), localization);
        ui.Handle({"m"}, keys, view, TestContent(), localization);
        ui.Handle({"1"}, keys, view, TestContent(), localization);
        ui.Refresh(view, TestContent(), localization, keys);
        REQUIRE(ui.Frame().screen == ItemUiScreen::Amount);
        CHECK(ui.Frame().amount == "1");
        draw(&ui.Frame());
        CHECK(HasColor(display.surface, {mapX, mapY, 1, 1}, {mapR, mapG, mapB, mapA}));
        CHECK(text.Width(68) > 0); CHECK(text.Width(69) > 0);
    }
}

TEST_CASE("SDL item examination preserves colored UTF8 cells clips wide art and scrolls stats", "[sdl][inventory][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    SdlAssets assets;
    for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
    REQUIRE(assets.State() == AssetState::Ready);
    TextArt art;
    art.lines = {{{"  /", ArtColor::Rgb(37, 219, 143), 3}, {"─\\  ", ArtColor::Rgb(90, 200, 210), 4}}}; art.width = 7;
    for (int i = 0; i < 100; ++i) { art.lines[0].push_back({"─", ArtColor::Rgb(88, 194, 132), 1}); ++art.width; }
    ItemUiFrame frame; frame.screen = ItemUiScreen::Examine; frame.title = "Examine";
    frame.examination.type = TestContent().FindItem("rock"); frame.examination.name = "Rock";
    frame.examination.description = "A description that wraps in a compact window.";
    frame.examination.art = &art;
    for (int i = 0; i < 60; ++i) { frame.examination.stats.push_back("An authored item statistic"); }
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    // The run palette reaches actual pixels, not just metadata.
    bool red = false, cyan = false;
    const auto redColor = ItemArtColor(ArtColor::Rgb(37, 219, 143)), cyanColor = ItemArtColor(ArtColor::Rgb(90, 200, 210));
    for (int y = 0; y < 360; ++y) {
        for (int x = 0; x < 480; ++x) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            REQUIRE(SDL_ReadSurfacePixel(display.surface, x, y, &r, &g, &b, &a));
            red |= r == redColor.r && g == redColor.g && b == redColor.b;
            cyan |= r == cyanColor.r && g == cyanColor.g && b == cyanColor.b;
        }
    }
    CHECK(red); CHECK(cyan);
    frame.examination.scroll = 9999;
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    CHECK(frame.examination.scroll > 0); CHECK(frame.examination.scroll < 9999);
    frame.examination.art = nullptr; frame.examination.stats.clear(); frame.examination.description.clear();
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    CHECK(frame.examination.scroll == 0); CHECK(text.Width(127) > 0); // Icon is independent of art.
}

TEST_CASE("SDL item submodal transitions stop held accept and quantity keys", "[sdl][inventory][input]") {
    Server server(TestContent());
    REQUIRE(server.GetWorld().CreateItem(TestContent().FindItem("rock"), ItemLocation::Carried(server.GetPlayer())));
    auto view = server.View();
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    const auto keys = DefaultKeymap();
    ItemUi ui; ui.Open(); ui.Refresh(view, TestContent(), localization, keys);
    HeldKey held; ModalController modal(held);
    modal.BeginOpen(ModalScreen::ItemInventory); modal.Paused();
    SDL_Event enter{}; enter.type = SDL_EVENT_KEY_DOWN; enter.key.key = SDLK_RETURN; enter.key.type = SDL_EVENT_KEY_DOWN;
    REQUIRE(held.Observe(enter, 0));
    HandleInventoryKey(ui, held, {"Enter"}, keys, view, TestContent(), localization);
    REQUIRE(ui.Frame().screen == ItemUiScreen::Examine);
    CHECK_FALSE(held.Poll(1000, 10));
    CHECK(modal.Active() == ModalScreen::ItemInventory); CHECK(modal.CapturesInput());
    HandleInventoryKey(ui, held, {"Escape"}, keys, view, TestContent(), localization);
    ui.Close(); ui.Open(true); ui.Refresh(view, TestContent(), localization, keys);
    SDL_Event amount = enter; amount.key.key = SDLK_M;
    REQUIRE(held.Observe(amount, 1001));
    HandleInventoryKey(ui, held, {"m"}, keys, view, TestContent(), localization);
    REQUIRE(ui.Frame().screen == ItemUiScreen::Destination);
    CHECK_FALSE(held.Poll(1500, 10));
    HandleInventoryKey(ui, held, {"Enter"}, keys, view, TestContent(), localization);
    REQUIRE(ui.Frame().screen == ItemUiScreen::Amount);
    CHECK_FALSE(held.Poll(2000, 10));
    auto customKeys = keys;
    SetBinding(customKeys, {InputContext::Items, InputAction::HoldLeft, {{"5"}}});
    SDL_Event five = enter; five.key.key = SDLK_KP_5;
    REQUIRE(held.Observe(five, 2001));
    HandleInventoryKey(ui, held, {"5"}, customKeys, view, TestContent(), localization);
    CHECK(ui.Frame().amount == "5");
    CHECK(held.Poll(2500, 10));
    CHECK_FALSE(ui.TakeCommand(view));
    HandleInventoryKey(ui, held, {"Escape"}, customKeys, view, TestContent(), localization);
    REQUIRE(held.Observe(five, 2501));
    HandleInventoryKey(ui, held, {"5"}, customKeys, view, TestContent(), localization);
    ui.Refresh(view, TestContent(), localization, customKeys);
    CHECK(ui.Frame().panes[0].storage.kind == ItemStorageKind::Ground);
    CHECK(ui.Frame().panes[0].anchor == view.player_position);
    REQUIRE(held.Observe(five, 2600));
    HandleInventoryKey(ui, held, {"5"}, customKeys, view, TestContent(), localization);
    CHECK_FALSE(held.Poll(3000, 10));
    CHECK(ui.Frame().panes[1].storage.kind == ItemStorageKind::Ground);
    CHECK(ui.Frame().activePane == 0);
    CHECK_FALSE(ui.TakeCommand(view));
    CHECK(modal.Active() != ModalScreen::Examine); // World Examine identity is separate.
}

TEST_CASE("SDL item command and cancellation keys cannot repeat across paid recovery", "[sdl][inventory][input]") {
    Server server(TestContent());
    REQUIRE(server.GetWorld().CreateItem(TestContent().FindItem("rock"), ItemLocation::Carried(server.GetPlayer())));
    auto view = server.View();
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    const auto keys = DefaultKeymap();
    ItemUi ui; ui.Open(true); ui.Refresh(view, TestContent(), localization, keys);
    HeldKey held;
    SDL_Event key{}; key.type = SDL_EVENT_KEY_DOWN; key.key.type = SDL_EVENT_KEY_DOWN; key.key.key = SDLK_W;
    REQUIRE(held.Observe(key, 0));
    HandleInventoryKey(ui, held, {"w"}, keys, view, TestContent(), localization);
    REQUIRE(ui.NeedsTime()); CHECK(ui.Frame().screen == ItemUiScreen::Advanced);
    CHECK_FALSE(held.Poll(1000, 10)); // Queueing on the same screen terminates the hold.
    REQUIRE(ui.TakeCommand(view));
    CommandResult result; result.outcome = Outcome::wielded; result.advanced = true; result.cost = 100; result.tick = view.tick;
    ui.CommandFinished(result);
    view.remainingTicks = 100; ++view.revision;
    key.key.key = SDLK_ESCAPE; REQUIRE(held.Observe(key, 1001));
    HandleInventoryKey(ui, held, {"Escape"}, keys, view, TestContent(), localization);
    CHECK(ui.IsOpen()); CHECK(ui.NeedsTime()); CHECK_FALSE(held.Poll(2000, 10));
    view.remainingTicks = 0; view.tick += 100; ++view.revision;
    ui.Refresh(view, TestContent(), localization, keys);
    CHECK_FALSE(ui.NeedsTime()); CHECK(ui.IsOpen()); CHECK_FALSE(held.Poll(3000, 10));
}

TEST_CASE("SDL compact advanced inventory draws one line per item and toggles controls", "[sdl][inventory][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    SdlAssets assets;
    for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
    REQUIRE(assets.State() == AssetState::Ready);
    ItemUiFrame frame; frame.advanced = true; frame.screen = ItemUiScreen::Advanced;
    frame.panes[0].title = "Inventory"; frame.panes[1].title = "All nearby";
    ItemUiRow row; row.type = TestItem("rock"); row.name = "MMMM"; row.items = {1}; row.massGrams = row.volumeMl = 1;
    frame.panes[0].rows.push_back(row);
    const auto draw = [&](const char* footerOverride = "? Controls") {
        REQUIRE(SDL_SetRenderDrawColor(display.renderer, 12, 16, 20, 255));
        REQUIRE(SDL_RenderClear(display.renderer));
        REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1,
            false, nullptr, footerOverride));
        REQUIRE(SDL_RenderPresent(display.renderer));
    };
    const auto checkFooter = [&] {
        frame.footer.clear(); draw();
        const auto first = SurfaceHash(display.surface);
        frame.footer = "Up/Down: select | Enter: accept | Escape: back";
        draw();
        CHECK(SurfaceHash(display.surface) == first);
        CHECK(text.Width(65) > 0);
        draw(nullptr);
        CHECK(SurfaceHash(display.surface) != first);
        draw();
        CHECK(SurfaceHash(display.surface) == first);
    };
    checkFooter();

    Vector<std::uint64_t> baseline;
    for (int y = 0; y < display.surface->h; ++y) { baseline.push_back(SurfaceHash(display.surface, y, 1)); }
    const auto firstChangedRow = [&] {
        for (int y = 0; y < display.surface->h; ++y) {
            if (SurfaceHash(display.surface, y, 1) != baseline[y]) { return y; }
        }
        return -1;
    };
    frame.panes[0].rows[0].name = "HHHH"; draw();
    const int nameRow = firstChangedRow(); REQUIRE(nameRow >= 0);
    auto& renderedRow = frame.panes[0].rows[0];
    const auto checkMetricRow = [&] {
        draw();
        const int metricRow = firstChangedRow(); REQUIRE(metricRow >= 0);
        CHECK(std::abs(nameRow - metricRow) <= 4);
        renderedRow = row;
    };
    renderedRow = row;
    renderedRow.items.push_back(2); checkMetricRow();
    renderedRow.volumeMl = UINT64_MAX; checkMetricRow();
    renderedRow.massGrams = UINT64_MAX; checkMetricRow();

    frame.screen = ItemUiScreen::Amount; frame.prompt = "Quantity (1–2)"; frame.amount = "1";
    checkFooter();
    frame.busy = true; frame.feedback = "Moved 1/2"; draw();
    CHECK(text.Width(66) > 0);
    draw(nullptr);
    CHECK(text.Width(66) > 0);
}

TEST_CASE("SDL colored art runs retain full monospaced widths and leading spaces", "[sdl][inventory][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    REQUIRE(text.Draw(125, "M", -10000, -10000, {255, 255, 255, 255}));
    const int cell = text.Width(125);
    REQUIRE(text.Draw(126, "  /─\\", -10000, -10000, ItemArtColor(ArtColor::Rgb(220, 83, 83))));
    const int firstWidth = text.Width(126);
    REQUIRE(text.DrawCells(128, "  /─\\", 5, 20, 20, ItemArtColor(ArtColor::Rgb(220, 83, 83)), cell));
    CHECK(text.Width(128) == firstWidth); CHECK(firstWidth > cell * 2);
    REQUIRE(text.Draw(126, "  []  ", -10000, -10000, ItemArtColor(ArtColor::Rgb(90, 200, 210))));
    const int secondWidth = text.Width(126);
    REQUIRE(text.DrawCells(129, "  []  ", 6, float(20 + cell * 5), 20, ItemArtColor(ArtColor::Rgb(90, 200, 210)), cell));
    CHECK(text.Width(129) == secondWidth); CHECK(secondWidth > cell * 4);
    REQUIRE(text.DrawCells(128, "  /─\\", 5, 20, 20, ItemArtColor(ArtColor::Rgb(220, 83, 83)), cell));
    CHECK(text.Width(128) == firstWidth); CHECK(text.Width(129) == secondWidth);
}

TEST_CASE("SDL real inventory screens render bundled sprites descriptions and art", "[sdl][inventory][render]") {
    const auto& content = TestContent();
    Server server(content);
    auto& world = server.GetWorld();
    for (int index = 0; index < 3; ++index) {
        REQUIRE(world.CreateItem(content.FindItem("rock"), ItemLocation::Carried(server.GetPlayer())));
    }
    REQUIRE(world.CreateItem(content.FindItem("metal_pipe"), ItemLocation::Carried(server.GetPlayer())));
    const auto bag = world.CreateItem(content.FindItem("backpack"), ItemLocation::Carried(server.GetPlayer())); REQUIRE(bag);
    REQUIRE(world.CreateItem(content.FindItem("bandage"), ItemLocation::Inside(*bag)));
    const auto view = server.View(4);
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    const auto keys = DefaultKeymap();
    for (const auto size : {SDL_Point{640, 480}, SDL_Point{1200, 800}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        SdlAssets assets(&content);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (assets.Poll(display.renderer) == AssetState::Loading && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        REQUIRE(assets.State() == AssetState::Ready);
        REQUIRE(content.FindItem("backpack").value < assets.Definition().items.size());
        CHECK(assets.Definition().items[content.FindItem("backpack").value].kind != TilesetMappingKind::Missing);
        auto map = PrepareMapPacket(view, content, assets.Definition(), {});
        // The software renderer exercises map geometry without the GPU shading pass.
        for (auto& cell : map.cells) { cell.terrainShading = {}; cell.overlayShading = {}; }
        ItemUi ui;
        const auto draw = [&](const char* screen) {
            ui.Refresh(view, content, localization, keys);
            if (ui.IsAdvanced()) {
                REQUIRE(DrawFrame(display.renderer, text, assets, &view, content, "", "", 0,
                    &map, 0, 1, {}, nullptr, nullptr, nullptr, {}, &ui.Frame()));
            } else {
                REQUIRE(DrawInventoryScreen(display.renderer, text, assets, content, ui.Frame(), size.x, size.y, 1));
                REQUIRE(SDL_RenderPresent(display.renderer));
            }
            if (const char* path = std::getenv("CATACLYSMX_SDL_INVENTORY_PREVIEW")) {
                const auto output = String(path) + "." + screen + "." + NumberText(size.x) + ".png";
                REQUIRE(SDL_SavePNG(display.surface, output.c_str()));
            }
        };
        ui.Open(); draw("inventory");
        REQUIRE(ui.Frame().panes[0].rows[0].items.size() == 3);
        ui.Handle({"e"}, keys, view, content, localization); draw("examine");
        REQUIRE(ui.Frame().examination.art); CHECK_FALSE(ui.Frame().examination.description.empty());
        ui.Close(); ui.Open(true); draw("advanced");
        CHECK_FALSE(ui.Frame().panes[0].rows.empty()); CHECK_FALSE(ui.Frame().panes[1].rows.empty());
    }
}

TEST_CASE("SDL art uses context default only when requested", "[sdl][inventory]") {
    const auto normal = ItemArtColor({});
    CHECK(normal.r == 210); CHECK(normal.g == 220); CHECK(normal.b == 218); CHECK(normal.a == 255);
    const auto custom = ItemArtColor(ArtColor::Rgb(13, 79, 241));
    CHECK(custom.r == 13); CHECK(custom.g == 79); CHECK(custom.b == 241); CHECK(custom.a == 255);
    const auto black = ItemArtColor(ArtColor::Rgb(0, 0, 0));
    CHECK(black.r == 0); CHECK(black.g == 0); CHECK(black.b == 0);
    CHECK(ItemArtColor({13, 79, 241, true}).r == normal.r);
}

TEST_CASE("SDL item pilot renders five authored assets at compact and wide sizes", "[item-pilot][sdl][inventory][render]") {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
    const auto& content = *loaded;
    Server server(content);
    for (const char* id : {"claw_hammer", "wood_scrap", "glass_bottle", "work_shirt", "arming_sword"}) {
        REQUIRE(server.GetWorld().CreateItem(content.FindItem(id), ItemLocation::Carried(server.GetPlayer())));
    }
    const auto view = server.View(0);
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 900}}) {
        CAPTURE(size.x, size.y);
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        SdlAssets assets(&content);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (assets.Poll(display.renderer) == AssetState::Loading && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        REQUIRE(assets.State() == AssetState::Ready);
        for (const char* id : {"claw_hammer", "wood_scrap", "glass_bottle", "work_shirt", "arming_sword"}) {
            CAPTURE(id);
            const auto type = content.FindItem(id); REQUIRE(type.value != 0);
            ItemUiFrame frame; frame.screen = ItemUiScreen::Examine;
            frame.title = "Examine"; frame.footer = "Up/Down scroll | Escape close";
            bool found = false;
            for (const auto& entry : view.inventory) {
                if (entry.type != type) { continue; }
                DescribeInventoryItem(entry, content, localization, frame.examination); found = true; break;
            }
            REQUIRE(found); REQUIRE(frame.examination.art == &content.GetItem(type).art);
            REQUIRE_FALSE(frame.examination.description.empty()); REQUIRE_FALSE(frame.examination.stats.empty());
            REQUIRE_FALSE(frame.examination.art->lines.empty());
            const SDL_Rect originalClip{2, 3, 100, 80};
            REQUIRE(SDL_SetRenderScale(display.renderer, 1.5f, 1.25f));
            REQUIRE(SDL_SetRenderClipRect(display.renderer, &originalClip));
            const auto draw = [&] {
                REQUIRE(DrawInventoryScreen(display.renderer, text, assets, content, frame, size.x, size.y, 1));
                REQUIRE(SDL_RenderPresent(display.renderer));
                float x = 0, y = 0; REQUIRE(SDL_GetRenderScale(display.renderer, &x, &y));
                CHECK(x == 1.5f); CHECK(y == 1.25f);
                REQUIRE(SDL_RenderClipEnabled(display.renderer));
                SDL_Rect clip{}; REQUIRE(SDL_GetRenderClipRect(display.renderer, &clip));
                CHECK(clip.x == originalClip.x); CHECK(clip.y == originalClip.y);
                CHECK(clip.w == originalClip.w); CHECK(clip.h == originalClip.h);
            };
            draw();
            const auto top = SurfaceHash(display.surface);
            if (const char* prefix = std::getenv("CATACLYSMX_ITEM_PILOT_PREVIEW")) {
                const auto output = String(prefix) + "_" + id + "_" + NumberText(size.x) + ".png";
                REQUIRE(SDL_SavePNG(display.surface, output.c_str()));
            }
            // Actual catalog art must reach pixels, including in the small pane.
            const auto* art = frame.examination.art;
            frame.examination.art = nullptr; draw();
            CHECK(SurfaceHash(display.surface) != top);
            frame.examination.art = art;
            frame.examination.scroll = 9999; draw();
            const int bottom = frame.examination.scroll;
            CHECK(bottom >= 0); CHECK(bottom < 9999);
            if (size.x == 480) { CHECK(bottom > 0); CHECK(SurfaceHash(display.surface) != top); }
            const auto bottomPixels = SurfaceHash(display.surface);
            frame.examination.scroll = 9999; draw();
            CHECK(frame.examination.scroll == bottom); CHECK(SurfaceHash(display.surface) == bottomPixels);
            frame.examination.scroll = 0; draw();
            CHECK(frame.examination.scroll == 0); CHECK(SurfaceHash(display.surface) == top);
        }
    }
}

TEST_CASE("SDL item slice expansion workshop and digital devices render through the item browser", "[item-slice][item-expansion][workshop][w3][digital][sdl][inventory][render]") {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
    const auto& content = *loaded;
    const auto obsolete = content.FindFlag("obsolete"); REQUIRE(obsolete.value);
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    const auto keys = DefaultKeymap();
    ItemBrowser browser; browser.Open(content, localization);
    CHECK(browser.Frame().matches.size() == content.ItemCount() - 2); // Both generated containers are hidden.
    constexpr const char* workshopIds[] = {
        "solid_steel_block", "refractory_brick", "charcoal_charge", "crude_anvil_kit",
        "charcoal_forge_kit", "forging_tongs", "smithing_hammer", "fabricated_anvil_kit",
        "spring_steel_bar", "tool_steel_blank", "wooden_workshop_crate", "sunglasses",
        "improvised_welding_goggles", "welding_filter", "welding_mask",
        "salvaged_welder_power_module", "electrical_lead_set", "makeshift_arc_welder",
        "arc_welder", "welding_electrode_packet", "welded_workshop_rack_kit",
        "workshop_battery_pack"
    };
    constexpr const char* renewableIds[] = {
        "recovered_pv_panel", "solar_charge_controller", "solar_generator_kit",
        "recovered_gasoline_engine", "generator_alternator", "generator_drive_assembly",
        "woodgas_processing_assembly", "woodgas_generator_kit", "prepared_wood"
    };
    constexpr const char* digitalIds[] = {
        "home_polymer_printer", "polymer_filament", "plastic_cup", "small_parts_case",
        "plastic_buckle_set", "printed_machete_grip", "printed_electronics_case",
        "usb_data_stick", "phone", "legacy_laptop"
    };
    for (const auto type : browser.Frame().matches) {
        CHECK(type != content.CorpseItem());
        CHECK_FALSE(content.GetItem(type).unfinishedCraft);
    }
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 900}}) {
        CAPTURE(size.x, size.y);
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        SdlAssets assets(&content);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (assets.Poll(display.renderer) == AssetState::Loading && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        REQUIRE(assets.State() == AssetState::Ready);
        unsigned rendered = 0;
        unsigned learningRendered = 0;
        unsigned powerRendered = 0;
        unsigned combatListed = 0;
        unsigned expansionRendered = 0;
        unsigned workshopRendered = 0;
        unsigned renewableRendered = 0;
        unsigned digitalRendered = 0;
        unsigned bootstrapRendered = 0;
        for (std::size_t index = 0; index < browser.Frame().matches.size(); ++index) {
            const auto type = browser.Frame().matches[index];
            const auto& item = content.GetItem(type);
            if (item.flags.Contains(obsolete)) { continue; }
            bool pilot = false;
            for (const char* id : {"claw_hammer", "wood_scrap", "glass_bottle", "work_shirt", "arming_sword"}) {
                pilot |= item.id == id;
            }
            if (pilot) { continue; }
            bool combat = false;
            for (const char* id : {"brass_knuckles", "wooden_shield", "boxing_manual",
                    "weapon_forms_manual", "cloth_face_wrap", "work_boots"}) {
                combat |= type == content.FindItem(id);
            }
            if (combat) {
                // These authored additions use the missing-art fallback, rather than slice artwork.
                CHECK(item.art.lines.empty());
                ++combatListed;
                continue;
            }
            bool expansion = false;
            for (const char* id : expansionItemIds) { expansion |= item.id == id; }
            bool workshop = false;
            for (const char* id : workshopIds) { workshop |= item.id == id; }
            bool renewable = false;
            for (const char* id : renewableIds) { renewable |= item.id == id; }
            bool digital = false;
            for (const char* id : digitalIds) { digital |= item.id == id; }
            const bool bootstrap = item.id == "makeshift_screwdriver";
            CAPTURE(item.id.c_str());
            if (renewable || digital || bootstrap) { REQUIRE_FALSE(item.art.lines.empty()); }
            browser.Frame().selected = index;
            browser.Handle({"e"}, keys, content, localization);
            REQUIRE(browser.Frame().screen == ItemBrowserScreen::Examine);
            auto& frame = browser.Frame().examination;
            REQUIRE(frame.examination.art == &item.art);
            REQUIRE_FALSE(frame.examination.description.empty());
            const auto draw = [&] {
                REQUIRE(DrawInventoryScreen(display.renderer, text, assets, content, frame, size.x, size.y, 1));
                REQUIRE(SDL_RenderPresent(display.renderer));
            };
            draw();
            const auto top = SurfaceHash(display.surface);
            const char* previewVariable = bootstrap ? "CATACLYSMX_BOOTSTRAP_PREVIEW"
                : digital ? "CATACLYSMX_DIGITAL_PREVIEW"
                : renewable ? "CATACLYSMX_RENEWABLE_PREVIEW"
                : workshop ? "CATACLYSMX_WORKSHOP_PREVIEW"
                : expansion ? "CATACLYSMX_ITEM_EXPANSION_PREVIEW" : "CATACLYSMX_ITEM_SLICE_PREVIEW";
            if (const char* prefix = std::getenv(previewVariable)) {
                const auto output = String(prefix) + "_" + item.id + "_" + NumberText(size.x) + ".png";
                REQUIRE(SDL_SavePNG(display.surface, output.c_str()));
            }
            frame.examination.art = nullptr; draw();
            CHECK(SurfaceHash(display.surface) != top);
            frame.examination.art = &item.art;
            frame.examination.scroll = 9999; draw();
            CHECK(frame.examination.scroll >= 0); CHECK(frame.examination.scroll < 9999);
            if (size.x == 480 && !expansion && !workshop && !renewable && !digital && !bootstrap) { CHECK(frame.examination.scroll > 0); }
            frame.examination.scroll = 0; draw();
            CHECK(SurfaceHash(display.surface) == top);
            browser.Handle({"Escape"}, keys, content, localization);
            // Render the new learning carriers too, while retaining the original
            // sixty-item slice coverage independently of the catalog additions.
            if (expansion) { ++expansionRendered; }
            else if (workshop) { ++workshopRendered; }
            else if (renewable) { ++renewableRendered; }
            else if (digital) { ++digitalRendered; }
            else if (bootstrap) { ++bootstrapRendered; }
            else if (item.id == "workshop_primer_print" || item.id == "salvage_notes_print"
                || item.id == "workshop_ebook_reader") { ++learningRendered; }
            else if (item.id == "wiring_kit" || item.id == "portable_fuel_generator"
                || item.id == "generator_fuel_packet") { ++powerRendered; }
            else { ++rendered; }
        }
        CHECK(rendered == 60);
        CHECK(expansionRendered == 80);
        CHECK(workshopRendered == 22);
        CHECK(renewableRendered == 9);
        CHECK(digitalRendered == 10);
        CHECK(bootstrapRendered == 1);
        CHECK(learningRendered == 3);
        CHECK(powerRendered == 3);
        CHECK(combatListed == 6);
    }
}

TEST_CASE("SDL larger colored artwork retains overflow runs and stable slots", "[sdl][inventory][render]") {
    Display display(1120, 1200); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    SdlAssets assets;
    for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
    REQUIRE(assets.State() == AssetState::Ready);
    TextArt art; art.width = 24;
    for (int y = 0; y < 24; ++y) {
        ArtLine line;
        for (int x = 0; x < 24; ++x) {
            line.push_back({"M", (x + y) % 2 ? ArtColor::Rgb(13, 79, 241) : ArtColor::Rgb(37, 219, 143), 1});
        }
        art.lines.push_back(eastl::move(line));
    }
    // Index 512 was the final overwrite of slot 128 with modulo run indexing.
    art.lines[21][8].text = "MM"; art.lines[21][8].cells = 2; art.width = 25;
    ItemUiFrame frame; frame.screen = ItemUiScreen::Examine; frame.title = "Examine";
    frame.examination.type = TestContent().FindItem("rock"); frame.examination.name = "Rock";
    frame.examination.art = &art;
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 1120, 1200, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    CHECK(text.Width(128) == text.Width(125)); // First run remains M, rather than overflow MM.
    const auto original = SurfaceHash(display.surface);
    // The final run is far beyond the stable-slot budget and must reach pixels.
    art.lines.back().back().color = ArtColor::Rgb(247, 31, 173);
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 1120, 1200, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    CHECK(SurfaceHash(display.surface) != original);
    frame.examination.scroll = 8;
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 1120, 1200, 1));
    CHECK(frame.examination.scroll == 0); // The complete 24-row artwork fits.
}
