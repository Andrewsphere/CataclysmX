#include "sdl/inventory_screen.h"
#include "sdl/input.h"
#include "sdl/modal.h"
#include "content_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace cataclysmx;
using namespace cataclysmx::sdl;

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
std::uint64_t SurfaceHash(SDL_Surface* surface) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (int y = 0; y < surface->h; ++y) {
        for (int x = 0; x < surface->w; ++x) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
            for (const auto value : {r, g, b, a}) { hash = (hash ^ value) * 1099511628211ULL; }
        }
    }
    return hash;
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

TEST_CASE("SDL inventory renders both capacities empty panes and compact selected rows", "[sdl][inventory][render]") {
    for (const auto size : {SDL_Point{480, 360}, SDL_Point{1120, 780}, SDL_Point{1600, 1200}}) {
        Display display(size.x, size.y); REQUIRE(display.renderer);
        SdlText text(display.renderer); REQUIRE(text.Ready());
        SdlAssets assets;
        for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
        REQUIRE(assets.State() == AssetState::Ready);
        ItemUiFrame frame; frame.screen = ItemUiScreen::Advanced;
        frame.title = "Advanced inventory"; frame.footer = "Tab pane | Enter transfer | Escape close";
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
        frame.screen = ItemUiScreen::Inventory;
        REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, size.x, size.y, 1));
        CHECK(text.Width(122) > 0); // Table labels; missing tileset mapping uses a glyph icon.
        CHECK(text.Width(127) > 0);
    }
}

TEST_CASE("SDL item examination preserves colored UTF8 cells clips wide art and scrolls stats", "[sdl][inventory][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    SdlAssets assets;
    for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
    REQUIRE(assets.State() == AssetState::Ready);
    TextArt art;
    art.lines = {{{"  /", ArtColor::Red, 3}, {"─\\  ", ArtColor::Cyan, 4}}}; art.width = 7;
    for (int i = 0; i < 100; ++i) { art.lines[0].push_back({"─", ArtColor::Green, 1}); ++art.width; }
    ItemUiFrame frame; frame.screen = ItemUiScreen::Examine; frame.title = "Examine";
    frame.examination.type = TestContent().FindItem("rock"); frame.examination.name = "Rock";
    frame.examination.description = "A description that wraps in a compact window.";
    frame.examination.art = &art;
    for (int i = 0; i < 60; ++i) { frame.examination.stats.push_back("An authored item statistic"); }
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    // The run palette reaches actual pixels, not just metadata.
    bool red = false, cyan = false;
    const auto redColor = ItemArtColor(ArtColor::Red), cyanColor = ItemArtColor(ArtColor::Cyan);
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
    REQUIRE(ui.Frame().screen == ItemUiScreen::Amount);
    CHECK_FALSE(held.Poll(2000, 10));
    HandleInventoryKey(ui, held, {"5"}, keys, view, TestContent(), localization);
    CHECK(ui.Frame().amount == "5");
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

TEST_CASE("SDL compact advanced inventory keeps the final supplied controls visible", "[sdl][inventory][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    SdlAssets assets;
    for (int i = 0; i < 1000000 && assets.Poll(display.renderer) == AssetState::Loading; ++i) { std::this_thread::yield(); }
    REQUIRE(assets.State() == AssetState::Ready);
    Server server(TestContent());
    Localization localization; REQUIRE(localization.Load(CATACLYSMX_LOC_DIRECTORY, "en"));
    ItemUi ui; ui.Open(true); ui.Refresh(server.View(), TestContent(), localization, DefaultKeymap());
    // Equal-width final markers keep wrapping/layout identical; pixel changes
    // therefore demonstrate that the end of the actual long footer is visible.
    auto frame = ui.Frame(); frame.footer += " | ZZZZZZ";
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    const auto first = SurfaceHash(display.surface);
    frame.footer.replace(frame.footer.size() - 6, 6, "MMMMMM");
    REQUIRE(DrawInventoryScreen(display.renderer, text, assets, TestContent(), frame, 480, 360, 1));
    REQUIRE(SDL_RenderPresent(display.renderer));
    CHECK(SurfaceHash(display.surface) != first);
}

TEST_CASE("SDL colored art runs retain full monospaced widths and leading spaces", "[sdl][inventory][render]") {
    Display display(480, 360); REQUIRE(display.renderer);
    SdlText text(display.renderer); REQUIRE(text.Ready());
    REQUIRE(text.Draw(125, "M", -10000, -10000, {255, 255, 255, 255}));
    const int cell = text.Width(125);
    REQUIRE(text.Draw(126, "  /─\\", -10000, -10000, ItemArtColor(ArtColor::Red)));
    const int firstWidth = text.Width(126);
    REQUIRE(text.DrawCells(128, "  /─\\", 5, 20, 20, ItemArtColor(ArtColor::Red), cell));
    CHECK(text.Width(128) == firstWidth); CHECK(firstWidth > cell * 2);
    REQUIRE(text.Draw(126, "  []  ", -10000, -10000, ItemArtColor(ArtColor::Cyan)));
    const int secondWidth = text.Width(126);
    REQUIRE(text.DrawCells(129, "  []  ", 6, float(20 + cell * 5), 20, ItemArtColor(ArtColor::Cyan), cell));
    CHECK(text.Width(129) == secondWidth); CHECK(secondWidth > cell * 4);
    REQUIRE(text.DrawCells(128, "  /─\\", 5, 20, 20, ItemArtColor(ArtColor::Red), cell));
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
    const auto view = server.View(0);
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
        ItemUi ui;
        const auto draw = [&](const char* screen) {
            ui.Refresh(view, content, localization, keys);
            REQUIRE(DrawInventoryScreen(display.renderer, text, assets, content, ui.Frame(), size.x, size.y, 1));
            REQUIRE(SDL_RenderPresent(display.renderer));
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
